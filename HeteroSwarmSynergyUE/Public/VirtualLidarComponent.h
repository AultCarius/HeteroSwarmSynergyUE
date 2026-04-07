// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 传感器系统 - 虚拟激光雷达

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VirtualLidarComponent.generated.h"

class FSocket;
class UPointCloudManager;

UENUM(BlueprintType)
enum class EVirtualLidarTransportMode : uint8
{
    // Default path: use the project's formal MessageType-based UDP pipeline.
    LegacyCustomUdp = 0 UMETA(DisplayName = "Legacy Custom UDP (0x0004 via Manager)"),

    // Compatibility path: keep the standalone compact chunked sender for old tests.
    CompactChunkedUdp = 1 UMETA(DisplayName = "Compact Chunked UDP")
};

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

    // DeviceID is written into the compact point cloud header.
    // If left as 0, the component derives a stable fallback id from its path.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|UDP",
        meta = (ClampMin = "0", ClampMax = "2147483647"))
    int32 DeviceID = 0;

    // The default transport goes through PointCloudManager -> UDPManager -> MessageType=0x0004.
    // Compact mode is kept only for compatibility with the previous standalone receiver.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|UDP")
    EVirtualLidarTransportMode TransportMode = EVirtualLidarTransportMode::CompactChunkedUdp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|UDP")
    FString FrameName = TEXT("map");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1.0", ClampMax = "60.0"))
    float ScanFrequencyHz = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1", ClampMax = "2048"))
    int32 HorizontalSampleCount = 48;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1", ClampMax = "128"))
    int32 VerticalSampleCount = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1.0", ClampMax = "360.0"))
    float HorizontalFovDegrees = 360.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1.0", ClampMax = "120.0"))
    float VerticalFovDegrees = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "-45.0", ClampMax = "45.0"))
    float VerticalCenterDegrees = -5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan")
    FVector ScanOriginOffsetCm = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "0.01", ClampMax = "5.0"))
    float MinRangeMeters = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "0.1", ClampMax = "500.0"))
    float MaxRangeMeters = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan")
    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan")
    bool bIgnoreOwner = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Scan",
        meta = (ClampMin = "1", ClampMax = "50000"))
    int32 MaxPointsPerFrame = 512;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Packet")
    bool bReplaceExistingPointCloud = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Packet",
        meta = (ClampMin = "0.1", ClampMax = "50.0"))
    float DefaultPointSizeCm = 18.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Packet",
        meta = (ClampMin = "256", ClampMax = "60000"))
    int32 MaxPacketPayloadBytes = 1200;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Packet")
    FLinearColor PointColor = FLinearColor(1.0f, 0.15f, 0.05f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug")
    bool bDrawDebugRays = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug",
        meta = (EditCondition = "bDrawDebugRays", ClampMin = "0.0", ClampMax = "2.0"))
    float DebugDrawDuration = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug")
    bool bDrawDebugImpactPoints = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug",
        meta = (EditCondition = "bDrawDebugImpactPoints", ClampMin = "1", ClampMax = "20000"))
    int32 MaxDebugImpactPoints = 3200;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug",
        meta = (EditCondition = "bDrawDebugImpactPoints", ClampMin = "1.0", ClampMax = "30.0"))
    float DebugImpactPointSizeCm = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug",
        meta = (EditCondition = "bDrawDebugImpactPoints", ClampMin = "0.0", ClampMax = "2.0"))
    float DebugImpactDrawDuration = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Lidar|Debug",
        meta = (EditCondition = "bDrawDebugImpactPoints"))
    FLinearColor DebugImpactPointColor = FLinearColor(1.0f, 0.45f, 0.08f, 1.0f);

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

    // Perform the real scan against scene geometry and output world-space hit points in meters.
    bool PerformScan(TArray<FVector>& OutHitPointsWorldMeters);

    // Route one lidar frame through the configured transport path.
    bool SendPointCloudFrame(const TArray<FVector>& WorldPointsMeters, uint32 FrameSequence, int32& OutChunkCount);
    bool SendLegacyPointCloudFrame(const TArray<FVector>& WorldPointsMeters);

    // Pack world-space points into the compact transport format used by the legacy standalone receiver.
    bool BuildCompactPointCloudPackets(const TArray<FVector>& WorldPointsMeters, uint32 FrameSequence, TArray<TArray<uint8>>& OutPackets) const;
    bool SendPacket(const TArray<uint8>& PacketBytes);
    void UpdateStatus(const FString& NewStatus);
    FVector ComputeTraceDirection(float HorizontalAngleDeg, float VerticalAngleDeg) const;
    uint32 ResolveDeviceID() const;
    UPointCloudManager* ResolvePointCloudManager() const;
};
