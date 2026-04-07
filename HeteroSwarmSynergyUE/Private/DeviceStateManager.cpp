#include "DeviceStateManager.h"
#include "ProtocolMappingLibrary.h"
#include "UDPManager.h"
#include "CoordinateConverter.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeviceStateManager, Log, All);

// ========== 构造与析构 ==========

UDeviceStateManager::UDeviceStateManager()
    : UDPManager(nullptr)
    , bIsInitialized(false)
    , TotalMessagesProcessed(0)
    , TotalStateUpdates(0)
{
    UE_LOG(LogDeviceStateManager, Log, TEXT("DeviceStateManager constructed"));
}

UDeviceStateManager::~UDeviceStateManager()
{
    Shutdown();
    UE_LOG(LogDeviceStateManager, Log, TEXT("DeviceStateManager destroyed"));
}

// ========== 初始化与关闭 ==========

bool UDeviceStateManager::Initialize(UUDPManager* InUDPManager)
{
    if (bIsInitialized)
    {
        UE_LOG(LogDeviceStateManager, Warning, TEXT("Already initialized"));
        return false;
    }

    if (!InUDPManager)
    {
        UE_LOG(LogDeviceStateManager, Error, TEXT("Invalid UDPManager pointer"));
        return false;
    }

    UDPManager = InUDPManager;

    // 注册旧协议消息处理器（兼容保留）
    UDPManager->RegisterMessageHandler(0x0001, this);  // 单个设备状态
    UDPManager->RegisterMessageHandler(0x0011, this);  // 批量设备状态

    // MAVLink 设备状态委托由 GameInstance 负责绑定（此处不绑定，避免耦合）

    bIsInitialized = true;

    UE_LOG(LogDeviceStateManager, Log,
        TEXT("DeviceStateManager initialized (LegacyUDP: registered 0x0001/0x0011, MAVLink: bound by GameInstance)"));

    return true;
}

void UDeviceStateManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogDeviceStateManager, Log, TEXT("Shutting down DeviceStateManager..."));
    PrintStatistics();

    if (UDPManager)
    {
        UDPManager->UnregisterMessageHandler(0x0001);
        UDPManager->UnregisterMessageHandler(0x0011);
        UDPManager = nullptr;
    }

    DeviceCache.Empty();
    bIsInitialized = false;

    UE_LOG(LogDeviceStateManager, Log, TEXT("DeviceStateManager shutdown complete"));
}

void UDeviceStateManager::Tick(float DeltaTime)
{
    // v2.0: 超时检测已移交 HeartbeatManager，此处暂为空实现，保留供未来扩展
}

// ========== IUDPMessageHandler 接口实现 ==========

void UDeviceStateManager::HandleMessage(const uint8* Data, uint32 Length)
{
    if (!Data || Length == 0)
    {
        UE_LOG(LogDeviceStateManager, Warning, TEXT("Received invalid message"));
        return;
    }

    TotalMessagesProcessed++;

    // 根据长度判断消息类型
    // 单个设备状态：44字节
    // 批量设备状态：4 + 44N 字节（N >= 1）

    if (Length == sizeof(FDeviceStatePacket))
    {
        // 单个设备状态
        ProcessSingleDeviceState(Data, Length);
    }
    else if (Length >= 4 + sizeof(FDeviceStatePacket))
    {
        // 批量设备状态
        ProcessBatchDeviceState(Data, Length);
    }
    else
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Invalid message length: %d bytes (expected 44 or 4+44N)"),
            Length
        );
    }
}

// ========== 内部处理函数 ==========

void UDeviceStateManager::ProcessSingleDeviceState(const uint8* Data, uint32 Length)
{
    check(Length == sizeof(FDeviceStatePacket));

    const FDeviceStatePacket* Packet = reinterpret_cast<const FDeviceStatePacket*>(Data);

    // 验证数据
    if (!ValidateDeviceStatePacket(*Packet))
    {
        return;
    }

    // 处理更新
    ProcessSingleUpdate(*Packet);

    UE_LOG(LogDeviceStateManager, Verbose,
        TEXT("Processed single device state: ID=%d"),
        Packet->DeviceID
    );
}

void UDeviceStateManager::ProcessBatchDeviceState(const uint8* Data, uint32 Length)
{
    // 解析设备数量
    const uint32* DeviceCountPtr = reinterpret_cast<const uint32*>(Data);
    const uint32 DeviceCount = *DeviceCountPtr;

    // 验证长度
    const uint32 ExpectedLength = sizeof(uint32) + DeviceCount * sizeof(FDeviceStatePacket);
    if (Length != ExpectedLength)
    {
        UE_LOG(LogDeviceStateManager, Error,
            TEXT("Batch message length mismatch: Expected %d, Got %d (DeviceCount=%d)"),
            ExpectedLength,
            Length,
            DeviceCount
        );
        return;
    }

    // 解析设备状态数组
    const FDeviceStatePacket* Packets = reinterpret_cast<const FDeviceStatePacket*>(Data + sizeof(uint32));

    UE_LOG(LogDeviceStateManager, Verbose,
        TEXT("Processing batch: %d devices"),
        DeviceCount
    );

    // 逐个处理
    int32 ValidCount = 0;
    for (uint32 i = 0; i < DeviceCount; ++i)
    {
        if (ValidateDeviceStatePacket(Packets[i]))
        {
            ProcessSingleUpdate(Packets[i]);
            ValidCount++;
        }
    }

    // 触发批量更新完成事件
    OnBatchUpdateComplete.Broadcast(ValidCount);

    UE_LOG(LogDeviceStateManager, Verbose,
        TEXT("Batch processing complete: %d/%d valid devices"),
        ValidCount,
        DeviceCount
    );
}

void UDeviceStateManager::ProcessSingleUpdate(const FDeviceStatePacket& Packet)
{
    // 1. 坐标转换（NED → UE）
    FVector UELocation = UCoordinateConverter::NEDToUE(Packet.Position);
    FRotator UERotation = UCoordinateConverter::AttitudeToUE(Packet.Attitude);
    FVector UEVelocity = UCoordinateConverter::NEDToUE(Packet.Velocity);

    const uint8 NormalizedDeviceType = FProtocolMapping::NormalizeDeviceTypeCode(Packet.DeviceType);

    if (NormalizedDeviceType == static_cast<uint8>(EUnifiedDeviceType::Unknown) &&
        Packet.DeviceType != 255)
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Unknown legacy device type: DeviceID=%d RawType=%d"),
            Packet.DeviceID,
            Packet.DeviceType);
    }

    // 2. 查找或创建设备状态
    FDeviceRuntimeState* StatePtr = DeviceCache.Find(Packet.DeviceID);
    const bool bIsNewDevice = (StatePtr == nullptr);

    if (bIsNewDevice)
    {
        // 新设备，创建状态
        FDeviceRuntimeState NewState;
        NewState.DeviceID = Packet.DeviceID;
        NewState.DeviceType = NormalizedDeviceType;
        NewState.Location = UELocation;
        NewState.Rotation = UERotation;
        NewState.Velocity = UEVelocity;
        NewState.LastUpdateTime = FPlatformTime::Seconds();
        NewState.bIsOnline = true;
        NewState.UpdateCount = 1;

        DeviceCache.Add(Packet.DeviceID, NewState);
        StatePtr = DeviceCache.Find(Packet.DeviceID);

        UE_LOG(LogDeviceStateManager, Log,
            TEXT("New device detected: ID=%d RawType=%d Type=%d(%s) Location=%s"),
            Packet.DeviceID,
            Packet.DeviceType,
            NormalizedDeviceType,
            *FProtocolMapping::GetDeviceTypeName(NormalizedDeviceType),
            *UELocation.ToString()
        );

        // 触发新设备事件
        OnDeviceAdded.Broadcast(Packet.DeviceID, NormalizedDeviceType);
    }
    else
    {
        // 更新现有设备
        StatePtr->DeviceType = NormalizedDeviceType;
        StatePtr->Location = UELocation;
        StatePtr->Rotation = UERotation;
        StatePtr->Velocity = UEVelocity;
        StatePtr->LastUpdateTime = FPlatformTime::Seconds();
        StatePtr->bIsOnline = true;
        StatePtr->UpdateCount++;
    }

    // 3. 触发状态更新事件
    OnDeviceStateChanged.Broadcast(Packet.DeviceID, *StatePtr);

    // 5. 统计
    TotalStateUpdates++;

    UE_LOG(LogDeviceStateManager, VeryVerbose,
        TEXT("Device %d updated: Type=%d(%s) Loc=%s Rot=%s Vel=%s (UpdateCount=%d)"),
        Packet.DeviceID,
        StatePtr->DeviceType,
        *FProtocolMapping::GetDeviceTypeName(StatePtr->DeviceType),
        *UELocation.ToCompactString(),
        *UERotation.ToCompactString(),
        *UEVelocity.ToCompactString(),
        StatePtr->UpdateCount
    );
}

// ========== MAVLink 处理函数实现 ==========

void UDeviceStateManager::HandleMAVLinkDeviceState(const FMAVLinkDeviceStateData& StateData)
{
    if (!bIsInitialized)
    {
        return;
    }

    const int32 DeviceID = StateData.SystemID;
    if (DeviceID <= 0)
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Invalid SystemID in MAVLink device state: %d"), DeviceID);
        return;
    }

    const uint8 NormalizedDeviceType = FProtocolMapping::NormalizeDeviceTypeCode(StateData.DeviceType);

    if (NormalizedDeviceType == static_cast<uint8>(EUnifiedDeviceType::Unknown))
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Unknown MAVLink device type: SystemID=%d RawType=%d"),
            DeviceID,
            StateData.DeviceType);
    }

    // 查找或创建设备状态
    FDeviceRuntimeState* StatePtr = DeviceCache.Find(DeviceID);
    const bool bIsNewDevice = (StatePtr == nullptr);

    if (bIsNewDevice)
    {
        FDeviceRuntimeState NewState;
        NewState.DeviceID = DeviceID;
        NewState.DeviceType = NormalizedDeviceType;

        DeviceCache.Add(DeviceID, NewState);
        StatePtr = DeviceCache.Find(DeviceID);

        UE_LOG(LogDeviceStateManager, Log,
            TEXT("New device state entry created: SystemID=%d RawType=%d Type=%d(%s)"),
            DeviceID,
            StateData.DeviceType,
            NormalizedDeviceType,
            *FProtocolMapping::GetDeviceTypeName(NormalizedDeviceType));

        // 首次出现才触发 OnDeviceAdded（HeartbeatManager 也会触发，但来源不同，此处保留）
        OnDeviceAdded.Broadcast(DeviceID, NormalizedDeviceType);
    }

    // 将 MAVLink 数据写入运行时状态
    ApplyMAVLinkStateToRuntime(StateData, *StatePtr);

    // 处理执行器状态
    UpdateActuatorStates(StateData, *StatePtr);

    // 触发状态更新事件
    OnDeviceStateChanged.Broadcast(DeviceID, *StatePtr);

    TotalStateUpdates++;

    UE_LOG(LogDeviceStateManager, VeryVerbose,
        TEXT("MAVLink device state updated: SystemID=%d RawType=%d Type=%d(%s) Loc=%s Bat=%d%%"),
        DeviceID,
        StateData.DeviceType,
        StatePtr->DeviceType,
        *FProtocolMapping::GetDeviceTypeName(StatePtr->DeviceType),
        *StatePtr->Location.ToCompactString(),
        StateData.BatteryRemaining);
}

void UDeviceStateManager::ApplyMAVLinkStateToRuntime(
    const FMAVLinkDeviceStateData& StateData,
    FDeviceRuntimeState& OutState) const
{
    OutState.DeviceID = StateData.SystemID;
    OutState.DeviceType = FProtocolMapping::NormalizeDeviceTypeCode(StateData.DeviceType);
    OutState.bIsOnline = true;
    OutState.ArmStatus = StateData.Status;
    OutState.BatteryRemaining = StateData.BatteryRemaining;

    // GPS
    OutState.GPSLatitude = StateData.GPSLatitude;
    OutState.GPSLongitude = StateData.GPSLongitude;
    OutState.GPSAltitude = StateData.GPSAltitude;
    OutState.GPSFixType = StateData.GPSFixType;
    OutState.SatellitesVisible = StateData.SatellitesVisible;

    // NED 位置 → UE 坐标（米 → 厘米，Z 轴翻转）
    // NEDPosition: X=North Y=East Z=Down
    OutState.Location = FVector(
        StateData.NEDPosition.X * 100.0f,   // North → UE X
        StateData.NEDPosition.Y * 100.0f,   // East  → UE Y
        -StateData.NEDPosition.Z * 100.0f   // Down  → -UE Z（向上为正）
    );

    // NED 速度 → UE 坐标（同上）
    OutState.Velocity = FVector(
        StateData.NEDVelocity.X * 100.0f,
        StateData.NEDVelocity.Y * 100.0f,
        -StateData.NEDVelocity.Z * 100.0f
    );

    // 角速度直接保留 NED 坐标系（rad/s），由动画蓝图自行处理
    OutState.AngularVelocity = StateData.AngularVelocity;

    // 四元数：StateData.Quaternion 已在 UDPManager 中完成 MAVLink→UE 顺序转换
    FQuat Q = StateData.Quaternion;
    if (Q.IsNormalized() == false)
    {
        Q.Normalize();
    }

    if (!Q.ContainsNaN() && Q.SizeSquared() > SMALL_NUMBER)
    {
        OutState.Quaternion = Q;
        OutState.Rotation = Q.Rotator();
    }
    else
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Invalid quaternion for SystemID=%d, keeping previous rotation"),
            StateData.SystemID);
    }

    OutState.LastUpdateTime = StateData.ReceiveTime;
    OutState.UpdateCount++;
}

void UDeviceStateManager::UpdateActuatorStates(
    const FMAVLinkDeviceStateData& StateData,
    FDeviceRuntimeState& OutState) const
{
    // 直接复制执行器数组
    OutState.ActuatorStates = StateData.ActuatorStates;

    // 后续扩展提示（当前仅存储数据，动画驱动由蓝图 Actor 的 Tick 读取 ActuatorStates 实现）:
    // 四旋翼 [0-3]: 螺旋桨 RPM → 蓝图中驱动 RotatingMovementComponent 或直接设置骨骼旋转
    // 四旋翼 [4-6]: 云台角度(deg) → 设置云台骨骼/Socket 旋转
    // 机器狗 [0-11]: 关节角度(deg) → 通过 AnimInstance SetBoneRotation
    // 机器狗 [12-14]: 云台角度(deg)
}


bool UDeviceStateManager::ValidateDeviceStatePacket(const FDeviceStatePacket& Packet) const
{
    // 1. 验证设备ID（非零）
    if (Packet.DeviceID == 0)
    {
        UE_LOG(LogDeviceStateManager, Warning, TEXT("Invalid device ID: 0"));
        return false;
    }

    // 2. 验证设备类型
    const uint8 NormalizedDeviceType = FProtocolMapping::NormalizeDeviceTypeCode(Packet.DeviceType);

    if (NormalizedDeviceType == static_cast<uint8>(EUnifiedDeviceType::Unknown) &&
        Packet.DeviceType != 255)
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Invalid device type: RawType=%d for device %d"),
            Packet.DeviceType,
            Packet.DeviceID);
        return false;
    }

    // 3. 验证位置数据
    if (!UCoordinateConverter::IsValidNED(Packet.Position, 10000.0f))
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Invalid position data for device %d"),
            Packet.DeviceID
        );
        return false;
    }

    // 4. 验证姿态数据
    if (!UCoordinateConverter::IsValidAttitude(Packet.Attitude))
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Invalid attitude data for device %d"),
            Packet.DeviceID
        );
        return false;
    }

    // 5. 验证速度数据（合理性检查：< 100 m/s）
    const float VelocityMagnitude = FMath::Sqrt(
        Packet.Velocity.North * Packet.Velocity.North +
        Packet.Velocity.East * Packet.Velocity.East +
        Packet.Velocity.Down * Packet.Velocity.Down
    );

    if (VelocityMagnitude > 100.0f)
    {
        UE_LOG(LogDeviceStateManager, Warning,
            TEXT("Unreasonable velocity magnitude: %.2f m/s for device %d"),
            VelocityMagnitude,
            Packet.DeviceID
        );
        return false;
    }

    return true;
}

// ========== 蓝图接口实现 - 查询 ==========

bool UDeviceStateManager::GetDeviceState(int32 DeviceID, FDeviceRuntimeState& OutState) const
{
    const FDeviceRuntimeState* StatePtr = DeviceCache.Find(DeviceID);
    if (StatePtr)
    {
        OutState = *StatePtr;
        return true;
    }
    return false;
}

TArray<int32> UDeviceStateManager::GetAllOnlineDeviceIDs() const
{
    TArray<int32> DeviceIDs;

    for (const auto& Pair : DeviceCache)
    {
        if (Pair.Value.bIsOnline)
        {
            DeviceIDs.Add(Pair.Key);
        }
    }

    return DeviceIDs;
}

TArray<int32> UDeviceStateManager::GetDevicesByType(uint8 DeviceType) const
{
    TArray<int32> DeviceIDs;

    for (const auto& Pair : DeviceCache)
    {
        if (Pair.Value.bIsOnline && Pair.Value.DeviceType == DeviceType)
        {
            DeviceIDs.Add(Pair.Key);
        }
    }

    return DeviceIDs;
}

bool UDeviceStateManager::IsDeviceOnline(int32 DeviceID) const
{
    const FDeviceRuntimeState* StatePtr = DeviceCache.Find(DeviceID);
    return StatePtr && StatePtr->bIsOnline;
}

int32 UDeviceStateManager::GetOnlineDeviceCount() const
{
    int32 Count = 0;
    for (const auto& Pair : DeviceCache)
    {
        if (Pair.Value.bIsOnline)
        {
            Count++;
        }
    }
    return Count;
}

bool UDeviceStateManager::SetDeviceOnlineState(int32 DeviceID, bool bOnline)
{
    FDeviceRuntimeState* StatePtr = DeviceCache.Find(DeviceID);
    if (!StatePtr)
    {
        return false;
    }

    StatePtr->bIsOnline = bOnline;
    StatePtr->LastUpdateTime = FPlatformTime::Seconds();

    UE_LOG(LogDeviceStateManager, Verbose,
        TEXT("Device %d online state set to: %s"),
        DeviceID,
        bOnline ? TEXT("ONLINE") : TEXT("OFFLINE"));

    return true;
}

void UDeviceStateManager::GetStatistics(FDeviceStatistics& OutStatistics) const
{
    OutStatistics = FDeviceStatistics();

    for (const auto& Pair : DeviceCache)
    {
        const FDeviceRuntimeState& State = Pair.Value;
        if (State.bIsOnline)
        {
            OutStatistics.OnlineDeviceCount++;
            switch (State.DeviceType)
            {
            case 1: OutStatistics.IndoorQuadcopterCount++;  break;
            case 2: OutStatistics.OutdoorQuadcopterCount++; break;
            case 3: OutStatistics.IndoorUGVCount++;         break;
            case 4: OutStatistics.OutdoorUGVCount++;        break;
            case 5: OutStatistics.RobotDogCount++;          break;
            }
        }
        else
        {
            OutStatistics.OfflineDeviceCount++;
        }
    }

    OutStatistics.TotalMessagesProcessed = TotalMessagesProcessed;
    OutStatistics.TotalStateUpdates = TotalStateUpdates;
    OutStatistics.UAVCount = OutStatistics.IndoorQuadcopterCount + OutStatistics.OutdoorQuadcopterCount;
    OutStatistics.UGVCount = OutStatistics.IndoorUGVCount + OutStatistics.OutdoorUGVCount;
}

// ========== 蓝图接口实现 - 配置 ==========

void UDeviceStateManager::ClearAllDevices()
{
    const int32 DeviceCount = DeviceCache.Num();
    DeviceCache.Empty();

    UE_LOG(LogDeviceStateManager, Log,
        TEXT("Cleared %d devices from cache"), DeviceCount);
}

bool UDeviceStateManager::RemoveDevice(int32 DeviceID)
{
    FDeviceRuntimeState* StatePtr = DeviceCache.Find(DeviceID);
    if (!StatePtr)
    {
        return false;
    }

    const FVector LastLocation = StatePtr->Location;
    DeviceCache.Remove(DeviceID);

    // 触发离线事件
    OnDeviceRemoved.Broadcast(DeviceID, LastLocation);

    UE_LOG(LogDeviceStateManager, Log, TEXT("Manually removed device %d"), DeviceID);
    return true;
}

void UDeviceStateManager::PrintStatistics() const
{
    FDeviceStatistics Stats;
    GetStatistics(Stats);

    UE_LOG(LogDeviceStateManager, Log, TEXT("========== Device State Manager Statistics =========="));
    UE_LOG(LogDeviceStateManager, Log, TEXT("  Device Status:"));
    UE_LOG(LogDeviceStateManager, Log, TEXT("    Online Devices        : %d"), Stats.OnlineDeviceCount);
    UE_LOG(LogDeviceStateManager, Log, TEXT("    Offline Devices       : %d"), Stats.OfflineDeviceCount);
    UE_LOG(LogDeviceStateManager, Log, TEXT("    Total Devices         : %d"), DeviceCache.Num());
    UE_LOG(LogDeviceStateManager, Log, TEXT("  Device Types (online):"));
    UE_LOG(LogDeviceStateManager, Log, TEXT("    Indoor Quadcopter     : %d"), Stats.IndoorQuadcopterCount);
    UE_LOG(LogDeviceStateManager, Log, TEXT("    Outdoor Quadcopter    : %d"), Stats.OutdoorQuadcopterCount);
    UE_LOG(LogDeviceStateManager, Log, TEXT("    Indoor UGV            : %d"), Stats.IndoorUGVCount);
    UE_LOG(LogDeviceStateManager, Log, TEXT("    Outdoor UGV           : %d"), Stats.OutdoorUGVCount);
    UE_LOG(LogDeviceStateManager, Log, TEXT("    RobotDog              : %d"), Stats.RobotDogCount);
    UE_LOG(LogDeviceStateManager, Log, TEXT("  Processing:"));
    UE_LOG(LogDeviceStateManager, Log, TEXT("    Messages Processed    : %d"), TotalMessagesProcessed);
    UE_LOG(LogDeviceStateManager, Log, TEXT("    State Updates         : %d"), TotalStateUpdates);
    UE_LOG(LogDeviceStateManager, Log, TEXT("====================================================="));
}

// ========== C++ 接口实现 ==========

const FDeviceRuntimeState* UDeviceStateManager::GetDeviceStatePtr(int32 DeviceID) const
{
    return DeviceCache.Find(DeviceID);
}
