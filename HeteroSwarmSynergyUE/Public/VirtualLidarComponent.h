// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 传感器系统 - 虚拟激光雷达

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VirtualLidarComponent.generated.h"

class FSocket;

UCLASS(Blueprintable, BlueprintType, ClassGroup = (Sensors),
    meta = (BlueprintSpawnableComponent, DisplayName = "Virtual Lidar Component"))
class HETEROSWARMSYNERGYUE_API UVirtualLidarComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UVirtualLidarComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|General")
    bool bAutoStart = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|General")
    bool bSensorEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|UDP")
    FString RemoteHost = TEXT("127.0.0.1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|UDP",
        meta = (ClampMin = "1", ClampMax = "65535"))
    int32 RemotePort = 15000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|UDP")
    FString FrameName = TEXT("map");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1.0", ClampMax = "60.0"))
    float ScanFrequencyHz = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1", ClampMax = "256"))
    int32 HorizontalSampleCount = 48;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1", ClampMax = "64"))
    int32 VerticalSampleCount = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1.0", ClampMax = "360.0"))
    float HorizontalFovDegrees = 360.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1.0", ClampMax = "120.0"))
    float VerticalFovDegrees = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "-45.0", ClampMax = "45.0"))
    float VerticalCenterDegrees = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "0.01", ClampMax = "5.0"))
    float MinRangeMeters = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "0.1", ClampMax = "500.0"))
    float MaxRangeMeters = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan")
    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan")
    bool bIgnoreOwner = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1", ClampMax = "4000"))
    int32 MaxPointsPerFrame = 2048;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Packet")
    bool bReplaceExistingPointCloud = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Packet",
        meta = (ClampMin = "0.1", ClampMax = "50.0"))
    float DefaultPointSizeCm = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Packet",
        meta = (ClampMin = "256", ClampMax = "60000"))
    int32 MaxPacketPayloadBytes = 1200;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Packet")
    FLinearColor PointColor = FLinearColor(0.1f, 1.0f, 0.2f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug")
    bool bDrawDebugRays = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug",
        meta = (EditCondition = "bDrawDebugRays", ClampMin = "0.0", ClampMax = "2.0"))
    float DebugDrawDuration = 0.05f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Virtual Lidar|Status")
    bool bIsSending = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Virtual Lidar|Status")
    int32 LastPointCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Virtual Lidar|Status")
    int64 TotalFramesSent = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Virtual Lidar|Status")
    FString LastStatusMessage;

    UFUNCTION(BlueprintCallable, Category = "Virtual Lidar|Control")
    bool StartLidar();

    UFUNCTION(BlueprintCallable, Category = "Virtual Lidar|Control")
    void StopLidar();

    UFUNCTION(BlueprintCallable, Category = "Virtual Lidar|Control")
    bool CaptureAndSendOnce();

    UFUNCTION(BlueprintPure, Category = "Virtual Lidar|Status")
    FString GetStatusSummary() const;

private:
    FSocket* SendSocket;
    double LastScanTimeSeconds;
    uint32 SequenceCounter;

    bool EnsureSendSocket();
    void CloseSendSocket();
    bool PerformScan(TArray<FVector>& OutHitPointsWorldMeters);
    bool BuildCompactPointCloudPackets(const TArray<FVector>& WorldPointsMeters, uint32 FrameSequence, TArray<TArray<uint8>>& OutPackets) const;
    bool SendPacket(const TArray<uint8>& PacketBytes);
    void UpdateStatus(const FString& NewStatus);
    FVector ComputeTraceDirection(float HorizontalAngleDeg, float VerticalAngleDeg) const;
};
