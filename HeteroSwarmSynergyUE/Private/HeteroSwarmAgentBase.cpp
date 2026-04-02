// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeteroSwarmAgentBase.h"
#include "Components/SceneComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeteroSwarmAgentBase, Log, All);

AHeteroSwarmAgentBase::AHeteroSwarmAgentBase()
    : DeviceID(INDEX_NONE)
    , DeviceType(0)
    , bIsOnline(false)
    , bHasValidRuntimeState(false)
    , bSnapToRuntimeStateOnNextUpdate(true)
    , LocationInterpSpeed(8.0f)
    , RotationInterpSpeed(10.0f)
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
    RootComponent = RootSceneComponent;

    SetActorEnableCollision(true);
}

void AHeteroSwarmAgentBase::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogHeteroSwarmAgentBase, Log,
        TEXT("Agent BeginPlay: %s (DeviceID=%d DeviceType=%d)"),
        *GetName(), DeviceID, DeviceType);
}

void AHeteroSwarmAgentBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bIsOnline)
    {
        return;
    }

    if (!bHasValidRuntimeState)
    {
        return;
    }

    UpdateTransformFromRuntimeState(DeltaSeconds);
    UpdateActuatorVisuals(DeltaSeconds);
}

void AHeteroSwarmAgentBase::InitializeAgent(int32 InDeviceID, int32 InDeviceType)
{
    DeviceID = InDeviceID;
    DeviceType = InDeviceType;

    if (DebugDisplayName.IsEmpty())
    {
        DebugDisplayName = FString::Printf(TEXT("Agent_%d"), DeviceID);
    }

    UE_LOG(LogHeteroSwarmAgentBase, Log,
        TEXT("InitializeAgent: Actor=%s DeviceID=%d DeviceType=%d"),
        *GetName(), DeviceID, DeviceType);

    BP_OnAgentInitialized();
}

void AHeteroSwarmAgentBase::SetAgentOnline(bool bInOnline)
{
    if (bIsOnline == bInOnline)
    {
        return;
    }

    bIsOnline = bInOnline;

    UE_LOG(LogHeteroSwarmAgentBase, Log,
        TEXT("SetAgentOnline: Actor=%s DeviceID=%d Online=%s"),
        *GetName(), DeviceID, bIsOnline ? TEXT("true") : TEXT("false"));

    BP_OnAgentOnlineStateChanged(bIsOnline);
}

void AHeteroSwarmAgentBase::ApplyRuntimeState(const FDeviceRuntimeState& InState)
{
    CurrentRuntimeState = InState;

    if (!bHasValidRuntimeState || bSnapToRuntimeStateOnNextUpdate)
    {
        SetActorLocationAndRotation(CurrentRuntimeState.Location, CurrentRuntimeState.Rotation);
        bSnapToRuntimeStateOnNextUpdate = false;
    }

    bHasValidRuntimeState = true;

    BP_OnRuntimeStateApplied(CurrentRuntimeState);
}

void AHeteroSwarmAgentBase::UpdateTransformFromRuntimeState(float DeltaSeconds)
{
    const FVector CurrentLocation = GetActorLocation();
    const FRotator CurrentRotation = GetActorRotation();

    const FVector TargetLocation = CurrentRuntimeState.Location;
    const FRotator TargetRotation = CurrentRuntimeState.Rotation;

    const FVector NewLocation =
        (LocationInterpSpeed <= 0.0f)
        ? TargetLocation
        : FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaSeconds, LocationInterpSpeed);

    const FRotator NewRotation =
        (RotationInterpSpeed <= 0.0f)
        ? TargetRotation
        : FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaSeconds, RotationInterpSpeed);

    SetActorLocationAndRotation(NewLocation, NewRotation);
}

void AHeteroSwarmAgentBase::UpdateActuatorVisuals(float DeltaSeconds)
{
    // 第2步先预留空实现。
    // 后续由四旋翼/机器狗等子类覆盖，读取 CurrentRuntimeState.ActuatorStates 进行可视化驱动。
}