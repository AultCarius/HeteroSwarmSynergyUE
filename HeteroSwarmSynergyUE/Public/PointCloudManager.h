// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 点云管理
#pragma once

#include "Common/UdpSocketReceiver.h"
#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "UObject/NoExportTypes.h"
#include "IUDPMessageHandler.h"
#include "UDPProtocolTypes.h"
#include "PointCloudManager.generated.h"

class UUDPManager;
class UWorld;
class FSocket;
class APointCloudRenderActor;

USTRUCT(BlueprintType)
struct FPointCloudRuntimePoint
{
    GENERATED_BODY()

    // Runtime location is always stored in UE world space, in centimeters.
    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    FVector Location = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    float Intensity = 1.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    float Timestamp = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    FLinearColor Color = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct FDevicePointCloud
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    int32 DeviceID = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    TArray<FPointCloudRuntimePoint> Points;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    int32 TotalPointsReceived = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    float LastUpdateTime = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud")
    float DefaultPointSizeCm = 5.0f;
};

USTRUCT(BlueprintType)
struct FPointCloudStatistics
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud Statistics")
    int32 ActiveCloudCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud Statistics")
    int32 TotalMessagesProcessed = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud Statistics")
    int32 TotalFramesReceived = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Point Cloud Statistics")
    int32 TotalPointsReceived = 0;
};

USTRUCT(BlueprintType)
struct FPointCloudManagerConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "1", ClampMax = "500000"))
    int32 MaxPointsPerCloud = 50000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config")
    bool bEnableCompactUdpReceiver = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (EditCondition = "bEnableCompactUdpReceiver"))
    FString CompactListenAddress = TEXT("0.0.0.0");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "1", ClampMax = "65535", EditCondition = "bEnableCompactUdpReceiver"))
    int32 CompactListenPort = 15000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "65536", ClampMax = "16777216", EditCondition = "bEnableCompactUdpReceiver"))
    int32 CompactReceiveBufferSizeBytes = 4 * 1024 * 1024;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "1", ClampMax = "2147483647", EditCondition = "bEnableCompactUdpReceiver"))
    int32 CompactSyntheticDeviceID = 15000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "0.1", ClampMax = "30.0", EditCondition = "bEnableCompactUdpReceiver"))
    float CompactFragmentTimeoutSeconds = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config")
    bool bEnableRendererActor = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "0.1", ClampMax = "100.0", EditCondition = "bEnableRendererActor"))
    float RendererPointSizeCm = 18.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (EditCondition = "bEnableRendererActor"))
    bool bPreferPacketPointSize = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "0.001", ClampMax = "100.0", EditCondition = "bEnableRendererActor"))
    float RendererCloudScale = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "1.0", ClampMax = "100000.0", EditCondition = "bEnableRendererActor"))
    float RendererDistanceScaling = 1200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (EditCondition = "bEnableRendererActor"))
    bool bRendererOverrideColor = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config")
    bool bEnableDebugDraw = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "1", ClampMax = "50000", EditCondition = "bEnableDebugDraw"))
    int32 MaxDebugDrawPointsPerCloud = 20000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "1.0", ClampMax = "30.0", EditCondition = "bEnableDebugDraw"))
    float DebugDrawPointSize = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config",
        meta = (ClampMin = "0.0", ClampMax = "60.0"))
    float CloudStaleTimeoutSeconds = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config|Debug")
    bool bAnchorCompactPreviewToActiveCamera = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config|Debug",
        meta = (ClampMin = "50.0", ClampMax = "5000.0", EditCondition = "bAnchorCompactPreviewToActiveCamera"))
    float CompactPreviewAnchorDistanceCm = 250.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config|Debug")
    bool bDrawDebugBounds = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config|Debug")
    bool bDrawDebugLabels = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Point Cloud Config|Debug",
        meta = (ClampMin = "0.0", ClampMax = "300.0", EditCondition = "bDrawDebugLabels"))
    float DebugLabelHeightOffsetCm = 40.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnPointCloudUpdated,
    int32, DeviceID,
    int32, PointCount
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnPointCloudCleared,
    int32, DeviceID
);

UCLASS(BlueprintType)
class HETEROSWARMSYNERGYUE_API UPointCloudManager : public UObject, public IUDPMessageHandler
{
    GENERATED_BODY()

public:
    UPointCloudManager();
    virtual ~UPointCloudManager();

    bool Initialize(UUDPManager* InUDPManager, UWorld* InWorld);
    void Shutdown();
    void Tick(float DeltaTime);

    virtual void HandleMessage(const uint8* Data, uint32 Length) override;

    UFUNCTION(BlueprintPure, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Get Device Point Cloud"))
    bool GetDevicePointCloud(int32 DeviceID, FDevicePointCloud& OutPointCloud) const;

    UFUNCTION(BlueprintPure, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Get Point Cloud Locations"))
    TArray<FVector> GetPointCloudLocations(int32 DeviceID) const;

    UFUNCTION(BlueprintPure, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Has Point Cloud"))
    bool HasPointCloud(int32 DeviceID) const;

    UFUNCTION(BlueprintPure, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Get Point Cloud Point Count"))
    int32 GetPointCloudPointCount(int32 DeviceID) const;

    UFUNCTION(BlueprintPure, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Get All Device IDs With Point Cloud"))
    TArray<int32> GetAllDeviceIDsWithPointCloud() const;

    UFUNCTION(BlueprintCallable, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Get Point Cloud Statistics"))
    void GetStatistics(FPointCloudStatistics& OutStatistics) const;

    UFUNCTION(BlueprintPure, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Get Point Cloud Config"))
    FPointCloudManagerConfig GetConfig() const { return Config; }

    UFUNCTION(BlueprintCallable, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Set Point Cloud Config"))
    void SetConfig(const FPointCloudManagerConfig& NewConfig);

    UFUNCTION(BlueprintCallable, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Clear Device Point Cloud"))
    bool ClearDevicePointCloud(int32 DeviceID);

    UFUNCTION(BlueprintCallable, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Clear All Point Clouds"))
    void ClearAllPointClouds();

    UFUNCTION(BlueprintCallable, Category = "UDP|Point Cloud Manager",
        meta = (DisplayName = "Print Statistics"))
    void PrintStatistics() const;

    // Manager-side send entry for the legacy custom-UDP point cloud format.
    // This is kept as an integration bridge so point cloud sending can gradually
    // move back into the manager architecture instead of living only in the lidar component.
    bool SendLegacyPointCloudFrame(
        int32 DeviceID,
        const TArray<FVector>& WorldPointsCm,
        int32 TargetPort,
        const FString& TargetIP = TEXT("127.0.0.1")) const;

    UPROPERTY(BlueprintAssignable, Category = "UDP|Point Cloud Manager")
    FOnPointCloudUpdated OnPointCloudUpdated;

    UPROPERTY(BlueprintAssignable, Category = "UDP|Point Cloud Manager")
    FOnPointCloudCleared OnPointCloudCleared;

private:
    struct FPendingCompactDatagram
    {
        TArray<uint8> Bytes;
        FString EndpointKey;
    };

    struct FCompactChunkHeader
    {
        uint8 PointFormat = 0;
        uint8 Flags = 0;
        uint8 FragmentIndex = 0;
        uint8 FragmentCount = 0;
        uint16 FrameNameByteCount = 0;
        uint32 Sequence = 0;
        uint32 DeviceID = 0;
        uint32 PointCount = 0;
        uint32 PayloadBytes = 0;
        float DefaultPointSizeCm = 5.0f;
        FVector Translation = FVector::ZeroVector;
        FQuat Rotation = FQuat::Identity;
    };

    struct FCompactFragmentAssembly
    {
        FCompactChunkHeader Header;
        FString EndpointKey;
        FString FrameName;
        double FirstSeenTimeSeconds = 0.0;
        int32 ReceivedFragmentCount = 0;
        TArray<TArray<uint8>> FragmentPayloads;
        TArray<bool> ReceivedFlags;
    };

    UPROPERTY()
    UUDPManager* UDPManager;

    UPROPERTY()
    UWorld* World;

    UPROPERTY()
    TMap<int32, FDevicePointCloud> PointCloudCache;

    UPROPERTY()
    FPointCloudManagerConfig Config;

    bool bIsInitialized;
    int32 TotalMessagesProcessed;
    int32 TotalFramesReceived;
    int32 TotalPointsReceived;
    TQueue<FPendingCompactDatagram, EQueueMode::Mpsc> PendingCompactDatagrams;
    TMap<FString, FCompactFragmentAssembly> CompactFragmentAssemblies;
    FSocket* CompactListenSocket;
    TUniquePtr<FUdpSocketReceiver> CompactSocketReceiver;

    UPROPERTY()
    APointCloudRenderActor* PointCloudRenderActor;

    bool bRendererDirty;
    bool bOwnsRenderActor;

    void ProcessPointCloudData(const uint8* Data, uint32 Length);
    void ProcessPendingCompactDatagrams();
    bool StartCompactUdpReceiver();
    void StopCompactUdpReceiver();
    void HandleCompactUdpDataReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Endpoint);
    bool TryParseCompactChunkedDatagram(const TArray<uint8>& Bytes, FCompactChunkHeader& OutHeader, FString& OutFrameName, const uint8*& OutPayloadData, int32& OutPayloadSize) const;
    bool DecodeCompactChunkedDatagram(const TArray<uint8>& Bytes, int32& OutDeviceID, TArray<FPointCloudRuntimePoint>& OutPoints, float& OutDefaultPointSizeCm) const;
    bool AccumulateCompactFragment(const FPendingCompactDatagram& Datagram, TArray<uint8>& OutCompletedDatagram);
    bool BuildCompactDatagram(const FCompactChunkHeader& Header, const FString& FrameName, const TArray<TArray<uint8>>& FragmentPayloads, TArray<uint8>& OutBytes) const;
    FString MakeCompactAssemblyKey(const FString& EndpointKey, uint32 DeviceID, uint32 Sequence) const;
    void PruneExpiredCompactFragments();
    void ApplyPointCloudFrame(int32 DeviceID, const TArray<FPointCloudRuntimePoint>& Points, float Timestamp, float DefaultPointSizeCm, const TCHAR* SourceLabel);
    bool ValidatePointCloudHeader(int32 DeviceID, int32 PointCount) const;
    void EnsureRenderActor();
    void CleanupRenderActor();
    void RefreshRenderedPointCloud();
    int32 CleanupStalePointClouds();
    void DrawDebugPointClouds() const;
    FBox ComputePointCloudBounds(const TArray<FPointCloudRuntimePoint>& Points) const;
    FLinearColor MakeColorFromIntensity(float Intensity) const;
    bool BuildLegacyPointCloudPayload(int32 DeviceID, const TArray<FVector>& WorldPointsCm, TArray<uint8>& OutPayload) const;
};
