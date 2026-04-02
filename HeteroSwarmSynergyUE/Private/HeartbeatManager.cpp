// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 业务逻辑层
// 作者：Carius
// 日期：2026-03-13

#include "HeartbeatManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "ProtocolMappingLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeartbeatManager, Log, All);

UHeartbeatManager::UHeartbeatManager()
    : World(nullptr)
    , bIsInitialized(false)
    , HeartbeatTimeoutSeconds(3.0f)
    , TimeoutCheckInterval(1.0f)
    , LastTimeoutCheckTime(0.0)
    , TotalHeartbeatsReceived(0)
    , TotalOnlineEvents(0)
    , TotalTimeoutEvents(0)
{
}

UHeartbeatManager::~UHeartbeatManager()
{
    Shutdown();
}

bool UHeartbeatManager::Initialize(UWorld* InWorld)
{
    if (bIsInitialized)
    {
        UE_LOG(LogHeartbeatManager, Warning, TEXT("Already initialized"));
        return false;
    }

    if (!InWorld)
    {
        UE_LOG(LogHeartbeatManager, Error, TEXT("Invalid World pointer"));
        return false;
    }

    World = InWorld;
    bIsInitialized = true;
    LastTimeoutCheckTime = FPlatformTime::Seconds();

    UE_LOG(LogHeartbeatManager, Log,
        TEXT("HeartbeatManager initialized (Timeout=%.1fs, CheckInterval=%.1fs)"),
        HeartbeatTimeoutSeconds, TimeoutCheckInterval);

    return true;
}

void UHeartbeatManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogHeartbeatManager, Log,
        TEXT("Shutting down HeartbeatManager. (OnlineAgents=%d, TotalHeartbeats=%d, Timeouts=%d)"),
        GetOnlineAgentCount(), TotalHeartbeatsReceived, TotalTimeoutEvents);

    // 只清状态和指针登记，不销毁Actor
    for (auto& Pair : AgentCache)
    {
        Pair.Value.bIsOnline = false;
        Pair.Value.SpawnedActor = nullptr;
    }

    AgentCache.Empty();
    World = nullptr;
    bIsInitialized = false;

    UE_LOG(LogHeartbeatManager, Log, TEXT("HeartbeatManager shutdown complete"));
}

void UHeartbeatManager::Tick(float DeltaTime)
{
    if (!bIsInitialized)
    {
        return;
    }

    const double CurrentTime = FPlatformTime::Seconds();
    if (CurrentTime - LastTimeoutCheckTime >= TimeoutCheckInterval)
    {
        CheckTimeouts();
        LastTimeoutCheckTime = CurrentTime;
    }
}

void UHeartbeatManager::HandleHeartbeat(const FMAVLinkHeartbeatData& HeartbeatData)
{
    if (!bIsInitialized)
    {
        return;
    }

    const int32 SysID = HeartbeatData.SystemID;
    if (SysID <= 0 || SysID > 255)
    {
        UE_LOG(LogHeartbeatManager, Warning,
            TEXT("Invalid SystemID in heartbeat: %d"), SysID);
        return;
    }

    const uint8 NormalizedDeviceType = FProtocolMapping::NormalizeDeviceTypeCode(HeartbeatData.CustomMode);

    if (NormalizedDeviceType == static_cast<uint8>(EUnifiedDeviceType::Unknown))
    {
        UE_LOG(LogHeartbeatManager, Warning,
            TEXT("Unknown device type from heartbeat: SystemID=%d RawCustomMode=%d"),
            SysID,
            HeartbeatData.CustomMode);
    }

    TotalHeartbeatsReceived++;

    const double Now = FPlatformTime::Seconds();
    FAgentInfo* Existing = AgentCache.Find(SysID);

    if (!Existing)
    {
        FAgentInfo NewInfo;
        NewInfo.SystemID = SysID;
        NewInfo.DeviceType = NormalizedDeviceType;
        NewInfo.MAVType = HeartbeatData.MAVType;
        NewInfo.LastHeartbeatTime = Now;
        NewInfo.bIsOnline = false;
        NewInfo.OnlineTime = Now;
        NewInfo.HeartbeatCount = 1;

        AgentCache.Add(SysID, NewInfo);
        FAgentInfo& Inserted = AgentCache[SysID];

        UE_LOG(LogHeartbeatManager, Log,
            TEXT("New agent detected: SystemID=%d RawCustomMode=%d DeviceType=%d(%s) MAVType=%d"),
            SysID,
            HeartbeatData.CustomMode,
            Inserted.DeviceType,
            *FProtocolMapping::GetDeviceTypeName(Inserted.DeviceType),
            Inserted.MAVType);

        HandleAgentOnline(Inserted, true);
    }
    else
    {
        const bool bWasOffline = !Existing->bIsOnline;

        Existing->LastHeartbeatTime = Now;
        Existing->HeartbeatCount++;
        Existing->DeviceType = NormalizedDeviceType;

        if (bWasOffline)
        {
            Existing->OnlineTime = Now;
            UE_LOG(LogHeartbeatManager, Log,
                TEXT("Agent reconnected: SystemID=%d RawCustomMode=%d DeviceType=%d(%s)"),
                SysID,
                HeartbeatData.CustomMode,
                Existing->DeviceType,
                *FProtocolMapping::GetDeviceTypeName(Existing->DeviceType));
        }

        HandleAgentOnline(*Existing, bWasOffline);
    }

    OnAgentHeartbeat.Broadcast(SysID);
}

void UHeartbeatManager::CheckTimeouts()
{
    const double CurrentTime = FPlatformTime::Seconds();

    TArray<int32> TimedOutIDs;
    for (const auto& Pair : AgentCache)
    {
        const FAgentInfo& Info = Pair.Value;
        if (Info.bIsOnline &&
            (CurrentTime - Info.LastHeartbeatTime) > HeartbeatTimeoutSeconds)
        {
            TimedOutIDs.Add(Pair.Key);
        }
    }

    for (int32 SysID : TimedOutIDs)
    {
        if (FAgentInfo* Info = AgentCache.Find(SysID))
        {
            UE_LOG(LogHeartbeatManager, Warning,
                TEXT("Agent timeout: SystemID=%d (%.1fs since last heartbeat)"),
                SysID, CurrentTime - Info->LastHeartbeatTime);

            HandleAgentOffline(*Info);
        }
    }
}

void UHeartbeatManager::HandleAgentOnline(FAgentInfo& AgentInfo, bool bWasOffline)
{
    AgentInfo.bIsOnline = true;

    if (bWasOffline)
    {
        TotalOnlineEvents++;

        UE_LOG(LogHeartbeatManager, Log,
            TEXT("Agent online: SystemID=%d DeviceType=%d(%s) (TotalOnlineEvents=%d)"),
            AgentInfo.SystemID,
            AgentInfo.DeviceType,
            *FProtocolMapping::GetDeviceTypeName(AgentInfo.DeviceType),
            TotalOnlineEvents);

        OnAgentOnline.Broadcast(AgentInfo.SystemID, AgentInfo.DeviceType);
    }
}

void UHeartbeatManager::HandleAgentOffline(FAgentInfo& AgentInfo)
{
    AgentInfo.bIsOnline = false;
    TotalTimeoutEvents++;

    // 不销毁Actor，只广播
    UE_LOG(LogHeartbeatManager, Log,
        TEXT("Agent offline: SystemID=%d (TotalTimeouts=%d)"),
        AgentInfo.SystemID, TotalTimeoutEvents);

    OnAgentOffline.Broadcast(AgentInfo.SystemID);
}

bool UHeartbeatManager::GetAgentInfo(int32 SystemID, FAgentInfo& OutInfo) const
{
    const FAgentInfo* Found = AgentCache.Find(SystemID);
    if (Found)
    {
        OutInfo = *Found;
        return true;
    }
    return false;
}

TArray<int32> UHeartbeatManager::GetOnlineAgentIDs() const
{
    TArray<int32> IDs;
    for (const auto& Pair : AgentCache)
    {
        if (Pair.Value.bIsOnline)
        {
            IDs.Add(Pair.Key);
        }
    }
    return IDs;
}

bool UHeartbeatManager::IsAgentOnline(int32 SystemID) const
{
    const FAgentInfo* Found = AgentCache.Find(SystemID);
    return Found && Found->bIsOnline;
}

int32 UHeartbeatManager::GetOnlineAgentCount() const
{
    int32 Count = 0;
    for (const auto& Pair : AgentCache)
    {
        if (Pair.Value.bIsOnline)
        {
            Count++;
        }
    }
    return Count;
}

void UHeartbeatManager::SetAgentActor(int32 SystemID, AActor* InActor)
{
    if (FAgentInfo* Found = AgentCache.Find(SystemID))
    {
        Found->SpawnedActor = InActor;
        UE_LOG(LogHeartbeatManager, Log,
            TEXT("Actor assigned to SystemID=%d: %s"),
            SystemID, InActor ? *InActor->GetName() : TEXT("nullptr"));
    }
    else
    {
        UE_LOG(LogHeartbeatManager, Warning,
            TEXT("SetAgentActor: SystemID=%d not found in cache"), SystemID);
    }
}

AActor* UHeartbeatManager::GetAgentActor(int32 SystemID) const
{
    const FAgentInfo* Found = AgentCache.Find(SystemID);
    if (Found && IsValid(Found->SpawnedActor))
    {
        return Found->SpawnedActor;
    }
    return nullptr;
}

void UHeartbeatManager::SetHeartbeatTimeout(float TimeoutSeconds)
{
    if (TimeoutSeconds < 0.5f)
    {
        UE_LOG(LogHeartbeatManager, Warning,
            TEXT("Timeout too small: %.2fs, using 0.5s"), TimeoutSeconds);
        TimeoutSeconds = 0.5f;
    }

    HeartbeatTimeoutSeconds = TimeoutSeconds;

    UE_LOG(LogHeartbeatManager, Log,
        TEXT("Heartbeat timeout set to %.1fs"), HeartbeatTimeoutSeconds);
}