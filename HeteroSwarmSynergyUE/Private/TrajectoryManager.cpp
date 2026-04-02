// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryManager.h"
#include "UDPManager.h"
#include "CoordinateConverter.h"
#include "Dom/JsonObject.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// 日志分类
DEFINE_LOG_CATEGORY_STATIC(LogTrajectoryManager, Log, All);

namespace
{
    FString BuildDefaultTrajectoryDisplayJson()
    {
        return FString(
            TEXT("{\n")
            TEXT("  \"version\": 1,\n")
            TEXT("  \"description\": \"Trajectory display and actual-track configuration for HeteroSwarmSynergyUE\",\n")
            TEXT("  \"display\": {\n")
            TEXT("    \"planned_color_rgba\": [0.0, 1.0, 0.0, 1.0],\n")
            TEXT("    \"actual_color_rgba\": [1.0, 0.45, 0.08, 1.0],\n")
            TEXT("    \"lifetime_seconds\": 20.0,\n")
            TEXT("    \"line_width\": 6.0,\n")
            TEXT("    \"opacity\": 0.95,\n")
            TEXT("    \"enable_fade_out\": true,\n")
            TEXT("    \"max_points_per_trajectory\": 600,\n")
            TEXT("    \"enable_debug_visualization\": true,\n")
            TEXT("    \"draw_device_label\": true,\n")
            TEXT("    \"draw_direction_arrows\": true,\n")
            TEXT("    \"debug_point_size\": 18.0,\n")
            TEXT("    \"planned_vertical_offset_cm\": 40.0,\n")
            TEXT("    \"actual_vertical_offset_cm\": 0.0,\n")
            TEXT("    \"direction_arrow_size_cm\": 90.0\n")
            TEXT("  },\n")
            TEXT("  \"actual_tracking\": {\n")
            TEXT("    \"enabled\": true,\n")
            TEXT("    \"min_distance_cm\": 80.0,\n")
            TEXT("    \"min_interval_seconds\": 0.15\n")
            TEXT("  }\n")
            TEXT("}\n"));
    }

    FString GetTrajectoryDisplayConfigPath(const FString& ConfigFilePath)
    {
        return FPaths::IsRelative(ConfigFilePath)
            ? FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / ConfigFilePath)
            : FPaths::ConvertRelativePathToFull(ConfigFilePath);
    }

    bool EnsureDefaultTrajectoryConfigFileExists(const FString& AbsoluteConfigPath)
    {
        if (FPaths::FileExists(AbsoluteConfigPath))
        {
            return true;
        }

        const FString Directory = FPaths::GetPath(AbsoluteConfigPath);
        if (!Directory.IsEmpty())
        {
            IFileManager::Get().MakeDirectory(*Directory, true);
        }

        return FFileHelper::SaveStringToFile(
            BuildDefaultTrajectoryDisplayJson(),
            *AbsoluteConfigPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    bool TryGetBoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool& OutValue)
    {
        return Object.IsValid() && Object->TryGetBoolField(FieldName, OutValue);
    }

    bool TryGetNumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, double& OutValue)
    {
        return Object.IsValid() && Object->TryGetNumberField(FieldName, OutValue);
    }

    bool TryReadColorField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FLinearColor& OutColor)
    {
        if (!Object.IsValid())
        {
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
        if (!Object->TryGetArrayField(FieldName, ColorArray) || ColorArray == nullptr || ColorArray->Num() < 3)
        {
            return false;
        }

        const double R = (*ColorArray)[0].IsValid() ? (*ColorArray)[0]->AsNumber() : 0.0;
        const double G = (*ColorArray)[1].IsValid() ? (*ColorArray)[1]->AsNumber() : 0.0;
        const double B = (*ColorArray)[2].IsValid() ? (*ColorArray)[2]->AsNumber() : 0.0;
        const double A = ColorArray->Num() > 3 && (*ColorArray)[3].IsValid() ? (*ColorArray)[3]->AsNumber() : 1.0;

        OutColor = FLinearColor(
            FMath::Clamp(static_cast<float>(R), 0.0f, 1.0f),
            FMath::Clamp(static_cast<float>(G), 0.0f, 1.0f),
            FMath::Clamp(static_cast<float>(B), 0.0f, 1.0f),
            FMath::Clamp(static_cast<float>(A), 0.0f, 1.0f));
        return true;
    }

    TArray<TSharedPtr<FJsonValue>> MakeColorArray(const FLinearColor& Color)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        Result.Add(MakeShared<FJsonValueNumber>(Color.R));
        Result.Add(MakeShared<FJsonValueNumber>(Color.G));
        Result.Add(MakeShared<FJsonValueNumber>(Color.B));
        Result.Add(MakeShared<FJsonValueNumber>(Color.A));
        return Result;
    }
}

// ========== 构造与析构 ==========

UTrajectoryManager::UTrajectoryManager()
    : UDPManager(nullptr)
    , World(nullptr)
    , bIsInitialized(false)
    , LastCleanupTime(0.0f)
    , CleanupInterval(1.0f)  // 每秒清理一次
    , TotalMessagesProcessed(0)
    , TotalTrajectoriesReceived(0)
    , TotalExpiredPointsCleared(0)
{
    // 初始化默认配置
    Config.PlannedColor = FLinearColor(0.0f, 1.0f, 0.0f, 1.0f);  // 绿色
    Config.ActualColor = FLinearColor(1.0f, 0.5f, 0.0f, 1.0f);   // 橙色
    Config.LifetimeSeconds = 10.0f;
    Config.LineWidth = 5.0f;
    Config.Opacity = 0.8f;
    Config.bEnableFadeOut = true;
    Config.MaxPointsPerTrajectory = 500;
    Config.bEnableDebugVisualization = true;
    Config.bDrawDeviceLabel = true;
    Config.DebugPointSize = 18.0f;
    Config.DebugVerticalOffsetCm = 40.0f;
    Config.ActualDebugVerticalOffsetCm = 0.0f;
    Config.bDrawDirectionArrows = true;
    Config.DirectionArrowSizeCm = 90.0f;
    Config.bAutoTrackActualTrajectory = true;
    Config.ActualTrackMinDistanceCm = 80.0f;
    Config.ActualTrackMinIntervalSeconds = 0.15f;

    UE_LOG(LogTrajectoryManager, Log, TEXT("TrajectoryManager constructed"));
}

UTrajectoryManager::~UTrajectoryManager()
{
    Shutdown();
    UE_LOG(LogTrajectoryManager, Log, TEXT("TrajectoryManager destroyed"));
}

// ========== 初始化与关闭 ==========

bool UTrajectoryManager::Initialize(UUDPManager* InUDPManager, UWorld* InWorld)
{
    if (bIsInitialized)
    {
        UE_LOG(LogTrajectoryManager, Warning, TEXT("Already initialized"));
        return false;
    }

    if (!InUDPManager || !InWorld)
    {
        UE_LOG(LogTrajectoryManager, Error, TEXT("Invalid initialization parameters"));
        return false;
    }

    UDPManager = InUDPManager;
    World = InWorld;

    if (!LoadConfigFromFile(TEXT("TrajectoryDisplay.json")))
    {
        UE_LOG(LogTrajectoryManager, Warning,
            TEXT("Falling back to in-memory trajectory defaults; failed to load TrajectoryDisplay.json"));
    }

    // 注册消息处理器
    UDPManager->RegisterMessageHandler(0x0003, this);  // 轨迹数据

    bIsInitialized = true;
    LastCleanupTime = FPlatformTime::Seconds();

    UE_LOG(LogTrajectoryManager, Log, TEXT("TrajectoryManager initialized"));
    UE_LOG(LogTrajectoryManager, Log, TEXT("  Trajectory Lifetime: %.1fs"), Config.LifetimeSeconds);
    UE_LOG(LogTrajectoryManager, Log, TEXT("  Max Points Per Trajectory: %d"), Config.MaxPointsPerTrajectory);
    UE_LOG(LogTrajectoryManager, Log,
        TEXT("  Actual Tracking: %s (MinDistance=%.1fcm, MinInterval=%.2fs)"),
        Config.bAutoTrackActualTrajectory ? TEXT("ON") : TEXT("OFF"),
        Config.ActualTrackMinDistanceCm,
        Config.ActualTrackMinIntervalSeconds);

    return true;
}

void UTrajectoryManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogTrajectoryManager, Log, TEXT("Shutting down TrajectoryManager..."));

    // 打印最终统计
    PrintStatistics();

    // 取消注册消息处理器
    if (UDPManager)
    {
        UDPManager->UnregisterMessageHandler(0x0003);
        UDPManager = nullptr;
    }

    World = nullptr;

    // 清空缓存
    TrajectoryCache.Empty();

    bIsInitialized = false;

    UE_LOG(LogTrajectoryManager, Log, TEXT("TrajectoryManager shutdown complete"));
}

void UTrajectoryManager::Tick(float DeltaTime)
{
    if (!bIsInitialized)
    {
        return;
    }

    // 定期清理过期轨迹点
    const float CurrentTime = FPlatformTime::Seconds();
    if (CurrentTime - LastCleanupTime >= CleanupInterval)
    {
        const int32 ClearedCount = CleanupExpiredPoints();

        if (ClearedCount > 0)
        {
            UE_LOG(LogTrajectoryManager, Verbose,
                TEXT("Cleaned up %d expired trajectory points"),
                ClearedCount
            );
        }

        LastCleanupTime = CurrentTime;
    }

    DrawDebugTrajectories();
}

// ========== IUDPMessageHandler 接口实现 ==========

void UTrajectoryManager::HandleMessage(const uint8* Data, uint32 Length)
{
    if (!Data || Length == 0)
    {
        UE_LOG(LogTrajectoryManager, Warning, TEXT("Received invalid message"));
        return;
    }

    TotalMessagesProcessed++;

    // 处理轨迹数据消息（0x0003）
    ProcessTrajectoryData(Data, Length);
}

void UTrajectoryManager::HandleMAVLinkWaypoints(const FMAVLinkWaypointsData& WaypointData)
{
    if (!bIsInitialized)
    {
        return;
    }

    const int32 DeviceID = WaypointData.SystemID;
    if (DeviceID <= 0)
    {
        UE_LOG(LogTrajectoryManager, Warning,
            TEXT("Invalid SystemID in MAVLink waypoints: %d"),
            DeviceID);
        return;
    }

    TotalMessagesProcessed++;

    if (WaypointData.Waypoints.Num() == 0)
    {
        ClearDeviceTrajectory(DeviceID, ETrajectoryType::Planned);

        UE_LOG(LogTrajectoryManager, Verbose,
            TEXT("Cleared planned trajectory from MAVLink: Device=%d"),
            DeviceID);
        return;
    }

    const int32 CacheKey = MakeTrajectoryKey(DeviceID, ETrajectoryType::Planned);
    FDeviceTrajectory* TrajectoryPtr = TrajectoryCache.Find(CacheKey);
    const bool bIsNewTrajectory = (TrajectoryPtr == nullptr);

    if (bIsNewTrajectory)
    {
        FDeviceTrajectory NewTrajectory;
        NewTrajectory.DeviceID = DeviceID;
        NewTrajectory.TrajectoryType = ETrajectoryType::Planned;
        NewTrajectory.Points.Reserve(FMath::Min(WaypointData.Waypoints.Num(), Config.MaxPointsPerTrajectory));
        NewTrajectory.TotalPointsReceived = 0;
        NewTrajectory.LastUpdateTime = FPlatformTime::Seconds();

        TrajectoryCache.Add(CacheKey, NewTrajectory);
        TrajectoryPtr = TrajectoryCache.Find(CacheKey);

        UE_LOG(LogTrajectoryManager, Log,
            TEXT("New planned trajectory created from MAVLink: Device=%d"),
            DeviceID);

        OnTrajectoryCreated.Broadcast(DeviceID, ETrajectoryType::Planned);
    }

    check(TrajectoryPtr);

    // MAVLink 航点表示最新规划结果，因此每次都用新结果完整覆盖 Planned 轨迹。
    TrajectoryPtr->Points.Reset();
    TrajectoryPtr->Points.Reserve(FMath::Min(WaypointData.Waypoints.Num(), Config.MaxPointsPerTrajectory));

    const float CurrentTime = WaypointData.ReceiveTime > 0.0f
        ? WaypointData.ReceiveTime
        : static_cast<float>(FPlatformTime::Seconds());

    int32 AddedCount = 0;

    for (const FVector& NEDWaypoint : WaypointData.Waypoints)
    {
        FNEDVector NEDPoint;
        NEDPoint.North = NEDWaypoint.X;
        NEDPoint.East = NEDWaypoint.Y;
        NEDPoint.Down = NEDWaypoint.Z;

        if (!UCoordinateConverter::IsValidNED(NEDPoint, 10000.0f))
        {
            UE_LOG(LogTrajectoryManager, Warning,
                TEXT("Skipped invalid MAVLink waypoint for Device=%d: (%.2f, %.2f, %.2f)"),
                DeviceID, NEDWaypoint.X, NEDWaypoint.Y, NEDWaypoint.Z);
            continue;
        }

        if (TrajectoryPtr->Points.Num() >= Config.MaxPointsPerTrajectory)
        {
            break;
        }

        TrajectoryPtr->Points.Emplace(UCoordinateConverter::NEDToUE(NEDPoint), CurrentTime);
        AddedCount++;
    }

    TrajectoryPtr->TotalPointsReceived += AddedCount;
    TrajectoryPtr->LastUpdateTime = CurrentTime;
    TotalTrajectoriesReceived++;

    OnTrajectoryUpdated.Broadcast(DeviceID, ETrajectoryType::Planned, AddedCount);

    UE_LOG(LogTrajectoryManager, Verbose,
        TEXT("Planned trajectory updated from MAVLink: Device=%d, Points=%d"),
        DeviceID,
        AddedCount);
}

void UTrajectoryManager::HandleDeviceStateForActualTrajectory(int32 DeviceID, const FDeviceRuntimeState& State)
{
    if (!bIsInitialized || !Config.bAutoTrackActualTrajectory || DeviceID <= 0 || !State.bIsOnline)
    {
        return;
    }

    FDeviceTrajectory* TrajectoryPtr = nullptr;
    if (!EnsureTrajectoryEntry(DeviceID, ETrajectoryType::Actual, TrajectoryPtr) || TrajectoryPtr == nullptr)
    {
        return;
    }

    const float Timestamp = State.LastUpdateTime > 0.0f
        ? State.LastUpdateTime
        : static_cast<float>(FPlatformTime::Seconds());

    if (!ShouldAppendActualPoint(*TrajectoryPtr, State.Location, Timestamp))
    {
        TrajectoryPtr->LastUpdateTime = Timestamp;
        return;
    }

    if (!AppendPointToTrajectory(*TrajectoryPtr, State.Location, Timestamp))
    {
        return;
    }

    TrajectoryPtr->TotalPointsReceived++;
    TrajectoryPtr->LastUpdateTime = Timestamp;
    TotalTrajectoriesReceived++;

    OnTrajectoryUpdated.Broadcast(DeviceID, ETrajectoryType::Actual, 1);
}

// ========== 内部处理函数 ==========

void UTrajectoryManager::ProcessTrajectoryData(const uint8* Data, uint32 Length)
{
    // 最小长度检查：12字节（DeviceID + Type + Reserved*3 + PointCount）
    if (Length < 12)
    {
        UE_LOG(LogTrajectoryManager, Error,
            TEXT("Invalid trajectory message length: %d bytes (minimum 12)"),
            Length
        );
        return;
    }

    // 解析消息头
    const uint32* DeviceIDPtr = reinterpret_cast<const uint32*>(Data);
    const uint8* TrajectoryTypePtr = Data + 4;
    const uint32* PointCountPtr = reinterpret_cast<const uint32*>(Data + 8);

    const int32 DeviceID = *DeviceIDPtr;
    const uint8 TrajectoryTypeRaw = *TrajectoryTypePtr;
    const int32 PointCount = *PointCountPtr;

    // 验证数据
    if (!ValidateTrajectoryData(DeviceID, TrajectoryTypeRaw, PointCount))
    {
        return;
    }

    // 验证长度
    const uint32 ExpectedLength = 12 + PointCount * sizeof(FTrajectoryPointPacket);
    if (Length != ExpectedLength)
    {
        UE_LOG(LogTrajectoryManager, Error,
            TEXT("Trajectory message length mismatch: Expected %d, Got %d"),
            ExpectedLength,
            Length
        );
        return;
    }

    // 转换轨迹类型
    const ETrajectoryType TrajectoryType = static_cast<ETrajectoryType>(TrajectoryTypeRaw);

    // 解析轨迹点数组
    const FTrajectoryPointPacket* PacketPoints = reinterpret_cast<const FTrajectoryPointPacket*>(Data + 12);

    // 生成缓存键
    const int32 CacheKey = MakeTrajectoryKey(DeviceID, TrajectoryType);

    // 查找或创建轨迹
    FDeviceTrajectory* TrajectoryPtr = TrajectoryCache.Find(CacheKey);
    const bool bIsNewTrajectory = (TrajectoryPtr == nullptr);

    if (bIsNewTrajectory)
    {
        // 创建新轨迹
        FDeviceTrajectory NewTrajectory;
        NewTrajectory.DeviceID = DeviceID;
        NewTrajectory.TrajectoryType = TrajectoryType;
        NewTrajectory.Points.Reserve(Config.MaxPointsPerTrajectory);
        NewTrajectory.TotalPointsReceived = 0;
        NewTrajectory.LastUpdateTime = FPlatformTime::Seconds();

        TrajectoryCache.Add(CacheKey, NewTrajectory);
        TrajectoryPtr = TrajectoryCache.Find(CacheKey);

        UE_LOG(LogTrajectoryManager, Log,
            TEXT("New trajectory created: Device=%d, Type=%s"),
            DeviceID,
            TrajectoryType == ETrajectoryType::Planned ? TEXT("Planned") : TEXT("Actual")
        );

        // 触发新轨迹创建事件
        OnTrajectoryCreated.Broadcast(DeviceID, TrajectoryType);
    }

    // 转换并添加轨迹点
    const float CurrentTime = FPlatformTime::Seconds();
    int32 AddedCount = 0;

    for (int32 i = 0; i < PointCount; ++i)
    {
        // NED → UE坐标转换
        FVector UELocation = UCoordinateConverter::NEDToUE(PacketPoints[i].Position);

        // 创建轨迹点
        FTrajectoryPoint Point(UELocation, CurrentTime);

        // 检查点数限制
        if (TrajectoryPtr->Points.Num() >= Config.MaxPointsPerTrajectory)
        {
            // 移除最老的点
            TrajectoryPtr->Points.RemoveAt(0);

            UE_LOG(LogTrajectoryManager, Verbose,
                TEXT("Trajectory point limit reached for Device %d, removing oldest point"),
                DeviceID
            );
        }

        // 添加新点
        TrajectoryPtr->Points.Add(Point);
        AddedCount++;
    }

    // 更新轨迹信息
    TrajectoryPtr->TotalPointsReceived += AddedCount;
    TrajectoryPtr->LastUpdateTime = CurrentTime;

    // 统计
    TotalTrajectoriesReceived++;

    // 触发轨迹更新事件
    OnTrajectoryUpdated.Broadcast(DeviceID, TrajectoryType, AddedCount);

    UE_LOG(LogTrajectoryManager, Verbose,
        TEXT("Trajectory updated: Device=%d, Type=%s, Added=%d points, Total=%d points"),
        DeviceID,
        TrajectoryType == ETrajectoryType::Planned ? TEXT("Planned") : TEXT("Actual"),
        AddedCount,
        TrajectoryPtr->Points.Num()
    );
}

bool UTrajectoryManager::ValidateTrajectoryData(int32 DeviceID, uint8 TrajectoryType, int32 PointCount) const
{
    // 验证设备ID
    if (DeviceID <= 0)
    {
        UE_LOG(LogTrajectoryManager, Warning,
            TEXT("Invalid device ID: %d"),
            DeviceID
        );
        return false;
    }

    // 验证轨迹类型（0=Planned, 1=Actual）
    if (TrajectoryType > 1)
    {
        UE_LOG(LogTrajectoryManager, Warning,
            TEXT("Invalid trajectory type: %d for device %d"),
            TrajectoryType,
            DeviceID
        );
        return false;
    }

    // 验证轨迹点数量
    if (PointCount <= 0 || PointCount > 10000)
    {
        UE_LOG(LogTrajectoryManager, Warning,
            TEXT("Invalid point count: %d for device %d"),
            PointCount,
            DeviceID
        );
        return false;
    }

    return true;
}

int32 UTrajectoryManager::CleanupExpiredPointsForTrajectory(FDeviceTrajectory& Trajectory)
{
    // 如果生存时间为0，表示永久保留
    if (Config.LifetimeSeconds <= 0.0f)
    {
        return 0;
    }

    const float CurrentTime = FPlatformTime::Seconds();
    const float ExpirationThreshold = CurrentTime - Config.LifetimeSeconds;

    int32 RemovedCount = 0;

    // 从前往后移除过期点
    while (Trajectory.Points.Num() > 0)
    {
        if (Trajectory.Points[0].Timestamp < ExpirationThreshold)
        {
            Trajectory.Points.RemoveAt(0);
            RemovedCount++;
        }
        else
        {
            // 点是按时间顺序添加的，一旦遇到未过期的点就可以停止
            break;
        }
    }

    return RemovedCount;
}

bool UTrajectoryManager::EnsureTrajectoryEntry(
    int32 DeviceID,
    ETrajectoryType TrajectoryType,
    FDeviceTrajectory*& OutTrajectoryPtr)
{
    const int32 CacheKey = MakeTrajectoryKey(DeviceID, TrajectoryType);
    OutTrajectoryPtr = TrajectoryCache.Find(CacheKey);
    if (OutTrajectoryPtr != nullptr)
    {
        return true;
    }

    FDeviceTrajectory NewTrajectory;
    NewTrajectory.DeviceID = DeviceID;
    NewTrajectory.TrajectoryType = TrajectoryType;
    NewTrajectory.Points.Reserve(Config.MaxPointsPerTrajectory);
    NewTrajectory.TotalPointsReceived = 0;
    NewTrajectory.LastUpdateTime = static_cast<float>(FPlatformTime::Seconds());

    TrajectoryCache.Add(CacheKey, NewTrajectory);
    OutTrajectoryPtr = TrajectoryCache.Find(CacheKey);

    UE_LOG(LogTrajectoryManager, Log,
        TEXT("New %s trajectory created: Device=%d"),
        TrajectoryType == ETrajectoryType::Planned ? TEXT("planned") : TEXT("actual"),
        DeviceID);

    OnTrajectoryCreated.Broadcast(DeviceID, TrajectoryType);
    return OutTrajectoryPtr != nullptr;
}

bool UTrajectoryManager::AppendPointToTrajectory(
    FDeviceTrajectory& Trajectory,
    const FVector& UELocation,
    float Timestamp)
{
    if (Trajectory.Points.Num() >= Config.MaxPointsPerTrajectory)
    {
        Trajectory.Points.RemoveAt(0);
    }

    Trajectory.Points.Emplace(UELocation, Timestamp);
    return true;
}

bool UTrajectoryManager::ShouldAppendActualPoint(
    const FDeviceTrajectory& Trajectory,
    const FVector& UELocation,
    float Timestamp) const
{
    if (Trajectory.Points.Num() <= 0)
    {
        return true;
    }

    const FTrajectoryPoint& LastPoint = Trajectory.Points.Last();
    const float DistanceCm = FVector::Dist(LastPoint.Location, UELocation);
    const float DeltaSeconds = FMath::Max(0.0f, Timestamp - LastPoint.Timestamp);

    return DistanceCm >= Config.ActualTrackMinDistanceCm
        && DeltaSeconds >= Config.ActualTrackMinIntervalSeconds;
}

// ========== 蓝图接口实现 - 查询 ==========

bool UTrajectoryManager::GetDeviceTrajectory(int32 DeviceID, ETrajectoryType TrajectoryType, FDeviceTrajectory& OutTrajectory) const
{
    const int32 CacheKey = MakeTrajectoryKey(DeviceID, TrajectoryType);
    const FDeviceTrajectory* TrajectoryPtr = TrajectoryCache.Find(CacheKey);

    if (TrajectoryPtr)
    {
        OutTrajectory = *TrajectoryPtr;
        return true;
    }

    return false;
}

TArray<FVector> UTrajectoryManager::GetTrajectoryPoints(int32 DeviceID, ETrajectoryType TrajectoryType) const
{
    TArray<FVector> Points;

    const int32 CacheKey = MakeTrajectoryKey(DeviceID, TrajectoryType);
    const FDeviceTrajectory* TrajectoryPtr = TrajectoryCache.Find(CacheKey);

    if (TrajectoryPtr)
    {
        Points.Reserve(TrajectoryPtr->Points.Num());

        for (const FTrajectoryPoint& Point : TrajectoryPtr->Points)
        {
            Points.Add(Point.Location);
        }
    }

    return Points;
}

TArray<int32> UTrajectoryManager::GetAllDeviceIDsWithTrajectory() const
{
    TSet<int32> DeviceIDSet;

    for (const auto& Pair : TrajectoryCache)
    {
        // 提取DeviceID（从CacheKey中）
        const int32 DeviceID = Pair.Key >> 1;
        DeviceIDSet.Add(DeviceID);
    }

    return DeviceIDSet.Array();
}

bool UTrajectoryManager::HasTrajectory(int32 DeviceID, ETrajectoryType TrajectoryType) const
{
    const int32 CacheKey = MakeTrajectoryKey(DeviceID, TrajectoryType);
    return TrajectoryCache.Contains(CacheKey);
}

int32 UTrajectoryManager::GetTrajectoryPointCount(int32 DeviceID, ETrajectoryType TrajectoryType) const
{
    const int32 CacheKey = MakeTrajectoryKey(DeviceID, TrajectoryType);
    const FDeviceTrajectory* TrajectoryPtr = TrajectoryCache.Find(CacheKey);

    return TrajectoryPtr ? TrajectoryPtr->Points.Num() : 0;
}

void UTrajectoryManager::GetStatistics(FTrajectoryStatistics& OutStatistics) const
{
    OutStatistics.ActiveTrajectoryCount = TrajectoryCache.Num();
    OutStatistics.PlannedTrajectoryCount = 0;
    OutStatistics.ActualTrajectoryCount = 0;
    OutStatistics.TotalPointsCount = 0;

    for (const auto& Pair : TrajectoryCache)
    {
        const FDeviceTrajectory& Trajectory = Pair.Value;

        if (Trajectory.TrajectoryType == ETrajectoryType::Planned)
        {
            OutStatistics.PlannedTrajectoryCount++;
        }
        else
        {
            OutStatistics.ActualTrajectoryCount++;
        }

        OutStatistics.TotalPointsCount += Trajectory.Points.Num();
    }

    OutStatistics.TotalMessagesProcessed = TotalMessagesProcessed;
    OutStatistics.TotalTrajectoriesReceived = TotalTrajectoriesReceived;
}

// ========== 蓝图接口实现 - 配置 ==========

void UTrajectoryManager::SetTrajectoryConfig(const FTrajectoryConfig& NewConfig)
{
    Config = NewConfig;
    Config.LineWidth = FMath::Clamp(Config.LineWidth, 1.0f, 50.0f);
    Config.Opacity = FMath::Clamp(Config.Opacity, 0.0f, 1.0f);
    Config.LifetimeSeconds = FMath::Clamp(Config.LifetimeSeconds, 0.0f, 300.0f);
    Config.MaxPointsPerTrajectory = FMath::Clamp(Config.MaxPointsPerTrajectory, 10, 10000);
    Config.DebugPointSize = FMath::Clamp(Config.DebugPointSize, 2.0f, 100.0f);
    Config.DebugVerticalOffsetCm = FMath::Clamp(Config.DebugVerticalOffsetCm, 0.0f, 500.0f);
    Config.ActualDebugVerticalOffsetCm = FMath::Clamp(Config.ActualDebugVerticalOffsetCm, 0.0f, 500.0f);
    Config.DirectionArrowSizeCm = FMath::Clamp(Config.DirectionArrowSizeCm, 10.0f, 500.0f);
    Config.ActualTrackMinDistanceCm = FMath::Clamp(Config.ActualTrackMinDistanceCm, 1.0f, 5000.0f);
    Config.ActualTrackMinIntervalSeconds = FMath::Clamp(Config.ActualTrackMinIntervalSeconds, 0.01f, 5.0f);

    UE_LOG(LogTrajectoryManager, Log, TEXT("Trajectory config updated"));
    UE_LOG(LogTrajectoryManager, Log, TEXT("  Lifetime: %.1fs"), Config.LifetimeSeconds);
    UE_LOG(LogTrajectoryManager, Log, TEXT("  Max Points: %d"), Config.MaxPointsPerTrajectory);
    UE_LOG(LogTrajectoryManager, Log, TEXT("  Line Width: %.1f"), Config.LineWidth);
    UE_LOG(LogTrajectoryManager, Log,
        TEXT("  Actual Tracking: %s (MinDistance=%.1fcm, MinInterval=%.2fs)"),
        Config.bAutoTrackActualTrajectory ? TEXT("ON") : TEXT("OFF"),
        Config.ActualTrackMinDistanceCm,
        Config.ActualTrackMinIntervalSeconds);
}

bool UTrajectoryManager::LoadConfigFromFile(const FString& ConfigFilePath)
{
    const FString FullPath = GetTrajectoryDisplayConfigPath(ConfigFilePath);
    if (!EnsureDefaultTrajectoryConfigFileExists(FullPath))
    {
        UE_LOG(LogTrajectoryManager, Warning,
            TEXT("Failed to create trajectory config file: %s"),
            *FullPath);
        return false;
    }

    if (!FPaths::FileExists(FullPath))
    {
        UE_LOG(LogTrajectoryManager, Warning,
            TEXT("Config file not found: %s"),
            *FullPath
        );
        return false;
    }

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FullPath))
    {
        UE_LOG(LogTrajectoryManager, Error,
            TEXT("Failed to load config file: %s"),
            *FullPath
        );
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTrajectoryManager, Error,
            TEXT("Failed to parse trajectory config file: %s"),
            *FullPath);
        return false;
    }

    const TSharedPtr<FJsonObject>* DisplayObjectPtr = nullptr;
    const TSharedPtr<FJsonObject>* ActualTrackingObjectPtr = nullptr;
    const TSharedPtr<FJsonObject> DisplayObject =
        RootObject->TryGetObjectField(TEXT("display"), DisplayObjectPtr) && DisplayObjectPtr != nullptr && DisplayObjectPtr->IsValid()
        ? *DisplayObjectPtr
        : RootObject;
    const TSharedPtr<FJsonObject> ActualTrackingObject =
        RootObject->TryGetObjectField(TEXT("actual_tracking"), ActualTrackingObjectPtr) && ActualTrackingObjectPtr != nullptr && ActualTrackingObjectPtr->IsValid()
        ? *ActualTrackingObjectPtr
        : RootObject;

    FTrajectoryConfig LoadedConfig = Config;
    double NumberValue = 0.0;
    bool BoolValue = false;
    FLinearColor ColorValue;

    if (TryReadColorField(DisplayObject, TEXT("planned_color_rgba"), ColorValue))
    {
        LoadedConfig.PlannedColor = ColorValue;
    }
    if (TryReadColorField(DisplayObject, TEXT("actual_color_rgba"), ColorValue))
    {
        LoadedConfig.ActualColor = ColorValue;
    }
    if (TryGetNumberField(DisplayObject, TEXT("lifetime_seconds"), NumberValue))
    {
        LoadedConfig.LifetimeSeconds = static_cast<float>(NumberValue);
    }
    if (TryGetNumberField(DisplayObject, TEXT("line_width"), NumberValue))
    {
        LoadedConfig.LineWidth = static_cast<float>(NumberValue);
    }
    if (TryGetNumberField(DisplayObject, TEXT("opacity"), NumberValue))
    {
        LoadedConfig.Opacity = static_cast<float>(NumberValue);
    }
    if (TryGetBoolField(DisplayObject, TEXT("enable_fade_out"), BoolValue))
    {
        LoadedConfig.bEnableFadeOut = BoolValue;
    }
    if (TryGetNumberField(DisplayObject, TEXT("max_points_per_trajectory"), NumberValue))
    {
        LoadedConfig.MaxPointsPerTrajectory = FMath::RoundToInt(NumberValue);
    }
    if (TryGetBoolField(DisplayObject, TEXT("enable_debug_visualization"), BoolValue))
    {
        LoadedConfig.bEnableDebugVisualization = BoolValue;
    }
    if (TryGetBoolField(DisplayObject, TEXT("draw_device_label"), BoolValue))
    {
        LoadedConfig.bDrawDeviceLabel = BoolValue;
    }
    if (TryGetBoolField(DisplayObject, TEXT("draw_direction_arrows"), BoolValue))
    {
        LoadedConfig.bDrawDirectionArrows = BoolValue;
    }
    if (TryGetNumberField(DisplayObject, TEXT("debug_point_size"), NumberValue))
    {
        LoadedConfig.DebugPointSize = static_cast<float>(NumberValue);
    }
    if (TryGetNumberField(DisplayObject, TEXT("planned_vertical_offset_cm"), NumberValue) ||
        TryGetNumberField(DisplayObject, TEXT("debug_vertical_offset_cm"), NumberValue))
    {
        LoadedConfig.DebugVerticalOffsetCm = static_cast<float>(NumberValue);
    }
    if (TryGetNumberField(DisplayObject, TEXT("actual_vertical_offset_cm"), NumberValue))
    {
        LoadedConfig.ActualDebugVerticalOffsetCm = static_cast<float>(NumberValue);
    }
    if (TryGetNumberField(DisplayObject, TEXT("direction_arrow_size_cm"), NumberValue))
    {
        LoadedConfig.DirectionArrowSizeCm = static_cast<float>(NumberValue);
    }

    if (TryGetBoolField(ActualTrackingObject, TEXT("enabled"), BoolValue))
    {
        LoadedConfig.bAutoTrackActualTrajectory = BoolValue;
    }
    if (TryGetNumberField(ActualTrackingObject, TEXT("min_distance_cm"), NumberValue))
    {
        LoadedConfig.ActualTrackMinDistanceCm = static_cast<float>(NumberValue);
    }
    if (TryGetNumberField(ActualTrackingObject, TEXT("min_interval_seconds"), NumberValue))
    {
        LoadedConfig.ActualTrackMinIntervalSeconds = static_cast<float>(NumberValue);
    }

    SetTrajectoryConfig(LoadedConfig);

    UE_LOG(LogTrajectoryManager, Log,
        TEXT("Config loaded from: %s"),
        *FullPath
    );

    return true;
}

bool UTrajectoryManager::SaveConfigToFile(const FString& ConfigFilePath) const
{
    const FString FullPath = GetTrajectoryDisplayConfigPath(ConfigFilePath);
    const FString Directory = FPaths::GetPath(FullPath);
    if (!Directory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    const TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetNumberField(TEXT("version"), 1);
    RootObject->SetStringField(TEXT("description"), TEXT("Trajectory display and actual-track configuration for HeteroSwarmSynergyUE"));

    const TSharedRef<FJsonObject> DisplayObject = MakeShared<FJsonObject>();
    DisplayObject->SetArrayField(TEXT("planned_color_rgba"), MakeColorArray(Config.PlannedColor));
    DisplayObject->SetArrayField(TEXT("actual_color_rgba"), MakeColorArray(Config.ActualColor));
    DisplayObject->SetNumberField(TEXT("lifetime_seconds"), Config.LifetimeSeconds);
    DisplayObject->SetNumberField(TEXT("line_width"), Config.LineWidth);
    DisplayObject->SetNumberField(TEXT("opacity"), Config.Opacity);
    DisplayObject->SetBoolField(TEXT("enable_fade_out"), Config.bEnableFadeOut);
    DisplayObject->SetNumberField(TEXT("max_points_per_trajectory"), Config.MaxPointsPerTrajectory);
    DisplayObject->SetBoolField(TEXT("enable_debug_visualization"), Config.bEnableDebugVisualization);
    DisplayObject->SetBoolField(TEXT("draw_device_label"), Config.bDrawDeviceLabel);
    DisplayObject->SetBoolField(TEXT("draw_direction_arrows"), Config.bDrawDirectionArrows);
    DisplayObject->SetNumberField(TEXT("debug_point_size"), Config.DebugPointSize);
    DisplayObject->SetNumberField(TEXT("planned_vertical_offset_cm"), Config.DebugVerticalOffsetCm);
    DisplayObject->SetNumberField(TEXT("actual_vertical_offset_cm"), Config.ActualDebugVerticalOffsetCm);
    DisplayObject->SetNumberField(TEXT("direction_arrow_size_cm"), Config.DirectionArrowSizeCm);
    RootObject->SetObjectField(TEXT("display"), DisplayObject);

    const TSharedRef<FJsonObject> ActualTrackingObject = MakeShared<FJsonObject>();
    ActualTrackingObject->SetBoolField(TEXT("enabled"), Config.bAutoTrackActualTrajectory);
    ActualTrackingObject->SetNumberField(TEXT("min_distance_cm"), Config.ActualTrackMinDistanceCm);
    ActualTrackingObject->SetNumberField(TEXT("min_interval_seconds"), Config.ActualTrackMinIntervalSeconds);
    RootObject->SetObjectField(TEXT("actual_tracking"), ActualTrackingObject);

    FString ConfigContent;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ConfigContent);
    if (!FJsonSerializer::Serialize(RootObject, Writer))
    {
        UE_LOG(LogTrajectoryManager, Error,
            TEXT("Failed to serialize trajectory config: %s"),
            *FullPath);
        return false;
    }

    if (!FFileHelper::SaveStringToFile(
        ConfigContent,
        *FullPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogTrajectoryManager, Error,
            TEXT("Failed to save config file: %s"),
            *FullPath
        );
        return false;
    }

    UE_LOG(LogTrajectoryManager, Log,
        TEXT("Config saved to: %s"),
        *FullPath
    );

    return true;
}

// ========== 蓝图接口实现 - 管理 ==========

bool UTrajectoryManager::ClearDeviceTrajectory(int32 DeviceID, ETrajectoryType TrajectoryType)
{
    const int32 CacheKey = MakeTrajectoryKey(DeviceID, TrajectoryType);
    const int32 RemovedCount = TrajectoryCache.Remove(CacheKey);

    if (RemovedCount > 0)
    {
        UE_LOG(LogTrajectoryManager, Log,
            TEXT("Cleared trajectory: Device=%d, Type=%s"),
            DeviceID,
            TrajectoryType == ETrajectoryType::Planned ? TEXT("Planned") : TEXT("Actual")
        );

        // 触发清除事件
        OnTrajectoryCleared.Broadcast(DeviceID, TrajectoryType);

        return true;
    }

    return false;
}

void UTrajectoryManager::ClearAllDeviceTrajectories(int32 DeviceID)
{
    ClearDeviceTrajectory(DeviceID, ETrajectoryType::Planned);
    ClearDeviceTrajectory(DeviceID, ETrajectoryType::Actual);

    UE_LOG(LogTrajectoryManager, Log,
        TEXT("Cleared all trajectories for device %d"),
        DeviceID
    );
}

void UTrajectoryManager::ClearAllTrajectories()
{
    const int32 TrajectoryCount = TrajectoryCache.Num();

    TrajectoryCache.Empty();

    UE_LOG(LogTrajectoryManager, Log,
        TEXT("Cleared all %d trajectories"),
        TrajectoryCount
    );
}

int32 UTrajectoryManager::CleanupExpiredPoints()
{
    int32 TotalClearedCount = 0;

    for (auto& Pair : TrajectoryCache)
    {
        FDeviceTrajectory& Trajectory = Pair.Value;
        const int32 ClearedCount = CleanupExpiredPointsForTrajectory(Trajectory);
        TotalClearedCount += ClearedCount;
    }

    TotalExpiredPointsCleared += TotalClearedCount;

    return TotalClearedCount;
}

void UTrajectoryManager::PrintStatistics() const
{
    FTrajectoryStatistics Stats;
    GetStatistics(Stats);

    UE_LOG(LogTrajectoryManager, Log, TEXT("========== Trajectory Manager Statistics =========="));
    UE_LOG(LogTrajectoryManager, Log, TEXT("  Trajectory Status:"));
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Active Trajectories: %d"), Stats.ActiveTrajectoryCount);
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Planned Trajectories: %d"), Stats.PlannedTrajectoryCount);
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Actual Trajectories : %d"), Stats.ActualTrajectoryCount);
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Total Points        : %d"), Stats.TotalPointsCount);
    UE_LOG(LogTrajectoryManager, Log, TEXT("  Processing:"));
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Messages Processed  : %d"), TotalMessagesProcessed);
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Trajectories Received: %d"), TotalTrajectoriesReceived);
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Expired Points Cleared: %d"), TotalExpiredPointsCleared);
    UE_LOG(LogTrajectoryManager, Log, TEXT("  Configuration:"));
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Lifetime            : %.1fs"), Config.LifetimeSeconds);
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Max Points Per Traj : %d"), Config.MaxPointsPerTrajectory);
    UE_LOG(LogTrajectoryManager, Log, TEXT("    Line Width          : %.1f"), Config.LineWidth);
    UE_LOG(LogTrajectoryManager, Log, TEXT("===================================================="));
}

// ========== C++ 接口实现 ==========

void UTrajectoryManager::DrawDebugTrajectories() const
{
    if (!World || !Config.bEnableDebugVisualization)
    {
        return;
    }

    const float LineThickness = FMath::Max(2.0f, Config.LineWidth * 1.5f);
    const float PointRadius = FMath::Max(4.0f, Config.DebugPointSize);
    const float CurrentTime = static_cast<float>(FPlatformTime::Seconds());

    auto MakeSegmentColor = [&](const FDeviceTrajectory& Trajectory, int32 PointIndex) -> FColor
    {
        const FLinearColor BaseColor =
            (Trajectory.TrajectoryType == ETrajectoryType::Planned ? Config.PlannedColor : Config.ActualColor);

        float FadeFactor = 1.0f;
        if (Config.bEnableFadeOut && Config.LifetimeSeconds > 0.0f && Trajectory.Points.IsValidIndex(PointIndex))
        {
            const float AgeSeconds = FMath::Max(0.0f, CurrentTime - Trajectory.Points[PointIndex].Timestamp);
            FadeFactor = 1.0f - FMath::Clamp(AgeSeconds / Config.LifetimeSeconds, 0.0f, 1.0f);
        }

        const float Intensity = FMath::Clamp((0.25f + FadeFactor * 0.75f) * FMath::Max(0.1f, Config.Opacity), 0.1f, 1.0f);
        FLinearColor AdjustedColor = BaseColor * Intensity;
        AdjustedColor.A = 1.0f;
        return AdjustedColor.ToFColor(true);
    };

    for (const auto& Pair : TrajectoryCache)
    {
        const FDeviceTrajectory& Trajectory = Pair.Value;
        if (Trajectory.Points.Num() <= 0)
        {
            continue;
        }

        const FVector TrajectoryOffset = FVector(
            0.0f,
            0.0f,
            Trajectory.TrajectoryType == ETrajectoryType::Planned
                ? FMath::Max(0.0f, Config.DebugVerticalOffsetCm)
                : FMath::Max(0.0f, Config.ActualDebugVerticalOffsetCm));

        FVector PreviousLocation = Trajectory.Points[0].Location + TrajectoryOffset;
        const FColor StartColor = MakeSegmentColor(Trajectory, 0);
        DrawDebugSphere(
            World,
            PreviousLocation,
            PointRadius,
            10,
            StartColor,
            false,
            0.0f,
            0,
            FMath::Max(1.0f, LineThickness * 0.15f));

        for (int32 PointIndex = 1; PointIndex < Trajectory.Points.Num(); ++PointIndex)
        {
            const FVector CurrentLocation = Trajectory.Points[PointIndex].Location + TrajectoryOffset;
            const FColor SegmentColor = MakeSegmentColor(Trajectory, PointIndex);

            DrawDebugLine(
                World,
                PreviousLocation,
                CurrentLocation,
                SegmentColor,
                false,
                0.0f,
                0,
                LineThickness);

            if (Config.bDrawDirectionArrows && FVector::DistSquared(PreviousLocation, CurrentLocation) > KINDA_SMALL_NUMBER)
            {
                DrawDebugDirectionalArrow(
                    World,
                    PreviousLocation,
                    CurrentLocation,
                    Config.DirectionArrowSizeCm,
                    SegmentColor,
                    false,
                    0.0f,
                    0,
                    FMath::Max(1.0f, LineThickness * 0.8f));
            }

            DrawDebugSphere(
                World,
                CurrentLocation,
                PointRadius,
                10,
                SegmentColor,
                false,
                0.0f,
                0,
                FMath::Max(1.0f, LineThickness * 0.15f));

            PreviousLocation = CurrentLocation;
        }

        if (Config.bDrawDeviceLabel)
        {
            const FString TypeLabel =
                (Trajectory.TrajectoryType == ETrajectoryType::Planned) ? TEXT("Planned") : TEXT("Actual");
            const FString DebugLabel = FString::Printf(
                TEXT("Traj %d %s (%d pts)"),
                Trajectory.DeviceID,
                *TypeLabel,
                Trajectory.Points.Num());

            DrawDebugString(
                World,
                PreviousLocation + FVector(0.0f, 0.0f, PointRadius * 3.0f),
                DebugLabel,
                nullptr,
                MakeSegmentColor(Trajectory, Trajectory.Points.Num() - 1),
                0.0f,
                false);
        }
    }
}

const FDeviceTrajectory* UTrajectoryManager::GetDeviceTrajectoryPtr(int32 DeviceID, ETrajectoryType TrajectoryType) const
{
    const int32 CacheKey = MakeTrajectoryKey(DeviceID, TrajectoryType);
    return TrajectoryCache.Find(CacheKey);
}
