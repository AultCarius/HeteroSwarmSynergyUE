// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudManager.h"

#include "Common/UdpSocketBuilder.h"
#include "Containers/StringConv.h"
#include "CoordinateConverter.h"
#include "Dom/JsonObject.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "PointCloudRenderActor.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "UDPManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPointCloudManager, Log, All);

namespace
{
    constexpr ANSICHAR CompactPointCloudMagic[4] = { 'P', 'C', 'U', '1' };
    constexpr uint8 CompactPointCloudVersion = 1;
    constexpr uint8 CompactPointFormatXYZ32F = 0;
    constexpr uint8 CompactPointFormatXYZ32FRGBA8 = 1;
    constexpr uint8 CompactFlagPointsAreWorldSpace = 1 << 3;
    constexpr int32 CompactHeaderBytes = 56;

    template <typename TValue>
    void AppendRawValue(TArray<uint8>& Buffer, const TValue& Value)
    {
        const int32 StartIndex = Buffer.AddUninitialized(sizeof(TValue));
        FMemory::Memcpy(Buffer.GetData() + StartIndex, &Value, sizeof(TValue));
    }

    void AppendRawBytes(TArray<uint8>& Buffer, const void* Data, int32 NumBytes)
    {
        if (NumBytes <= 0)
        {
            return;
        }

        const int32 StartIndex = Buffer.AddUninitialized(NumBytes);
        FMemory::Memcpy(Buffer.GetData() + StartIndex, Data, NumBytes);
    }

    int32 GetCompactPointStride(const uint8 PointFormat)
    {
        switch (PointFormat)
        {
        case CompactPointFormatXYZ32F:
            return 12;
        case CompactPointFormatXYZ32FRGBA8:
            return 16;
        default:
            return 0;
        }
    }

    FString BuildDefaultPointCloudReceiverJson()
    {
        return FString(
            TEXT("{\n")
            TEXT("  \"max_points_per_cloud\": 50000,\n")
            TEXT("  \"enable_compact_udp_receiver\": true,\n")
            TEXT("  \"compact_listen_address\": \"0.0.0.0\",\n")
            TEXT("  \"compact_listen_port\": 15000,\n")
            TEXT("  \"compact_receive_buffer_size_bytes\": 4194304,\n")
            TEXT("  \"compact_synthetic_device_id\": 15000,\n")
            TEXT("  \"compact_fragment_timeout_seconds\": 2.0,\n")
            TEXT("  \"enable_renderer_actor\": true,\n")
            TEXT("  \"renderer_point_size_cm\": 18.0,\n")
            TEXT("  \"renderer_cloud_scale\": 1.5,\n")
            TEXT("  \"renderer_distance_scaling\": 1200.0,\n")
            TEXT("  \"renderer_override_color\": false,\n")
            TEXT("  \"enable_debug_draw\": true,\n")
            TEXT("  \"max_debug_draw_points_per_cloud\": 3000,\n")
            TEXT("  \"debug_draw_point_size\": 14.0,\n")
            TEXT("  \"cloud_stale_timeout_seconds\": 3.0,\n")
            TEXT("  \"draw_debug_bounds\": true,\n")
            TEXT("  \"draw_debug_labels\": true,\n")
            TEXT("  \"debug_label_height_offset_cm\": 40.0\n")
            TEXT("}\n"));
    }

    FString GetPointCloudReceiverConfigPath()
    {
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("PointCloudReceiver.json"));
    }

    bool EnsureDefaultPointCloudReceiverConfigFileExists(const FString& AbsoluteConfigPath)
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
            BuildDefaultPointCloudReceiverJson(),
            *AbsoluteConfigPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    bool TryGetBoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool& OutValue)
    {
        return Object.IsValid() && Object->TryGetBoolField(FieldName, OutValue);
    }

    bool TryGetStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FString& OutValue)
    {
        return Object.IsValid() && Object->TryGetStringField(FieldName, OutValue);
    }

    bool TryGetNumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, double& OutValue)
    {
        return Object.IsValid() && Object->TryGetNumberField(FieldName, OutValue);
    }

    bool LoadPointCloudConfigFromJson(FPointCloudManagerConfig& InOutConfig, FString& OutStatus)
    {
        const FString ConfigPath = GetPointCloudReceiverConfigPath();
        if (!EnsureDefaultPointCloudReceiverConfigFileExists(ConfigPath))
        {
            OutStatus = FString::Printf(TEXT("Failed to create point cloud config: %s"), *ConfigPath);
            return false;
        }

        FString JsonText;
        if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath))
        {
            OutStatus = FString::Printf(TEXT("Failed to load point cloud config: %s"), *ConfigPath);
            return false;
        }

        TSharedPtr<FJsonObject> RootObject;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
        {
            OutStatus = FString::Printf(TEXT("Failed to parse point cloud config: %s"), *ConfigPath);
            return false;
        }

        double NumberValue = 0.0;
        bool BoolValue = false;
        FString StringValue;

        if (TryGetNumberField(RootObject, TEXT("max_points_per_cloud"), NumberValue))
        {
            InOutConfig.MaxPointsPerCloud = FMath::Clamp(FMath::RoundToInt(NumberValue), 1, 500000);
        }

        if (TryGetBoolField(RootObject, TEXT("enable_compact_udp_receiver"), BoolValue))
        {
            InOutConfig.bEnableCompactUdpReceiver = BoolValue;
        }

        if (TryGetStringField(RootObject, TEXT("compact_listen_address"), StringValue))
        {
            InOutConfig.CompactListenAddress = StringValue;
        }

        if (TryGetNumberField(RootObject, TEXT("compact_listen_port"), NumberValue))
        {
            InOutConfig.CompactListenPort = FMath::Clamp(FMath::RoundToInt(NumberValue), 1, 65535);
        }

        if (TryGetNumberField(RootObject, TEXT("compact_receive_buffer_size_bytes"), NumberValue))
        {
            InOutConfig.CompactReceiveBufferSizeBytes = FMath::Clamp(FMath::RoundToInt(NumberValue), 65536, 16777216);
        }

        if (TryGetNumberField(RootObject, TEXT("compact_synthetic_device_id"), NumberValue))
        {
            InOutConfig.CompactSyntheticDeviceID = FMath::Max(1, FMath::RoundToInt(NumberValue));
        }

        if (TryGetNumberField(RootObject, TEXT("compact_fragment_timeout_seconds"), NumberValue))
        {
            InOutConfig.CompactFragmentTimeoutSeconds = FMath::Clamp(static_cast<float>(NumberValue), 0.1f, 30.0f);
        }

        if (TryGetBoolField(RootObject, TEXT("enable_renderer_actor"), BoolValue))
        {
            InOutConfig.bEnableRendererActor = BoolValue;
        }

        if (TryGetNumberField(RootObject, TEXT("renderer_point_size_cm"), NumberValue))
        {
            InOutConfig.RendererPointSizeCm = FMath::Clamp(static_cast<float>(NumberValue), 0.1f, 100.0f);
        }

        if (TryGetNumberField(RootObject, TEXT("renderer_cloud_scale"), NumberValue))
        {
            InOutConfig.RendererCloudScale = FMath::Clamp(static_cast<float>(NumberValue), 0.001f, 100.0f);
        }

        if (TryGetNumberField(RootObject, TEXT("renderer_distance_scaling"), NumberValue))
        {
            InOutConfig.RendererDistanceScaling = FMath::Clamp(static_cast<float>(NumberValue), 1.0f, 100000.0f);
        }

        if (TryGetBoolField(RootObject, TEXT("renderer_override_color"), BoolValue))
        {
            InOutConfig.bRendererOverrideColor = BoolValue;
        }

        if (TryGetBoolField(RootObject, TEXT("enable_debug_draw"), BoolValue))
        {
            InOutConfig.bEnableDebugDraw = BoolValue;
        }

        if (TryGetNumberField(RootObject, TEXT("max_debug_draw_points_per_cloud"), NumberValue))
        {
            InOutConfig.MaxDebugDrawPointsPerCloud = FMath::Clamp(FMath::RoundToInt(NumberValue), 1, 10000);
        }

        if (TryGetNumberField(RootObject, TEXT("debug_draw_point_size"), NumberValue))
        {
            InOutConfig.DebugDrawPointSize = FMath::Clamp(static_cast<float>(NumberValue), 1.0f, 30.0f);
        }

        if (TryGetNumberField(RootObject, TEXT("cloud_stale_timeout_seconds"), NumberValue))
        {
            InOutConfig.CloudStaleTimeoutSeconds = FMath::Clamp(static_cast<float>(NumberValue), 0.0f, 60.0f);
        }

        if (TryGetBoolField(RootObject, TEXT("draw_debug_bounds"), BoolValue))
        {
            InOutConfig.bDrawDebugBounds = BoolValue;
        }

        if (TryGetBoolField(RootObject, TEXT("draw_debug_labels"), BoolValue))
        {
            InOutConfig.bDrawDebugLabels = BoolValue;
        }

        if (TryGetNumberField(RootObject, TEXT("debug_label_height_offset_cm"), NumberValue))
        {
            InOutConfig.DebugLabelHeightOffsetCm = FMath::Clamp(static_cast<float>(NumberValue), 0.0f, 300.0f);
        }

        OutStatus = FString::Printf(
            TEXT("Loaded point cloud config from %s (CompactUDP=%s, Port=%d, Renderer=%s, DebugDraw=%s, StaleTimeout=%.1fs)"),
            *ConfigPath,
            InOutConfig.bEnableCompactUdpReceiver ? TEXT("ON") : TEXT("OFF"),
            InOutConfig.CompactListenPort,
            InOutConfig.bEnableRendererActor ? TEXT("ON") : TEXT("OFF"),
            InOutConfig.bEnableDebugDraw ? TEXT("ON") : TEXT("OFF"),
            InOutConfig.CloudStaleTimeoutSeconds);
        return true;
    }
}

UPointCloudManager::UPointCloudManager()
    : UDPManager(nullptr)
    , World(nullptr)
    , bIsInitialized(false)
    , TotalMessagesProcessed(0)
    , TotalFramesReceived(0)
    , TotalPointsReceived(0)
    , CompactListenSocket(nullptr)
    , PointCloudRenderActor(nullptr)
    , bRendererDirty(false)
    , bOwnsRenderActor(false)
{
    UE_LOG(LogPointCloudManager, Log, TEXT("PointCloudManager constructed"));
}

UPointCloudManager::~UPointCloudManager()
{
    Shutdown();
    UE_LOG(LogPointCloudManager, Log, TEXT("PointCloudManager destroyed"));
}

bool UPointCloudManager::Initialize(UUDPManager* InUDPManager, UWorld* InWorld)
{
    if (bIsInitialized)
    {
        UE_LOG(LogPointCloudManager, Warning, TEXT("Already initialized"));
        return false;
    }

    if (!InUDPManager || !InWorld)
    {
        UE_LOG(LogPointCloudManager, Error, TEXT("Invalid initialization parameters"));
        return false;
    }

    UDPManager = InUDPManager;
    World = InWorld;

    FString PointCloudConfigStatus;
    if (LoadPointCloudConfigFromJson(Config, PointCloudConfigStatus))
    {
        UE_LOG(LogPointCloudManager, Log, TEXT("%s"), *PointCloudConfigStatus);
    }
    else
    {
        UE_LOG(LogPointCloudManager, Warning, TEXT("%s"), *PointCloudConfigStatus);
    }

    UDPManager->RegisterMessageHandler(static_cast<uint16>(EUDPMessageType::PointCloudData), this);

    if (Config.bEnableCompactUdpReceiver)
    {
        StartCompactUdpReceiver();
    }

    if (Config.bEnableRendererActor)
    {
        EnsureRenderActor();
    }

    bIsInitialized = true;

    UE_LOG(LogPointCloudManager, Log,
        TEXT("PointCloudManager initialized (MaxPointsPerCloud=%d, CompactUDP=%s, Renderer=%s, DebugDraw=%s)"),
        Config.MaxPointsPerCloud,
        Config.bEnableCompactUdpReceiver ? TEXT("ON") : TEXT("OFF"),
        Config.bEnableRendererActor ? TEXT("ON") : TEXT("OFF"),
        Config.bEnableDebugDraw ? TEXT("ON") : TEXT("OFF"));

    return true;
}

void UPointCloudManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogPointCloudManager, Log, TEXT("Shutting down PointCloudManager..."));
    PrintStatistics();

    if (UDPManager)
    {
        UDPManager->UnregisterMessageHandler(static_cast<uint16>(EUDPMessageType::PointCloudData));
        UDPManager = nullptr;
    }

    StopCompactUdpReceiver();
    CleanupRenderActor();
    PointCloudCache.Empty();
    CompactFragmentAssemblies.Empty();
    World = nullptr;
    bIsInitialized = false;

    UE_LOG(LogPointCloudManager, Log, TEXT("PointCloudManager shutdown complete"));
}

void UPointCloudManager::Tick(float DeltaTime)
{
    (void)DeltaTime;

    if (!bIsInitialized)
    {
        return;
    }

    ProcessPendingCompactDatagrams();
    PruneExpiredCompactFragments();
    CleanupStalePointClouds();

    if (bRendererDirty)
    {
        RefreshRenderedPointCloud();
    }

    if (Config.bEnableDebugDraw)
    {
        DrawDebugPointClouds();
    }
}

void UPointCloudManager::HandleMessage(const uint8* Data, uint32 Length)
{
    if (!Data || Length == 0)
    {
        UE_LOG(LogPointCloudManager, Warning, TEXT("Received invalid point cloud message"));
        return;
    }

    TotalMessagesProcessed++;
    ProcessPointCloudData(Data, Length);
}

bool UPointCloudManager::GetDevicePointCloud(int32 DeviceID, FDevicePointCloud& OutPointCloud) const
{
    const FDevicePointCloud* PointCloudPtr = PointCloudCache.Find(DeviceID);
    if (!PointCloudPtr)
    {
        return false;
    }

    OutPointCloud = *PointCloudPtr;
    return true;
}

TArray<FVector> UPointCloudManager::GetPointCloudLocations(int32 DeviceID) const
{
    TArray<FVector> Locations;

    const FDevicePointCloud* PointCloudPtr = PointCloudCache.Find(DeviceID);
    if (!PointCloudPtr)
    {
        return Locations;
    }

    Locations.Reserve(PointCloudPtr->Points.Num());
    for (const FPointCloudRuntimePoint& Point : PointCloudPtr->Points)
    {
        Locations.Add(Point.Location);
    }

    return Locations;
}

bool UPointCloudManager::HasPointCloud(int32 DeviceID) const
{
    const FDevicePointCloud* PointCloudPtr = PointCloudCache.Find(DeviceID);
    return PointCloudPtr && PointCloudPtr->Points.Num() > 0;
}

int32 UPointCloudManager::GetPointCloudPointCount(int32 DeviceID) const
{
    const FDevicePointCloud* PointCloudPtr = PointCloudCache.Find(DeviceID);
    return PointCloudPtr ? PointCloudPtr->Points.Num() : 0;
}

TArray<int32> UPointCloudManager::GetAllDeviceIDsWithPointCloud() const
{
    TArray<int32> DeviceIDs;
    PointCloudCache.GetKeys(DeviceIDs);
    DeviceIDs.Sort();
    return DeviceIDs;
}

void UPointCloudManager::GetStatistics(FPointCloudStatistics& OutStatistics) const
{
    OutStatistics.ActiveCloudCount = PointCloudCache.Num();
    OutStatistics.TotalMessagesProcessed = TotalMessagesProcessed;
    OutStatistics.TotalFramesReceived = TotalFramesReceived;
    OutStatistics.TotalPointsReceived = TotalPointsReceived;
}

void UPointCloudManager::SetConfig(const FPointCloudManagerConfig& NewConfig)
{
    const bool bReceiverSettingsChanged =
        Config.bEnableCompactUdpReceiver != NewConfig.bEnableCompactUdpReceiver ||
        Config.CompactListenAddress != NewConfig.CompactListenAddress ||
        Config.CompactListenPort != NewConfig.CompactListenPort ||
        Config.CompactReceiveBufferSizeBytes != NewConfig.CompactReceiveBufferSizeBytes;
    const bool bRendererSettingsChanged =
        Config.bEnableRendererActor != NewConfig.bEnableRendererActor ||
        !FMath::IsNearlyEqual(Config.RendererPointSizeCm, NewConfig.RendererPointSizeCm) ||
        !FMath::IsNearlyEqual(Config.RendererCloudScale, NewConfig.RendererCloudScale) ||
        !FMath::IsNearlyEqual(Config.RendererDistanceScaling, NewConfig.RendererDistanceScaling) ||
        Config.bRendererOverrideColor != NewConfig.bRendererOverrideColor;

    Config = NewConfig;
    Config.MaxPointsPerCloud = FMath::Clamp(Config.MaxPointsPerCloud, 1, 500000);
    Config.CompactListenPort = FMath::Clamp(Config.CompactListenPort, 1, 65535);
    Config.CompactReceiveBufferSizeBytes = FMath::Clamp(Config.CompactReceiveBufferSizeBytes, 65536, 16777216);
    Config.CompactSyntheticDeviceID = FMath::Max(1, Config.CompactSyntheticDeviceID);
    Config.CompactFragmentTimeoutSeconds = FMath::Clamp(Config.CompactFragmentTimeoutSeconds, 0.1f, 30.0f);
    Config.RendererPointSizeCm = FMath::Clamp(Config.RendererPointSizeCm, 0.1f, 100.0f);
    Config.RendererCloudScale = FMath::Clamp(Config.RendererCloudScale, 0.001f, 100.0f);
    Config.RendererDistanceScaling = FMath::Clamp(Config.RendererDistanceScaling, 1.0f, 100000.0f);
    Config.MaxDebugDrawPointsPerCloud = FMath::Clamp(Config.MaxDebugDrawPointsPerCloud, 1, 10000);
    Config.DebugDrawPointSize = FMath::Clamp(Config.DebugDrawPointSize, 1.0f, 30.0f);
    Config.CloudStaleTimeoutSeconds = FMath::Clamp(Config.CloudStaleTimeoutSeconds, 0.0f, 60.0f);
    Config.DebugLabelHeightOffsetCm = FMath::Clamp(Config.DebugLabelHeightOffsetCm, 0.0f, 300.0f);

    if (bIsInitialized && bReceiverSettingsChanged)
    {
        StopCompactUdpReceiver();
        if (Config.bEnableCompactUdpReceiver)
        {
            StartCompactUdpReceiver();
        }
    }

    if (bIsInitialized && bRendererSettingsChanged)
    {
        if (Config.bEnableRendererActor)
        {
            EnsureRenderActor();
            bRendererDirty = true;
        }
        else
        {
            CleanupRenderActor();
        }
    }

    UE_LOG(LogPointCloudManager, Log,
        TEXT("PointCloud config updated (MaxPointsPerCloud=%d, CompactUDP=%s, Port=%d, FragmentTimeout=%.2fs, Renderer=%s, DebugDraw=%s, DebugLimit=%d, StaleTimeout=%.1fs)"),
        Config.MaxPointsPerCloud,
        Config.bEnableCompactUdpReceiver ? TEXT("ON") : TEXT("OFF"),
        Config.CompactListenPort,
        Config.CompactFragmentTimeoutSeconds,
        Config.bEnableRendererActor ? TEXT("ON") : TEXT("OFF"),
        Config.bEnableDebugDraw ? TEXT("ON") : TEXT("OFF"),
        Config.MaxDebugDrawPointsPerCloud,
        Config.CloudStaleTimeoutSeconds);
}

bool UPointCloudManager::ClearDevicePointCloud(int32 DeviceID)
{
    const int32 Removed = PointCloudCache.Remove(DeviceID);
    if (Removed > 0)
    {
        bRendererDirty = true;
        UE_LOG(LogPointCloudManager, Log, TEXT("Cleared point cloud for Device=%d"), DeviceID);
        OnPointCloudCleared.Broadcast(DeviceID);
        return true;
    }

    return false;
}

void UPointCloudManager::ClearAllPointClouds()
{
    TArray<int32> DeviceIDs;
    PointCloudCache.GetKeys(DeviceIDs);

    PointCloudCache.Empty();
    bRendererDirty = true;

    for (int32 DeviceID : DeviceIDs)
    {
        OnPointCloudCleared.Broadcast(DeviceID);
    }

    UE_LOG(LogPointCloudManager, Log, TEXT("Cleared all point clouds (%d devices)"), DeviceIDs.Num());
}

void UPointCloudManager::PrintStatistics() const
{
    UE_LOG(LogPointCloudManager, Log, TEXT("========== Point Cloud Statistics =========="));
    UE_LOG(LogPointCloudManager, Log, TEXT("  Active Clouds           : %d"), PointCloudCache.Num());
    UE_LOG(LogPointCloudManager, Log, TEXT("  Total Messages Processed: %d"), TotalMessagesProcessed);
    UE_LOG(LogPointCloudManager, Log, TEXT("  Total Frames Received   : %d"), TotalFramesReceived);
    UE_LOG(LogPointCloudManager, Log, TEXT("  Total Points Received   : %d"), TotalPointsReceived);
    UE_LOG(LogPointCloudManager, Log, TEXT("============================================"));
}

void UPointCloudManager::ProcessPointCloudData(const uint8* Data, uint32 Length)
{
    constexpr uint32 HeaderSize = sizeof(uint32) + sizeof(uint32);

    if (Length < HeaderSize)
    {
        UE_LOG(LogPointCloudManager, Error,
            TEXT("Invalid point cloud message length: %d bytes (minimum %d)"),
            Length, HeaderSize);
        return;
    }

    const uint32* DeviceIDPtr = reinterpret_cast<const uint32*>(Data);
    const uint32* PointCountPtr = reinterpret_cast<const uint32*>(Data + sizeof(uint32));

    const int32 DeviceID = static_cast<int32>(*DeviceIDPtr);
    const int32 PointCount = static_cast<int32>(*PointCountPtr);

    if (!ValidatePointCloudHeader(DeviceID, PointCount))
    {
        return;
    }

    const uint32 ExpectedLength = HeaderSize + PointCount * sizeof(FPointCloudPoint);
    if (Length != ExpectedLength)
    {
        UE_LOG(LogPointCloudManager, Error,
            TEXT("Point cloud message length mismatch: expected=%d got=%d"),
            ExpectedLength, Length);
        return;
    }

    const FPointCloudPoint* PacketPoints =
        reinterpret_cast<const FPointCloudPoint*>(Data + HeaderSize);
    const float CurrentTime = static_cast<float>(FPlatformTime::Seconds());
    TArray<FPointCloudRuntimePoint> RuntimePoints;
    RuntimePoints.Reserve(FMath::Min(PointCount, Config.MaxPointsPerCloud));

    for (int32 Index = 0; Index < PointCount; ++Index)
    {
        if (RuntimePoints.Num() >= Config.MaxPointsPerCloud)
        {
            break;
        }

        const FPointCloudPoint& PacketPoint = PacketPoints[Index];
        if (!UCoordinateConverter::IsValidNED(PacketPoint.Position, 10000.0f))
        {
            continue;
        }

        FPointCloudRuntimePoint RuntimePoint;
        RuntimePoint.Location = UCoordinateConverter::NEDToUE(PacketPoint.Position);
        RuntimePoint.Intensity = FMath::IsFinite(PacketPoint.Intensity)
            ? FMath::Clamp(PacketPoint.Intensity, 0.0f, 1.0f)
            : 1.0f;
        RuntimePoint.Timestamp = CurrentTime;
        RuntimePoint.Color = MakeColorFromIntensity(RuntimePoint.Intensity);

        RuntimePoints.Add(RuntimePoint);
    }

    ApplyPointCloudFrame(DeviceID, RuntimePoints, CurrentTime, TEXT("Legacy0x0004"));
}

bool UPointCloudManager::ValidatePointCloudHeader(int32 DeviceID, int32 PointCount) const
{
    if (DeviceID <= 0)
    {
        UE_LOG(LogPointCloudManager, Warning, TEXT("Invalid point cloud device ID: %d"), DeviceID);
        return false;
    }

    if (PointCount <= 0 || PointCount > 1000000)
    {
        UE_LOG(LogPointCloudManager, Warning,
            TEXT("Invalid point cloud count: %d for device %d"),
            PointCount, DeviceID);
        return false;
    }

    return true;
}

void UPointCloudManager::ProcessPendingCompactDatagrams()
{
    FPendingCompactDatagram Datagram;

    while (PendingCompactDatagrams.Dequeue(Datagram))
    {
        TotalMessagesProcessed++;

        TArray<uint8> CompletedDatagram;
        if (!AccumulateCompactFragment(Datagram, CompletedDatagram))
        {
            continue;
        }

        int32 DeviceID = Config.CompactSyntheticDeviceID;
        TArray<FPointCloudRuntimePoint> RuntimePoints;
        if (DecodeCompactChunkedDatagram(CompletedDatagram, DeviceID, RuntimePoints))
        {
            ApplyPointCloudFrame(
                DeviceID,
                RuntimePoints,
                static_cast<float>(FPlatformTime::Seconds()),
                TEXT("CompactChunkedV1"));
        }
    }
}
bool UPointCloudManager::StartCompactUdpReceiver()
{
    if (CompactSocketReceiver || CompactListenSocket)
    {
        return true;
    }

    FIPv4Address ParsedAddress;
    if (!FIPv4Address::Parse(Config.CompactListenAddress, ParsedAddress))
    {
        UE_LOG(LogPointCloudManager, Warning,
            TEXT("Compact UDP receiver start failed: invalid address '%s'"),
            *Config.CompactListenAddress);
        return false;
    }

    CompactListenSocket = FUdpSocketBuilder(TEXT("HeteroSwarm_PointCloudCompactUDP"))
        .AsNonBlocking()
        .AsReusable()
        .BoundToAddress(ParsedAddress)
        .BoundToPort(Config.CompactListenPort)
        .WithReceiveBufferSize(Config.CompactReceiveBufferSizeBytes);

    if (!CompactListenSocket)
    {
        UE_LOG(LogPointCloudManager, Warning,
            TEXT("Compact UDP receiver start failed: could not bind %s:%d"),
            *Config.CompactListenAddress, Config.CompactListenPort);
        return false;
    }

    int32 BufferSize = Config.CompactReceiveBufferSizeBytes;
    CompactListenSocket->SetReceiveBufferSize(BufferSize, BufferSize);

    CompactSocketReceiver = MakeUnique<FUdpSocketReceiver>(
        CompactListenSocket,
        FTimespan::FromMilliseconds(5),
        TEXT("HeteroSwarm_PointCloudCompactUDP_Receiver"));
    CompactSocketReceiver->OnDataReceived().BindUObject(this, &UPointCloudManager::HandleCompactUdpDataReceived);
    CompactSocketReceiver->Start();

    UE_LOG(LogPointCloudManager, Log,
        TEXT("Compact UDP receiver listening on %s:%d"),
        *Config.CompactListenAddress,
        Config.CompactListenPort);

    return true;
}

void UPointCloudManager::StopCompactUdpReceiver()
{
    if (CompactSocketReceiver)
    {
        CompactSocketReceiver->Stop();
        CompactSocketReceiver.Reset();
    }

    if (CompactListenSocket)
    {
        CompactListenSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(CompactListenSocket);
        CompactListenSocket = nullptr;
    }

    FPendingCompactDatagram DroppedDatagram;
    while (PendingCompactDatagrams.Dequeue(DroppedDatagram))
    {
    }

    CompactFragmentAssemblies.Empty();
}

void UPointCloudManager::HandleCompactUdpDataReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Endpoint)
{
    if (!Data.IsValid() || Data->Num() <= 0)
    {
        return;
    }

    FPendingCompactDatagram Datagram;
    Datagram.Bytes.Append(Data->GetData(), Data->Num());
    Datagram.EndpointKey = Endpoint.ToString();
    PendingCompactDatagrams.Enqueue(MoveTemp(Datagram));
}

bool UPointCloudManager::TryParseCompactChunkedDatagram(
    const TArray<uint8>& Bytes,
    FCompactChunkHeader& OutHeader,
    FString& OutFrameName,
    const uint8*& OutPayloadData,
    int32& OutPayloadSize) const
{
    if (Bytes.Num() < CompactHeaderBytes)
    {
        return false;
    }

    const uint8* Cursor = Bytes.GetData();
    int32 Remaining = Bytes.Num();

    auto ReadBytes = [&Cursor, &Remaining](void* OutBuffer, int32 Size) -> bool
    {
        if (Size < 0 || Remaining < Size)
        {
            return false;
        }

        FMemory::Memcpy(OutBuffer, Cursor, Size);
        Cursor += Size;
        Remaining -= Size;
        return true;
    };

    char Magic[4] = {};
    uint8 Version = 0;
    uint8 Reserved = 0;

    if (!ReadBytes(Magic, 4) ||
        !ReadBytes(&Version, sizeof(Version)) ||
        !ReadBytes(&OutHeader.PointFormat, sizeof(OutHeader.PointFormat)) ||
        !ReadBytes(&OutHeader.Flags, sizeof(OutHeader.Flags)) ||
        !ReadBytes(&OutHeader.FragmentIndex, sizeof(OutHeader.FragmentIndex)) ||
        !ReadBytes(&OutHeader.FragmentCount, sizeof(OutHeader.FragmentCount)) ||
        !ReadBytes(&Reserved, sizeof(Reserved)) ||
        !ReadBytes(&OutHeader.FrameNameByteCount, sizeof(OutHeader.FrameNameByteCount)) ||
        !ReadBytes(&OutHeader.Sequence, sizeof(OutHeader.Sequence)) ||
        !ReadBytes(&OutHeader.PointCount, sizeof(OutHeader.PointCount)) ||
        !ReadBytes(&OutHeader.PayloadBytes, sizeof(OutHeader.PayloadBytes)) ||
        !ReadBytes(&OutHeader.DefaultPointSizeCm, sizeof(OutHeader.DefaultPointSizeCm)) ||
        !ReadBytes(&OutHeader.Translation.X, sizeof(OutHeader.Translation.X)) ||
        !ReadBytes(&OutHeader.Translation.Y, sizeof(OutHeader.Translation.Y)) ||
        !ReadBytes(&OutHeader.Translation.Z, sizeof(OutHeader.Translation.Z)) ||
        !ReadBytes(&OutHeader.Rotation.X, sizeof(OutHeader.Rotation.X)) ||
        !ReadBytes(&OutHeader.Rotation.Y, sizeof(OutHeader.Rotation.Y)) ||
        !ReadBytes(&OutHeader.Rotation.Z, sizeof(OutHeader.Rotation.Z)) ||
        !ReadBytes(&OutHeader.Rotation.W, sizeof(OutHeader.Rotation.W)))
    {
        return false;
    }

    if (Magic[0] != CompactPointCloudMagic[0] ||
        Magic[1] != CompactPointCloudMagic[1] ||
        Magic[2] != CompactPointCloudMagic[2] ||
        Magic[3] != CompactPointCloudMagic[3] ||
        Version != CompactPointCloudVersion)
    {
        return false;
    }

    if (OutHeader.FragmentCount == 0 || OutHeader.FragmentIndex >= OutHeader.FragmentCount)
    {
        return false;
    }

    if (OutHeader.FrameNameByteCount > static_cast<uint16>(Remaining))
    {
        return false;
    }

    if (OutHeader.FrameNameByteCount > 0)
    {
        const FUTF8ToTCHAR FrameNameConverter(
            reinterpret_cast<const ANSICHAR*>(Cursor),
            static_cast<int32>(OutHeader.FrameNameByteCount));
        OutFrameName = FString(FrameNameConverter.Length(), FrameNameConverter.Get());
    }
    else
    {
        OutFrameName.Reset();
    }

    Cursor += OutHeader.FrameNameByteCount;
    Remaining -= OutHeader.FrameNameByteCount;

    if (OutHeader.PayloadBytes > static_cast<uint32>(Remaining))
    {
        return false;
    }

    OutPayloadData = Cursor;
    OutPayloadSize = static_cast<int32>(OutHeader.PayloadBytes);
    return true;
}

bool UPointCloudManager::DecodeCompactChunkedDatagram(
    const TArray<uint8>& Bytes,
    int32& OutDeviceID,
    TArray<FPointCloudRuntimePoint>& OutPoints) const
{
    FCompactChunkHeader Header;
    FString FrameName;
    const uint8* PayloadData = nullptr;
    int32 PayloadSize = 0;

    if (!TryParseCompactChunkedDatagram(Bytes, Header, FrameName, PayloadData, PayloadSize))
    {
        return false;
    }

    if (Header.FragmentCount > 1)
    {
        return false;
    }

    const int32 PointStride = GetCompactPointStride(Header.PointFormat);
    if (PointStride <= 0 || PayloadSize != static_cast<int32>(Header.PointCount) * PointStride)
    {
        return false;
    }

    OutDeviceID = FMath::Max(1, Config.CompactSyntheticDeviceID);
    OutPoints.Reset();
    OutPoints.Reserve(FMath::Min(static_cast<int32>(Header.PointCount), Config.MaxPointsPerCloud));

    const bool bPointsAreWorldSpace = (Header.Flags & CompactFlagPointsAreWorldSpace) != 0;
    const FTransform PacketTransform(
        Header.Rotation.GetNormalized(),
        Header.Translation * 100.0f,
        FVector::OneVector);

    const uint8* Cursor = PayloadData;
    int32 Remaining = PayloadSize;
    const float Timestamp = static_cast<float>(FPlatformTime::Seconds());

    auto ReadPayloadBytes = [&Cursor, &Remaining](void* OutBuffer, int32 Size) -> bool
    {
        if (Size < 0 || Remaining < Size)
        {
            return false;
        }

        FMemory::Memcpy(OutBuffer, Cursor, Size);
        Cursor += Size;
        Remaining -= Size;
        return true;
    };

    for (uint32 Index = 0; Index < Header.PointCount; ++Index)
    {
        if (OutPoints.Num() >= Config.MaxPointsPerCloud)
        {
            break;
        }

        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        if (!ReadPayloadBytes(&X, sizeof(X)) ||
            !ReadPayloadBytes(&Y, sizeof(Y)) ||
            !ReadPayloadBytes(&Z, sizeof(Z)))
        {
            return false;
        }

        FVector Position = FVector(X, Y, Z) * 100.0f;
        if (!bPointsAreWorldSpace)
        {
            Position = PacketTransform.TransformPosition(Position);
        }

        FPointCloudRuntimePoint RuntimePoint;
        RuntimePoint.Location = Position;
        RuntimePoint.Timestamp = Timestamp;

        if (PointStride == 16)
        {
            uint8 R = 255;
            uint8 G = 255;
            uint8 B = 255;
            uint8 A = 255;
            if (!ReadPayloadBytes(&R, sizeof(R)) ||
                !ReadPayloadBytes(&G, sizeof(G)) ||
                !ReadPayloadBytes(&B, sizeof(B)) ||
                !ReadPayloadBytes(&A, sizeof(A)))
            {
                return false;
            }

            RuntimePoint.Color = FLinearColor(
                static_cast<float>(R) / 255.0f,
                static_cast<float>(G) / 255.0f,
                static_cast<float>(B) / 255.0f,
                static_cast<float>(A) / 255.0f);
            RuntimePoint.Intensity = FMath::Clamp(
                (RuntimePoint.Color.R + RuntimePoint.Color.G + RuntimePoint.Color.B) / 3.0f,
                0.0f,
                1.0f);
        }
        else
        {
            RuntimePoint.Intensity = 1.0f;
            RuntimePoint.Color = MakeColorFromIntensity(RuntimePoint.Intensity);
        }

        OutPoints.Add(RuntimePoint);
    }

    return OutPoints.Num() > 0;
}
bool UPointCloudManager::AccumulateCompactFragment(
    const FPendingCompactDatagram& Datagram,
    TArray<uint8>& OutCompletedDatagram)
{
    OutCompletedDatagram.Reset();

    FCompactChunkHeader Header;
    FString FrameName;
    const uint8* PayloadData = nullptr;
    int32 PayloadSize = 0;
    if (!TryParseCompactChunkedDatagram(Datagram.Bytes, Header, FrameName, PayloadData, PayloadSize))
    {
        return false;
    }

    if (Header.FragmentCount <= 1)
    {
        OutCompletedDatagram = Datagram.Bytes;
        return true;
    }

    const int32 PointStride = GetCompactPointStride(Header.PointFormat);
    if (PointStride <= 0 || Header.PointCount == 0)
    {
        return false;
    }

    const FString AssemblyKey = MakeCompactAssemblyKey(Datagram.EndpointKey, Header.Sequence);

    auto InitializeAssembly = [&](FCompactFragmentAssembly& Assembly)
    {
        Assembly.Header = Header;
        Assembly.EndpointKey = Datagram.EndpointKey;
        Assembly.FrameName = FrameName;
        Assembly.FirstSeenTimeSeconds = FPlatformTime::Seconds();
        Assembly.ReceivedFragmentCount = 0;
        Assembly.FragmentPayloads.Reset();
        Assembly.FragmentPayloads.SetNum(Header.FragmentCount);
        Assembly.ReceivedFlags.Init(false, Header.FragmentCount);
    };

    FCompactFragmentAssembly* Assembly = CompactFragmentAssemblies.Find(AssemblyKey);
    if (Assembly == nullptr)
    {
        FCompactFragmentAssembly NewAssembly;
        InitializeAssembly(NewAssembly);
        CompactFragmentAssemblies.Add(AssemblyKey, MoveTemp(NewAssembly));
        Assembly = CompactFragmentAssemblies.Find(AssemblyKey);
    }
    else
    {
        const bool bHeaderMismatch =
            Assembly->Header.PointFormat != Header.PointFormat ||
            Assembly->Header.Flags != Header.Flags ||
            Assembly->Header.FragmentCount != Header.FragmentCount ||
            Assembly->Header.PointCount != Header.PointCount ||
            !Assembly->Header.Translation.Equals(Header.Translation, KINDA_SMALL_NUMBER) ||
            !Assembly->Header.Rotation.Equals(Header.Rotation, KINDA_SMALL_NUMBER) ||
            Assembly->FrameName != FrameName;

        if (bHeaderMismatch)
        {
            UE_LOG(LogPointCloudManager, Warning,
                TEXT("Compact point cloud fragment header changed during reassembly (Endpoint=%s, Sequence=%u); resetting assembly"),
                *Datagram.EndpointKey,
                Header.Sequence);
            InitializeAssembly(*Assembly);
        }
    }

    check(Assembly);

    if (!Assembly->ReceivedFlags.IsValidIndex(Header.FragmentIndex))
    {
        CompactFragmentAssemblies.Remove(AssemblyKey);
        return false;
    }

    if (!Assembly->ReceivedFlags[Header.FragmentIndex])
    {
        Assembly->FragmentPayloads[Header.FragmentIndex].Reset();
        Assembly->FragmentPayloads[Header.FragmentIndex].Append(PayloadData, PayloadSize);
        Assembly->ReceivedFlags[Header.FragmentIndex] = true;
        ++Assembly->ReceivedFragmentCount;
    }
    else
    {
        Assembly->FragmentPayloads[Header.FragmentIndex].Reset();
        Assembly->FragmentPayloads[Header.FragmentIndex].Append(PayloadData, PayloadSize);
    }

    if (Assembly->ReceivedFragmentCount < Assembly->Header.FragmentCount)
    {
        return false;
    }

    int32 ReassembledPayloadBytes = 0;
    for (int32 FragmentIndex = 0; FragmentIndex < Assembly->Header.FragmentCount; ++FragmentIndex)
    {
        if (!Assembly->ReceivedFlags.IsValidIndex(FragmentIndex) || !Assembly->ReceivedFlags[FragmentIndex])
        {
            return false;
        }

        ReassembledPayloadBytes += Assembly->FragmentPayloads[FragmentIndex].Num();
    }

    const int32 ExpectedPayloadBytes = static_cast<int32>(Assembly->Header.PointCount) * PointStride;
    if (ReassembledPayloadBytes != ExpectedPayloadBytes)
    {
        UE_LOG(LogPointCloudManager, Warning,
            TEXT("Compact point cloud reassembly size mismatch (Endpoint=%s, Sequence=%u, Expected=%d, Actual=%d)"),
            *Datagram.EndpointKey,
            Assembly->Header.Sequence,
            ExpectedPayloadBytes,
            ReassembledPayloadBytes);
        CompactFragmentAssemblies.Remove(AssemblyKey);
        return false;
    }

    const bool bBuilt = BuildCompactDatagram(
        Assembly->Header,
        Assembly->FrameName,
        Assembly->FragmentPayloads,
        OutCompletedDatagram);

    CompactFragmentAssemblies.Remove(AssemblyKey);

    if (bBuilt)
    {
        UE_LOG(LogPointCloudManager, Verbose,
            TEXT("Compact point cloud reassembled successfully (Endpoint=%s, Sequence=%u, Fragments=%u, Points=%u)"),
            *Datagram.EndpointKey,
            Header.Sequence,
            Header.FragmentCount,
            Header.PointCount);
    }

    return bBuilt;
}

bool UPointCloudManager::BuildCompactDatagram(
    const FCompactChunkHeader& Header,
    const FString& FrameName,
    const TArray<TArray<uint8>>& FragmentPayloads,
    TArray<uint8>& OutBytes) const
{
    const int32 PointStride = GetCompactPointStride(Header.PointFormat);
    if (PointStride <= 0)
    {
        return false;
    }

    FTCHARToUTF8 FrameNameUtf8(*FrameName);
    const uint16 FrameNameByteCount = static_cast<uint16>(FMath::Min(FrameNameUtf8.Length(), 65535));

    int32 PayloadBytes = 0;
    for (const TArray<uint8>& FragmentPayload : FragmentPayloads)
    {
        PayloadBytes += FragmentPayload.Num();
    }

    if (PayloadBytes != static_cast<int32>(Header.PointCount) * PointStride)
    {
        return false;
    }

    OutBytes.Reset();
    OutBytes.Reserve(CompactHeaderBytes + FrameNameByteCount + PayloadBytes);

    AppendRawBytes(OutBytes, CompactPointCloudMagic, 4);
    AppendRawValue<uint8>(OutBytes, CompactPointCloudVersion);
    AppendRawValue<uint8>(OutBytes, Header.PointFormat);
    AppendRawValue<uint8>(OutBytes, Header.Flags);
    AppendRawValue<uint8>(OutBytes, 0);
    AppendRawValue<uint8>(OutBytes, 1);
    AppendRawValue<uint8>(OutBytes, 0);
    AppendRawValue<uint16>(OutBytes, FrameNameByteCount);
    AppendRawValue<uint32>(OutBytes, Header.Sequence);
    AppendRawValue<uint32>(OutBytes, Header.PointCount);
    AppendRawValue<uint32>(OutBytes, static_cast<uint32>(PayloadBytes));
    AppendRawValue<float>(OutBytes, Header.DefaultPointSizeCm);
    AppendRawValue<float>(OutBytes, Header.Translation.X);
    AppendRawValue<float>(OutBytes, Header.Translation.Y);
    AppendRawValue<float>(OutBytes, Header.Translation.Z);
    AppendRawValue<float>(OutBytes, Header.Rotation.X);
    AppendRawValue<float>(OutBytes, Header.Rotation.Y);
    AppendRawValue<float>(OutBytes, Header.Rotation.Z);
    AppendRawValue<float>(OutBytes, Header.Rotation.W);

    AppendRawBytes(OutBytes, FrameNameUtf8.Get(), FrameNameByteCount);

    for (const TArray<uint8>& FragmentPayload : FragmentPayloads)
    {
        AppendRawBytes(OutBytes, FragmentPayload.GetData(), FragmentPayload.Num());
    }

    return true;
}

FString UPointCloudManager::MakeCompactAssemblyKey(const FString& EndpointKey, uint32 Sequence) const
{
    return FString::Printf(TEXT("%s|%u"), *EndpointKey, Sequence);
}

void UPointCloudManager::PruneExpiredCompactFragments()
{
    if (CompactFragmentAssemblies.Num() == 0)
    {
        return;
    }

    const double CurrentTimeSeconds = FPlatformTime::Seconds();
    const double TimeoutSeconds = FMath::Max(0.1, static_cast<double>(Config.CompactFragmentTimeoutSeconds));

    TArray<FString> ExpiredKeys;
    for (const TPair<FString, FCompactFragmentAssembly>& Pair : CompactFragmentAssemblies)
    {
        if ((CurrentTimeSeconds - Pair.Value.FirstSeenTimeSeconds) > TimeoutSeconds)
        {
            ExpiredKeys.Add(Pair.Key);
        }
    }

    for (const FString& ExpiredKey : ExpiredKeys)
    {
        const FCompactFragmentAssembly* ExpiredAssembly = CompactFragmentAssemblies.Find(ExpiredKey);
        if (ExpiredAssembly != nullptr)
        {
            UE_LOG(LogPointCloudManager, Warning,
                TEXT("Dropping expired compact point cloud assembly (Endpoint=%s, Sequence=%u, Received=%d/%u)"),
                *ExpiredAssembly->EndpointKey,
                ExpiredAssembly->Header.Sequence,
                ExpiredAssembly->ReceivedFragmentCount,
                ExpiredAssembly->Header.FragmentCount);
        }

        CompactFragmentAssemblies.Remove(ExpiredKey);
    }
}

void UPointCloudManager::EnsureRenderActor()
{
    if (!World || !Config.bEnableRendererActor)
    {
        return;
    }

    if (IsValid(PointCloudRenderActor))
    {
        return;
    }

    if (AActor* ExistingActor = UGameplayStatics::GetActorOfClass(World, APointCloudRenderActor::StaticClass()))
    {
        PointCloudRenderActor = Cast<APointCloudRenderActor>(ExistingActor);
        bOwnsRenderActor = false;
        return;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = TEXT("AutoPointCloudRenderActor");
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    PointCloudRenderActor = World->SpawnActor<APointCloudRenderActor>(
        APointCloudRenderActor::StaticClass(),
        FTransform::Identity,
        SpawnParameters);
    bOwnsRenderActor = IsValid(PointCloudRenderActor);
}

void UPointCloudManager::CleanupRenderActor()
{
    if (!IsValid(PointCloudRenderActor))
    {
        PointCloudRenderActor = nullptr;
        bOwnsRenderActor = false;
        return;
    }

    if (bOwnsRenderActor)
    {
        PointCloudRenderActor->Destroy();
    }

    PointCloudRenderActor = nullptr;
    bOwnsRenderActor = false;
}

void UPointCloudManager::RefreshRenderedPointCloud()
{
    bRendererDirty = false;

    if (!Config.bEnableRendererActor)
    {
        CleanupRenderActor();
        return;
    }

    EnsureRenderActor();

    if (!IsValid(PointCloudRenderActor))
    {
        return;
    }

    int32 TotalPointCount = 0;
    for (const auto& Pair : PointCloudCache)
    {
        TotalPointCount += Pair.Value.Points.Num();
    }

    TArray<FVector> Positions;
    TArray<FColor> Colors;
    Positions.Reserve(TotalPointCount);
    Colors.Reserve(TotalPointCount);

    for (const auto& Pair : PointCloudCache)
    {
        for (const FPointCloudRuntimePoint& Point : Pair.Value.Points)
        {
            Positions.Add(Point.Location);
            Colors.Add(Point.Color.ToFColor(true));
        }
    }

    if (Positions.Num() <= 0)
    {
        PointCloudRenderActor->ClearRenderedCloud();
        return;
    }

    PointCloudRenderActor->UpdateRenderedCloud(
        Positions,
        Colors,
        Config.RendererPointSizeCm,
        Config.RendererCloudScale,
        Config.RendererDistanceScaling,
        Config.bRendererOverrideColor);
}

void UPointCloudManager::ApplyPointCloudFrame(
    int32 DeviceID,
    const TArray<FPointCloudRuntimePoint>& Points,
    float Timestamp,
    const TCHAR* SourceLabel)
{
    if (Points.Num() <= 0)
    {
        return;
    }

    FDevicePointCloud* PointCloudPtr = PointCloudCache.Find(DeviceID);
    const bool bIsNewCloud = (PointCloudPtr == nullptr);

    if (bIsNewCloud)
    {
        FDevicePointCloud NewPointCloud;
        NewPointCloud.DeviceID = DeviceID;
        PointCloudCache.Add(DeviceID, NewPointCloud);
        PointCloudPtr = PointCloudCache.Find(DeviceID);

        UE_LOG(LogPointCloudManager, Log,
            TEXT("New point cloud stream detected: Device=%d Source=%s"),
            DeviceID, SourceLabel);
    }

    check(PointCloudPtr);

    PointCloudPtr->Points = Points;
    PointCloudPtr->TotalPointsReceived += Points.Num();
    PointCloudPtr->LastUpdateTime = Timestamp;

    TotalFramesReceived++;
    TotalPointsReceived += Points.Num();
    bRendererDirty = true;

    OnPointCloudUpdated.Broadcast(DeviceID, Points.Num());

    UE_LOG(LogPointCloudManager, Verbose,
        TEXT("Point cloud updated: Device=%d, Source=%s, Points=%d"),
        DeviceID, SourceLabel, Points.Num());
}

int32 UPointCloudManager::CleanupStalePointClouds()
{
    if (Config.CloudStaleTimeoutSeconds <= 0.0f || PointCloudCache.Num() <= 0)
    {
        return 0;
    }

    const float CurrentTime = static_cast<float>(FPlatformTime::Seconds());
    TArray<int32> ExpiredDeviceIDs;

    for (const auto& Pair : PointCloudCache)
    {
        if (CurrentTime - Pair.Value.LastUpdateTime > Config.CloudStaleTimeoutSeconds)
        {
            ExpiredDeviceIDs.Add(Pair.Key);
        }
    }

    for (const int32 DeviceID : ExpiredDeviceIDs)
    {
        PointCloudCache.Remove(DeviceID);
        OnPointCloudCleared.Broadcast(DeviceID);
    }

    if (ExpiredDeviceIDs.Num() > 0)
    {
        bRendererDirty = true;
        UE_LOG(LogPointCloudManager, Verbose,
            TEXT("Removed %d stale point cloud(s) older than %.1fs"),
            ExpiredDeviceIDs.Num(),
            Config.CloudStaleTimeoutSeconds);
    }

    return ExpiredDeviceIDs.Num();
}

FBox UPointCloudManager::ComputePointCloudBounds(const TArray<FPointCloudRuntimePoint>& Points) const
{
    FBox Bounds(EForceInit::ForceInit);
    for (const FPointCloudRuntimePoint& Point : Points)
    {
        Bounds += Point.Location;
    }

    return Bounds;
}

void UPointCloudManager::DrawDebugPointClouds() const
{
    if (!World)
    {
        return;
    }

    const int32 MaxDebugPoints = FMath::Max(1, Config.MaxDebugDrawPointsPerCloud);

    for (const auto& Pair : PointCloudCache)
    {
        const FDevicePointCloud& PointCloud = Pair.Value;
        if (PointCloud.Points.Num() <= 0)
        {
            continue;
        }

        const int32 DrawCount = FMath::Min(PointCloud.Points.Num(), MaxDebugPoints);
        const FBox Bounds = ComputePointCloudBounds(PointCloud.Points);
        const FColor AccentColor = PointCloud.Points[0].Color.ToFColor(true);

        for (int32 Index = 0; Index < DrawCount; ++Index)
        {
            const FPointCloudRuntimePoint& Point = PointCloud.Points[Index];

            DrawDebugPoint(
                World,
                Point.Location,
                Config.DebugDrawPointSize,
                Point.Color.ToFColor(true),
                false,
                0.0f,
                0);
        }

        if (Config.bDrawDebugBounds && Bounds.IsValid)
        {
            DrawDebugBox(
                World,
                Bounds.GetCenter(),
                Bounds.GetExtent(),
                AccentColor,
                false,
                0.0f,
                0,
                1.5f);
        }

        if (Config.bDrawDebugLabels && Bounds.IsValid)
        {
            const float CloudAge = FMath::Max(0.0f, static_cast<float>(FPlatformTime::Seconds()) - PointCloud.LastUpdateTime);
            const FString Label = FString::Printf(
                TEXT("Cloud %d | %d pts | %.1fs"),
                PointCloud.DeviceID,
                PointCloud.Points.Num(),
                CloudAge);

            DrawDebugString(
                World,
                Bounds.GetCenter() + FVector(0.0f, 0.0f, Bounds.GetExtent().Z + Config.DebugLabelHeightOffsetCm),
                Label,
                nullptr,
                AccentColor,
                0.0f,
                false);
        }
    }
}

FLinearColor UPointCloudManager::MakeColorFromIntensity(float Intensity) const
{
    const float Alpha = FMath::Clamp(Intensity, 0.0f, 1.0f);
    return FLinearColor::LerpUsingHSV(
        FLinearColor(0.0f, 0.4f, 1.0f, 1.0f),
        FLinearColor(1.0f, 0.15f, 0.1f, 1.0f),
        Alpha);
}
