// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 业务逻辑层
// 作者：Carius
// 日期：2026-03-13

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UDPManager.h"
#include "HeartbeatManager.generated.h"

class AActor;
class UWorld;

USTRUCT(BlueprintType)
struct FAgentInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Agent")
    int32 SystemID = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Agent")
    int32 DeviceType = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Agent")
    int32 MAVType = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Agent")
    float LastHeartbeatTime = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Agent")
    bool bIsOnline = false;

    UPROPERTY(BlueprintReadOnly, Category = "Agent")
    float OnlineTime = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Agent")
    int32 HeartbeatCount = 0;

    /**
     * 设备对应的场景Actor
     *
     * 注意：
     * - 仅做登记，不拥有生命周期
     * - Spawn / Destroy 由 GameInstance 统一负责
     */
    UPROPERTY(BlueprintReadWrite, Category = "Agent")
    AActor* SpawnedActor = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnAgentOnline,
    int32, SystemID,
    int32, DeviceType
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnAgentOffline,
    int32, SystemID
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnAgentHeartbeat,
    int32, SystemID
);

UCLASS(BlueprintType)
class HETEROSWARMSYNERGYUE_API UHeartbeatManager : public UObject
{
    GENERATED_BODY()

public:
    UHeartbeatManager();
    virtual ~UHeartbeatManager();

    bool Initialize(UWorld* InWorld);
    void Shutdown();
    void Tick(float DeltaTime);

    UFUNCTION()
    void HandleHeartbeat(const FMAVLinkHeartbeatData& HeartbeatData);

    UFUNCTION(BlueprintPure, Category = "Heartbeat Manager")
    bool GetAgentInfo(int32 SystemID, FAgentInfo& OutInfo) const;

    UFUNCTION(BlueprintPure, Category = "Heartbeat Manager")
    TArray<int32> GetOnlineAgentIDs() const;

    UFUNCTION(BlueprintPure, Category = "Heartbeat Manager")
    bool IsAgentOnline(int32 SystemID) const;

    UFUNCTION(BlueprintPure, Category = "Heartbeat Manager")
    int32 GetOnlineAgentCount() const;

    /**
     * 仅登记Actor引用，不拥有生命周期
     */
    UFUNCTION(BlueprintCallable, Category = "Heartbeat Manager")
    void SetAgentActor(int32 SystemID, AActor* InActor);

    UFUNCTION(BlueprintPure, Category = "Heartbeat Manager")
    AActor* GetAgentActor(int32 SystemID) const;

    UFUNCTION(BlueprintCallable, Category = "Heartbeat Manager")
    void SetHeartbeatTimeout(float TimeoutSeconds);

    UFUNCTION(BlueprintPure, Category = "Heartbeat Manager")
    float GetHeartbeatTimeout() const { return HeartbeatTimeoutSeconds; }

    UPROPERTY(BlueprintAssignable, Category = "Heartbeat Manager")
    FOnAgentOnline OnAgentOnline;

    UPROPERTY(BlueprintAssignable, Category = "Heartbeat Manager")
    FOnAgentOffline OnAgentOffline;

    UPROPERTY(BlueprintAssignable, Category = "Heartbeat Manager")
    FOnAgentHeartbeat OnAgentHeartbeat;

private:
    UPROPERTY()
    TMap<int32, FAgentInfo> AgentCache;

    UPROPERTY()
    UWorld* World;

    bool bIsInitialized;

    float HeartbeatTimeoutSeconds;
    float TimeoutCheckInterval;
    double LastTimeoutCheckTime;

    int32 TotalHeartbeatsReceived;
    int32 TotalOnlineEvents;
    int32 TotalTimeoutEvents;

private:
    void CheckTimeouts();
    void HandleAgentOnline(FAgentInfo& AgentInfo, bool bWasOffline);

    /**
     * 仅更新在线状态并广播事件
     * 不负责 Destroy Actor
     */
    void HandleAgentOffline(FAgentInfo& AgentInfo);
};