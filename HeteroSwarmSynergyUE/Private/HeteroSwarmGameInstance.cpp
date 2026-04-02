// Copyright Epic Games, Inc. All Rights Reserved.
// 修改：2026-03-30  v2.3 — 第3步：事件标记 Actor 映射

#include "HeteroSwarmGameInstance.h"
#include "UDPManager.h"
#include "HeartbeatManager.h"
#include "DeviceStateManager.h"
#include "EventMarkerManager.h"
#include "TrajectoryManager.h"
#include "PointCloudManager.h"
#include "VirtualLidarTestActor.h"
#include "HeteroSwarmAgentBase.h"
#include "EventMarkerBase.h"
#include "TrajectoryVisualizerActor.h"
#include "RTSPCameraActor.h"

#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeteroSwarmGameInstance, Log, All);

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

    // 1. HeartbeatManager
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

    // 2. DeviceStateManager
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

    // 3. EventMarkerManager
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

    // 4. TrajectoryManager
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

    // 5. PointCloudManager
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

    // 新增：解绑 TrajectoryManager -> GameInstance 的表现层委托
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

    // 新增：销毁所有轨迹可视化 Actor
    DestroyAllSpawnedTrajectoryVisualizers();
    DestroyAllSpawnedVideoActors();

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

    SpawnAgentActorForDevice(SystemID, DeviceType);

    // 新增：设备上线时自动生成视频流Actor
    SpawnVideoActorForDevice(SystemID);
}

void UHeteroSwarmGameInstance::HandleAgentOfflineForLifecycle(int32 SystemID)
{
    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("HandleAgentOfflineForLifecycle: SystemID=%d"),
        SystemID);

    DestroyAgentActorForDevice(SystemID);

    // 新增：设备离线时自动销毁视频流Actor
    DestroyVideoActorForDevice(SystemID);
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
    if (!Config.bAutoSpawnVirtualLidarTestActor)
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
        if (!bIsEditorWorld)
        {
            return;
        }
    }
#else
    if (Config.bAutoSpawnVirtualLidarOnlyInEditor)
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

bool UHeteroSwarmGameInstance::SpawnVideoActorForDevice(int32 DeviceID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("SpawnVideoActorForDevice failed: World is null (DeviceID=%d)"),
            DeviceID);
        return false;
    }

    if (ARTSPCameraActor** Existing = SpawnedVideoActorMap.Find(DeviceID))
    {
        if (IsValid(*Existing))
        {
            UE_LOG(LogHeteroSwarmGameInstance, Verbose,
                TEXT("SpawnVideoActorForDevice skipped: already exists for DeviceID=%d (%s)"),
                DeviceID, *(*Existing)->GetName());
            return true;
        }
    }

    if (*DefaultRTSPCameraActorClass == nullptr)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Warning,
            TEXT("SpawnVideoActorForDevice failed: DefaultRTSPCameraActorClass is null (DeviceID=%d)"),
            DeviceID);
        return false;
    }

    FVector SpawnLocation = FVector::ZeroVector;
    FRotator SpawnRotation = FRotator::ZeroRotator;

    // 如果对应设备Actor已经存在，就把视频Actor生成在设备附近
    if (AActor* AgentActor = GetSpawnedAgentActor(DeviceID))
    {
        SpawnLocation = AgentActor->GetActorLocation() + FVector(0.0f, 0.0f, 200.0f);
        SpawnRotation = AgentActor->GetActorRotation();
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = FName(*FString::Printf(TEXT("RTSPCamera_%d"), DeviceID));

    ARTSPCameraActor* VideoActor = World->SpawnActor<ARTSPCameraActor>(
        DefaultRTSPCameraActorClass,
        SpawnLocation,
        SpawnRotation,
        SpawnParams);

    if (!VideoActor)
    {
        UE_LOG(LogHeteroSwarmGameInstance, Error,
            TEXT("SpawnVideoActorForDevice failed: SpawnActor returned null (DeviceID=%d)"),
            DeviceID);
        return false;
    }

    SpawnedVideoActorMap.Add(DeviceID, VideoActor);

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Spawned RTSP camera actor: DeviceID=%d Actor=%s Loc=%s"),
        DeviceID, *VideoActor->GetName(), *SpawnLocation.ToCompactString());

    return true;
}

bool UHeteroSwarmGameInstance::DestroyVideoActorForDevice(int32 DeviceID)
{
    ARTSPCameraActor** Found = SpawnedVideoActorMap.Find(DeviceID);
    if (!Found || !IsValid(*Found))
    {
        SpawnedVideoActorMap.Remove(DeviceID);
        return false;
    }

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("Destroying RTSP camera actor: DeviceID=%d Actor=%s"),
        DeviceID, *(*Found)->GetName());

    (*Found)->Destroy();
    SpawnedVideoActorMap.Remove(DeviceID);
    return true;
}

void UHeteroSwarmGameInstance::DestroyAllSpawnedVideoActors()
{
    TArray<int32> DeviceIDs;
    SpawnedVideoActorMap.GetKeys(DeviceIDs);

    for (int32 DeviceID : DeviceIDs)
    {
        DestroyVideoActorForDevice(DeviceID);
    }

    SpawnedVideoActorMap.Empty();

    UE_LOG(LogHeteroSwarmGameInstance, Log,
        TEXT("DestroyAllSpawnedVideoActors complete"));
}