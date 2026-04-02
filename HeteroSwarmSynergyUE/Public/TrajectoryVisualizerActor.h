// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrajectoryManager.h"
#include "Components/SplineMeshComponent.h"
#include "TrajectoryVisualizerActor.generated.h"

class USceneComponent;
class USplineComponent;
class USplineMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UTrajectoryManager;

UCLASS(Blueprintable)
class HETEROSWARMSYNERGYUE_API ATrajectoryVisualizerActor : public AActor
{
    GENERATED_BODY()

public:
    ATrajectoryVisualizerActor();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaSeconds) override;

public:
    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Trajectory Visualizer")
    void InitializeVisualizer(int32 InDeviceID, UTrajectoryManager* InTrajectoryManager);

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Trajectory Visualizer")
    void RefreshAllTrajectories();

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Trajectory Visualizer")
    void RefreshTrajectory(ETrajectoryType TrajectoryType);

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Trajectory Visualizer")
    void ClearTrajectory(ETrajectoryType TrajectoryType);

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Trajectory Visualizer")
    int32 GetDeviceID() const { return DeviceID; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Trajectory Visualizer")
    bool HasAnyTrajectory() const { return bHasPlannedTrajectory || bHasActualTrajectory; }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USceneComponent* RootSceneComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USplineComponent* PlannedSplineComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USplineComponent* ActualSplineComponent;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    UStaticMesh* TrajectorySegmentMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    UMaterialInterface* PlannedTrajectoryMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    UMaterialInterface* ActualTrajectoryMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render",
        meta = (ClampMin = "0.1", UIMin = "0.1", ClampMax = "200.0", UIMax = "200.0"))
    float PlannedWidthCm;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render",
        meta = (ClampMin = "0.1", UIMin = "0.1", ClampMax = "200.0", UIMax = "200.0"))
    float ActualWidthCm;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    float PlannedVerticalOffsetCm;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    float ActualVerticalOffsetCm;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trajectory Render",
        meta = (ClampMin = "0.01", UIMin = "0.01", ClampMax = "5.0", UIMax = "5.0"))
    float RefreshIntervalSeconds;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    int32 DeviceID;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    bool bHasPlannedTrajectory;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory Render")
    bool bHasActualTrajectory;

    UPROPERTY()
    UTrajectoryManager* TrajectoryManager;

    UPROPERTY()
    TArray<USplineMeshComponent*> PlannedSplineMeshes;

    UPROPERTY()
    TArray<USplineMeshComponent*> ActualSplineMeshes;

    float TimeSinceLastRefresh;

protected:
    void RefreshTrajectoryInternal(
        ETrajectoryType TrajectoryType,
        USplineComponent* SplineComponent,
        TArray<USplineMeshComponent*>& SplineMeshes,
        UMaterialInterface* Material,
        float WidthCm,
        float VerticalOffsetCm,
        bool& bOutHasTrajectory);

    void RebuildSplineMeshes(
        USplineComponent* SplineComponent,
        TArray<USplineMeshComponent*>& SplineMeshes,
        UMaterialInterface* Material,
        float WidthCm);

    void ClearSplineMeshes(TArray<USplineMeshComponent*>& SplineMeshes);

    void ClearSpline(USplineComponent* SplineComponent, TArray<USplineMeshComponent*>& SplineMeshes, bool& bHasTrajectory);
};