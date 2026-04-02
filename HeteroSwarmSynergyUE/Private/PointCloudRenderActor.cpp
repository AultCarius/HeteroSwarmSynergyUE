// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudRenderActor.h"

#include "Components/SceneComponent.h"
#include "GPUPointCloudRendererComponent.h"

APointCloudRenderActor::APointCloudRenderActor()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    PointCloudRenderer = CreateDefaultSubobject<UGPUPointCloudRendererComponent>(TEXT("PointCloudRenderer"));
    PointCloudRenderer->SetupAttachment(SceneRoot);
    PointCloudRenderer->SetVisibility(false, true);
}

void APointCloudRenderActor::UpdateRenderedCloud(
    const TArray<FVector>& PointPositions,
    const TArray<FColor>& PointColors,
    float PointSizeCm,
    float CloudScale,
    float DistanceScaling,
    bool bOverrideColor)
{
    if (!PointCloudRenderer || PointPositions.Num() <= 0 || PointPositions.Num() != PointColors.Num())
    {
        ClearRenderedCloud();
        return;
    }

    FBox Bounds(EForceInit::ForceInit);
    for (const FVector& Position : PointPositions)
    {
        Bounds += Position;
    }

    PointCloudRenderer->SetExtent(Bounds.IsValid ? Bounds : FBox(FVector(-100.0f), FVector(100.0f)));
    PointCloudRenderer->SetDynamicProperties(
        FLinearColor::White,
        FMath::Max(0.001f, CloudScale),
        FMath::Max(1.0f, PointSizeCm),
        FMath::Max(1.0f, DistanceScaling),
        bOverrideColor);

    TArray<FVector> MutablePositions = PointPositions;
    TArray<FColor> MutableColors = PointColors;
    PointCloudRenderer->SetInputAndConvert2(MutablePositions, MutableColors);
    PointCloudRenderer->SetVisibility(true, true);
    SetActorHiddenInGame(false);
}

void APointCloudRenderActor::ClearRenderedCloud()
{
    if (!PointCloudRenderer)
    {
        return;
    }

    PointCloudRenderer->SetVisibility(false, true);
    SetActorHiddenInGame(true);
}
