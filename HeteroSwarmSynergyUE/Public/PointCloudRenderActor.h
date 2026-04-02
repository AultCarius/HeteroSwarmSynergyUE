// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PointCloudRenderActor.generated.h"

class USceneComponent;
class UGPUPointCloudRendererComponent;

UCLASS()
class HETEROSWARMSYNERGYUE_API APointCloudRenderActor : public AActor
{
    GENERATED_BODY()

public:
    APointCloudRenderActor();

    void UpdateRenderedCloud(
        const TArray<FVector>& PointPositions,
        const TArray<FColor>& PointColors,
        float PointSizeCm,
        float CloudScale,
        float DistanceScaling,
        bool bOverrideColor);

    void ClearRenderedCloud();

private:
    UPROPERTY(VisibleAnywhere, Category = "Point Cloud")
    USceneComponent* SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Point Cloud")
    UGPUPointCloudRendererComponent* PointCloudRenderer;
};
