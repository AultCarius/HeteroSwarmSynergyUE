#include "HeteroSwarmGameInstance.h"
#include "UDPManager.h"
#include "HeartbeatManager.h"
#include "DeviceStateManager.h"
#include "DeviceFirstPersonCamera.h"
#include "EventMarkerManager.h"
#include "TrajectoryManager.h"
#include "PointCloudManager.h"
#include "VirtualLidarTestActor.h"
#include "CoordinateConverter.h"
#include "HeteroSwarmAgentBase.h"
#include "EventMarkerBase.h"
#include "TrajectoryVisualizerActor.h"

#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeteroSwarmGameInstance, Log, All);

namespace
{
    struct FTrajectoryVerificationConfigData
    {
        bool bEnabled = true;
        bool bEditorOnly = true;
        int32 DeviceID = 9101;
        float RefreshSeconds = 2.0f;
        TArray<FVector> PointsWorldCm;
        FString StatusMessage;
    };

    FTrajectoryVerificationConfigData GTrajectoryVerificationConfig;
    bool GTrajectoryVerificationConfigLoaded = false;

    TArray<FVector> BuildDefaultTrajectoryOffsetsCm()
    {
        return {
            FVector(-550.0f, -260.0f, 0.0f),
            FVector(-320.0f, -120.0f, 70.0f),
            FVector(-80.0f, 0.0f, 160.0f),
            FVector(120.0f, 120.0f, 200.0f),
            FVector(320.0f, 100.0f, 120.0f),
            FVector(520.0f, -80.0f, 40.0f),
            FVector(280.0f, -260.0f, 20.0f),
            FVector(-40.0f, -320.0f, 90.0f)
        };
    }

    FString BuildDefaultTrajectoryVerificationJson()
    {
        return FString(
            TEXT("{\n")
            TEXT("  \"enabled\": true,\n")
            TEXT("  \"editor_only\": true,\n")
            TEXT("  \"device_id\": 9101,\n")
            TEXT("  \"refresh_seconds\": 2.0,\n")
            TEXT("  \"points_are_world_space\": false,\n")
            TEXT("  \"origin_cm\": [0.0, 0.0, 220.0],\n")
            TEXT("  \"points_cm\": [\n")
            TEXT("    [-550.0, -260.0, 0.0],\n")
            TEXT("    [-320.0, -120.0, 70.0],\n")
            TEXT("    [-80.0, 0.0, 160.0],\n")
            TEXT("    [120.0, 120.0, 200.0],\n")
            TEXT("    [320.0, 100.0, 120.0],\n")
            TEXT("    [520.0, -80.0, 40.0],\n")
            TEXT("    [280.0, -260.0, 20.0],\n")
            TEXT("    [-40.0, -320.0, 90.0]\n")
            TEXT("  ]\n")
            TEXT("}\n"));
    }

    FString GetTrajectoryVerificationConfigPath()
    {
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("TrajectoryVerification.json"));
    }

    bool TryReadVectorFromJsonValue(const TSharedPtr<FJsonValue>& JsonValue, FVector& OutVector)
    {
        if (!JsonValue.IsValid())
        {
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
        if (JsonValue->TryGetArray(ArrayValues) && ArrayValues != nullptr && ArrayValues->Num() >= 3)
        {
            double X = 0.0;
            double Y = 0.0;
            double Z = 0.0;
            if ((*ArrayValues)[0].IsValid() && (*ArrayValues)[1].IsValid() && (*ArrayValues)[2].IsValid() &&
                (*ArrayValues)[0]->TryGetNumber(X) &&
                (*ArrayValues)[1]->TryGetNumber(Y) &&
                (*ArrayValues)[2]->TryGetNumber(Z))
            {
                OutVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
                return true;
            }
        }

        const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
        if (JsonValue->TryGetObject(ObjectValue) && ObjectValue != nullptr && ObjectValue->IsValid())
        {
            double X = 0.0;
            double Y = 0.0;
            double Z = 0.0;
            if ((*ObjectValue)->TryGetNumberField(TEXT("x"), X) &&
                (*ObjectValue)->TryGetNumberField(TEXT("y"), Y) &&
                (*ObjectValue)->TryGetNumberField(TEXT("z"), Z))
            {
                OutVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
                return true;
            }
        }

        return false;
    }

    bool EnsureDefaultTrajectoryVerificationConfigFileExists(const FString& AbsoluteConfigPath)
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
            BuildDefaultTrajectoryVerificationJson(),
            *AbsoluteConfigPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    void ResetTrajectoryVerificationConfigFromGameConfig(
        const FGameInstanceConfig& GameConfig,
        FTrajectoryVerificationConfigData& OutConfig)
    {
        OutConfig = FTrajectoryVerificationConfigData{};
        OutConfig.bEnabled = GameConfig.bAutoInjectTrajectoryTestData;
        OutConfig.bEditorOnly = GameConfig.bAutoInjectTrajectoryOnlyInEditor;
        OutConfig.DeviceID = FMath::Max(1, GameConfig.AutoInjectTrajectoryDeviceID);
        OutConfig.RefreshSeconds = FMath::Clamp(GameConfig.AutoInjectTrajectoryRefreshSeconds, 0.2f, 30.0f);

        const TArray<FVector> DefaultOffsets = BuildDefaultTrajectoryOffsetsCm();
        OutConfig.PointsWorldCm.Reserve(DefaultOffsets.Num());
        for (const FVector& Offset : DefaultOffsets)
        {
            OutConfig.PointsWorldCm.Add(GameConfig.AutoInjectTrajectoryOrigin + Offset);
        }
    }

    bool LoadTrajectoryVerificationConfig(
        const FGameInstanceConfig& GameConfig,
        FTrajectoryVerificationConfigData& OutConfig)
    {
        ResetTrajectoryVerificationConfigFromGameConfig(GameConfig, OutConfig);

        const FString ConfigPath = GetTrajectoryVerificationConfigPath();
        if (!EnsureDefaultTrajectoryVerificationConfigFileExists(ConfigPath))
        {
            OutConfig.StatusMessage = FString::Printf(
                TEXT("Failed to create trajectory verification config: %s"),
                *ConfigPath);
            return false;
        }

        FString JsonText;
        if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath))
        {
            OutConfig.StatusMessage = FString::Printf(
                TEXT("Failed to load trajectory verification config: %s"),
                *ConfigPath);
            return false;
        }

        TSharedPtr<FJsonObject> RootObject;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
        {
            OutConfig.StatusMessage = FString::Printf(
                TEXT("Failed to parse trajectory verification config: %s"),
                *ConfigPath);
            return false;
        }

        bool BoolValue = false;
        double NumberValue = 0.0;
        FVector OriginCm = GameConfig.AutoInjectTrajectoryOrigin;
        bool bPointsAreWorldSpace = false;

        if (RootObject->TryGetBoolField(TEXT("enabled"), BoolValue))
        {
            OutConfig.bEnabled = BoolValue;
        }
        if (RootObject->TryGetBoolField(TEXT("editor_only"), BoolValue))
        {
            OutConfig.bEditorOnly = BoolValue;
        }
        if (RootObject->TryGetNumberField(TEXT("device_id"), NumberValue))
        {
            OutConfig.DeviceID = FMath::Max(1, FMath::RoundToInt(NumberValue));
        }
        if (RootObject->TryGetNumberField(TEXT("refresh_seconds"), NumberValue))
        {
            OutConfig.RefreshSeconds = FMath::Clamp(static_cast<float>(NumberValue), 0.2f, 30.0f);
        }
        if (RootObject->TryGetBoolField(TEXT("points_are_world_space"), BoolValue))
        {
            bPointsAreWorldSpace = BoolValue;
        }

        const TArray<TSharedPtr<FJsonValue>>* OriginArray = nullptr;
        if (RootObject->TryGetArrayField(TEXT("origin_cm"), OriginArray) &&
            OriginArray != nullptr &&
            OriginArray->Num() >= 3)
        {
            double X = 0.0;
            double Y = 0.0;
            double Z = 0.0;
            if ((*OriginArray)[0].IsValid() && (*OriginArray)[1].IsValid() && (*OriginArray)[2].IsValid() &&
                (*OriginArray)[0]->TryGetNumber(X) &&
                (*OriginArray)[1]->TryGetNumber(Y) &&
                (*OriginArray)[2]->TryGetNumber(Z))
            {
                OriginCm = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
            }
        }

        const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
        if (RootObject->TryGetArrayField(TEXT("points_cm"), PointsArray) &&
            PointsArray != nullptr &&
            PointsArray->Num() > 0)
        {
            OutConfig.PointsWorldCm.Reset();
            OutConfig.PointsWorldCm.Reserve(PointsArray->Num());

            for (const TSharedPtr<FJsonValue>& PointValue : *PointsArray)
            {
                FVector ParsedPoint;
                if (!TryReadVectorFromJsonValue(PointValue, ParsedPoint))
                {
                    continue;
                }

                OutConfig.PointsWorldCm.Add(bPointsAreWorldSpace ? ParsedPoint : (OriginCm + ParsedPoint));
            }
        }

        OutConfig.StatusMessage = FString::Printf(
            TEXT("Loaded trajectory verification config from %s"),
            *ConfigPath);

        return OutConfig.PointsWorldCm.Num() > 0;
    }
}

UHeteroSwarmGameInstance::UHeteroSwarmGameInstance()
    : HeartbeatManager(nullptr)
    , DeviceStateManager(nullptr)
    , EventMarkerManager(nullptr)
    , TrajectoryManager(nullptr)
    , PointCloudManager(nullptr)
    , AutoSpawnedVirtualLidarTestActor(nullptr)
    , bHasInjectedTrajectoryTestData(false)
    , LastAutoInjectedTrajectoryTimeSeconds(0.0)
    , bIsInitialized(false)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("HeteroSwarmGameInstance constructed"));
}

UHeteroSwarmGameInstance::~UHeteroSwarmGameInstance()
{
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("HeteroSwarmGameInstance destroyed"));
}

void UHeteroSwarmGameInstance::Init()
{
    Super::Init();

    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("HeteroSwarmGameInstance initializing."));

    if (UDPConfig.Receivers.Num() == 0)
    {
        UDPConfig.AddReceiver(10001, TEXT("MAVLink_Heartbeat_State_Trajectory"));
        UDPConfig.AddReceiver(10003, TEXT("JSON_Scene_Event_Control"));
        UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("Using default UDP configuration (10001/10003)"));
    }

    if (Config.bAutoInitialize)
    {
        if (InitializeUDPSystem())
        {
            UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("UDP System auto-initialized successfully"));

            // ===== 调试：模拟一个设备上线（新组件方案）=====
            HandleAgentOnlineForLifecycle(1, 1);

            if (AActor* TestAgentActor = GetSpawnedAgentActor(1))
            {
                TestAgentActor->SetActorLocationAndRotation(
                    FVector(0.0f, 0.0f, 300.0f),
                    FRotator::ZeroRotator
                );

                if (AHeteroSwarmAgentBase* AgentBase = Cast<AHeteroSwarmAgentBase>(TestAgentActor))
                {
                    TArray<UDeviceFirstPersonCamera*> Cameras;
                    AgentBase->GetComponents<UDeviceFirstPersonCamera>(Cameras);

                    if (Cameras.Num() > 0 && IsValid(Cameras[0]))
                    {
                        Cameras[0]->StartVideoStream();

                        UE_LOG(LogHeteroSwarmGameInstance, Log,
                            TEXT("Test device spawned and video stream started: DeviceID=1 Actor=%s Camera=%s"),
                            *AgentBase->GetName(),
                            *Cameras[0]->GetName());
                    }
                    else
                    {
                        UE_LOG(LogHeteroSwarmGameInstance, Warning,
                            TEXT("Test device spawned, but no DeviceFirstPersonCamera found on Actor=%s"),
                            *AgentBase->GetName());
                    }
                }
            }
            else
            {
                UE_LOG(LogHeteroSwarmGameInstance, Warning,
                    TEXT("Test device spawn failed: no spawned agent actor for DeviceID=1"));
            }
        }
        else
        {
            UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("UDP System auto-initialization failed"));
        }
    }
    else
    {
        UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("Auto-initialize disabled, call InitializeUDPSystem manually"));
    }

    if (!TickDelegateHandle.IsValid())
    {
        TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateUObject(this, &UHeteroSwarmGameInstance::HandleCoreTicker));
    }
}

void UHeteroSwarmGameInstance::Shutdown()
{
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("HeteroSwarmGameInstance shutting down."));

    if (TickDelegateHandle.IsValid())
    {
        FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
        TickDelegateHandle.Reset();
    }

    ShutdownUDPSystem();

    Super::Shutdown();

    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("HeteroSwarmGameInstance shutdown complete"));
}

bool UHeteroSwarmGameInstance::HandleCoreTicker(float DeltaTime)
{
    Tick(DeltaTime);
    return true;
}

void UHeteroSwarmGameInstance::Tick(float DeltaTime)
{
    if (!bIsInitialized)
    {
        return;
    }

    if (HeartbeatManager)
    {
        HeartbeatManager->Tick(DeltaTime);
    }

    if (DeviceStateManager)
    {
        DeviceStateManager->Tick(DeltaTime);
    }

    if (EventMarkerManager)
    {
        EventMarkerManager->Tick(DeltaTime);
    }

    if (TrajectoryManager)
    {
        TrajectoryManager->Tick(DeltaTime);
    }

    if (PointCloudManager)
    {
        PointCloudManager->Tick(DeltaTime);
    }

    EnsureAutoInjectedTrajectoryTestData();
}

bool UHeteroSwarmGameInstance::InitializeUDPSystem()
{
    if (bIsInitialized)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning, TEXT("UDP System already initialized"));
        return false;
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("Initializing UDP System."));

    if (UDPConfig.GetEnabledReceiverCount() == 0)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("No enabled receivers in UDP configuration"));
        return false;
    }

    UUDPManager* UDPManager = GetSubsystem<UUDPManager>();
    if (!UDPManager)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("Failed to get UUDPManager subsystem"));
        return false;
    }

    if (!UDPManager->InitializeUDPWithConfig(UDPConfig, Config.BufferPoolSize))
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("Failed to initialize UDPManager"));
        return false;
    }

    UDPManager->SetMaxPacketsPerFrame(Config.MaxPacketsPerFrame);

    UDPManager->OnConfigurationChanged().AddUObject(
        this,
        &UHeteroSwarmGameInstance::OnUDPConfigurationChanged
    );
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("Bound to UDP configuration changed delegate"));

    if (!InitializeManagers())
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("Failed to initialize managers"));
        UDPManager->ShutdownUDP();
        return false;
    }

    bIsInitialized = true;

    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("UDP System initialized successfully"));
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  Active Receivers: %d"), UDPConfig.GetEnabledReceiverCount());
    for (const auto& Receiver : UDPConfig.Receivers)
    {
        if (Receiver.bEnabled)
        {
            UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("    - Port %d: %s"),
                Receiver.LocalPort, *Receiver.Description);
        }
    }
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  Buffer Pool Size: %d"), Config.BufferPoolSize);
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  Max Packets Per Frame: %d"), Config.MaxPacketsPerFrame);
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  Heartbeat Timeout: %.1fs"), Config.DeviceTimeoutSeconds);

    GTrajectoryVerificationConfigLoaded = LoadTrajectoryVerificationConfig(Config, GTrajectoryVerificationConfig);
    if (GTrajectoryVerificationConfig.StatusMessage.Len() > 0)
    {
        if (GTrajectoryVerificationConfigLoaded)
        {
            UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("%s"), *GTrajectoryVerificationConfig.StatusMessage);
        }
        else
        {
            UE_LOG(LogHeteroSwarmGameInstance, Warning, TEXT("%s"), *GTrajectoryVerificationConfig.StatusMessage);
        }
    }

    OnUDPSystemInitialized();

    EnsureAutoSpawnedVirtualLidarTestActor();
    EnsureAutoInjectedTrajectoryTestData();

    return true;
}

void UHeteroSwarmGameInstance::ShutdownUDPSystem()
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("Shutting down UDP System."));

    UUDPManager* UDPManager = GetSubsystem<UUDPManager>();

    if (UDPManager)
    {
        UDPManager->OnConfigurationChanged().RemoveAll(this);
        UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("Unbound from UDP configuration changed delegate"));
    }

    ShutdownManagers();

    if (UDPManager)
    {
        UDPManager->ShutdownUDP();
    }

    bIsInitialized = false;

    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("UDP System shutdown complete"));

    OnUDPSystemShutdown();
}

bool UHeteroSwarmGameInstance::RestartUDPSystem()
{
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("Restarting UDP System..."));

    ShutdownUDPSystem();
    FPlatformProcess::Sleep(0.1f);

    return InitializeUDPSystem();
}

bool UHeteroSwarmGameInstance::InitializeManagers()
{
    UUDPManager* UDPManager = GetSubsystem<UUDPManager>();
    if (!UDPManager)
    {
        return false;
    }

    HeartbeatManager = NewObject<UHeartbeatManager>(this);
    if (!HeartbeatManager)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("Failed to create HeartbeatManager"));
        return false;
    }

    if (!HeartbeatManager->Initialize(GetWorld()))
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("Failed to initialize HeartbeatManager"));
        return false;
    }

    HeartbeatManager->SetHeartbeatTimeout(Config.DeviceTimeoutSeconds);

    UDPManager->OnMAVLinkHeartbeat.AddUObject(
        HeartbeatManager, &UHeartbeatManager::HandleHeartbeat);

    HeartbeatManager->OnAgentOnline.AddDynamic(
        this, &UHeteroSwarmGameInstance::HandleAgentOnlineForLifecycle);

    HeartbeatManager->OnAgentOffline.AddDynamic(
        this, &UHeteroSwarmGameInstance::HandleAgentOfflineForLifecycle);

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HeartbeatManager initialized (timeout=%.1fs), bound to OnMAVLinkHeartbeat and lifecycle handlers"),
        Config.DeviceTimeoutSeconds);

    DeviceStateManager = NewObject<UDeviceStateManager>(this);
    if (!DeviceStateManager)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("Failed to create DeviceStateManager"));
        return false;
    }

    if (!DeviceStateManager->Initialize(UDPManager))
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error, TEXT("Failed to initialize DeviceStateManager"));
        return false;
    }

    UDPManager->OnMAVLinkDeviceState.AddUObject(
        DeviceStateManager, &UDeviceStateManager::HandleMAVLinkDeviceState);

    DeviceStateManager->OnDeviceStateChanged.AddDynamic(
        this, &UHeteroSwarmGameInstance::HandleDeviceStateChangedForActors);

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("DeviceStateManager initialized, bound to OnMAVLinkDeviceState and GameInstance actor sync"));

    EventMarkerManager = NewObject<UEventMarkerManager>(this);
    if (EventMarkerManager && EventMarkerManager->Initialize(UDPManager))
    {
        EventMarkerManager->OnEventCreated.AddDynamic(
            this, &UHeteroSwarmGameInstance::HandleEventCreatedForPresentation);

        EventMarkerManager->OnEventHighlighted.AddDynamic(
            this, &UHeteroSwarmGameInstance::HandleEventHighlightedForPresentation);

        EventMarkerManager->OnEventDisappeared.AddDynamic(
            this, &UHeteroSwarmGameInstance::HandleEventDisappearedForPresentation);

        EventMarkerManager->OnSceneControlReceived.AddDynamic(
            this, &UHeteroSwarmGameInstance::HandleSceneControlForPresentation);

        UE_LOG(LogHeteroSwarmGameInstance, Log,
            TEXT("EventMarkerManager initialized and bound to presentation handlers"));
    }

    TrajectoryManager = NewObject<UTrajectoryManager>(this);
    if (TrajectoryManager && TrajectoryManager->Initialize(UDPManager, GetWorld()))
    {
        UDPManager->OnMAVLinkWaypoints.AddUObject(
            TrajectoryManager, &UTrajectoryManager::HandleMAVLinkWaypoints);

        DeviceStateManager->OnDeviceStateChanged.AddDynamic(
            TrajectoryManager, &UTrajectoryManager::HandleDeviceStateForActualTrajectory);

        TrajectoryManager->OnTrajectoryCreated.AddDynamic(
            this, &UHeteroSwarmGameInstance::HandleTrajectoryCreatedForPresentation);

        TrajectoryManager->OnTrajectoryUpdated.AddDynamic(
            this, &UHeteroSwarmGameInstance::HandleTrajectoryUpdatedForPresentation);

        TrajectoryManager->OnTrajectoryCleared.AddDynamic(
            this, &UHeteroSwarmGameInstance::HandleTrajectoryClearedForPresentation);

        UE_LOG(LogHeteroSwarmGameInstance, Log,
            TEXT("TrajectoryManager initialized, bound to data and presentation delegates"));
    }

    PointCloudManager = NewObject<UPointCloudManager>(this);
    if (PointCloudManager && PointCloudManager->Initialize(UDPManager, GetWorld()))
    {
        UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("PointCloudManager initialized"));
    }

    return true;
}

void UHeteroSwarmGameInstance::ShutdownManagers()
{
    CleanupAutoSpawnedVirtualLidarTestActor();
    bHasInjectedTrajectoryTestData = false;
    LastAutoInjectedTrajectoryTimeSeconds = 0.0;

    UUDPManager* UDPManager = GetSubsystem<UUDPManager>();

    if (UDPManager)
    {
        if (HeartbeatManager)
        {
            UDPManager->OnMAVLinkHeartbeat.RemoveAll(HeartbeatManager);
        }
        if (DeviceStateManager)
        {
            UDPManager->OnMAVLinkDeviceState.RemoveAll(DeviceStateManager);
        }
        if (TrajectoryManager)
        {
            UDPManager->OnMAVLinkWaypoints.RemoveAll(TrajectoryManager);
        }
    }

    if (HeartbeatManager)
    {
        HeartbeatManager->OnAgentOnline.RemoveAll(this);
        HeartbeatManager->OnAgentOffline.RemoveAll(this);
    }

    if (DeviceStateManager)
    {
        DeviceStateManager->OnDeviceStateChanged.RemoveAll(this);
    }

    if (DeviceStateManager && TrajectoryManager)
    {
        DeviceStateManager->OnDeviceStateChanged.RemoveAll(TrajectoryManager);
    }

    if (TrajectoryManager)
    {
        TrajectoryManager->OnTrajectoryCreated.RemoveAll(this);
        TrajectoryManager->OnTrajectoryUpdated.RemoveAll(this);
        TrajectoryManager->OnTrajectoryCleared.RemoveAll(this);
    }

    if (EventMarkerManager)
    {
        EventMarkerManager->OnEventCreated.RemoveAll(this);
        EventMarkerManager->OnEventHighlighted.RemoveAll(this);
        EventMarkerManager->OnEventDisappeared.RemoveAll(this);
        EventMarkerManager->OnSceneControlReceived.RemoveAll(this);
    }

    DestroyAllSpawnedAgentActors();
    DestroyAllSpawnedEventActors();
    DestroyAllSpawnedTrajectoryVisualizers();

    if (PointCloudManager)
    {
        PointCloudManager->Shutdown();
        PointCloudManager = nullptr;
    }

    if (TrajectoryManager)
    {
        TrajectoryManager->Shutdown();
        TrajectoryManager = nullptr;
    }

    if (EventMarkerManager)
    {
        EventMarkerManager->Shutdown();
        EventMarkerManager = nullptr;
    }

    if (DeviceStateManager)
    {
        DeviceStateManager->Shutdown();
        DeviceStateManager = nullptr;
    }

    if (HeartbeatManager)
    {
        HeartbeatManager->Shutdown();
        HeartbeatManager = nullptr;
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("All managers shutdown"));
}

void UHeteroSwarmGameInstance::RebindMessageHandlers()
{
    UUDPManager* UDPManager = GetSubsystem<UUDPManager>();
    if (!UDPManager)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error,
            TEXT("RebindMessageHandlers: UDPManager is null"));
        return;
    }

    if (DeviceStateManager)
    {
        UDPManager->RegisterMessageHandler(
            static_cast<uint16>(EUDPMessageType::DeviceState),
            DeviceStateManager
        );
        UDPManager->RegisterMessageHandler(
            static_cast<uint16>(EUDPMessageType::DeviceStateBatch),
            DeviceStateManager
        );
        UE_LOG(LogHeteroSwarmGameInstance, Log,
            TEXT("Rebound DeviceStateManager handlers (0x0001/0x0011)"));
    }

    if (EventMarkerManager)
    {
        UDPManager->RegisterMessageHandler(
            static_cast<uint16>(EUDPMessageType::EventMarker),
            EventMarkerManager
        );
        UDPManager->RegisterMessageHandler(
            static_cast<uint16>(EUDPMessageType::EventMarkerBatch),
            EventMarkerManager
        );
        UDPManager->RegisterMessageHandler(
            static_cast<uint16>(EUDPMessageType::JSONEventControl),
            EventMarkerManager
        );
        UE_LOG(LogHeteroSwarmGameInstance, Log,
            TEXT("Rebound EventMarkerManager handlers (0x0002/0x0012/0x1001)"));
    }

    if (TrajectoryManager)
    {
        UDPManager->RegisterMessageHandler(
            static_cast<uint16>(EUDPMessageType::TrajectoryData),
            TrajectoryManager
        );
        UE_LOG(LogHeteroSwarmGameInstance, Log,
            TEXT("Rebound TrajectoryManager handlers (0x0003)"));
    }

    if (PointCloudManager)
    {
        UDPManager->RegisterMessageHandler(
            static_cast<uint16>(EUDPMessageType::PointCloudData),
            PointCloudManager
        );
        UE_LOG(LogHeteroSwarmGameInstance, Log,
            TEXT("Rebound PointCloudManager handlers (0x0004)"));
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("All message handlers rebound successfully"));
}

void UHeteroSwarmGameInstance::OnUDPConfigurationChanged()
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("UDP configuration changed, rebinding message handlers."));

    RebindMessageHandlers();

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Message handlers rebinding complete"));
}

AActor* UHeteroSwarmGameInstance::GetSpawnedAgentActor(int32 DeviceID) const
{
    if (AActor* const* Found = SpawnedAgentActorMap.Find(DeviceID))
    {
        return IsValid(*Found) ? *Found : nullptr;
    }

    return nullptr;
}

TArray<int32> UHeteroSwarmGameInstance::GetAllSpawnedAgentIDs() const
{
    TArray<int32> Keys;
    SpawnedAgentActorMap.GetKeys(Keys);
    return Keys;
}

void UHeteroSwarmGameInstance::HandleAgentOnlineForLifecycle(int32 SystemID, int32 DeviceType)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleAgentOnlineForLifecycle: SystemID=%d DeviceType=%d"),
        SystemID, DeviceType);

    if (DeviceStateManager)
    {
        DeviceStateManager->SetDeviceOnlineState(SystemID, true);
    }

    SpawnAgentActorForDevice(SystemID, DeviceType);
}

void UHeteroSwarmGameInstance::HandleAgentOfflineForLifecycle(int32 SystemID)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleAgentOfflineForLifecycle: SystemID=%d"),
        SystemID);

    DestroyAgentActorForDevice(SystemID);
    DestroyTrajectoryVisualizerForDevice(SystemID);

    if (DeviceStateManager)
    {
        DeviceStateManager->SetDeviceOnlineState(SystemID, false);
    }
}

void UHeteroSwarmGameInstance::HandleDeviceStateChangedForActors(int32 DeviceID, const FDeviceRuntimeState& State)
{
    AActor* FoundActor = GetSpawnedAgentActor(DeviceID);
    if (!FoundActor)
    {
        return;
    }

    AHeteroSwarmAgentBase* AgentBase = Cast<AHeteroSwarmAgentBase>(FoundActor);
    if (!AgentBase)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("HandleDeviceStateChangedForActors: Actor for DeviceID=%d is not AHeteroSwarmAgentBase (%s)"),
            DeviceID,
            *FoundActor->GetName());
        return;
    }

    AgentBase->ApplyRuntimeState(State);
}

void UHeteroSwarmGameInstance::HandleEventCreatedForPresentation(int32 EventID, uint8 EventType, FVector Location)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleEventCreatedForPresentation: EventID=%d EventType=%d Location=%s"),
        EventID, EventType, *Location.ToCompactString());

    SpawnEventActorForEvent(EventID, EventType, Location);

    if (EventMarkerManager)
    {
        FEventMarkerRuntimeState State;
        if (EventMarkerManager->GetEventState(EventID, State))
        {
            if (AEventMarkerBase** Found = SpawnedEventActorMap.Find(EventID))
            {
                if (IsValid(*Found))
                {
                    (*Found)->ApplyEventState(State);
                }
            }
        }
    }
}

void UHeteroSwarmGameInstance::HandleEventHighlightedForPresentation(int32 EventID, FVector Location)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleEventHighlightedForPresentation: EventID=%d Location=%s"),
        EventID, *Location.ToCompactString());

    AEventMarkerBase** Found = SpawnedEventActorMap.Find(EventID);
    if (!Found || !IsValid(*Found))
    {
        SpawnEventActorForEvent(EventID, 0, Location);
        Found = SpawnedEventActorMap.Find(EventID);
    }

    if (Found && IsValid(*Found))
    {
        (*Found)->SetActorLocation(Location);
        (*Found)->SetHighlighted(true);

        if (EventMarkerManager)
        {
            FEventMarkerRuntimeState State;
            if (EventMarkerManager->GetEventState(EventID, State))
            {
                (*Found)->ApplyEventState(State);
            }
        }
    }
}

void UHeteroSwarmGameInstance::HandleEventDisappearedForPresentation(int32 EventID, FVector Location)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleEventDisappearedForPresentation: EventID=%d Location=%s"),
        EventID, *Location.ToCompactString());

    AEventMarkerBase** Found = SpawnedEventActorMap.Find(EventID);
    if (!Found || !IsValid(*Found))
    {
        return;
    }

    (*Found)->SetActorLocation(Location);
    (*Found)->PlayDisappear();

    SpawnedEventActorMap.Remove(EventID);
}

void UHeteroSwarmGameInstance::HandleSceneControlForPresentation(int32 SceneID, const FString& Operation, bool bEmergencyStop)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleSceneControlForPresentation: SceneID=%d Operation=%s Emergency=%s"),
        SceneID,
        *Operation,
        bEmergencyStop ? TEXT("true") : TEXT("false"));

    OnSceneControlPresentationReceived(SceneID, Operation, bEmergencyStop);
}

TSubclassOf<AHeteroSwarmAgentBase> UHeteroSwarmGameInstance::FindActorClassByDeviceType(
    int32 DeviceType,
    FVector& OutSpawnOffset,
    FRotator& OutSpawnRotationOffset) const
{
    OutSpawnOffset = FVector::ZeroVector;
    OutSpawnRotationOffset = FRotator::ZeroRotator;

    for (const FDeviceActorClassConfig& Item : DeviceActorClassConfigs)
    {
        if (Item.DeviceType == DeviceType && *Item.ActorClass != nullptr)
        {
            OutSpawnOffset = Item.SpawnOffset;
            OutSpawnRotationOffset = Item.SpawnRotationOffset;
            return Item.ActorClass;
        }
    }

    return DefaultAgentActorClass;
}

bool UHeteroSwarmGameInstance::SpawnAgentActorForDevice(int32 DeviceID, int32 DeviceType)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("SpawnAgentActorForDevice failed: World is null (DeviceID=%d)"),
            DeviceID);
        return false;
    }

    if (AActor* ExistingActor = GetSpawnedAgentActor(DeviceID))
    {
        UE_LOG(LogHeteroSwarmGameInstance, Verbose,
            TEXT("SpawnAgentActorForDevice skipped: actor already exists for DeviceID=%d (%s)"),
            DeviceID, *ExistingActor->GetName());

        if (HeartbeatManager)
        {
            HeartbeatManager->SetAgentActor(DeviceID, ExistingActor);
        }

        if (AHeteroSwarmAgentBase* ExistingAgent = Cast<AHeteroSwarmAgentBase>(ExistingActor))
        {
            ExistingAgent->SetAgentOnline(true);

            if (DeviceStateManager)
            {
                FDeviceRuntimeState CachedState;
                if (DeviceStateManager->GetDeviceState(DeviceID, CachedState))
                {
                    ExistingAgent->ApplyRuntimeState(CachedState);
                }
            }
        }

        return true;
    }

    FVector SpawnOffset = FVector::ZeroVector;
    FRotator SpawnRotationOffset = FRotator::ZeroRotator;

    TSubclassOf<AHeteroSwarmAgentBase> ActorClass =
        FindActorClassByDeviceType(DeviceType, SpawnOffset, SpawnRotationOffset);

    if (*ActorClass == nullptr)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error,
            TEXT("SpawnAgentActorForDevice failed: no ActorClass configured for DeviceType=%d DeviceID=%d"),
            DeviceType, DeviceID);
        return false;
    }

    FVector SpawnLocation = FVector::ZeroVector;
    FRotator SpawnRotation = FRotator::ZeroRotator;

    if (DeviceStateManager)
    {
        FDeviceRuntimeState CachedState;
        if (DeviceStateManager->GetDeviceState(DeviceID, CachedState))
        {
            SpawnLocation = CachedState.Location;
            SpawnRotation = CachedState.Rotation;
        }
    }

    SpawnLocation += SpawnOffset;
    SpawnRotation += SpawnRotationOffset;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = nullptr;
    SpawnParams.Instigator = nullptr;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = FName(*FString::Printf(TEXT("Agent_%d"), DeviceID));

    AHeteroSwarmAgentBase* SpawnedAgent = World->SpawnActor<AHeteroSwarmAgentBase>(
        ActorClass,
        SpawnLocation,
        SpawnRotation,
        SpawnParams);

    if (!SpawnedAgent)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error,
            TEXT("SpawnAgentActorForDevice failed: SpawnActor returned null. DeviceID=%d DeviceType=%d"),
            DeviceID, DeviceType);
        return false;
    }

    SpawnedAgent->InitializeAgent(DeviceID, DeviceType);
    SpawnedAgent->SetAgentOnline(true);

    if (DeviceStateManager)
    {
        FDeviceRuntimeState CachedState;
        if (DeviceStateManager->GetDeviceState(DeviceID, CachedState))
        {
            SpawnedAgent->ApplyRuntimeState(CachedState);
        }
    }

    SpawnedAgentActorMap.Add(DeviceID, SpawnedAgent);

    if (HeartbeatManager)
    {
        HeartbeatManager->SetAgentActor(DeviceID, SpawnedAgent);
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Spawned agent actor: DeviceID=%d DeviceType=%d Actor=%s Loc=%s Rot=%s"),
        DeviceID,
        DeviceType,
        *SpawnedAgent->GetName(),
        *SpawnLocation.ToCompactString(),
        *SpawnRotation.ToCompactString());

    return true;
}

bool UHeteroSwarmGameInstance::DestroyAgentActorForDevice(int32 DeviceID)
{
    AActor* ActorToDestroy = GetSpawnedAgentActor(DeviceID);

    if (!ActorToDestroy)
    {
        SpawnedAgentActorMap.Remove(DeviceID);

        if (HeartbeatManager)
        {
            HeartbeatManager->SetAgentActor(DeviceID, nullptr);
        }

        UE_LOG(LogHeteroSwarmGameInstance, Verbose,
            TEXT("DestroyAgentActorForDevice: no valid actor found for DeviceID=%d"),
            DeviceID);

        return false;
    }

    if (AHeteroSwarmAgentBase* AgentBase = Cast<AHeteroSwarmAgentBase>(ActorToDestroy))
    {
        AgentBase->SetAgentOnline(false);
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Destroying agent actor: DeviceID=%d Actor=%s"),
        DeviceID, *ActorToDestroy->GetName());

    ActorToDestroy->Destroy();

    SpawnedAgentActorMap.Remove(DeviceID);

    if (HeartbeatManager)
    {
        HeartbeatManager->SetAgentActor(DeviceID, nullptr);
    }

    return true;
}

void UHeteroSwarmGameInstance::DestroyAllSpawnedAgentActors()
{
    TArray<int32> DeviceIDs;
    SpawnedAgentActorMap.GetKeys(DeviceIDs);

    for (int32 DeviceID : DeviceIDs)
    {
        DestroyAgentActorForDevice(DeviceID);
    }

    SpawnedAgentActorMap.Empty();

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("DestroyAllSpawnedAgentActors complete"));
}

bool UHeteroSwarmGameInstance::SpawnEventActorForEvent(int32 EventID, uint8 EventType, const FVector& Location)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("SpawnEventActorForEvent failed: World is null (EventID=%d)"),
            EventID);
        return false;
    }

    if (AEventMarkerBase** Existing = SpawnedEventActorMap.Find(EventID))
    {
        if (IsValid(*Existing))
        {
            (*Existing)->SetActorLocation(Location);
            return true;
        }
    }

    if (*DefaultEventMarkerActorClass == nullptr)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error,
            TEXT("SpawnEventActorForEvent failed: DefaultEventMarkerActorClass is null (EventID=%d)"),
            EventID);
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = FName(*FString::Printf(TEXT("EventMarker_%d"), EventID));

    AEventMarkerBase* SpawnedEvent = World->SpawnActor<AEventMarkerBase>(
        DefaultEventMarkerActorClass,
        Location,
        FRotator::ZeroRotator,
        SpawnParams);

    if (!SpawnedEvent)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error,
            TEXT("SpawnEventActorForEvent failed: SpawnActor returned null (EventID=%d)"),
            EventID);
        return false;
    }

    SpawnedEvent->InitializeEventMarker(EventID, EventType);
    SpawnedEventActorMap.Add(EventID, SpawnedEvent);

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Spawned event actor: EventID=%d EventType=%d Actor=%s Loc=%s"),
        EventID, EventType, *SpawnedEvent->GetName(), *Location.ToCompactString());

    return true;
}

bool UHeteroSwarmGameInstance::DestroyEventActorForEvent(int32 EventID)
{
    AEventMarkerBase** Found = SpawnedEventActorMap.Find(EventID);
    if (!Found || !IsValid(*Found))
    {
        SpawnedEventActorMap.Remove(EventID);
        return false;
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Destroying event actor: EventID=%d Actor=%s"),
        EventID, *(*Found)->GetName());

    (*Found)->Destroy();
    SpawnedEventActorMap.Remove(EventID);
    return true;
}

void UHeteroSwarmGameInstance::DestroyAllSpawnedEventActors()
{
    TArray<int32> EventIDs;
    SpawnedEventActorMap.GetKeys(EventIDs);

    for (int32 EventID : EventIDs)
    {
        DestroyEventActorForEvent(EventID);
    }

    SpawnedEventActorMap.Empty();

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("DestroyAllSpawnedEventActors complete"));
}

void UHeteroSwarmGameInstance::SetConfig(const FGameInstanceConfig& NewConfig)
{
    Config = NewConfig;
}

bool UHeteroSwarmGameInstance::ApplyConfig(const FGameInstanceConfig& NewConfig)
{
    Config = NewConfig;
    return RestartUDPSystem();
}

void UHeteroSwarmGameInstance::PrintSystemStatus() const
{
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("=========== HeteroSwarm System Status ==========="));
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  UDP Initialized: %s"), bIsInitialized ? TEXT("YES") : TEXT("NO"));
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  Spawned Agents : %d"), SpawnedAgentActorMap.Num());
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  Spawned Events : %d"), SpawnedEventActorMap.Num());
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  Spawned Traj   : %d"), SpawnedTrajectoryVisualizerMap.Num());
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  HeartbeatManager:   %s"), HeartbeatManager ? TEXT("Active") : TEXT("Inactive"));
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  DeviceStateManager: %s"), DeviceStateManager ? TEXT("Active") : TEXT("Inactive"));
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  EventMarkerManager: %s"), EventMarkerManager ? TEXT("Active") : TEXT("Inactive"));
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  TrajectoryManager:  %s"), TrajectoryManager ? TEXT("Active") : TEXT("Inactive"));
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("  PointCloudManager:  %s"), PointCloudManager ? TEXT("Active") : TEXT("Inactive"));
    UE_LOG(LogHeteroSwarmGameInstance, Log, TEXT("==============================================="));
}

void UHeteroSwarmGameInstance::PrintAllStatistics() const
{
    if (!bIsInitialized)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning, TEXT("System not initialized"));
        return;
    }

    UUDPManager* UDPManager = GetSubsystem<UUDPManager>();
    if (UDPManager)
    {
        UDPManager->PrintStatistics();
    }

    if (DeviceStateManager)
    {
        DeviceStateManager->PrintStatistics();
    }

    if (EventMarkerManager)
    {
        EventMarkerManager->PrintStatistics();
    }

    if (PointCloudManager)
    {
        PointCloudManager->PrintStatistics();
    }
}

UUDPManager* UHeteroSwarmGameInstance::GetUDPManager() const
{
    return GetSubsystem<UUDPManager>();
}

void UHeteroSwarmGameInstance::EnsureAutoSpawnedVirtualLidarTestActor()
{
    if (!Config.bEnableVerificationHelpers || !Config.bAutoSpawnVirtualLidarTestActor)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

#if WITH_EDITOR
    if (Config.bAutoSpawnVirtualLidarOnlyInEditor)
    {
        const bool bIsEditorWorld =
            World->WorldType == EWorldType::PIE ||
            World->WorldType == EWorldType::EditorPreview ||
            World->WorldType == EWorldType::GamePreview;
        if (!bIsEditorWorld && !Config.bAutoSpawnVirtualLidarInStandaloneGame)
        {
            return;
        }
    }
#else
    if (Config.bAutoSpawnVirtualLidarOnlyInEditor && !Config.bAutoSpawnVirtualLidarInStandaloneGame)
    {
        return;
    }
#endif

    if (IsValid(AutoSpawnedVirtualLidarTestActor))
    {
        return;
    }

    if (AActor* ExistingActor = UGameplayStatics::GetActorOfClass(World, AVirtualLidarTestActor::StaticClass()))
    {
        AutoSpawnedVirtualLidarTestActor = Cast<AVirtualLidarTestActor>(ExistingActor);
        UE_LOG(LogHeteroSwarmGameInstance, Log,
            TEXT("Using existing VirtualLidarTestActor: %s"),
            *ExistingActor->GetName());
        return;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = TEXT("AutoVirtualLidarTestActor");
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AutoSpawnedVirtualLidarTestActor = World->SpawnActor<AVirtualLidarTestActor>(
        Config.AutoSpawnVirtualLidarLocation,
        FRotator::ZeroRotator,
        SpawnParameters);

    if (AutoSpawnedVirtualLidarTestActor)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Log,
            TEXT("Auto-spawned VirtualLidarTestActor at %s"),
            *Config.AutoSpawnVirtualLidarLocation.ToCompactString());
    }
    else
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("Failed to auto-spawn VirtualLidarTestActor"));
    }
}

void UHeteroSwarmGameInstance::CleanupAutoSpawnedVirtualLidarTestActor()
{
    if (!IsValid(AutoSpawnedVirtualLidarTestActor))
    {
        AutoSpawnedVirtualLidarTestActor = nullptr;
        return;
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Destroying auto-spawned VirtualLidarTestActor: %s"),
        *AutoSpawnedVirtualLidarTestActor->GetName());

    AutoSpawnedVirtualLidarTestActor->Destroy();
    AutoSpawnedVirtualLidarTestActor = nullptr;
}

void UHeteroSwarmGameInstance::EnsureAutoInjectedTrajectoryTestData()
{
    if (!Config.bEnableVerificationHelpers || !TrajectoryManager || !bIsInitialized)
    {
        return;
    }

    if (!GTrajectoryVerificationConfigLoaded ||
        !GTrajectoryVerificationConfig.bEnabled ||
        GTrajectoryVerificationConfig.PointsWorldCm.Num() <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

#if WITH_EDITOR
    if (GTrajectoryVerificationConfig.bEditorOnly)
    {
        const bool bIsEditorWorld =
            World->WorldType == EWorldType::PIE ||
            World->WorldType == EWorldType::EditorPreview ||
            World->WorldType == EWorldType::GamePreview;
        if (!bIsEditorWorld && !Config.bAutoInjectTrajectoryInStandaloneGame)
        {
            return;
        }
    }
#else
    if (GTrajectoryVerificationConfig.bEditorOnly && !Config.bAutoInjectTrajectoryInStandaloneGame)
    {
        return;
    }
#endif

    const double CurrentTimeSeconds = FPlatformTime::Seconds();
    const double RefreshIntervalSeconds = FMath::Max(
        0.2,
        static_cast<double>(GTrajectoryVerificationConfig.RefreshSeconds));
    if ((CurrentTimeSeconds - LastAutoInjectedTrajectoryTimeSeconds) < RefreshIntervalSeconds)
    {
        return;
    }

    LastAutoInjectedTrajectoryTimeSeconds = CurrentTimeSeconds;

    FMAVLinkWaypointsData TestTrajectory;
    TestTrajectory.SystemID = GTrajectoryVerificationConfig.DeviceID;
    TestTrajectory.ReceiveTime = static_cast<float>(CurrentTimeSeconds);

    TestTrajectory.Waypoints.Reserve(GTrajectoryVerificationConfig.PointsWorldCm.Num());

    for (const FVector& UEPoint : GTrajectoryVerificationConfig.PointsWorldCm)
    {
        const FNEDVector NEDPoint = UCoordinateConverter::UEToNED(UEPoint);
        TestTrajectory.Waypoints.Add(FVector(NEDPoint.North, NEDPoint.East, NEDPoint.Down));
    }

    if (UUDPManager* UDPManager = GetSubsystem<UUDPManager>())
    {
        UDPManager->OnMAVLinkWaypoints.Broadcast(TestTrajectory);

        if (bHasInjectedTrajectoryTestData)
        {
            UE_LOG(
                LogHeteroSwarmGameInstance,
                Verbose,
                TEXT("Refreshed mock MAVLink trajectory for verification: Device=%d, Waypoints=%d"),
                TestTrajectory.SystemID,
                TestTrajectory.Waypoints.Num());
        }
        else
        {
            UE_LOG(
                LogHeteroSwarmGameInstance,
                Log,
                TEXT("Injected mock MAVLink trajectory for verification: Device=%d, Waypoints=%d"),
                TestTrajectory.SystemID,
                TestTrajectory.Waypoints.Num());
        }

        bHasInjectedTrajectoryTestData = true;
    }

    // 第3步阶段先禁用复杂轨迹自动注入
}

void UHeteroSwarmGameInstance::HandleTrajectoryCreatedForPresentation(int32 DeviceID, ETrajectoryType TrajectoryType)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleTrajectoryCreatedForPresentation: DeviceID=%d Type=%d"),
        DeviceID, static_cast<int32>(TrajectoryType));

    if (SpawnTrajectoryVisualizerForDevice(DeviceID))
    {
        if (ATrajectoryVisualizerActor** Found = SpawnedTrajectoryVisualizerMap.Find(DeviceID))
        {
            if (IsValid(*Found))
            {
                (*Found)->RefreshTrajectory(TrajectoryType);
            }
        }
    }
}

void UHeteroSwarmGameInstance::HandleTrajectoryUpdatedForPresentation(int32 DeviceID, ETrajectoryType TrajectoryType, int32 AddedPoints)
{
    UE_LOG(LogHeteroSwarmGameInstance, Verbose,
        TEXT("HandleTrajectoryUpdatedForPresentation: DeviceID=%d Type=%d Added=%d"),
        DeviceID, static_cast<int32>(TrajectoryType), AddedPoints);

    if (SpawnTrajectoryVisualizerForDevice(DeviceID))
    {
        if (ATrajectoryVisualizerActor** Found = SpawnedTrajectoryVisualizerMap.Find(DeviceID))
        {
            if (IsValid(*Found))
            {
                (*Found)->RefreshTrajectory(TrajectoryType);
            }
        }
    }
}

void UHeteroSwarmGameInstance::HandleTrajectoryClearedForPresentation(int32 DeviceID, ETrajectoryType TrajectoryType)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleTrajectoryClearedForPresentation: DeviceID=%d Type=%d"),
        DeviceID, static_cast<int32>(TrajectoryType));

    ATrajectoryVisualizerActor** Found = SpawnedTrajectoryVisualizerMap.Find(DeviceID);
    if (!Found || !IsValid(*Found))
    {
        return;
    }

    (*Found)->ClearTrajectory(TrajectoryType);

    if (!(*Found)->HasAnyTrajectory())
    {
        DestroyTrajectoryVisualizerForDevice(DeviceID);
    }
}

bool UHeteroSwarmGameInstance::SpawnTrajectoryVisualizerForDevice(int32 DeviceID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("SpawnTrajectoryVisualizerForDevice failed: World is null (DeviceID=%d)"),
            DeviceID);
        return false;
    }

    if (!TrajectoryManager)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("SpawnTrajectoryVisualizerForDevice failed: TrajectoryManager is null (DeviceID=%d)"),
            DeviceID);
        return false;
    }

    if (ATrajectoryVisualizerActor** Existing = SpawnedTrajectoryVisualizerMap.Find(DeviceID))
    {
        if (IsValid(*Existing))
        {
            return true;
        }
    }

    if (*DefaultTrajectoryVisualizerActorClass == nullptr)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("DefaultTrajectoryVisualizerActorClass is null, skip trajectory visualizer spawn (DeviceID=%d)"),
            DeviceID);
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = FName(*FString::Printf(TEXT("TrajectoryVisualizer_%d"), DeviceID));

    ATrajectoryVisualizerActor* Visualizer = World->SpawnActor<ATrajectoryVisualizerActor>(
        DefaultTrajectoryVisualizerActorClass,
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParams);

    if (!Visualizer)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error,
            TEXT("SpawnTrajectoryVisualizerForDevice failed: SpawnActor returned null (DeviceID=%d)"),
            DeviceID);
        return false;
    }

    Visualizer->InitializeVisualizer(DeviceID, TrajectoryManager);
    SpawnedTrajectoryVisualizerMap.Add(DeviceID, Visualizer);

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Spawned trajectory visualizer: DeviceID=%d Actor=%s"),
        DeviceID, *Visualizer->GetName());

    return true;
}

bool UHeteroSwarmGameInstance::DestroyTrajectoryVisualizerForDevice(int32 DeviceID)
{
    ATrajectoryVisualizerActor** Found = SpawnedTrajectoryVisualizerMap.Find(DeviceID);
    if (!Found || !IsValid(*Found))
    {
        SpawnedTrajectoryVisualizerMap.Remove(DeviceID);
        return false;
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Destroying trajectory visualizer: DeviceID=%d Actor=%s"),
        DeviceID, *(*Found)->GetName());

    (*Found)->Destroy();
    SpawnedTrajectoryVisualizerMap.Remove(DeviceID);
    return true;
}

void UHeteroSwarmGameInstance::DestroyAllSpawnedTrajectoryVisualizers()
{
    TArray<int32> DeviceIDs;
    SpawnedTrajectoryVisualizerMap.GetKeys(DeviceIDs);

    for (int32 DeviceID : DeviceIDs)
    {
        DestroyTrajectoryVisualizerForDevice(DeviceID);
    }

    SpawnedTrajectoryVisualizerMap.Empty();

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("DestroyAllSpawnedTrajectoryVisualizers complete"));
}
