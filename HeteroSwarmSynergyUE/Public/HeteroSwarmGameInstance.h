#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "UDPProtocolTypes.h"
#include "DeviceStateManager.h"
#include "HeteroSwarmAgentBase.h"
#include "HeteroSwarmGameInstance.generated.h"

// 前置声明
class UUDPManager;
class UHeartbeatManager;
class UEventMarkerManager;
class UTrajectoryManager;
class UPointCloudManager;
class AVirtualLidarTestActor;
class AEventMarkerBase;
class ATrajectoryVisualizerActor;

USTRUCT(BlueprintType)
struct FGameInstanceConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration",
        meta = (ClampMin = "10", ClampMax = "500"))
    int32 BufferPoolSize = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration",
        meta = (ClampMin = "10", ClampMax = "500"))
    int32 MaxPacketsPerFrame = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration",
        meta = (ClampMin = "0.5", ClampMax = "60.0"))
    float DeviceTimeoutSeconds = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    bool bAutoInitialize = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    bool bEnableVerboseLogging = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    bool bEnableVerificationHelpers = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    bool bAutoSpawnVirtualLidarTestActor = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    bool bAutoSpawnVirtualLidarOnlyInEditor = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    bool bAutoSpawnVirtualLidarInStandaloneGame = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    FVector AutoSpawnVirtualLidarLocation = FVector(0.0f, 0.0f, 150.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    bool bAutoInjectTrajectoryTestData = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    bool bAutoInjectTrajectoryOnlyInEditor = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    bool bAutoInjectTrajectoryInStandaloneGame = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification",
        meta = (ClampMin = "1", ClampMax = "2147483647"))
    int32 AutoInjectTrajectoryDeviceID = 9101;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification")
    FVector AutoInjectTrajectoryOrigin = FVector(0.0f, 0.0f, 220.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration|Verification",
        meta = (ClampMin = "0.2", ClampMax = "30.0"))
    float AutoInjectTrajectoryRefreshSeconds = 2.0f;
};

USTRUCT(BlueprintType)
struct FDeviceActorClassConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lifecycle")
    int32 DeviceType = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lifecycle")
    TSubclassOf<AHeteroSwarmAgentBase> ActorClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lifecycle")
    FVector SpawnOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lifecycle")
    FRotator SpawnRotationOffset = FRotator::ZeroRotator;
};

UCLASS(Blueprintable)
class HETEROSWARMSYNERGYUE_API UHeteroSwarmGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UHeteroSwarmGameInstance();
    virtual ~UHeteroSwarmGameInstance();

    virtual void Init() override;
    virtual void Shutdown() override;
    virtual void Tick(float DeltaTime);

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|UDP System")
    bool InitializeUDPSystem();

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|UDP System")
    void ShutdownUDPSystem();

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|UDP System")
    bool RestartUDPSystem();

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|UDP System")
    bool IsUDPSystemInitialized() const { return bIsInitialized; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Managers",
        meta = (DisplayName = "Get Heartbeat Manager"))
    UHeartbeatManager* GetHeartbeatManager() const { return HeartbeatManager; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Managers",
        meta = (DisplayName = "Get Device State Manager"))
    UDeviceStateManager* GetDeviceStateManager() const { return DeviceStateManager; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Managers",
        meta = (DisplayName = "Get Event Marker Manager"))
    UEventMarkerManager* GetEventMarkerManager() const { return EventMarkerManager; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Managers",
        meta = (DisplayName = "Get Trajectory Manager"))
    UTrajectoryManager* GetTrajectoryManager() const { return TrajectoryManager; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Managers",
        meta = (DisplayName = "Get Point Cloud Manager"))
    UPointCloudManager* GetPointCloudManager() const { return PointCloudManager; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Managers",
        meta = (DisplayName = "Get UDP Manager"))
    UUDPManager* GetUDPManager() const;

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Configuration")
    FGameInstanceConfig GetConfig() const { return Config; }

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Configuration")
    void SetConfig(const FGameInstanceConfig& NewConfig);

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Configuration")
    bool ApplyConfig(const FGameInstanceConfig& NewConfig);

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Lifecycle")
    AActor* GetSpawnedAgentActor(int32 DeviceID) const;

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Lifecycle")
    TArray<int32> GetAllSpawnedAgentIDs() const;

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Lifecycle")
    bool DestroyAgentActorForDevice(int32 DeviceID);

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Debug")
    void PrintSystemStatus() const;

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Debug")
    void PrintAllStatistics() const;

    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Events",
        meta = (DisplayName = "On UDP System Initialized"))
    void OnUDPSystemInitialized();

    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Events",
        meta = (DisplayName = "On UDP System Shutdown"))
    void OnUDPSystemShutdown();

    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Events",
        meta = (DisplayName = "On Scene Control Received"))
    void OnSceneControlPresentationReceived(int32 SceneID, const FString& Operation, bool bEmergencyStop);

protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration",
        meta = (DisplayName = "Game Instance Configuration"))
    FGameInstanceConfig Config;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration",
        meta = (DisplayName = "UDP Configuration"))
    FUDPConfiguration UDPConfig;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lifecycle")
    TArray<FDeviceActorClassConfig> DeviceActorClassConfigs;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lifecycle")
    TSubclassOf<AHeteroSwarmAgentBase> DefaultAgentActorClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event Presentation")
    TSubclassOf<AEventMarkerBase> DefaultEventMarkerActorClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Presentation")
    TSubclassOf<ATrajectoryVisualizerActor> DefaultTrajectoryVisualizerActorClass;

private:
    UPROPERTY()
    UHeartbeatManager* HeartbeatManager;

    UPROPERTY()
    UDeviceStateManager* DeviceStateManager;

    UPROPERTY()
    UEventMarkerManager* EventMarkerManager;

    UPROPERTY()
    UTrajectoryManager* TrajectoryManager;

    UPROPERTY()
    UPointCloudManager* PointCloudManager;

    UPROPERTY()
    AVirtualLidarTestActor* AutoSpawnedVirtualLidarTestActor;

    UPROPERTY()
    TMap<int32, AActor*> SpawnedAgentActorMap;

    UPROPERTY()
    TMap<int32, AEventMarkerBase*> SpawnedEventActorMap;

    UPROPERTY()
    TMap<int32, ATrajectoryVisualizerActor*> SpawnedTrajectoryVisualizerMap;

    bool bHasInjectedTrajectoryTestData;
    double LastAutoInjectedTrajectoryTimeSeconds;
    bool bIsInitialized;
    FDelegateHandle TickDelegateHandle;

private:
    bool InitializeManagers();
    void ShutdownManagers();

    void RebindMessageHandlers();
    void OnUDPConfigurationChanged();

    bool HandleCoreTicker(float DeltaTime);

    void EnsureAutoSpawnedVirtualLidarTestActor();
    void CleanupAutoSpawnedVirtualLidarTestActor();
    void EnsureAutoInjectedTrajectoryTestData();

    UFUNCTION()
    void HandleAgentOnlineForLifecycle(int32 SystemID, int32 DeviceType);

    UFUNCTION()
    void HandleAgentOfflineForLifecycle(int32 SystemID);

    UFUNCTION()
    void HandleDeviceStateChangedForActors(int32 DeviceID, const FDeviceRuntimeState& State);

    UFUNCTION()
    void HandleEventCreatedForPresentation(int32 EventID, uint8 EventType, FVector Location);

    UFUNCTION()
    void HandleEventHighlightedForPresentation(int32 EventID, FVector Location);

    UFUNCTION()
    void HandleEventDisappearedForPresentation(int32 EventID, FVector Location);

    UFUNCTION()
    void HandleSceneControlForPresentation(int32 SceneID, const FString& Operation, bool bEmergencyStop);

    UFUNCTION()
    void HandleTrajectoryCreatedForPresentation(int32 DeviceID, ETrajectoryType TrajectoryType);

    UFUNCTION()
    void HandleTrajectoryUpdatedForPresentation(int32 DeviceID, ETrajectoryType TrajectoryType, int32 AddedPoints);

    UFUNCTION()
    void HandleTrajectoryClearedForPresentation(int32 DeviceID, ETrajectoryType TrajectoryType);

    bool SpawnTrajectoryVisualizerForDevice(int32 DeviceID);
    bool DestroyTrajectoryVisualizerForDevice(int32 DeviceID);
    void DestroyAllSpawnedTrajectoryVisualizers();

    bool SpawnAgentActorForDevice(int32 DeviceID, int32 DeviceType);
    void DestroyAllSpawnedAgentActors();

    bool SpawnEventActorForEvent(int32 EventID, uint8 EventType, const FVector& Location);
    bool DestroyEventActorForEvent(int32 EventID);
    void DestroyAllSpawnedEventActors();

    TSubclassOf<AHeteroSwarmAgentBase> FindActorClassByDeviceType(
        int32 DeviceType,
        FVector& OutSpawnOffset,
        FRotator& OutSpawnRotationOffset) const;
};
