// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventMarkerBase.h"
#include "Components/SceneComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogEventMarkerBase, Log, All);

AEventMarkerBase::AEventMarkerBase()
    : EventID(INDEX_NONE)
    , EventType(0)
    , bHighlighted(false)
    , bHasValidRuntimeState(false)
    , bDestroyImmediatelyOnDisappear(true)
{
    PrimaryActorTick.bCanEverTick = false;

    RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
    RootComponent = RootSceneComponent;
}

void AEventMarkerBase::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogEventMarkerBase, Log,
        TEXT("EventMarker BeginPlay: %s (EventID=%d EventType=%d)"),
        *GetName(), EventID, EventType);
}

void AEventMarkerBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
}

void AEventMarkerBase::InitializeEventMarker(int32 InEventID, uint8 InEventType)
{
    EventID = InEventID;
    EventType = InEventType;

    UE_LOG(LogEventMarkerBase, Log,
        TEXT("InitializeEventMarker: Actor=%s EventID=%d EventType=%d"),
        *GetName(), EventID, EventType);

    BP_OnEventInitialized();
}

void AEventMarkerBase::ApplyEventState(const FEventMarkerRuntimeState& InState)
{
    CurrentRuntimeState = InState;
    bHasValidRuntimeState = true;

    SetActorLocation(InState.Location);

    const bool bShouldHighlight = (InState.State == 1);
    if (bHighlighted != bShouldHighlight)
    {
        SetHighlighted(bShouldHighlight);
    }

    BP_OnEventStateApplied(CurrentRuntimeState);
}

void AEventMarkerBase::SetHighlighted(bool bInHighlighted)
{
    if (bHighlighted == bInHighlighted)
    {
        return;
    }

    bHighlighted = bInHighlighted;

    UE_LOG(LogEventMarkerBase, Log,
        TEXT("SetHighlighted: Actor=%s EventID=%d Highlighted=%s"),
        *GetName(), EventID, bHighlighted ? TEXT("true") : TEXT("false"));

    BP_OnHighlightChanged(bHighlighted);
}

void AEventMarkerBase::PlayDisappear()
{
    UE_LOG(LogEventMarkerBase, Log,
        TEXT("PlayDisappear: Actor=%s EventID=%d DestroyImmediately=%s"),
        *GetName(), EventID, bDestroyImmediatelyOnDisappear ? TEXT("true") : TEXT("false"));

    BP_OnDisappear();

    if (bDestroyImmediatelyOnDisappear)
    {
        Destroy();
    }
}