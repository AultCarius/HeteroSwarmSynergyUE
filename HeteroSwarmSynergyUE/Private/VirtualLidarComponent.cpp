// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 传感器系统 - 虚拟激光雷达

#include "VirtualLidarComponent.h"

#include "CollisionQueryParams.h"
#include "Containers/StringConv.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HeteroSwarmGameInstance.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "IPAddress.h"
#include "Misc/Crc.h"
#include "PointCloudManager.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogVirtualLidar, Log, All);

namespace
{
    constexpr ANSICHAR CompactPointCloudMagic[4] = { 'P', 'C', 'U', '1' };
    constexpr uint8 CompactPointCloudVersion = 1;
    constexpr uint8 CompactPointFormatXYZ32FRGBA8 = 1;
    constexpr uint8 CompactFlagReplaceExisting = 1 << 1;
    constexpr uint8 CompactFlagPointsAreWorldSpace = 1 << 3;
    constexpr int32 CompactHeaderBytes = 60;

    template <typename TValue>
    void AppendRawValue(TArray<uint8>& Buffer, const TValue& Value)
    {
        const int32 StartIndex = Buffer.AddUninitialized(sizeof(TValue));
        FMemory::Memcpy(Buffer.GetData() + StartIndex, &Value, sizeof(TValue));
    }

    void AppendRawBytes(TArray<uint8>& Buffer, const void* Data, int32 NumBytes)
    {
        if (NumBytes <= 0)
        {
            return;
        }

        const int32 StartIndex = Buffer.AddUninitialized(NumBytes);
        FMemory::Memcpy(Buffer.GetData() + StartIndex, Data, NumBytes);
    }
}

UVirtualLidarComponent::UVirtualLidarComponent()
    : SendSocket(nullptr)
    , LastScanTimeSeconds(0.0)
    , SequenceCounter(0)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UVirtualLidarComponent::BeginPlay()
{
    Super::BeginPlay();

    LastScanTimeSeconds = FPlatformTime::Seconds();

    if (bAutoStart && bSensorEnabled)
    {
        StartLidar();
    }
}

void UVirtualLidarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopLidar();
    Super::EndPlay(EndPlayReason);
}

void UVirtualLidarComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bSensorEnabled || !bIsSending)
    {
        return;
    }

    const float SafeFrequencyHz = FMath::Max(1.0f, ScanFrequencyHz);
    const double ScanIntervalSeconds = 1.0 / static_cast<double>(SafeFrequencyHz);
    const double CurrentTimeSeconds = FPlatformTime::Seconds();

    if ((CurrentTimeSeconds - LastScanTimeSeconds) < ScanIntervalSeconds)
    {
        return;
    }

    CaptureAndSendOnce();
    LastScanTimeSeconds = CurrentTimeSeconds;
}

bool UVirtualLidarComponent::StartLidar()
{
    if (!bSensorEnabled)
    {
        UpdateStatus(TEXT("Virtual lidar is disabled"));
        bIsSending = false;
        return false;
    }

    if (TransportMode == EVirtualLidarTransportMode::CompactChunkedUdp)
    {
        const FString NormalizedHost = RemoteHost.TrimStartAndEnd();
        const bool bLoopbackHost =
            NormalizedHost.Equals(TEXT("127.0.0.1"), ESearchCase::IgnoreCase) ||
            NormalizedHost.Equals(TEXT("localhost"), ESearchCase::IgnoreCase);

        // Avoid feeding the compact receiver from an in-project sender by default.
        // Validation should show the externally injected test cloud, not an engine self-loop.
        if (bLoopbackHost && RemotePort == 15000)
        {
            UpdateStatus(TEXT("Compact loopback sender is disabled for port 15000; use an external test script instead"));
            bIsSending = false;
            UE_LOG(LogVirtualLidar, Warning,
                TEXT("Blocked compact self-loop point cloud sender to %s:%d"),
                *RemoteHost,
                RemotePort);
            return false;
        }

        if (!EnsureSendSocket())
        {
            bIsSending = false;
            return false;
        }
    }
    else
    {
        CloseSendSocket();

        if (ResolvePointCloudManager() == nullptr)
        {
            UpdateStatus(TEXT("PointCloudManager is unavailable for legacy point cloud sending"));
            bIsSending = false;
            return false;
        }
    }

    bIsSending = true;
    LastScanTimeSeconds = FPlatformTime::Seconds();
    UpdateStatus(FString::Printf(
        TEXT("Sending via %s to %s:%d"),
        TransportMode == EVirtualLidarTransportMode::LegacyCustomUdp
            ? TEXT("PointCloudManager/UDPManager")
            : TEXT("compact UDP"),
        *RemoteHost,
        RemotePort));

    return true;
}

void UVirtualLidarComponent::StopLidar()
{
    bIsSending = false;
    CloseSendSocket();
    UpdateStatus(TEXT("Virtual lidar stopped"));
}

bool UVirtualLidarComponent::CaptureAndSendOnce()
{
    TArray<FVector> HitPointsWorldMeters;
    if (!PerformScan(HitPointsWorldMeters))
    {
        LastPointCount = 0;
        UpdateStatus(TEXT("Scan completed but no hit points were produced"));
        return false;
    }

    const uint32 FrameSequence = SequenceCounter + 1;
    int32 ChunkCount = 0;
    if (!SendPointCloudFrame(HitPointsWorldMeters, FrameSequence, ChunkCount))
    {
        LastPointCount = 0;
        UpdateStatus(TEXT("Failed to send point cloud frame"));
        return false;
    }

    SequenceCounter = FrameSequence;
    LastPointCount = HitPointsWorldMeters.Num();
    ++TotalFramesSent;
    UpdateStatus(FString::Printf(TEXT("Sent frame %lld with %d points via %s in %d message(s) to %s:%d"),
        TotalFramesSent,
        LastPointCount,
        TransportMode == EVirtualLidarTransportMode::LegacyCustomUdp
            ? TEXT("legacy/0x0004")
            : TEXT("compact"),
        ChunkCount,
        *RemoteHost,
        RemotePort));

    return true;
}

FString UVirtualLidarComponent::GetStatusSummary() const
{
    return FString::Printf(TEXT("Enabled=%s Sending=%s LastPoints=%d Frames=%lld Status=%s"),
        bSensorEnabled ? TEXT("true") : TEXT("false"),
        bIsSending ? TEXT("true") : TEXT("false"),
        LastPointCount,
        TotalFramesSent,
        *LastStatusMessage);
}

bool UVirtualLidarComponent::EnsureSendSocket()
{
    if (SendSocket != nullptr)
    {
        return true;
    }

    FIPv4Address ParsedAddress;
    if (!FIPv4Address::Parse(RemoteHost, ParsedAddress))
    {
        UpdateStatus(FString::Printf(TEXT("Invalid lidar target host: %s"), *RemoteHost));
        UE_LOG(LogVirtualLidar, Warning, TEXT("Invalid lidar target host: %s"), *RemoteHost);
        return false;
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSubsystem == nullptr)
    {
        UpdateStatus(TEXT("Socket subsystem is unavailable"));
        return false;
    }

    SendSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("VirtualLidarSendSocket"), false);
    if (SendSocket == nullptr)
    {
        UpdateStatus(TEXT("Failed to create lidar UDP socket"));
        return false;
    }

    SendSocket->SetNonBlocking(false);
    SendSocket->SetReuseAddr(true);
    SendSocket->SetBroadcast(false);
    int32 ActualSendBufferSize = 0;
    SendSocket->SetSendBufferSize(4 * 1024 * 1024, ActualSendBufferSize);

    UpdateStatus(FString::Printf(TEXT("Lidar UDP socket ready for %s:%d"), *RemoteHost, RemotePort));
    return true;
}

void UVirtualLidarComponent::CloseSendSocket()
{
    if (SendSocket == nullptr)
    {
        return;
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSubsystem != nullptr)
    {
        SocketSubsystem->DestroySocket(SendSocket);
    }

    SendSocket = nullptr;
}

bool UVirtualLidarComponent::PerformScan(TArray<FVector>& OutHitPointsWorldMeters)
{
    OutHitPointsWorldMeters.Reset();

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        UpdateStatus(TEXT("No world available for lidar scan"));
        return false;
    }

    const int32 HorizontalCount = FMath::Clamp(HorizontalSampleCount, 1, 2048);
    const int32 VerticalCount = FMath::Clamp(VerticalSampleCount, 1, 128);
    const int32 MaxSamples = FMath::Max(1, MaxPointsPerFrame);

    const float HorizontalFov = FMath::Clamp(HorizontalFovDegrees, 1.0f, 360.0f);
    const float VerticalFov = FMath::Clamp(VerticalFovDegrees, 1.0f, 120.0f);
    const float MinRangeCm = FMath::Max(1.0f, MinRangeMeters * 100.0f);
    const float MaxRangeCm = FMath::Max(MinRangeCm + 1.0f, MaxRangeMeters * 100.0f);

    const FVector TraceStart = GetComponentTransform().TransformPosition(ScanOriginOffsetCm);
    const bool bWrapHorizontally = HorizontalCount > 1 && HorizontalFov >= 359.9f;
    const float HorizontalStep =
        (HorizontalCount <= 1) ? 0.0f : HorizontalFov / static_cast<float>(bWrapHorizontally ? HorizontalCount : (HorizontalCount - 1));
    const float VerticalStep =
        (VerticalCount <= 1) ? 0.0f : VerticalFov / static_cast<float>(VerticalCount - 1);
    const float HorizontalStart = -HorizontalFov * 0.5f;
    const float VerticalStart = VerticalCenterDegrees - VerticalFov * 0.5f;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VirtualLidarTrace), true);
    QueryParams.bReturnPhysicalMaterial = false;
    QueryParams.bTraceComplex = true;

    if (bIgnoreOwner && GetOwner() != nullptr)
    {
        QueryParams.AddIgnoredActor(GetOwner());
    }

    auto DrawImpactDebugPoints = [&]()
    {
        if (!bDrawDebugImpactPoints || OutHitPointsWorldMeters.Num() <= 0)
        {
            return;
        }

        const int32 SafeMaxDebugImpactPoints = FMath::Clamp(MaxDebugImpactPoints, 1, 20000);
        const int32 DrawStep = FMath::Max(
            1,
            FMath::CeilToInt(static_cast<float>(OutHitPointsWorldMeters.Num()) / static_cast<float>(SafeMaxDebugImpactPoints)));
        const FColor DebugImpactColor = DebugImpactPointColor.ToFColor(true);
        const float DebugPointSize = FMath::Clamp(DebugImpactPointSizeCm, 1.0f, 30.0f);
        const float DebugDuration = FMath::Clamp(DebugImpactDrawDuration, 0.0f, 2.0f);

        for (int32 PointIndex = 0; PointIndex < OutHitPointsWorldMeters.Num(); PointIndex += DrawStep)
        {
            DrawDebugPoint(
                World,
                OutHitPointsWorldMeters[PointIndex] * 100.0f,
                DebugPointSize,
                DebugImpactColor,
                false,
                DebugDuration,
                0);
        }
    };

    // The scan runs against real scene geometry through line traces.
    // We keep the transport payload in meters to match the custom UDP protocol.
    OutHitPointsWorldMeters.Reserve(FMath::Min(HorizontalCount * VerticalCount, MaxSamples));

    for (int32 VerticalIndex = 0; VerticalIndex < VerticalCount; ++VerticalIndex)
    {
        const float VerticalAngleDeg = VerticalStart + VerticalStep * static_cast<float>(VerticalIndex);

        for (int32 HorizontalIndex = 0; HorizontalIndex < HorizontalCount; ++HorizontalIndex)
        {
            if (OutHitPointsWorldMeters.Num() >= MaxSamples)
            {
                DrawImpactDebugPoints();
                return OutHitPointsWorldMeters.Num() > 0;
            }

            const float HorizontalAngleDeg = HorizontalStart + HorizontalStep * static_cast<float>(HorizontalIndex);
            const FVector TraceDirection = ComputeTraceDirection(HorizontalAngleDeg, VerticalAngleDeg);
            const FVector TraceMinPoint = TraceStart + TraceDirection * MinRangeCm;
            const FVector TraceEnd = TraceStart + TraceDirection * MaxRangeCm;

            FHitResult HitResult;
            const bool bHit = World->LineTraceSingleByChannel(HitResult, TraceMinPoint, TraceEnd, TraceChannel, QueryParams);

            if (bDrawDebugRays)
            {
                const FVector DebugEnd = bHit ? HitResult.ImpactPoint : TraceEnd;
                const FColor DebugColor = bHit ? FColor::Green : FColor::Red;
                DrawDebugLine(World, TraceMinPoint, DebugEnd, DebugColor, false, DebugDrawDuration, 0, 0.5f);
                if (bHit)
                {
                    DrawDebugPoint(World, HitResult.ImpactPoint, 6.0f, FColor::Yellow, false, DebugDrawDuration, 0);
                }
            }

            if (!bHit)
            {
                continue;
            }

            OutHitPointsWorldMeters.Add(HitResult.ImpactPoint / 100.0f);
        }
    }

    DrawImpactDebugPoints();
    return OutHitPointsWorldMeters.Num() > 0;
}

bool UVirtualLidarComponent::SendPointCloudFrame(
    const TArray<FVector>& WorldPointsMeters,
    uint32 FrameSequence,
    int32& OutChunkCount)
{
    OutChunkCount = 0;

    if (TransportMode == EVirtualLidarTransportMode::LegacyCustomUdp)
    {
        const bool bSent = SendLegacyPointCloudFrame(WorldPointsMeters);
        OutChunkCount = bSent ? 1 : 0;
        return bSent;
    }

    if (!EnsureSendSocket())
    {
        bIsSending = false;
        return false;
    }

    TArray<TArray<uint8>> Packets;
    if (!BuildCompactPointCloudPackets(WorldPointsMeters, FrameSequence, Packets))
    {
        return false;
    }

    for (const TArray<uint8>& PacketBytes : Packets)
    {
        if (!SendPacket(PacketBytes))
        {
            bIsSending = false;
            return false;
        }
    }

    OutChunkCount = Packets.Num();
    return true;
}

bool UVirtualLidarComponent::SendLegacyPointCloudFrame(const TArray<FVector>& WorldPointsMeters)
{
    UPointCloudManager* PointCloudManager = ResolvePointCloudManager();
    if (PointCloudManager == nullptr)
    {
        UpdateStatus(TEXT("PointCloudManager is unavailable for legacy point cloud sending"));
        return false;
    }

    TArray<FVector> WorldPointsCm;
    WorldPointsCm.Reserve(WorldPointsMeters.Num());
    for (const FVector& WorldPointMeters : WorldPointsMeters)
    {
        WorldPointsCm.Add(WorldPointMeters * 100.0f);
    }

    // The formal project path is:
    // VirtualLidarComponent -> PointCloudManager -> UDPManager -> MessageType=0x0004.
    const bool bSent = PointCloudManager->SendLegacyPointCloudFrame(
        static_cast<int32>(ResolveDeviceID()),
        WorldPointsCm,
        RemotePort,
        RemoteHost);

    if (!bSent)
    {
        UpdateStatus(FString::Printf(
            TEXT("Failed to send legacy point cloud frame via manager to %s:%d"),
            *RemoteHost,
            RemotePort));
    }

    return bSent;
}

bool UVirtualLidarComponent::BuildCompactPointCloudPackets(
    const TArray<FVector>& WorldPointsMeters,
    uint32 FrameSequence,
    TArray<TArray<uint8>>& OutPackets) const
{
    OutPackets.Reset();

    if (WorldPointsMeters.Num() <= 0)
    {
        return false;
    }

    const FString SafeFrameName = FrameName.IsEmpty() ? TEXT("map") : FrameName;
    FTCHARToUTF8 FrameNameUtf8(*SafeFrameName);
    const uint16 FrameNameByteCount = static_cast<uint16>(FMath::Min(FrameNameUtf8.Length(), 65535));
    const uint32 PointCount = static_cast<uint32>(WorldPointsMeters.Num());
    const uint32 ResolvedDeviceID = ResolveDeviceID();
    const int32 PointStride = 16;
    const int32 SafeMaxPayloadBytes = FMath::Clamp(MaxPacketPayloadBytes, 256, 60000);
    const int32 MaxPointsPerPacket = FMath::Max(1, SafeMaxPayloadBytes / PointStride);
    const int32 FragmentCount = FMath::DivideAndRoundUp(WorldPointsMeters.Num(), MaxPointsPerPacket);

    if (FragmentCount <= 0 || FragmentCount > 255)
    {
        return false;
    }

    uint8 Flags = CompactFlagPointsAreWorldSpace;
    if (bReplaceExistingPointCloud)
    {
        Flags |= CompactFlagReplaceExisting;
    }

    const FColor PackedColor = PointColor.ToFColor(true);
    OutPackets.Reserve(FragmentCount);

    for (int32 FragmentIndex = 0; FragmentIndex < FragmentCount; ++FragmentIndex)
    {
        const int32 StartPointIndex = FragmentIndex * MaxPointsPerPacket;
        const int32 PointsInFragment = FMath::Min(MaxPointsPerPacket, WorldPointsMeters.Num() - StartPointIndex);
        const uint32 PayloadBytes = static_cast<uint32>(PointsInFragment * PointStride);

        TArray<uint8> Packet;
        Packet.Reserve(CompactHeaderBytes + FrameNameByteCount + static_cast<int32>(PayloadBytes));

        AppendRawBytes(Packet, CompactPointCloudMagic, 4);
        AppendRawValue<uint8>(Packet, CompactPointCloudVersion);
        AppendRawValue<uint8>(Packet, CompactPointFormatXYZ32FRGBA8);
        AppendRawValue<uint8>(Packet, Flags);
        AppendRawValue<uint8>(Packet, static_cast<uint8>(FragmentIndex));
        AppendRawValue<uint8>(Packet, static_cast<uint8>(FragmentCount));
        AppendRawValue<uint8>(Packet, 0);
        AppendRawValue<uint16>(Packet, FrameNameByteCount);
        AppendRawValue<uint32>(Packet, FrameSequence);
        AppendRawValue<uint32>(Packet, ResolvedDeviceID);
        AppendRawValue<uint32>(Packet, PointCount);
        AppendRawValue<uint32>(Packet, PayloadBytes);
        AppendRawValue<float>(Packet, DefaultPointSizeCm);
        AppendRawValue<float>(Packet, 0.0f);
        AppendRawValue<float>(Packet, 0.0f);
        AppendRawValue<float>(Packet, 0.0f);
        AppendRawValue<float>(Packet, 0.0f);
        AppendRawValue<float>(Packet, 0.0f);
        AppendRawValue<float>(Packet, 0.0f);
        AppendRawValue<float>(Packet, 1.0f);

        AppendRawBytes(Packet, FrameNameUtf8.Get(), FrameNameByteCount);

        for (int32 PointIndex = 0; PointIndex < PointsInFragment; ++PointIndex)
        {
            const FVector& WorldPointMeters = WorldPointsMeters[StartPointIndex + PointIndex];
            AppendRawValue<float>(Packet, WorldPointMeters.X);
            AppendRawValue<float>(Packet, WorldPointMeters.Y);
            AppendRawValue<float>(Packet, WorldPointMeters.Z);
            AppendRawValue<uint8>(Packet, PackedColor.R);
            AppendRawValue<uint8>(Packet, PackedColor.G);
            AppendRawValue<uint8>(Packet, PackedColor.B);
            AppendRawValue<uint8>(Packet, PackedColor.A);
        }

        OutPackets.Add(MoveTemp(Packet));
    }

    return OutPackets.Num() > 0;
}

uint32 UVirtualLidarComponent::ResolveDeviceID() const
{
    if (DeviceID > 0)
    {
        return static_cast<uint32>(DeviceID);
    }

    const FString StablePath = GetPathNameSafe(this);
    const uint32 Hash = FCrc::StrCrc32(*StablePath);
    return Hash == 0 ? 1u : Hash;
}

UPointCloudManager* UVirtualLidarComponent::ResolvePointCloudManager() const
{
    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return nullptr;
    }

    UHeteroSwarmGameInstance* GameInstance = Cast<UHeteroSwarmGameInstance>(World->GetGameInstance());
    if (GameInstance == nullptr)
    {
        return nullptr;
    }

    return GameInstance->GetPointCloudManager();
}

bool UVirtualLidarComponent::SendPacket(const TArray<uint8>& PacketBytes)
{
    if (SendSocket == nullptr)
    {
        UpdateStatus(TEXT("Lidar socket is not initialized"));
        return false;
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSubsystem == nullptr)
    {
        UpdateStatus(TEXT("Socket subsystem is unavailable"));
        return false;
    }

    FIPv4Address ParsedAddress;
    if (!FIPv4Address::Parse(RemoteHost, ParsedAddress))
    {
        UpdateStatus(FString::Printf(TEXT("Invalid lidar target host: %s"), *RemoteHost));
        return false;
    }

    TSharedRef<FInternetAddr> RemoteAddress = SocketSubsystem->CreateInternetAddr();
    RemoteAddress->SetIp(ParsedAddress.Value);
    RemoteAddress->SetPort(RemotePort);

    int32 BytesSent = 0;
    const bool bSent = SendSocket->SendTo(PacketBytes.GetData(), PacketBytes.Num(), BytesSent, *RemoteAddress);
    if (!bSent || BytesSent != PacketBytes.Num())
    {
        UpdateStatus(FString::Printf(TEXT("Failed to send lidar packet to %s:%d (bytes=%d/%d)"),
            *RemoteHost,
            RemotePort,
            BytesSent,
            PacketBytes.Num()));
        UE_LOG(LogVirtualLidar, Warning,
            TEXT("Failed to send lidar packet to %s:%d (bytes=%d/%d)"),
            *RemoteHost,
            RemotePort,
            BytesSent,
            PacketBytes.Num());
        return false;
    }
    return true;
}

void UVirtualLidarComponent::UpdateStatus(const FString& NewStatus)
{
    LastStatusMessage = NewStatus;
    UE_LOG(LogVirtualLidar, Verbose, TEXT("%s"), *NewStatus);
}

FVector UVirtualLidarComponent::ComputeTraceDirection(float HorizontalAngleDeg, float VerticalAngleDeg) const
{
    const FRotator LocalScanRotation(VerticalAngleDeg, HorizontalAngleDeg, 0.0f);
    const FVector LocalDirection = LocalScanRotation.RotateVector(FVector::ForwardVector);
    return GetComponentTransform().TransformVectorNoScale(LocalDirection).GetSafeNormal();
}



