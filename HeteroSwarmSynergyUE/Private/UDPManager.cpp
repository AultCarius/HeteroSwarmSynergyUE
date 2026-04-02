// Copyright Epic Games, Inc. All Rights Reserved.
// 修改：2026-03-13  v2.0 — MAVLink v2 + JSON 混合协议处理

#include "UDPManager.h"

// 必须在使用TUniquePtr之前包含完整定义，否则析构时会出错
#include "FUDPReceiverRunnable.h"
#include "PacketBufferPool.h"
#include "MessageDispatcher.h"
#include "IUDPMessageHandler.h"
#include "UDPProtocolTypes.h"
#include "HeteroSwarmGameInstance.h"

// UE引擎头文件
#include "HAL/RunnableThread.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

// 日志分类
DEFINE_LOG_CATEGORY_STATIC(LogUDPManager, Log, All);

// ========== 构造/析构 ==========

UUDPManager::UUDPManager()
    : ReceiverThread(nullptr)
    , SendSocket(nullptr)
    , MaxPacketsPerFrame(100)
    , BufferPoolSize(100)
    , bIsInitialized(false)
    , TotalPacketsProcessed(0)
    , LastStatsPrintTime(0.0)
    , StatsPrintInterval(60.0f)
{
}

UUDPManager::~UUDPManager()
{
    // 确保清理资源
    ShutdownUDP();
}

// ========== USubsystem 接口实现 ==========

void UUDPManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogUDPManager, Log, TEXT("UUDPManager subsystem initializing..."));

    // 子系统初始化时不自动启动网络，等待显式调用InitializeUDP

    UE_LOG(LogUDPManager, Log, TEXT("UUDPManager subsystem initialized (waiting for InitializeUDP call)"));
}

void UUDPManager::Deinitialize()
{
    UE_LOG(LogUDPManager, Log, TEXT("UUDPManager subsystem deinitializing..."));

    // 关闭UDP系统
    ShutdownUDP();

    Super::Deinitialize();

    UE_LOG(LogUDPManager, Log, TEXT("UUDPManager subsystem deinitialized"));
}

// ========== FTickableGameObject 接口实现 ==========

void UUDPManager::Tick(float DeltaTime)
{
    if (!bIsInitialized)
    {
        return;
    }

    // 处理接收队列中的数据包
    int32 PacketsProcessedThisFrame = 0;

    for (int32 i = 0; i < MaxPacketsPerFrame; ++i)
    {
        FRawPacket Packet;
        if (!RawPacketQueue.Dequeue(Packet))
        {
            break; // 队列已空
        }

        // 处理数据包
        ProcessPacket(Packet);

        PacketsProcessedThisFrame++;
        TotalPacketsProcessed++;
    }

    // 详细日志（仅在Verbose级别）
    if (PacketsProcessedThisFrame > 0)
    {
        UE_LOG(LogUDPManager, Verbose,
            TEXT("Processed %d packets this frame (Total: %d)"),
            PacketsProcessedThisFrame,
            TotalPacketsProcessed
        );
    }

    // 定期打印统计信息
    const double CurrentTime = FPlatformTime::Seconds();
    UpdateStatistics(CurrentTime);
}

TStatId UUDPManager::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UUDPManager, STATGROUP_Tickables);
}

bool UUDPManager::IsTickable() const
{
    // 仅在初始化后允许Tick
    return bIsInitialized;
}

// ========== 蓝图接口实现 ==========

bool UUDPManager::InitializeUDP(const FString& InListenIP, int32 InListenPort, int32 InBufferPoolSize)
{
    // 创建简单配置（单端口）
    FUDPConfiguration Config;
    Config.AddReceiver(InListenPort, FString::Printf(TEXT("Main Receiver (%s:%d)"), *InListenIP, InListenPort));

    UE_LOG(LogUDPManager, Log,
        TEXT("InitializeUDP called with single port configuration: %s:%d"),
        *InListenIP,
        InListenPort
    );

    return InitializeInternal(Config, InBufferPoolSize);
}

bool UUDPManager::InitializeUDPWithConfig(const FUDPConfiguration& Config, int32 InBufferPoolSize)
{
    UE_LOG(LogUDPManager, Log,
        TEXT("InitializeUDPWithConfig called with %d receivers, %d senders"),
        Config.Receivers.Num(),
        Config.Senders.Num()
    );

    return InitializeInternal(Config, InBufferPoolSize);
}

bool UUDPManager::InitializeInternal(const FUDPConfiguration& Config, int32 InBufferPoolSize)
{
    // 防止重复初始化
    if (bIsInitialized)
    {
        UE_LOG(LogUDPManager, Warning, TEXT("UDP Manager already initialized, shutdown first"));
        return false;
    }

    // 验证配置
    if (Config.GetEnabledReceiverCount() == 0)
    {
        UE_LOG(LogUDPManager, Error, TEXT("No enabled receivers in configuration"));
        return false;
    }

    // 验证缓冲池大小
    if (InBufferPoolSize < 10)
    {
        UE_LOG(LogUDPManager, Warning,
            TEXT("Buffer pool size too small: %d (recommended >= 50), using 50"),
            InBufferPoolSize
        );
        InBufferPoolSize = 50;
    }

    // 保存配置
    CurrentConfig = Config;
    BufferPoolSize = InBufferPoolSize;

    UE_LOG(LogUDPManager, Log,
        TEXT("Initializing UDP Manager: %d receivers, %d senders, BufferPoolSize=%d"),
        Config.GetEnabledReceiverCount(),
        Config.GetEnabledSenderCount(),
        BufferPoolSize
    );

    // 1. 创建缓冲区对象池
    BufferPool = MakeUnique<FPacketBufferPool>(BufferPoolSize);
    if (!BufferPool.IsValid())
    {
        UE_LOG(LogUDPManager, Error, TEXT("Failed to create buffer pool"));
        CleanupResources();
        return false;
    }

    // 2. 创建消息分发器
    MessageDispatcher = MakeUnique<FMessageDispatcher>();
    if (!MessageDispatcher.IsValid())
    {
        UE_LOG(LogUDPManager, Error, TEXT("Failed to create message dispatcher"));
        CleanupResources();
        return false;
    }

    // 3. 创建接收线程Runnable（会自动创建所有Socket）
    ReceiverRunnable = MakeUnique<FUDPReceiverRunnable>(
        CurrentConfig.Receivers,
        &RawPacketQueue,
        BufferPool.Get()
    );

    if (!ReceiverRunnable.IsValid())
    {
        UE_LOG(LogUDPManager, Error, TEXT("Failed to create receiver runnable"));
        CleanupResources();
        return false;
    }

    // 4. 启动接收线程
    ReceiverThread = FRunnableThread::Create(
        ReceiverRunnable.Get(),
        TEXT("UDPMultiReceiverThread"),
        0,
        TPri_AboveNormal,
        FPlatformAffinity::GetPoolThreadMask()
    );

    if (!ReceiverThread)
    {
        UE_LOG(LogUDPManager, Error, TEXT("Failed to create receiver thread"));
        CleanupResources();
        return false;
    }

    // 初始化成功
    bIsInitialized = true;
    LastStatsPrintTime = FPlatformTime::Seconds();

    // 打印配置信息
    UE_LOG(LogUDPManager, Log, TEXT("UDP Manager initialized successfully!"));
    UE_LOG(LogUDPManager, Log, TEXT("  Receivers:"));
    for (const FUDPReceiverConfig& RecvConfig : CurrentConfig.Receivers)
    {
        if (RecvConfig.bEnabled)
        {
            UE_LOG(LogUDPManager, Log, TEXT("    - Port %d: %s"),
                RecvConfig.LocalPort, *RecvConfig.Description);
        }
    }

    if (CurrentConfig.Senders.Num() > 0)
    {
        UE_LOG(LogUDPManager, Log, TEXT("  Senders:"));
        for (const FUDPSenderConfig& SendConfig : CurrentConfig.Senders)
        {
            if (SendConfig.bEnabled)
            {
                UE_LOG(LogUDPManager, Log, TEXT("    - %s:%d: %s"),
                    *SendConfig.TargetIP, SendConfig.TargetPort, *SendConfig.Description);
            }
        }
    }

    return true;
}

void UUDPManager::ShutdownUDP()
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogUDPManager, Log, TEXT("Shutting down UDP Manager..."));

    // 打印最终统计
    PrintStatistics();

    // 清理资源
    CleanupResources();

    bIsInitialized = false;

    UE_LOG(LogUDPManager, Log, TEXT("UDP Manager shutdown complete"));
}

void UUDPManager::GetNetworkStatistics(FUDPNetworkStatistics& OutStatistics)
{
    OutStatistics.bIsOnline = bIsInitialized;

    if (!bIsInitialized)
    {
        OutStatistics = FUDPNetworkStatistics();
        return;
    }

    OutStatistics.PacketsReceived = ReceiverRunnable->GetPacketsReceived();
    OutStatistics.PacketsDropped = ReceiverRunnable->GetPacketsDropped();
    OutStatistics.SocketErrors = ReceiverRunnable->GetSocketErrors();
    OutStatistics.ActiveReceiverSockets = ReceiverRunnable->GetActiveSocketCount();

    const int32 TotalPackets = OutStatistics.PacketsReceived + OutStatistics.PacketsDropped;
    OutStatistics.PacketLossRate = (TotalPackets > 0)
        ? (OutStatistics.PacketsDropped * 100.0f / TotalPackets)
        : 0.0f;

    OutStatistics.BufferPoolInUse = BufferPool->GetInUseCount();
    OutStatistics.BufferPoolPeakUsage = BufferPool->GetPeakUsage();
    OutStatistics.BufferPoolDynamicAllocations = BufferPool->GetDynamicAllocations();

    OutStatistics.QueuedPackets = 0;
}

void UUDPManager::SetMaxPacketsPerFrame(int32 MaxCount)
{
    if (MaxCount < 1)
    {
        UE_LOG(LogUDPManager, Warning,
            TEXT("Invalid MaxPacketsPerFrame: %d, using 1"),
            MaxCount
        );
        MaxCount = 1;
    }

    if (MaxCount > 500)
    {
        UE_LOG(LogUDPManager, Warning,
            TEXT("MaxPacketsPerFrame very high: %d, may cause frame drops"),
            MaxCount
        );
    }

    MaxPacketsPerFrame = MaxCount;

    UE_LOG(LogUDPManager, Log,
        TEXT("MaxPacketsPerFrame set to %d"),
        MaxPacketsPerFrame
    );
}

void UUDPManager::PrintStatistics()
{
    if (!bIsInitialized)
    {
        UE_LOG(LogUDPManager, Warning, TEXT("UDP Manager not initialized, no statistics available"));
        return;
    }

    FUDPNetworkStatistics Stats;
    GetNetworkStatistics(Stats);

    UE_LOG(LogUDPManager, Log, TEXT("========== UDP Manager Statistics =========="));
    UE_LOG(LogUDPManager, Log, TEXT("  Network:"));
    UE_LOG(LogUDPManager, Log, TEXT("    Online Status       : %s"), Stats.bIsOnline ? TEXT("Online") : TEXT("Offline"));
    UE_LOG(LogUDPManager, Log, TEXT("    Active Sockets      : %d"), Stats.ActiveReceiverSockets);

    // 打印所有接收端口
    UE_LOG(LogUDPManager, Log, TEXT("    Receiver Ports      :"));
    for (const FUDPReceiverConfig& Config : CurrentConfig.Receivers)
    {
        if (Config.bEnabled)
        {
            UE_LOG(LogUDPManager, Log, TEXT("      - Port %d: %s"),
                Config.LocalPort, *Config.Description);
        }
    }

    UE_LOG(LogUDPManager, Log, TEXT("    Packets Received    : %d"), Stats.PacketsReceived);
    UE_LOG(LogUDPManager, Log, TEXT("    Packets Dropped     : %d"), Stats.PacketsDropped);
    UE_LOG(LogUDPManager, Log, TEXT("    Packet Loss Rate    : %.2f%%"), Stats.PacketLossRate);
    UE_LOG(LogUDPManager, Log, TEXT("    Socket Errors       : %d"), Stats.SocketErrors);
    UE_LOG(LogUDPManager, Log, TEXT("  Processing:"));
    UE_LOG(LogUDPManager, Log, TEXT("    Total Processed     : %d"), TotalPacketsProcessed);
    UE_LOG(LogUDPManager, Log, TEXT("    Max Per Frame       : %d"), MaxPacketsPerFrame);
    UE_LOG(LogUDPManager, Log, TEXT("  Buffer Pool:"));
    UE_LOG(LogUDPManager, Log, TEXT("    In Use              : %d"), Stats.BufferPoolInUse);
    UE_LOG(LogUDPManager, Log, TEXT("    Peak Usage          : %d"), Stats.BufferPoolPeakUsage);
    UE_LOG(LogUDPManager, Log, TEXT("    Dynamic Allocations : %d"), Stats.BufferPoolDynamicAllocations);
    UE_LOG(LogUDPManager, Log, TEXT("============================================"));

    if (BufferPool.IsValid())
    {
        BufferPool->PrintStatistics();
    }
}

// ========== C++ 接口实现 ==========

void UUDPManager::RegisterMessageHandler(uint16 MessageType, IUDPMessageHandler* Handler)
{
    if (!MessageDispatcher.IsValid())
    {
        UE_LOG(LogUDPManager, Error,
            TEXT("Cannot register handler: MessageDispatcher not initialized")
        );
        return;
    }

    if (!Handler)
    {
        UE_LOG(LogUDPManager, Error,
            TEXT("Cannot register null handler for message type 0x%04X"),
            MessageType
        );
        return;
    }

    MessageDispatcher->RegisterHandler(MessageType, Handler);

    UE_LOG(LogUDPManager, Log,
        TEXT("Registered message handler for type 0x%04X"),
        MessageType
    );
}

void UUDPManager::UnregisterMessageHandler(uint16 MessageType)
{
    if (!MessageDispatcher.IsValid())
    {
        return;
    }

    MessageDispatcher->UnregisterHandler(MessageType);

    UE_LOG(LogUDPManager, Log,
        TEXT("Unregistered message handler for type 0x%04X"),
        MessageType
    );
}

// ========== JSON 发送接口实现 ==========

bool UUDPManager::SendJSONMessage(uint16 MessageType,
    const FString& JsonString,
    int32 TargetPort,
    const FString& TargetIP)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogUDPManager, Warning, TEXT("SendJSONMessage: UDPManager not initialized"));
        return false;
    }

    if (JsonString.IsEmpty())
    {
        UE_LOG(LogUDPManager, Warning, TEXT("SendJSONMessage: empty JSON string"));
        return false;
    }

    // 1. 懒初始化 SendSocket（首次发送时创建，之后复用）
    if (!SendSocket)
    {
        ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!SocketSub)
        {
            UE_LOG(LogUDPManager, Error, TEXT("SendJSONMessage: SocketSubsystem unavailable"));
            return false;
        }

        SendSocket = SocketSub->CreateSocket(NAME_DGram, TEXT("UDPManager_SendSocket"), false);
        if (!SendSocket)
        {
            UE_LOG(LogUDPManager, Error, TEXT("SendJSONMessage: failed to create send socket"));
            return false;
        }

        SendSocket->SetBroadcast(true);
        SendSocket->SetNonBlocking(true);

        UE_LOG(LogUDPManager, Log, TEXT("SendJSONMessage: send socket created (lazy init)"));
    }

    // 2. 将 FString 转为 UTF-8 字节
    FTCHARToUTF8 Converter(*JsonString);
    const int32  JsonByteLen = Converter.Length();
    const uint8* JsonBytes = reinterpret_cast<const uint8*>(Converter.Get());

    // 3. 构造完整帧：[FUDPMessageHeader 8B][JSON UTF-8 字节]
    const int32 TotalLen = static_cast<int32>(sizeof(FUDPMessageHeader)) + JsonByteLen;
    TArray<uint8> Frame;
    Frame.SetNumUninitialized(TotalLen);

    FUDPMessageHeader* Header = reinterpret_cast<FUDPMessageHeader*>(Frame.GetData());
    Header->MagicNumber = 0xABCD;
    Header->MessageType = MessageType;
    Header->PayloadLength = static_cast<uint32>(JsonByteLen);

    FMemory::Memcpy(Frame.GetData() + sizeof(FUDPMessageHeader), JsonBytes, JsonByteLen);

    // 4. 解析目标地址
    ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    TSharedRef<FInternetAddr> TargetAddr = SocketSub->CreateInternetAddr();

    bool bIsValid = false;
    TargetAddr->SetIp(*TargetIP, bIsValid);
    if (!bIsValid)
    {
        UE_LOG(LogUDPManager, Warning,
            TEXT("SendJSONMessage: invalid target IP '%s'"), *TargetIP);
        return false;
    }
    TargetAddr->SetPort(TargetPort);

    // 5. 发送
    int32 BytesSent = 0;
    const bool bOK = SendSocket->SendTo(Frame.GetData(), TotalLen, BytesSent, *TargetAddr);

    if (!bOK || BytesSent != TotalLen)
    {
        UE_LOG(LogUDPManager, Warning,
            TEXT("SendJSONMessage: send failed or partial (sent=%d, total=%d, dst=%s:%d)"),
            BytesSent, TotalLen, *TargetIP, TargetPort);
        return false;
    }

    UE_LOG(LogUDPManager, VeryVerbose,
        TEXT("SendJSONMessage: OK type=0x%04X bytes=%d dst=%s:%d"),
        MessageType, TotalLen, *TargetIP, TargetPort);

    return true;
}


// ========== 内部辅助函数 ==========

void UUDPManager::ProcessPacket(FRawPacket& Packet)
{
    if (!Packet.Data.IsValid() || Packet.Length <= 0)
    {
        UE_LOG(LogUDPManager, Warning, TEXT("Invalid packet received, skipping"));
        return;
    }

    // 根据协议类型路由到对应处理函数
    switch (Packet.ProtocolType)
    {
    case EProtocolType::MAVLink:
        ProcessMAVLinkPacket(Packet);
        break;

    case EProtocolType::CustomUDP:
        ProcessCustomUDPPacket(Packet);
        break;

    case EProtocolType::Unknown:
    default:
        // 接收线程不应该将Unknown包推入队列，但作为保险仍然处理
        UE_LOG(LogUDPManager, Warning,
            TEXT("Received packet with Unknown protocol type, discarding (%d bytes from %s)"),
            Packet.Length, *Packet.SenderAddress);
        break;
    }

    // 归还缓冲区（无论哪种协议）
    if (BufferPool.IsValid() && Packet.Data.IsValid())
    {
        BufferPool->Release(Packet.Data);
    }
}

void UUDPManager::ProcessCustomUDPPacket(FRawPacket& Packet)
{
    // 直接交给 MessageDispatcher 按 MessageType 路由
    // Dispatcher 会校验魔数和长度，JSON类型由 EventMarkerManager 注册的 Handler 处理
    if (MessageDispatcher.IsValid())
    {
        MessageDispatcher->Dispatch(Packet.Data->GetData(), Packet.Length);
    }
}

void UUDPManager::ProcessMAVLinkPacket(FRawPacket& Packet)
{
    const uint8* Data = Packet.Data->GetData();
    const int32 Length = Packet.Length;
    const double ReceiveTime = Packet.ReceiveTime;

    // 使用 mavlink_parse_char() 逐字节解析，自动处理帧同步和CRC校验
    // channel=0: 所有MAVLink包共用同一通道（单路接收）
    mavlink_message_t Message;
    mavlink_status_t Status;

    for (int32 i = 0; i < Length; ++i)
    {
        const uint8 ParseResult = mavlink_parse_char(MAVLINK_COMM_0, Data[i], &Message, &Status);

        if (ParseResult == MAVLINK_FRAMING_OK)
        {
            // 成功解析到一个完整的MAVLink消息，分发处理
            HandleMAVLinkMessage(Message, Message.sysid, ReceiveTime);
        }
        else if (ParseResult == MAVLINK_FRAMING_BAD_CRC)
        {
            UE_LOG(LogUDPManager, Warning,
                TEXT("MAVLink CRC error: msgid=%u from sysid=%u"),
                Message.msgid, Message.sysid);
        }
        // MAVLINK_FRAMING_INCOMPLETE: 继续等待后续字节，正常情况忽略
    }
}

void UUDPManager::HandleMAVLinkMessage(const mavlink_message_t& Message, uint8 SystemID, double ReceiveTime)
{
    switch (Message.msgid)
    {
        // ── HEARTBEAT (msg_id = 0) ──────────────────────────────────────────
    case MAVLINK_MSG_ID_HEARTBEAT:
    {
        mavlink_heartbeat_t Heartbeat;
        mavlink_msg_heartbeat_decode(&Message, &Heartbeat);

        FMAVLinkHeartbeatData Data;
        Data.SystemID = SystemID;
        Data.ComponentID = Message.compid;
        Data.CustomMode = static_cast<int32>(Heartbeat.custom_mode);
        Data.MAVType = static_cast<int32>(Heartbeat.type);
        Data.Autopilot = static_cast<int32>(Heartbeat.autopilot);
        Data.SystemStatus = static_cast<int32>(Heartbeat.system_status);
        Data.ReceiveTime = ReceiveTime;

        UE_LOG(LogUDPManager, Verbose,
            TEXT("MAVLink HEARTBEAT: sysid=%d type=%d status=%d"),
            SystemID, Heartbeat.type, Heartbeat.system_status);

        OnMAVLinkHeartbeat.Broadcast(Data);
        break;
    }

    // ── LINKS_MOBILE_ROBOT_STATES (msg_id = 13001) ─────────────────────
    case MAVLINK_MSG_ID_LINKS_MOBILE_ROBOT_STATES:
    {
        mavlink_links_mobile_robot_states_t RobotStates;
        mavlink_msg_links_mobile_robot_states_decode(&Message, &RobotStates);

        FMAVLinkDeviceStateData Data;
        Data.SystemID = SystemID;
        Data.DeviceType = static_cast<int32>(RobotStates.type);
        Data.Status = static_cast<int32>(RobotStates.status);
        Data.BatteryRemaining = static_cast<int32>(RobotStates.battery_remaining);
        Data.GPSFixType = static_cast<int32>(RobotStates.fix_type);        // 协议字段名 fix_type
        Data.SatellitesVisible = static_cast<int32>(RobotStates.satellites_visible);
        Data.ReceiveTime = ReceiveTime;

        // GPS 单位换算：
        //   lat/lon：int32，单位 degE7（度×1e7），转为度
        //   alt：    int32，单位 mm（毫米），转为米
        Data.GPSLatitude = static_cast<double>(RobotStates.lat) * 1e-7;
        Data.GPSLongitude = static_cast<double>(RobotStates.lon) * 1e-7;
        Data.GPSAltitude = static_cast<float>(RobotStates.alt) * 0.001f;

        // NED 位置（协议单位：米，字段 x/y/z）
        Data.NEDPosition = FVector(RobotStates.x, RobotStates.y, RobotStates.z);

        // NED 速度（协议单位：米/秒，字段 vx/vy/vz）
        Data.NEDVelocity = FVector(RobotStates.vx, RobotStates.vy, RobotStates.vz);

        // 角速度（rad/s，字段 rollspeed/pitchspeed/yawspeed）
        Data.AngularVelocity = FVector(RobotStates.rollspeed, RobotStates.pitchspeed, RobotStates.yawspeed);

        // 四元数：协议字段名 q1/q2/q3/q4（独立成员，非数组）
        //   q1=w  q2=x  q3=y  q4=z  → UE FQuat(x, y, z, w)
        Data.Quaternion = FQuat(
            RobotStates.q2,   // x
            RobotStates.q3,   // y
            RobotStates.q4,   // z
            RobotStates.q1    // w
        );

        // 执行器状态（协议定义 float[24]，字段名 actuator）
        constexpr int32 ActuatorCount = 24;
        Data.ActuatorStates.SetNumUninitialized(ActuatorCount);
        for (int32 i = 0; i < ActuatorCount; ++i)
        {
            Data.ActuatorStates[i] = RobotStates.actuator[i];
        }

        UE_LOG(LogUDPManager, Verbose,
            TEXT("MAVLink DEVICE_STATE: sysid=%d type=%d pos=(%.2f,%.2f,%.2f) bat=%d%% gps=(%.6f,%.6f,%.1fm)"),
            SystemID, RobotStates.type,
            RobotStates.x, RobotStates.y, RobotStates.z,
            RobotStates.battery_remaining,
            Data.GPSLatitude, Data.GPSLongitude, Data.GPSAltitude);

        OnMAVLinkDeviceState.Broadcast(Data);
        break;
    }

    // ── LINKS_WAYPOINTS (msg_id = 13002) ───────────────────────────────
    case MAVLINK_MSG_ID_LINKS_WAYPOINTS:
    {
        mavlink_links_waypoints_t MAVWaypoints;
        mavlink_msg_links_waypoints_decode(&Message, &MAVWaypoints);

        FMAVLinkWaypointsData Data;
        Data.SystemID = SystemID;
        Data.ReceiveTime = ReceiveTime;

        // 协议字段 count 表示有效点数（最大15），只读取有效点，避免传入全零填充点
        const int32 ValidCount = FMath::Clamp(static_cast<int32>(MAVWaypoints.count), 0, 15);
        Data.Waypoints.Reserve(ValidCount);
        for (int32 i = 0; i < ValidCount; ++i)
        {
            // x=North(m)  y=East(m)  z=Down(m)，保持NED，由TrajectoryManager转换
            Data.Waypoints.Add(FVector(MAVWaypoints.x[i], MAVWaypoints.y[i], MAVWaypoints.z[i]));
        }

        UE_LOG(LogUDPManager, Verbose,
            TEXT("MAVLink WAYPOINTS: sysid=%d valid_points=%d"),
            SystemID, ValidCount);

        OnMAVLinkWaypoints.Broadcast(Data);
        break;
    }

    default:
        UE_LOG(LogUDPManager, Verbose,
            TEXT("Unhandled MAVLink message: msgid=%u sysid=%u"),
            Message.msgid, SystemID);
        break;
    }
}

void UUDPManager::UpdateStatistics(double CurrentTime)
{
    if (CurrentTime - LastStatsPrintTime >= StatsPrintInterval)
    {
        PrintStatistics();
        LastStatsPrintTime = CurrentTime;
    }
}

void UUDPManager::CleanupResources()
{
    // 1. 停止接收线程
    if (ReceiverThread)
    {
        UE_LOG(LogUDPManager, Log, TEXT("Stopping receiver thread..."));

        if (ReceiverRunnable.IsValid())
        {
            ReceiverRunnable->Stop();
        }

        ReceiverThread->WaitForCompletion();
        delete ReceiverThread;
        ReceiverThread = nullptr;

        UE_LOG(LogUDPManager, Log, TEXT("Receiver thread stopped"));
    }

    // 2. 清理Runnable（会自动清理所有Socket）
    ReceiverRunnable.Reset();

    // 3. 清理队列中的剩余包
    int32 RemainingPackets = 0;
    FRawPacket Packet;
    while (RawPacketQueue.Dequeue(Packet))
    {
        if (Packet.Data.IsValid() && BufferPool.IsValid())
        {
            BufferPool->Release(Packet.Data);
        }
        RemainingPackets++;
    }

    if (RemainingPackets > 0)
    {
        UE_LOG(LogUDPManager, Log,
            TEXT("Cleaned up %d remaining packets from queue"),
            RemainingPackets
        );
    }

    // 4. 关闭发送 Socket
    if (SendSocket)
    {
        UE_LOG(LogUDPManager, Log, TEXT("Closing send socket..."));
        SendSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SendSocket);
        SendSocket = nullptr;
        UE_LOG(LogUDPManager, Log, TEXT("Send socket closed"));
    }

    // 4. 清理其他组件
    MessageDispatcher.Reset();
    BufferPool.Reset();

    // 5. 重置状态
    TotalPacketsProcessed = 0;
    CurrentConfig = FUDPConfiguration();
}

// ========== 动态配置接口实现 ==========

bool UUDPManager::AddReceiver(int32 Port, const FString& Description)
{
    FString ErrorMsg;
    if (!ValidatePort(Port, ErrorMsg))
    {
        UE_LOG(LogUDPManager, Error, TEXT("Cannot add receiver: %s"), *ErrorMsg);
        return false;
    }

    for (const FUDPReceiverConfig& Config : CurrentConfig.Receivers)
    {
        if (Config.LocalPort == Port)
        {
            UE_LOG(LogUDPManager, Warning, TEXT("Receiver on port %d already exists"), Port);
            return false;
        }
    }

    CurrentConfig.AddReceiver(Port, Description, true);
    UE_LOG(LogUDPManager, Log, TEXT("Added receiver: Port %d - %s"), Port, *Description);

    if (bIsInitialized)
    {
        UE_LOG(LogUDPManager, Warning,
            TEXT("UDP system is running, restart required to apply changes. Call UpdateConfiguration() or restart manually."));
    }

    return true;
}

bool UUDPManager::RemoveReceiver(int32 Port)
{
    int32 RemovedIndex = -1;
    for (int32 i = 0; i < CurrentConfig.Receivers.Num(); ++i)
    {
        if (CurrentConfig.Receivers[i].LocalPort == Port)
        {
            RemovedIndex = i;
            break;
        }
    }

    if (RemovedIndex == -1)
    {
        UE_LOG(LogUDPManager, Warning, TEXT("Receiver on port %d not found"), Port);
        return false;
    }

    CurrentConfig.Receivers.RemoveAt(RemovedIndex);
    UE_LOG(LogUDPManager, Log, TEXT("Removed receiver on port %d"), Port);

    if (bIsInitialized)
    {
        UE_LOG(LogUDPManager, Warning,
            TEXT("UDP system is running, restart required to apply changes. Call UpdateConfiguration() or restart manually."));
    }

    return true;
}

bool UUDPManager::SetReceiverEnabled(int32 Port, bool bEnabled)
{
    bool bFound = false;
    for (FUDPReceiverConfig& Config : CurrentConfig.Receivers)
    {
        if (Config.LocalPort == Port)
        {
            Config.bEnabled = bEnabled;
            bFound = true;
            UE_LOG(LogUDPManager, Log, TEXT("Receiver on port %d %s"),
                Port, bEnabled ? TEXT("enabled") : TEXT("disabled"));
            break;
        }
    }

    if (!bFound)
    {
        UE_LOG(LogUDPManager, Warning, TEXT("Receiver on port %d not found"), Port);
        return false;
    }

    if (bIsInitialized)
    {
        UE_LOG(LogUDPManager, Warning,
            TEXT("UDP system is running, restart required to apply changes. Call UpdateConfiguration() or restart manually."));
    }

    return true;
}

bool UUDPManager::UpdateConfiguration(const FUDPConfiguration& NewConfig)
{
    if (NewConfig.GetEnabledReceiverCount() == 0)
    {
        UE_LOG(LogUDPManager, Error, TEXT("New configuration has no enabled receivers"));
        return false;
    }

    for (const FUDPReceiverConfig& RecvConfig : NewConfig.Receivers)
    {
        if (RecvConfig.bEnabled)
        {
            FString ErrorMsg;
            if (!ValidatePort(RecvConfig.LocalPort, ErrorMsg))
            {
                UE_LOG(LogUDPManager, Error, TEXT("Invalid receiver config: %s"), *ErrorMsg);
                return false;
            }
        }
    }

    UE_LOG(LogUDPManager, Log, TEXT("Updating UDP configuration..."));

    if (bIsInitialized)
    {
        UE_LOG(LogUDPManager, Log, TEXT("Shutting down to apply new configuration..."));
        ShutdownUDP();

        FPlatformProcess::Sleep(0.1f);

        if (!InitializeInternal(NewConfig, BufferPoolSize))
        {
            UE_LOG(LogUDPManager, Error, TEXT("Failed to reinitialize with new configuration"));
            return false;
        }

        UE_LOG(LogUDPManager, Log, TEXT("UDP configuration updated and applied successfully"));
        ConfigurationChangedDelegate.Broadcast();
        return true;
    }
    else
    {
        CurrentConfig = NewConfig;
        UE_LOG(LogUDPManager, Log, TEXT("Configuration updated (will be applied on next initialization)"));
        return true;
    }
}

TArray<FUDPReceiverConfig> UUDPManager::GetReceivers() const
{
    return CurrentConfig.Receivers;
}

bool UUDPManager::GetReceiverByPort(int32 Port, FUDPReceiverConfig& OutConfig) const
{
    for (const FUDPReceiverConfig& Config : CurrentConfig.Receivers)
    {
        if (Config.LocalPort == Port)
        {
            OutConfig = Config;
            return true;
        }
    }
    return false;
}

bool UUDPManager::ValidatePort(int32 Port, FString& OutErrorMessage) const
{
    if (Port < 1024 || Port > 65535)
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid port: %d (must be 1024-65535)"), Port);
        return false;
    }

    OutErrorMessage.Empty();
    return true;
}