// Copyright Epic Games, Inc. All Rights Reserved.
// 修改：2026-03-13  v2.0 — 新增JSON事件控制接收路径 + 事件状态定时广播

#include "EventMarkerManager.h"
#include "ProtocolMappingLibrary.h"
#include "UDPManager.h"
#include "CoordinateConverter.h"
#include "JSONMessageParser.h"

DEFINE_LOG_CATEGORY_STATIC(LogEventMarkerManager, Log, All);

// ============================================================
// 构造与析构
// ============================================================

UEventMarkerManager::UEventMarkerManager()
    : UDPManager(nullptr)
    , bIsInitialized(false)
    , BroadcastIntervalSeconds(2.0f)
    , BroadcastAccumulatedTime(0.0f)
    , TotalMessagesProcessed(0)
    , TotalStateUpdates(0)
    , TotalEventsCreated(0)
    , TotalHighlights(0)
    , TotalDisappearances(0)
    , TotalBroadcastsSent(0)
{
    UE_LOG(LogEventMarkerManager, Log, TEXT("EventMarkerManager constructed"));
}

UEventMarkerManager::~UEventMarkerManager()
{
    Shutdown();
    UE_LOG(LogEventMarkerManager, Log, TEXT("EventMarkerManager destroyed"));
}

// ============================================================
// 初始化与关闭
// ============================================================

bool UEventMarkerManager::Initialize(UUDPManager* InUDPManager)
{
    if (bIsInitialized)
    {
        UE_LOG(LogEventMarkerManager, Warning, TEXT("Already initialized"));
        return false;
    }

    if (!InUDPManager)
    {
        UE_LOG(LogEventMarkerManager, Error, TEXT("Invalid UDPManager pointer"));
        return false;
    }

    UDPManager = InUDPManager;

    // 旧二进制路径
    UDPManager->RegisterMessageHandler(static_cast<uint16>(EUDPMessageType::EventMarker), this);
    UDPManager->RegisterMessageHandler(static_cast<uint16>(EUDPMessageType::EventMarkerBatch), this);

    // 新JSON路径（端口10003，msgid=1001/2001 共用此消息类型）
    UDPManager->RegisterMessageHandler(static_cast<uint16>(EUDPMessageType::JSONEventControl), this);

    bIsInitialized = true;

    UE_LOG(LogEventMarkerManager, Log,
        TEXT("EventMarkerManager initialized (broadcast interval=%.1fs)"),
        BroadcastIntervalSeconds);

    return true;
}

void UEventMarkerManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogEventMarkerManager, Log, TEXT("Shutting down EventMarkerManager..."));

    PrintStatistics();

    if (UDPManager)
    {
        UDPManager->UnregisterMessageHandler(static_cast<uint16>(EUDPMessageType::EventMarker));
        UDPManager->UnregisterMessageHandler(static_cast<uint16>(EUDPMessageType::EventMarkerBatch));
        UDPManager->UnregisterMessageHandler(static_cast<uint16>(EUDPMessageType::JSONEventControl));
        UDPManager = nullptr;
    }

    EventCache.Empty();
    bIsInitialized = false;

    UE_LOG(LogEventMarkerManager, Log, TEXT("EventMarkerManager shutdown complete"));
}

// ============================================================
// Tick（广播定时器）
// ============================================================

void UEventMarkerManager::Tick(float DeltaTime)
{
    if (!bIsInitialized || BroadcastIntervalSeconds <= 0.0f)
    {
        return;
    }

    BroadcastAccumulatedTime += DeltaTime;

    if (BroadcastAccumulatedTime >= BroadcastIntervalSeconds)
    {
        BroadcastAccumulatedTime = 0.0f;

        const int32 SentCount = BroadcastEventStatusesNow();
        if (SentCount > 0)
        {
            TotalBroadcastsSent += SentCount;   // 累计发出的包数，非轮次数
            UE_LOG(LogEventMarkerManager, Verbose,
                TEXT("Periodic broadcast: sent %d event statuses (total sent=%d)"),
                SentCount, TotalBroadcastsSent);
        }
    }
}

// ============================================================
// IUDPMessageHandler 接口实现
// ============================================================

void UEventMarkerManager::HandleMessage(const uint8* Data, uint32 Length)
{
    if (!Data || Length == 0)
    {
        UE_LOG(LogEventMarkerManager, Warning, TEXT("Received invalid message"));
        return;
    }

    TotalMessagesProcessed++;

    // 协议路由策略：
    //   MessageDispatcher 按 MessageType 路由，0x0002/0x0012/0x1001 三种类型
    //   都会进入此函数，无法从 HandleMessage 签名中获知当前 MessageType。
    //
    //   区分方法：
    //   - JSON payload 首字节必须是 '{' (0x7B)，跳过前导空白后检测
    //   - 二进制单事件：首字节是 uint32 EventID 的低字节（业务ID从1起，≠ '{'}
    //   - 二进制批量：同上

    // 跳过前导空白，定位有效首字节
    const uint8* Start = Data;
    uint32       Remain = Length;
    while (Remain > 0 && (*Start == ' ' || *Start == '\t' || *Start == '\r' || *Start == '\n'))
    {
        ++Start;
        --Remain;
    }

    if (Remain > 0 && *Start == static_cast<uint8>('{'))
    {
        // JSON路径（msgid=1001 或 2001）
        HandleJSONEventControl(Start, Remain);
    }
    else if (Length == sizeof(FEventMarkerPacket))
    {
        // 旧二进制单事件（24字节）
        ProcessSingleEventMarker(Data, Length);
    }
    else if (Length >= sizeof(uint32) + sizeof(FEventMarkerPacket))
    {
        // 旧二进制批量事件（4 + 24N 字节）
        ProcessBatchEventMarker(Data, Length);
    }
    else
    {
        UE_LOG(LogEventMarkerManager, Warning,
            TEXT("Unrecognized payload: length=%d, first_byte=0x%02X"),
            Length, Data[0]);
    }
}

// ============================================================
// 旧二进制路径
// ============================================================

void UEventMarkerManager::ProcessSingleEventMarker(const uint8* Data, uint32 Length)
{
    check(Length == sizeof(FEventMarkerPacket));

    const FEventMarkerPacket* Packet = reinterpret_cast<const FEventMarkerPacket*>(Data);

    if (!ValidateEventMarkerPacket(*Packet))
    {
        return;
    }

    const FVector NEDPos(Packet->Position.North, Packet->Position.East, Packet->Position.Down);
    ProcessSingleUpdate(Packet->EventID, Packet->EventType, Packet->State,
        NEDPos, FVector::ZeroVector);

    UE_LOG(LogEventMarkerManager, Verbose,
        TEXT("Binary single event: ID=%d Type=%d State=%d"),
        Packet->EventID, Packet->EventType, Packet->State);
}

void UEventMarkerManager::ProcessBatchEventMarker(const uint8* Data, uint32 Length)
{
    const uint32* EventCountPtr = reinterpret_cast<const uint32*>(Data);
    const uint32  EventCount = *EventCountPtr;

    const uint32 ExpectedLength = sizeof(uint32) + EventCount * sizeof(FEventMarkerPacket);
    if (Length != ExpectedLength)
    {
        UE_LOG(LogEventMarkerManager, Error,
            TEXT("Binary batch length mismatch: expected=%u got=%u count=%u"),
            ExpectedLength, Length, EventCount);
        return;
    }

    const FEventMarkerPacket* Packets =
        reinterpret_cast<const FEventMarkerPacket*>(Data + sizeof(uint32));

    int32 ValidCount = 0;
    for (uint32 i = 0; i < EventCount; ++i)
    {
        if (ValidateEventMarkerPacket(Packets[i]))
        {
            const FVector NEDPos(Packets[i].Position.North,
                Packets[i].Position.East,
                Packets[i].Position.Down);
            ProcessSingleUpdate(Packets[i].EventID, Packets[i].EventType, Packets[i].State,
                NEDPos, FVector::ZeroVector);
            ValidCount++;
        }
    }

    OnBatchEventUpdateComplete.Broadcast(ValidCount);

    UE_LOG(LogEventMarkerManager, Verbose,
        TEXT("Binary batch: %d/%u valid events"), ValidCount, EventCount);
}

// ============================================================
// 公共更新路径（二进制和JSON共用）
// ============================================================

void UEventMarkerManager::ProcessSingleUpdate(int32 EventID, uint8 EventType,
    uint8 NewStateValue,
    const FVector& NEDPos,
    const FVector& Attitude)
{
    // NED（米）→ UE（厘米）
    FNEDVector NEDStruct;
    NEDStruct.North = NEDPos.X;
    NEDStruct.East = NEDPos.Y;
    NEDStruct.Down = NEDPos.Z;
    const FVector UELocation = UCoordinateConverter::NEDToUE(NEDStruct);
    const float   CurrentTime = static_cast<float>(FPlatformTime::Seconds());

    FEventMarkerRuntimeState* StatePtr = EventCache.Find(EventID);
    const bool                bIsNewEvent = (StatePtr == nullptr);

    if (bIsNewEvent)
    {
        // ── 新事件：用分离的局部结构体构造，避免变量名与参数冲突 ──
        FEventMarkerRuntimeState Entry;
        Entry.EventID = EventID;
        Entry.EventType = EventType;
        Entry.State = NewStateValue;
        Entry.Location = UELocation;
        Entry.NEDPosition = NEDPos;
        Entry.Attitude = Attitude;
        Entry.CreationTime = CurrentTime;
        Entry.LastUpdateTime = CurrentTime;
        Entry.StateChangeCount = 0;

        EventCache.Add(EventID, Entry);
        StatePtr = EventCache.Find(EventID);   // 指向 Map 内部的元素

        TotalEventsCreated++;

        UE_LOG(LogEventMarkerManager, Log,
            TEXT("New event: ID=%d Type=%s State=%s Loc=%s"),
            EventID,
            *GetEventTypeName(EventType),
            *GetEventStateName(NewStateValue),
            *UELocation.ToString());

        OnEventCreated.Broadcast(EventID, EventType, UELocation);
    }
    else
    {
        // ── 更新已有事件 ──
        const uint8 OldStateValue = StatePtr->State;

        StatePtr->State = NewStateValue;
        StatePtr->Location = UELocation;
        StatePtr->NEDPosition = NEDPos;
        StatePtr->Attitude = Attitude;
        StatePtr->LastUpdateTime = CurrentTime;

        if (OldStateValue != NewStateValue)
        {
            StatePtr->StateChangeCount++;
            HandleStateTransition(EventID, OldStateValue, NewStateValue, UELocation);
        }
    }

    // 广播状态变更（新事件和更新都触发）
    OnEventStateChanged.Broadcast(EventID, *StatePtr);
    TotalStateUpdates++;
}

bool UEventMarkerManager::ValidateEventMarkerPacket(const FEventMarkerPacket& Packet) const
{
    if (Packet.EventID <= 0)
    {
        UE_LOG(LogEventMarkerManager, Warning,
            TEXT("Invalid EventID: %u"), Packet.EventID);
        return false;
    }

    if (Packet.State > 2)
    {
        UE_LOG(LogEventMarkerManager, Warning,
            TEXT("Invalid state %d for event %u"), Packet.State, Packet.EventID);
        return false;
    }

    //const FVector NEDPos(Packet.Position.North, Packet.Position.East, Packet.Position.Down);
    if (!UCoordinateConverter::IsValidNED(Packet.Position, 10000.0f))
    {
        UE_LOG(LogEventMarkerManager, Warning,
            TEXT("Invalid NED position for event %u"), Packet.EventID);
        return false;
    }

    return true;
}

void UEventMarkerManager::HandleStateTransition(int32 EventID,
    uint8 OldStateValue,
    uint8 NewStateValue,
    const FVector& Location)
{
    UE_LOG(LogEventMarkerManager, Log,
        TEXT("Event %d: %s → %s"),
        EventID,
        *GetEventStateName(OldStateValue),
        *GetEventStateName(NewStateValue));

    switch (NewStateValue)
    {
    case 1:
        TotalHighlights++;
        OnEventHighlighted.Broadcast(EventID, Location);
        break;
    case 2:
        TotalDisappearances++;
        OnEventDisappeared.Broadcast(EventID, Location);
        break;
    default:
        break;
    }
}

// ============================================================
// 新JSON路径
// ============================================================

void UEventMarkerManager::HandleJSONEventControl(const uint8* Data, uint32 Length)
{
    // 将 UTF-8 字节序列（非 '\0' 结尾）安全地转换为 FString。
    // 必须先加 '\0' 再转换，否则 FUTF8ToTCHAR 无法确定字符串边界。
    TArray<uint8> NullTerminated(Data, static_cast<int32>(Length));
    NullTerminated.Add(0);
    const FString JsonString(UTF8_TO_TCHAR(
        reinterpret_cast<const ANSICHAR*>(NullTerminated.GetData())));

    if (JsonString.IsEmpty())
    {
        UE_LOG(LogEventMarkerManager, Warning, TEXT("HandleJSONEventControl: empty string after decode"));
        return;
    }

    // 通过字符串包含快速确定 msgid，避免完整解析两次
    const bool bIs1001 = JsonString.Contains(TEXT("\"msgid\":1001")) ||
        JsonString.Contains(TEXT("\"msgid\": 1001"));
    const bool bIs2001 = JsonString.Contains(TEXT("\"msgid\":2001")) ||
        JsonString.Contains(TEXT("\"msgid\": 2001"));

    if (bIs1001)
    {
        FJSONEventControlMessage Msg;
        if (FJSONMessageParser::ParseEventControl(JsonString, Msg))
        {
            HandleJSONEventControlMessage(Msg);
        }
        else
        {
            UE_LOG(LogEventMarkerManager, Warning,
                TEXT("HandleJSONEventControl: ParseEventControl failed. Payload: %s"),
                *JsonString.Left(200));
        }
    }
    else if (bIs2001)
    {
        FJSONSceneControlMessage Msg;
        if (FJSONMessageParser::ParseSceneControl(JsonString, Msg))
        {
            HandleJSONSceneControlMessage(Msg);
        }
        else
        {
            UE_LOG(LogEventMarkerManager, Warning,
                TEXT("HandleJSONEventControl: ParseSceneControl failed. Payload: %s"),
                *JsonString.Left(200));
        }
    }
    else
    {
        UE_LOG(LogEventMarkerManager, Warning,
            TEXT("HandleJSONEventControl: unrecognized msgid. Payload: %s"),
            *JsonString.Left(200));
    }
}

void UEventMarkerManager::HandleJSONEventControlMessage(const FJSONEventControlMessage& Message)
{
    UE_LOG(LogEventMarkerManager, Log,
        TEXT("JSON event control: publisher=%s %d events"),
        *Message.Publisher,
        Message.Events.Num());

    for (const FJSONEventEntry& Entry : Message.Events)
    {
        const uint8 NewStateValue = FProtocolMapping::EventOperationToMarkerState(Entry.Operation);

        if (NewStateValue == 0xFF)
        {
            UE_LOG(LogEventMarkerManager, Warning,
                TEXT("JSON event id=%d: unknown enum operation, skipping"),
                Entry.EventID);
            continue;
        }

        ProcessSingleUpdate(
            Entry.EventID,
            Entry.EventType,
            NewStateValue,
            Entry.NEDPosition,
            Entry.Attitude
        );
    }
}

void UEventMarkerManager::HandleJSONSceneControlMessage(const FJSONSceneControlMessage& Message)
{
    const FString NormalizedOperation = FProtocolMapping::NormalizeSceneOperationString(Message.Operation);

    UE_LOG(LogEventMarkerManager, Log,
        TEXT("JSON scene control: scene=%d '%s' raw_op=%s normalized_op=%s emergency=%s"),
        Message.SceneID,
        *Message.SceneName,
        *Message.Operation,
        *NormalizedOperation,
        Message.bEmergencyStop ? TEXT("YES") : TEXT("no"));

    OnSceneControlReceived.Broadcast(Message.SceneID, NormalizedOperation, Message.bEmergencyStop);
}

// ============================================================
// 广播发送（UE → 外部系统，端口10004）
// ============================================================

int32 UEventMarkerManager::BroadcastEventStatusesNow()
{
    if (!UDPManager)
    {
        return 0;
    }

    int32 SentCount = 0;

    for (const auto& Pair : EventCache)
    {
        const FEventMarkerRuntimeState& State = Pair.Value;

        // 只广播活跃事件（State != Disappeared）
        if (State.State == 2)
        {
            continue;
        }

        if (SendEventStatusBroadcast(State))
        {
            SentCount++;
        }
    }

    return SentCount;
}

bool UEventMarkerManager::SendEventStatusBroadcast(const FEventMarkerRuntimeState& State)
{
    FJSONEventStatusMessage Msg;
    // MsgID 和 Publisher 已由结构体默认值设定（1003 / "UEGCS"）
    // Timestamp 留空，由 GenerateEventStatus 填充当前时间

    Msg.EventStatus.EventID = State.EventID;
    Msg.EventStatus.EventType = State.EventType;
    Msg.EventStatus.Status = StateToJSONStatusString(State.State);
    Msg.EventStatus.NEDPosition = State.NEDPosition;
    Msg.EventStatus.Attitude = State.Attitude;

    FString JsonString;
    if (!FJSONMessageParser::GenerateEventStatus(Msg, JsonString))
    {
        UE_LOG(LogEventMarkerManager, Warning,
            TEXT("SendEventStatusBroadcast: serialize failed for EventID=%d"), State.EventID);
        return false;
    }

    const bool bSent = UDPManager->SendJSONMessage(
        static_cast<uint16>(EUDPMessageType::JSONEventStatus),
        JsonString,
        10004
    );

    if (!bSent)
    {
        UE_LOG(LogEventMarkerManager, Warning,
            TEXT("SendEventStatusBroadcast: send failed for EventID=%d"), State.EventID);
    }

    return bSent;
}

void UEventMarkerManager::SetBroadcastInterval(float IntervalSeconds)
{
    BroadcastIntervalSeconds = FMath::Max(0.0f, IntervalSeconds);
    BroadcastAccumulatedTime = 0.0f;

    UE_LOG(LogEventMarkerManager, Log,
        TEXT("Broadcast interval set to %.1fs%s"),
        BroadcastIntervalSeconds,
        BroadcastIntervalSeconds <= 0.0f ? TEXT(" (disabled)") : TEXT(""));
}

// ============================================================
// 蓝图查询接口
// ============================================================

bool UEventMarkerManager::GetEventState(int32 EventID, FEventMarkerRuntimeState& OutState) const
{
    const FEventMarkerRuntimeState* StatePtr = EventCache.Find(EventID);
    if (StatePtr)
    {
        OutState = *StatePtr;
        return true;
    }
    return false;
}

TArray<int32> UEventMarkerManager::GetAllActiveEventIDs() const
{
    TArray<int32> EventIDs;
    for (const auto& Pair : EventCache)
    {
        if (Pair.Value.State != 2)
        {
            EventIDs.Add(Pair.Key);
        }
    }
    return EventIDs;
}

TArray<int32> UEventMarkerManager::GetEventsByType(uint8 EventType, bool bOnlyActive) const
{
    TArray<int32> EventIDs;
    for (const auto& Pair : EventCache)
    {
        if (Pair.Value.EventType == EventType)
        {
            if (!bOnlyActive || Pair.Value.State != 2)
            {
                EventIDs.Add(Pair.Key);
            }
        }
    }
    return EventIDs;
}

bool UEventMarkerManager::DoesEventExist(int32 EventID) const
{
    return EventCache.Contains(EventID);
}

bool UEventMarkerManager::IsEventActive(int32 EventID) const
{
    const FEventMarkerRuntimeState* StatePtr = EventCache.Find(EventID);
    return StatePtr && (StatePtr->State != 2);
}

int32 UEventMarkerManager::GetActiveEventCount() const
{
    int32 Count = 0;
    for (const auto& Pair : EventCache)
    {
        if (Pair.Value.State != 2) Count++;
    }
    return Count;
}

void UEventMarkerManager::GetStatistics(FEventMarkerStatistics& OutStatistics) const
{
    OutStatistics.ActiveEventCount = 0;
    OutStatistics.DisappearedEventCount = 0;
    OutStatistics.TotalEventCount = EventCache.Num();
    OutStatistics.RoadEventCount = 0;
    OutStatistics.EnvironmentEventCount = 0;
    OutStatistics.IndoorEventCount = 0;
    OutStatistics.OutdoorEventCount = 0;

    for (const auto& Pair : EventCache)
    {
        const FEventMarkerRuntimeState& State = Pair.Value;

        if (State.State == 2) OutStatistics.DisappearedEventCount++;
        else                  OutStatistics.ActiveEventCount++;

        // 按类型码归类（含 events.json 实际使用的 101-103）
        const uint8 T = State.EventType;
        if (T <= 9 || (T >= 101 && T <= 109)) OutStatistics.RoadEventCount++;
        else if (T <= 19 || (T >= 110 && T <= 119)) OutStatistics.EnvironmentEventCount++;
        else if (T <= 29 || (T >= 120 && T <= 129)) OutStatistics.IndoorEventCount++;
        else if (T <= 39 || (T >= 130 && T <= 139)) OutStatistics.OutdoorEventCount++;
    }

    OutStatistics.TotalMessagesProcessed = TotalMessagesProcessed;
    OutStatistics.TotalStateUpdates = TotalStateUpdates;
}

// ============================================================
// 蓝图管理接口
// ============================================================

void UEventMarkerManager::ClearAllEvents()
{
    const int32 Count = EventCache.Num();
    EventCache.Empty();
    UE_LOG(LogEventMarkerManager, Log, TEXT("Cleared %d events"), Count);
}

int32 UEventMarkerManager::ClearDisappearedEvents()
{
    TArray<int32> ToRemove;
    for (const auto& Pair : EventCache)
    {
        if (Pair.Value.State == 2) ToRemove.Add(Pair.Key);
    }
    for (int32 ID : ToRemove) EventCache.Remove(ID);

    if (ToRemove.Num() > 0)
    {
        UE_LOG(LogEventMarkerManager, Log,
            TEXT("Cleared %d disappeared events"), ToRemove.Num());
    }
    return ToRemove.Num();
}

bool UEventMarkerManager::RemoveEvent(int32 EventID)
{
    const bool bRemoved = (EventCache.Remove(EventID) > 0);
    if (bRemoved)
    {
        UE_LOG(LogEventMarkerManager, Log, TEXT("Manually removed event %d"), EventID);
    }
    return bRemoved;
}

void UEventMarkerManager::PrintStatistics() const
{
    FEventMarkerStatistics Stats;
    GetStatistics(Stats);

    UE_LOG(LogEventMarkerManager, Log, TEXT("===== EventMarkerManager Statistics ====="));
    UE_LOG(LogEventMarkerManager, Log, TEXT("  Active / Disappeared / Total : %d / %d / %d"),
        Stats.ActiveEventCount, Stats.DisappearedEventCount, Stats.TotalEventCount);
    UE_LOG(LogEventMarkerManager, Log, TEXT("  Road / Env / Indoor / Outdoor: %d / %d / %d / %d"),
        Stats.RoadEventCount, Stats.EnvironmentEventCount,
        Stats.IndoorEventCount, Stats.OutdoorEventCount);
    UE_LOG(LogEventMarkerManager, Log, TEXT("  Msgs / Updates / Creates     : %d / %d / %d"),
        TotalMessagesProcessed, TotalStateUpdates, TotalEventsCreated);
    UE_LOG(LogEventMarkerManager, Log, TEXT("  Highlights / Disappears      : %d / %d"),
        TotalHighlights, TotalDisappearances);
    UE_LOG(LogEventMarkerManager, Log, TEXT("  Broadcast packets sent       : %d"), TotalBroadcastsSent);
    UE_LOG(LogEventMarkerManager, Log, TEXT("========================================="));
}

// ============================================================
// C++ 接口
// ============================================================

const FEventMarkerRuntimeState* UEventMarkerManager::GetEventStatePtr(int32 EventID) const
{
    return EventCache.Find(EventID);
}

// ============================================================
// 静态工具函数
// ============================================================

FString UEventMarkerManager::GetEventTypeName(uint8 EventType)
{
    switch (EventType)
    {
        // 原始定义类型码（0-31）
    case 0:   return TEXT("积水");
    case 1:   return TEXT("障碍物");
    case 2:   return TEXT("违章停车");
    case 10:  return TEXT("非法排污口");
    case 11:  return TEXT("裸露扬尘源");
    case 12:  return TEXT("建筑垃圾");
    case 20:  return TEXT("物资运输需求");
    case 21:  return TEXT("巡检需求");
    case 22:  return TEXT("清洁需求");
    case 30:  return TEXT("室外物资运输");
    case 31:  return TEXT("室外巡检需求");
        // events.json 实际使用的类型码（101-103）
    case 101: return TEXT("事件类型101");
    case 102: return TEXT("事件类型102");
    case 103: return TEXT("事件类型103");
    case 255: return TEXT("未知类型");
    default:  return FString::Printf(TEXT("未定义(%d)"), EventType);
    }
}

FString UEventMarkerManager::GetEventStateName(uint8 State)
{
    switch (State)
    {
    case 0:  return TEXT("初始状态");
    case 1:  return TEXT("高亮状态");
    case 2:  return TEXT("消失状态");
    default: return FString::Printf(TEXT("未知(%d)"), State);
    }
}

FString UEventMarkerManager::GetEventCategory(uint8 EventType)
{
    if (EventType <= 9)  return TEXT("道路巡检");
    else if (EventType <= 19) return TEXT("环境勘探");
    else if (EventType <= 29) return TEXT("室内楼宇");
    else if (EventType <= 39) return TEXT("室外场景");
    else                      return TEXT("未知分类");
}