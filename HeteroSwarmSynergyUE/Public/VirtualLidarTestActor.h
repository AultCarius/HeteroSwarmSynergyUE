// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 传感器系统 - 虚拟激光雷达测试Actor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VirtualLidarTestActor.generated.h"

class USceneComponent;
class UVirtualLidarComponent;

UCLASS(Blueprintable)
class HETEROSWARMSYNERGYUE_API AVirtualLidarTestActor : public AActor
{
    GENERATED_BODY()

public:
    AVirtualLidarTestActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintPure, Category = "Virtual Lidar Test")
    UVirtualLidarComponent* GetVirtualLidarComponent() const { return VirtualLidarComponent; }

    UFUNCTION(BlueprintCallable, Category = "Virtual Lidar Test|Config")
    bool ReloadConfigFromJson();

    UFUNCTION(BlueprintPure, Category = "Virtual Lidar Test|Config")
    FString GetResolvedConfigPath() const;

private:
    UPROPERTY(VisibleAnywhere, Category = "Virtual Lidar Test")
    USceneComponent* SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Virtual Lidar Test")
    UVirtualLidarComponent* VirtualLidarComponent;

    UPROPERTY(EditAnywhere, Category = "Virtual Lidar Test|Config")
    FString JsonConfigRelativePath = TEXT("VirtualLidarSimulation.json");

    UPROPERTY(VisibleAnywhere, Category = "Virtual Lidar Test|Config")
    bool bLoadedConfigFromJson = false;

    UPROPERTY(VisibleAnywhere, Category = "Virtual Lidar Test|Config")
    FString LastConfigStatus;

    bool bPathEnabled;
    bool bLoopPath;
    bool bRotateToPathDirection;
    bool bDrawDebugPath;
    bool bStartAtFirstPathPoint;
    bool bPathPointsAreWorldSpace;
    float PathMoveSpeedCmPerSecond;
    float PathReachThresholdCm;
    bool bDrawPathLabels;
    bool bDrawPathDirectionArrows;
    bool bDrawSensorMarker;
    float PathLineThickness;
    float PathWaypointRadiusCm;
    float PathActiveWaypointRadiusCm;
    float PathDirectionArrowSizeCm;
    float PathSensorMarkerRadiusCm;
    float PathLabelHeightCm;
    bool bDrawSensorLabel;
    bool bDrawSensorForwardArrow;
    float PathSensorLabelHeightCm;
    float PathSensorForwardArrowLengthCm;
    FString PathSensorLabelText;
    FLinearColor PathLineColor;
    FLinearColor PathActiveWaypointColor;
    FLinearColor PathLabelColor;
    FLinearColor PathSensorMarkerColor;
    int32 CurrentTargetWaypointIndex;
    TArray<FVector> ConfiguredPathPointsWorld;

    void ApplyDefaultFallbackSettings();
    bool EnsureDefaultConfigFileExists(const FString& AbsoluteConfigPath) const;
    bool LoadConfigFromJsonFile(const FString& AbsoluteConfigPath);
    void ApplyConfiguredPathStart();
    void UpdatePathMovement(float DeltaSeconds);
    void DrawConfiguredPath() const;
    int32 GetNextWaypointIndex(int32 CurrentIndex) const;
};
