// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 传感器系统 - 虚拟激光雷达测试Actor

#include "VirtualLidarTestActor.h"

#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "HAL/FileManager.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "VirtualLidarComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogVirtualLidarTestActor, Log, All);

namespace
{
    bool TryReadVectorFromJsonValue(const TSharedPtr<FJsonValue>& JsonValue, FVector& OutVector)
    {
        if (!JsonValue.IsValid())
        {
            return false;
        }

        if (JsonValue->Type == EJson::Array)
        {
            const TArray<TSharedPtr<FJsonValue>>& Values = JsonValue->AsArray();
            if (Values.Num() < 3)
            {
                return false;
            }

            OutVector.X = static_cast<float>(Values[0]->AsNumber());
            OutVector.Y = static_cast<float>(Values[1]->AsNumber());
            OutVector.Z = static_cast<float>(Values[2]->AsNumber());
            return true;
        }

        if (JsonValue->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject> Object = JsonValue->AsObject();
            if (!Object.IsValid())
            {
                return false;
            }

            double X = 0.0;
            double Y = 0.0;
            double Z = 0.0;
            const bool bHasX = Object->TryGetNumberField(TEXT("x"), X) || Object->TryGetNumberField(TEXT("X"), X);
            const bool bHasY = Object->TryGetNumberField(TEXT("y"), Y) || Object->TryGetNumberField(TEXT("Y"), Y);
            const bool bHasZ = Object->TryGetNumberField(TEXT("z"), Z) || Object->TryGetNumberField(TEXT("Z"), Z);
            if (!bHasX || !bHasY || !bHasZ)
            {
                return false;
            }

            OutVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
            return true;
        }

        return false;
    }

    bool TryReadVectorField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FVector& OutVector)
    {
        if (!JsonObject.IsValid() || !JsonObject->HasField(FieldName))
        {
            return false;
        }

        return TryReadVectorFromJsonValue(JsonObject->TryGetField(FieldName), OutVector);
    }

    bool TryReadColorField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FLinearColor& OutColor)
    {
        FVector RGB = FVector::ZeroVector;
        if (!TryReadVectorField(JsonObject, FieldName, RGB))
        {
            if (!JsonObject.IsValid() || !JsonObject->HasField(FieldName))
            {
                return false;
            }

            const TSharedPtr<FJsonValue> JsonValue = JsonObject->TryGetField(FieldName);
            if (!JsonValue.IsValid() || JsonValue->Type != EJson::Array)
            {
                return false;
            }

            const TArray<TSharedPtr<FJsonValue>>& Values = JsonValue->AsArray();
            if (Values.Num() < 4)
            {
                return false;
            }

            OutColor = FLinearColor(
                static_cast<float>(Values[0]->AsNumber()),
                static_cast<float>(Values[1]->AsNumber()),
                static_cast<float>(Values[2]->AsNumber()),
                static_cast<float>(Values[3]->AsNumber()));
            return true;
        }

        OutColor.R = RGB.X;
        OutColor.G = RGB.Y;
        OutColor.B = RGB.Z;
        OutColor.A = 1.0f;
        return true;
    }

    FString BuildDefaultVirtualLidarSimulationJson()
    {
        return
            TEXT("{\n")
            TEXT("  \"version\": 1,\n")
            TEXT("  \"description\": \"Virtual lidar simulation and path configuration for HeteroSwarmSynergyUE\",\n")
            TEXT("  \"lidar\": {\n")
            TEXT("    \"auto_start\": true,\n")
            TEXT("    \"sensor_enabled\": true,\n")
            TEXT("    \"remote_host\": \"127.0.0.1\",\n")
            TEXT("    \"remote_port\": 15000,\n")
            TEXT("    \"frame_name\": \"map\",\n")
            TEXT("    \"scan_frequency_hz\": 8.0,\n")
            TEXT("    \"horizontal_sample_count\": 48,\n")
            TEXT("    \"vertical_sample_count\": 8,\n")
            TEXT("    \"horizontal_fov_degrees\": 360.0,\n")
            TEXT("    \"vertical_fov_degrees\": 30.0,\n")
            TEXT("    \"vertical_center_degrees\": -5.0,\n")
            TEXT("    \"min_range_meters\": 0.2,\n")
            TEXT("    \"max_range_meters\": 25.0,\n")
            TEXT("    \"max_points_per_frame\": 512,\n")
            TEXT("    \"replace_existing_point_cloud\": true,\n")
            TEXT("    \"default_point_size_cm\": 18.0,\n")
            TEXT("    \"max_packet_payload_bytes\": 1200,\n")
            TEXT("    \"point_color_rgba\": [1.0, 0.15, 0.05, 1.0],\n")
            TEXT("    \"draw_debug_rays\": false,\n")
            TEXT("    \"debug_draw_duration\": 0.05\n")
            TEXT("  },\n")
            TEXT("  \"path\": {\n")
            TEXT("    \"enabled\": true,\n")
            TEXT("    \"loop\": true,\n")
            TEXT("    \"rotate_to_path_direction\": true,\n")
            TEXT("    \"draw_debug_path\": true,\n")
            TEXT("    \"start_at_first_point\": true,\n")
            TEXT("    \"points_are_world_space\": false,\n")
            TEXT("    \"origin_cm\": [0.0, 0.0, 150.0],\n")
            TEXT("    \"move_speed_cm_per_sec\": 220.0,\n")
            TEXT("    \"reach_threshold_cm\": 18.0,\n")
            TEXT("    \"visualization\": {\n")
            TEXT("      \"line_color_rgba\": [0.24, 0.86, 1.0, 1.0],\n")
            TEXT("      \"active_waypoint_color_rgba\": [1.0, 0.9, 0.18, 1.0],\n")
            TEXT("      \"label_color_rgba\": [0.92, 0.98, 1.0, 1.0],\n")
            TEXT("      \"sensor_marker_color_rgba\": [0.08, 0.1, 0.12, 1.0],\n")
            TEXT("      \"line_thickness\": 2.5,\n")
            TEXT("      \"waypoint_radius_cm\": 12.0,\n")
            TEXT("      \"active_waypoint_radius_cm\": 20.0,\n")
            TEXT("      \"direction_arrow_size_cm\": 70.0,\n")
            TEXT("      \"sensor_marker_radius_cm\": 22.0,\n")
            TEXT("      \"label_height_cm\": 32.0,\n")
            TEXT("      \"draw_waypoint_labels\": true,\n")
            TEXT("      \"draw_direction_arrows\": true,\n")
            TEXT("      \"draw_sensor_marker\": true\n")
            TEXT("    },\n")
            TEXT("    \"points_cm\": [\n")
            TEXT("      [0.0, 0.0, 0.0],\n")
            TEXT("      [600.0, 0.0, 60.0],\n")
            TEXT("      [760.0, 420.0, 80.0],\n")
            TEXT("      [300.0, 760.0, 40.0],\n")
            TEXT("      [-260.0, 620.0, 20.0],\n")
            TEXT("      [-520.0, 160.0, 60.0],\n")
            TEXT("      [-120.0, -260.0, 30.0]\n")
            TEXT("    ]\n")
            TEXT("  }\n")
            TEXT("}\n");
    }
}

AVirtualLidarTestActor::AVirtualLidarTestActor()
    : bPathEnabled(true)
    , bLoopPath(true)
    , bRotateToPathDirection(true)
    , bDrawDebugPath(true)
    , bStartAtFirstPathPoint(true)
    , bPathPointsAreWorldSpace(false)
    , PathMoveSpeedCmPerSecond(220.0f)
    , PathReachThresholdCm(18.0f)
    , bDrawPathLabels(true)
    , bDrawPathDirectionArrows(true)
    , bDrawSensorMarker(true)
    , PathLineThickness(2.5f)
    , PathWaypointRadiusCm(12.0f)
    , PathActiveWaypointRadiusCm(20.0f)
    , PathDirectionArrowSizeCm(70.0f)
    , PathSensorMarkerRadiusCm(22.0f)
    , PathLabelHeightCm(32.0f)
    , PathLineColor(FLinearColor(0.24f, 0.86f, 1.0f, 1.0f))
    , PathActiveWaypointColor(FLinearColor(1.0f, 0.9f, 0.18f, 1.0f))
    , PathLabelColor(FLinearColor(0.92f, 0.98f, 1.0f, 1.0f))
    , PathSensorMarkerColor(FLinearColor(0.08f, 0.1f, 0.12f, 1.0f))
    , CurrentTargetWaypointIndex(INDEX_NONE)
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    VirtualLidarComponent = CreateDefaultSubobject<UVirtualLidarComponent>(TEXT("VirtualLidarComponent"));
    VirtualLidarComponent->SetupAttachment(SceneRoot);

    VirtualLidarComponent->bAutoStart = false;
    ApplyDefaultFallbackSettings();
}

void AVirtualLidarTestActor::BeginPlay()
{
    Super::BeginPlay();
    ReloadConfigFromJson();
}

void AVirtualLidarTestActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    UpdatePathMovement(DeltaSeconds);
    DrawConfiguredPath();
}

bool AVirtualLidarTestActor::ReloadConfigFromJson()
{
    if (!VirtualLidarComponent)
    {
        LastConfigStatus = TEXT("Virtual lidar component is missing");
        return false;
    }

    VirtualLidarComponent->StopLidar();
    ApplyDefaultFallbackSettings();

    const FString AbsoluteConfigPath = GetResolvedConfigPath();
    if (!EnsureDefaultConfigFileExists(AbsoluteConfigPath))
    {
        LastConfigStatus = FString::Printf(TEXT("Failed to ensure config file: %s"), *AbsoluteConfigPath);
        return false;
    }

    bLoadedConfigFromJson = LoadConfigFromJsonFile(AbsoluteConfigPath);
    if (!bLoadedConfigFromJson)
    {
        LastConfigStatus = FString::Printf(TEXT("Failed to load config, fallback defaults are used: %s"), *AbsoluteConfigPath);
        ApplyConfiguredPathStart();

        if (VirtualLidarComponent->bAutoStart && VirtualLidarComponent->bSensorEnabled)
        {
            VirtualLidarComponent->StartLidar();
        }

        UE_LOG(LogVirtualLidarTestActor, Warning, TEXT("%s"), *LastConfigStatus);
        return false;
    }

    LastConfigStatus = FString::Printf(TEXT("Loaded virtual lidar config from %s"), *AbsoluteConfigPath);
    ApplyConfiguredPathStart();

    if (VirtualLidarComponent->bAutoStart && VirtualLidarComponent->bSensorEnabled)
    {
        VirtualLidarComponent->StartLidar();
    }

    UE_LOG(LogVirtualLidarTestActor, Log, TEXT("%s"), *LastConfigStatus);
    return true;
}

FString AVirtualLidarTestActor::GetResolvedConfigPath() const
{
    const FString TrimmedPath = JsonConfigRelativePath.TrimStartAndEnd();
    if (TrimmedPath.IsEmpty())
    {
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("VirtualLidarSimulation.json"));
    }

    if (FPaths::IsRelative(TrimmedPath))
    {
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TrimmedPath);
    }

    return FPaths::ConvertRelativePathToFull(TrimmedPath);
}

void AVirtualLidarTestActor::ApplyDefaultFallbackSettings()
{
    bLoadedConfigFromJson = false;
    CurrentTargetWaypointIndex = INDEX_NONE;
    ConfiguredPathPointsWorld.Reset();

    bPathEnabled = true;
    bLoopPath = true;
    bRotateToPathDirection = true;
    bDrawDebugPath = true;
    bStartAtFirstPathPoint = true;
    bPathPointsAreWorldSpace = false;
    PathMoveSpeedCmPerSecond = 220.0f;
    PathReachThresholdCm = 18.0f;
    bDrawPathLabels = true;
    bDrawPathDirectionArrows = true;
    bDrawSensorMarker = true;
    PathLineThickness = 2.5f;
    PathWaypointRadiusCm = 12.0f;
    PathActiveWaypointRadiusCm = 20.0f;
    PathDirectionArrowSizeCm = 70.0f;
    PathSensorMarkerRadiusCm = 22.0f;
    PathLabelHeightCm = 32.0f;
    PathLineColor = FLinearColor(0.24f, 0.86f, 1.0f, 1.0f);
    PathActiveWaypointColor = FLinearColor(1.0f, 0.9f, 0.18f, 1.0f);
    PathLabelColor = FLinearColor(0.92f, 0.98f, 1.0f, 1.0f);
    PathSensorMarkerColor = FLinearColor(0.08f, 0.1f, 0.12f, 1.0f);

    if (!VirtualLidarComponent)
    {
        return;
    }

    VirtualLidarComponent->bAutoStart = true;
    VirtualLidarComponent->bSensorEnabled = true;
    VirtualLidarComponent->RemoteHost = TEXT("127.0.0.1");
    VirtualLidarComponent->RemotePort = 15000;
    VirtualLidarComponent->FrameName = TEXT("map");
    VirtualLidarComponent->ScanFrequencyHz = 8.0f;
    VirtualLidarComponent->HorizontalSampleCount = 48;
    VirtualLidarComponent->VerticalSampleCount = 8;
    VirtualLidarComponent->HorizontalFovDegrees = 360.0f;
    VirtualLidarComponent->VerticalFovDegrees = 30.0f;
    VirtualLidarComponent->VerticalCenterDegrees = -5.0f;
    VirtualLidarComponent->MinRangeMeters = 0.2f;
    VirtualLidarComponent->MaxRangeMeters = 25.0f;
    VirtualLidarComponent->bIgnoreOwner = true;
    VirtualLidarComponent->MaxPointsPerFrame = 512;
    VirtualLidarComponent->bReplaceExistingPointCloud = true;
    VirtualLidarComponent->DefaultPointSizeCm = 18.0f;
    VirtualLidarComponent->MaxPacketPayloadBytes = 1200;
    VirtualLidarComponent->PointColor = FLinearColor(1.0f, 0.15f, 0.05f, 1.0f);
    VirtualLidarComponent->bDrawDebugRays = false;
    VirtualLidarComponent->DebugDrawDuration = 0.05f;
}

bool AVirtualLidarTestActor::EnsureDefaultConfigFileExists(const FString& AbsoluteConfigPath) const
{
    if (FPaths::FileExists(AbsoluteConfigPath))
    {
        return true;
    }

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteConfigPath), true);
    const FString DefaultJson = BuildDefaultVirtualLidarSimulationJson();
    const bool bSaved = FFileHelper::SaveStringToFile(
        DefaultJson,
        *AbsoluteConfigPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

    if (bSaved)
    {
        UE_LOG(LogVirtualLidarTestActor, Log,
            TEXT("Created default virtual lidar simulation config: %s"),
            *AbsoluteConfigPath);
    }

    return bSaved;
}

bool AVirtualLidarTestActor::LoadConfigFromJsonFile(const FString& AbsoluteConfigPath)
{
    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *AbsoluteConfigPath))
    {
        return false;
    }

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
    TSharedPtr<FJsonObject> RootObject;
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* LidarObjectPtr = nullptr;
    if (RootObject->TryGetObjectField(TEXT("lidar"), LidarObjectPtr) && LidarObjectPtr && LidarObjectPtr->IsValid() && VirtualLidarComponent)
    {
        const TSharedPtr<FJsonObject>& LidarObject = *LidarObjectPtr;

        bool BoolValue = false;
        double NumberValue = 0.0;
        FString StringValue;
        FLinearColor ColorValue;

        if (LidarObject->TryGetBoolField(TEXT("auto_start"), BoolValue))
        {
            VirtualLidarComponent->bAutoStart = BoolValue;
        }
        if (LidarObject->TryGetBoolField(TEXT("sensor_enabled"), BoolValue))
        {
            VirtualLidarComponent->bSensorEnabled = BoolValue;
        }
        if (LidarObject->TryGetStringField(TEXT("remote_host"), StringValue))
        {
            VirtualLidarComponent->RemoteHost = StringValue;
        }
        if (LidarObject->TryGetNumberField(TEXT("remote_port"), NumberValue))
        {
            VirtualLidarComponent->RemotePort = FMath::Clamp(FMath::RoundToInt(NumberValue), 1, 65535);
        }
        if (LidarObject->TryGetStringField(TEXT("frame_name"), StringValue))
        {
            VirtualLidarComponent->FrameName = StringValue;
        }
        if (LidarObject->TryGetNumberField(TEXT("scan_frequency_hz"), NumberValue))
        {
            VirtualLidarComponent->ScanFrequencyHz = FMath::Clamp(static_cast<float>(NumberValue), 1.0f, 60.0f);
        }
        if (LidarObject->TryGetNumberField(TEXT("horizontal_sample_count"), NumberValue))
        {
            VirtualLidarComponent->HorizontalSampleCount = FMath::Clamp(FMath::RoundToInt(NumberValue), 1, 256);
        }
        if (LidarObject->TryGetNumberField(TEXT("vertical_sample_count"), NumberValue))
        {
            VirtualLidarComponent->VerticalSampleCount = FMath::Clamp(FMath::RoundToInt(NumberValue), 1, 64);
        }
        if (LidarObject->TryGetNumberField(TEXT("horizontal_fov_degrees"), NumberValue))
        {
            VirtualLidarComponent->HorizontalFovDegrees = FMath::Clamp(static_cast<float>(NumberValue), 1.0f, 360.0f);
        }
        if (LidarObject->TryGetNumberField(TEXT("vertical_fov_degrees"), NumberValue))
        {
            VirtualLidarComponent->VerticalFovDegrees = FMath::Clamp(static_cast<float>(NumberValue), 1.0f, 120.0f);
        }
        if (LidarObject->TryGetNumberField(TEXT("vertical_center_degrees"), NumberValue))
        {
            VirtualLidarComponent->VerticalCenterDegrees = FMath::Clamp(static_cast<float>(NumberValue), -45.0f, 45.0f);
        }
        if (LidarObject->TryGetNumberField(TEXT("min_range_meters"), NumberValue))
        {
            VirtualLidarComponent->MinRangeMeters = FMath::Clamp(static_cast<float>(NumberValue), 0.01f, 5.0f);
        }
        if (LidarObject->TryGetNumberField(TEXT("max_range_meters"), NumberValue))
        {
            VirtualLidarComponent->MaxRangeMeters = FMath::Clamp(static_cast<float>(NumberValue), 0.1f, 500.0f);
        }
        if (LidarObject->TryGetNumberField(TEXT("max_points_per_frame"), NumberValue))
        {
            VirtualLidarComponent->MaxPointsPerFrame = FMath::Clamp(FMath::RoundToInt(NumberValue), 1, 4000);
        }
        if (LidarObject->TryGetBoolField(TEXT("replace_existing_point_cloud"), BoolValue))
        {
            VirtualLidarComponent->bReplaceExistingPointCloud = BoolValue;
        }
        if (LidarObject->TryGetNumberField(TEXT("default_point_size_cm"), NumberValue))
        {
            VirtualLidarComponent->DefaultPointSizeCm = FMath::Clamp(static_cast<float>(NumberValue), 0.1f, 50.0f);
        }
        if (LidarObject->TryGetNumberField(TEXT("max_packet_payload_bytes"), NumberValue))
        {
            VirtualLidarComponent->MaxPacketPayloadBytes = FMath::Clamp(FMath::RoundToInt(NumberValue), 256, 60000);
        }
        if (TryReadColorField(LidarObject, TEXT("point_color_rgba"), ColorValue))
        {
            VirtualLidarComponent->PointColor = ColorValue;
        }
        if (LidarObject->TryGetBoolField(TEXT("draw_debug_rays"), BoolValue))
        {
            VirtualLidarComponent->bDrawDebugRays = BoolValue;
        }
        if (LidarObject->TryGetNumberField(TEXT("debug_draw_duration"), NumberValue))
        {
            VirtualLidarComponent->DebugDrawDuration = FMath::Clamp(static_cast<float>(NumberValue), 0.0f, 2.0f);
        }
    }

    const TSharedPtr<FJsonObject>* PathObjectPtr = nullptr;
    if (RootObject->TryGetObjectField(TEXT("path"), PathObjectPtr) && PathObjectPtr && PathObjectPtr->IsValid())
    {
        const TSharedPtr<FJsonObject>& PathObject = *PathObjectPtr;

        bool BoolValue = false;
        double NumberValue = 0.0;
        FVector PathOrigin = GetActorLocation();
        PathObject->TryGetBoolField(TEXT("enabled"), bPathEnabled);
        if (PathObject->TryGetBoolField(TEXT("loop"), BoolValue))
        {
            bLoopPath = BoolValue;
        }
        if (PathObject->TryGetBoolField(TEXT("rotate_to_path_direction"), BoolValue))
        {
            bRotateToPathDirection = BoolValue;
        }
        if (PathObject->TryGetBoolField(TEXT("draw_debug_path"), BoolValue))
        {
            bDrawDebugPath = BoolValue;
        }
        if (PathObject->TryGetBoolField(TEXT("start_at_first_point"), BoolValue))
        {
            bStartAtFirstPathPoint = BoolValue;
        }
        if (PathObject->TryGetBoolField(TEXT("points_are_world_space"), BoolValue))
        {
            bPathPointsAreWorldSpace = BoolValue;
        }
        if (PathObject->TryGetNumberField(TEXT("move_speed_cm_per_sec"), NumberValue))
        {
            PathMoveSpeedCmPerSecond = FMath::Max(0.0f, static_cast<float>(NumberValue));
        }
        if (PathObject->TryGetNumberField(TEXT("reach_threshold_cm"), NumberValue))
        {
            PathReachThresholdCm = FMath::Clamp(static_cast<float>(NumberValue), 1.0f, 500.0f);
        }
        TryReadVectorField(PathObject, TEXT("origin_cm"), PathOrigin);

        const TSharedPtr<FJsonObject>* PathVisualizationObjectPtr = nullptr;
        if (PathObject->TryGetObjectField(TEXT("visualization"), PathVisualizationObjectPtr) &&
            PathVisualizationObjectPtr != nullptr &&
            PathVisualizationObjectPtr->IsValid())
        {
            const TSharedPtr<FJsonObject>& VisualizationObject = *PathVisualizationObjectPtr;
            FLinearColor ColorValue;

            if (TryReadColorField(VisualizationObject, TEXT("line_color_rgba"), ColorValue))
            {
                PathLineColor = ColorValue;
            }
            if (TryReadColorField(VisualizationObject, TEXT("active_waypoint_color_rgba"), ColorValue))
            {
                PathActiveWaypointColor = ColorValue;
            }
            if (TryReadColorField(VisualizationObject, TEXT("label_color_rgba"), ColorValue))
            {
                PathLabelColor = ColorValue;
            }
            if (TryReadColorField(VisualizationObject, TEXT("sensor_marker_color_rgba"), ColorValue))
            {
                PathSensorMarkerColor = ColorValue;
            }
            if (VisualizationObject->TryGetBoolField(TEXT("draw_waypoint_labels"), BoolValue))
            {
                bDrawPathLabels = BoolValue;
            }
            if (VisualizationObject->TryGetBoolField(TEXT("draw_direction_arrows"), BoolValue))
            {
                bDrawPathDirectionArrows = BoolValue;
            }
            if (VisualizationObject->TryGetBoolField(TEXT("draw_sensor_marker"), BoolValue))
            {
                bDrawSensorMarker = BoolValue;
            }
            if (VisualizationObject->TryGetNumberField(TEXT("line_thickness"), NumberValue))
            {
                PathLineThickness = FMath::Clamp(static_cast<float>(NumberValue), 0.5f, 20.0f);
            }
            if (VisualizationObject->TryGetNumberField(TEXT("waypoint_radius_cm"), NumberValue))
            {
                PathWaypointRadiusCm = FMath::Clamp(static_cast<float>(NumberValue), 4.0f, 100.0f);
            }
            if (VisualizationObject->TryGetNumberField(TEXT("active_waypoint_radius_cm"), NumberValue))
            {
                PathActiveWaypointRadiusCm = FMath::Clamp(static_cast<float>(NumberValue), 4.0f, 140.0f);
            }
            if (VisualizationObject->TryGetNumberField(TEXT("direction_arrow_size_cm"), NumberValue))
            {
                PathDirectionArrowSizeCm = FMath::Clamp(static_cast<float>(NumberValue), 10.0f, 200.0f);
            }
            if (VisualizationObject->TryGetNumberField(TEXT("sensor_marker_radius_cm"), NumberValue))
            {
                PathSensorMarkerRadiusCm = FMath::Clamp(static_cast<float>(NumberValue), 4.0f, 120.0f);
            }
            if (VisualizationObject->TryGetNumberField(TEXT("label_height_cm"), NumberValue))
            {
                PathLabelHeightCm = FMath::Clamp(static_cast<float>(NumberValue), 0.0f, 200.0f);
            }
        }

        const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
        if (PathObject->TryGetArrayField(TEXT("points_cm"), PointsArray) && PointsArray != nullptr)
        {
            ConfiguredPathPointsWorld.Reset();
            ConfiguredPathPointsWorld.Reserve(PointsArray->Num());

            for (const TSharedPtr<FJsonValue>& PointValue : *PointsArray)
            {
                FVector ParsedPoint = FVector::ZeroVector;
                if (!TryReadVectorFromJsonValue(PointValue, ParsedPoint))
                {
                    continue;
                }

                ConfiguredPathPointsWorld.Add(bPathPointsAreWorldSpace ? ParsedPoint : (PathOrigin + ParsedPoint));
            }
        }
    }

    return true;
}

void AVirtualLidarTestActor::ApplyConfiguredPathStart()
{
    CurrentTargetWaypointIndex = INDEX_NONE;

    if (!bPathEnabled || ConfiguredPathPointsWorld.Num() <= 0)
    {
        return;
    }

    if (bStartAtFirstPathPoint)
    {
        SetActorLocation(ConfiguredPathPointsWorld[0]);
        CurrentTargetWaypointIndex = GetNextWaypointIndex(0);
    }
    else
    {
        CurrentTargetWaypointIndex = 0;
    }

    if (bRotateToPathDirection && CurrentTargetWaypointIndex != INDEX_NONE)
    {
        const FVector Direction = (ConfiguredPathPointsWorld[CurrentTargetWaypointIndex] - GetActorLocation()).GetSafeNormal();
        if (!Direction.IsNearlyZero())
        {
            SetActorRotation(Direction.Rotation());
        }
    }
}

void AVirtualLidarTestActor::UpdatePathMovement(float DeltaSeconds)
{
    if (!bPathEnabled || ConfiguredPathPointsWorld.Num() <= 0 || CurrentTargetWaypointIndex == INDEX_NONE)
    {
        return;
    }

    const float SafeMoveSpeed = FMath::Max(0.0f, PathMoveSpeedCmPerSecond);
    if (SafeMoveSpeed <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const FVector CurrentLocation = GetActorLocation();
    const FVector TargetLocation = ConfiguredPathPointsWorld[CurrentTargetWaypointIndex];
    const FVector ToTarget = TargetLocation - CurrentLocation;
    const float DistanceToTarget = ToTarget.Size();
    const float StepDistance = SafeMoveSpeed * FMath::Max(0.0f, DeltaSeconds);

    if (DistanceToTarget <= PathReachThresholdCm || DistanceToTarget <= StepDistance)
    {
        SetActorLocation(TargetLocation);
        CurrentTargetWaypointIndex = GetNextWaypointIndex(CurrentTargetWaypointIndex);
    }
    else
    {
        SetActorLocation(CurrentLocation + ToTarget.GetSafeNormal() * StepDistance);
    }

    if (bRotateToPathDirection && CurrentTargetWaypointIndex != INDEX_NONE)
    {
        const FVector Direction = (ConfiguredPathPointsWorld[CurrentTargetWaypointIndex] - GetActorLocation()).GetSafeNormal();
        if (!Direction.IsNearlyZero())
        {
            SetActorRotation(Direction.Rotation());
        }
    }
}

void AVirtualLidarTestActor::DrawConfiguredPath() const
{
    if (!bDrawDebugPath || ConfiguredPathPointsWorld.Num() <= 0 || GetWorld() == nullptr)
    {
        return;
    }

    const FColor PathColor = PathLineColor.ToFColor(true);
    const FColor ActiveColor = PathActiveWaypointColor.ToFColor(true);
    const FColor LabelColor = PathLabelColor.ToFColor(true);
    const FColor SensorMarkerColor = PathSensorMarkerColor.ToFColor(true);

    if (bDrawSensorMarker)
    {
        DrawDebugSphere(
            GetWorld(),
            GetActorLocation(),
            PathSensorMarkerRadiusCm,
            16,
            SensorMarkerColor,
            false,
            0.0f,
            0,
            FMath::Max(1.0f, PathLineThickness * 0.7f));
    }

    for (int32 Index = 0; Index < ConfiguredPathPointsWorld.Num(); ++Index)
    {
        const FVector Point = ConfiguredPathPointsWorld[Index];
        const bool bIsActiveTarget = (Index == CurrentTargetWaypointIndex);

        DrawDebugSphere(
            GetWorld(),
            Point,
            bIsActiveTarget ? PathActiveWaypointRadiusCm : PathWaypointRadiusCm,
            12,
            bIsActiveTarget ? ActiveColor : PathColor,
            false,
            0.0f,
            0,
            FMath::Max(1.0f, PathLineThickness * 0.65f));

        if (bDrawPathLabels)
        {
            const FString WaypointLabel = FString::Printf(TEXT("WP %d"), Index + 1);
            DrawDebugString(
                GetWorld(),
                Point + FVector(0.0f, 0.0f, PathLabelHeightCm),
                WaypointLabel,
                nullptr,
                LabelColor,
                0.0f,
                false);
        }

        const int32 NextIndex = Index + 1;
        const FVector NextPoint =
            NextIndex < ConfiguredPathPointsWorld.Num()
                ? ConfiguredPathPointsWorld[NextIndex]
                : ConfiguredPathPointsWorld[0];

        if (NextIndex < ConfiguredPathPointsWorld.Num())
        {
            DrawDebugLine(
                GetWorld(),
                Point,
                NextPoint,
                PathColor,
                false,
                0.0f,
                0,
                PathLineThickness);
        }
        else if (bLoopPath && ConfiguredPathPointsWorld.Num() > 1)
        {
            DrawDebugLine(
                GetWorld(),
                Point,
                NextPoint,
                PathColor,
                false,
                0.0f,
                0,
                PathLineThickness);
        }

        if (bDrawPathDirectionArrows &&
            ((NextIndex < ConfiguredPathPointsWorld.Num()) || (bLoopPath && ConfiguredPathPointsWorld.Num() > 1)) &&
            FVector::DistSquared(Point, NextPoint) > KINDA_SMALL_NUMBER)
        {
            DrawDebugDirectionalArrow(
                GetWorld(),
                Point,
                NextPoint,
                PathDirectionArrowSizeCm,
                PathColor,
                false,
                0.0f,
                0,
                FMath::Max(1.0f, PathLineThickness * 0.8f));
        }
    }
}

int32 AVirtualLidarTestActor::GetNextWaypointIndex(int32 CurrentIndex) const
{
    if (ConfiguredPathPointsWorld.Num() <= 1)
    {
        return INDEX_NONE;
    }

    const int32 NextIndex = CurrentIndex + 1;
    if (ConfiguredPathPointsWorld.IsValidIndex(NextIndex))
    {
        return NextIndex;
    }

    return bLoopPath ? 0 : INDEX_NONE;
}
