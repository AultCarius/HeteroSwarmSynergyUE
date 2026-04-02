#include "RTSPCameraActor.h"

#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogRTSPCameraActor, Log, All);

ARTSPCameraActor::ARTSPCameraActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
    SceneCapture->SetupAttachment(RootComponent);

    SceneCapture->bCaptureEveryFrame = false;
    SceneCapture->bCaptureOnMovement = false;
    SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
}

void ARTSPCameraActor::BeginPlay()
{
    Super::BeginPlay();

    InitRenderTarget();

    if (!RenderTarget)
    {
        UE_LOG(LogRTSPCameraActor, Error, TEXT("RTSPCameraActor BeginPlay failed: RenderTarget is null"));
        return;
    }

    if (SceneCapture)
    {
        SceneCapture->TextureTarget = RenderTarget;
    }

    if (bAutoStartOnBeginPlay)
    {
        const bool bFFmpegStarted = StartFFmpeg();
        if (!bFFmpegStarted)
        {
            UE_LOG(LogRTSPCameraActor, Error, TEXT("Failed to start FFmpeg for actor %s"), *GetName());
            return;
        }

        bStarted = true;

        // 给 FFmpeg 一点启动监听时间
        ConnectRetryAccumulator = -InitialConnectDelay;

        UE_LOG(LogRTSPCameraActor, Log,
            TEXT("RTSP camera actor started: %s (RTSP=%s TCP=%s:%d %dx%d@%d)"),
            *GetName(),
            *RTSPUrl,
            *TCPHost,
            TCPPort,
            CaptureWidth,
            CaptureHeight,
            FPS);
    }
}

void ARTSPCameraActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bStarted)
    {
        return;
    }

    const float FrameInterval = (FPS > 0) ? (1.0f / static_cast<float>(FPS)) : (1.0f / 30.0f);

    if (!bSocketConnected)
    {
        ConnectRetryAccumulator += DeltaSeconds;
        if (ConnectRetryAccumulator >= ConnectRetryInterval)
        {
            ConnectRetryAccumulator = 0.0f;
            ConnectToFFmpegSocket();
        }
    }

    TimeAccumulator += DeltaSeconds;
    if (TimeAccumulator >= FrameInterval)
    {
        TimeAccumulator -= FrameInterval;

        if (bSocketConnected)
        {
            CaptureAndSendFrame();
        }
    }
}

void ARTSPCameraActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DisconnectSocket();
    StopFFmpeg();
    bStarted = false;

    Super::EndPlay(EndPlayReason);
}

void ARTSPCameraActor::InitRenderTarget()
{
    if (CaptureWidth <= 0)
    {
        CaptureWidth = 1280;
    }

    if (CaptureHeight <= 0)
    {
        CaptureHeight = 720;
    }

    if (RenderTarget)
    {
        RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
        RenderTarget->InitAutoFormat(CaptureWidth, CaptureHeight);
        RenderTarget->UpdateResourceImmediate(true);

        if (bVerboseLog)
        {
            UE_LOG(LogRTSPCameraActor, Log,
                TEXT("Using existing RenderTarget: %dx%d"),
                CaptureWidth, CaptureHeight);
        }

        return;
    }

    RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RTSP_RenderTarget"));
    if (!RenderTarget)
    {
        UE_LOG(LogRTSPCameraActor, Error, TEXT("Failed to create render target"));
        return;
    }

    RenderTarget->ClearColor = FLinearColor::Black;
    RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    RenderTarget->InitAutoFormat(CaptureWidth, CaptureHeight);
    RenderTarget->UpdateResourceImmediate(true);

    if (bVerboseLog)
    {
        UE_LOG(LogRTSPCameraActor, Log,
            TEXT("RenderTarget initialized: %dx%d"),
            CaptureWidth, CaptureHeight);
    }
}

bool ARTSPCameraActor::StartFFmpeg()
{
    if (!FPaths::FileExists(FFmpegPath))
    {
        UE_LOG(LogRTSPCameraActor, Error, TEXT("FFmpeg executable not found: %s"), *FFmpegPath);
        return false;
    }

    if (IsFFmpegRunning())
    {
        UE_LOG(LogRTSPCameraActor, Warning, TEXT("FFmpeg already running for actor %s"), *GetName());
        return true;
    }

    const FString CommandLine = BuildFFmpegCommandLine();

    if (bVerboseLog)
    {
        UE_LOG(LogRTSPCameraActor, Log, TEXT("Launching FFmpeg: %s %s"), *FFmpegPath, *CommandLine);
    }

    uint32 ProcessID = 0;
    FFmpegProc = FPlatformProcess::CreateProc(
        *FFmpegPath,
        *CommandLine,
        true,   // bLaunchDetached
        false,  // bLaunchHidden
        false,  // bLaunchReallyHidden
        &ProcessID,
        0,
        nullptr,
        nullptr
    );

    if (!FFmpegProc.IsValid())
    {
        UE_LOG(LogRTSPCameraActor, Error, TEXT("Failed to launch FFmpeg process"));
        return false;
    }

    UE_LOG(LogRTSPCameraActor, Log,
        TEXT("FFmpeg launched successfully. PID=%u"),
        ProcessID);

    return true;
}

void ARTSPCameraActor::StopFFmpeg()
{
    if (FFmpegProc.IsValid())
    {
        UE_LOG(LogRTSPCameraActor, Log, TEXT("Stopping FFmpeg process for actor %s"), *GetName());

        FPlatformProcess::TerminateProc(FFmpegProc, true);
        FPlatformProcess::CloseProc(FFmpegProc);
        FFmpegProc.Reset();
    }
}

void ARTSPCameraActor::CaptureAndSendFrame()
{
    if (!RenderTarget || !FrameSocket || !bSocketConnected || !SceneCapture)
    {
        return;
    }

    SceneCapture->CaptureScene();

    FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!RTResource)
    {
        UE_LOG(LogRTSPCameraActor, Warning, TEXT("RenderTarget resource is null"));
        return;
    }

    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(CaptureWidth * CaptureHeight);

    const bool bReadOk = RTResource->ReadPixels(Pixels);
    if (!bReadOk)
    {
        UE_LOG(LogRTSPCameraActor, Warning, TEXT("ReadPixels failed"));
        return;
    }

    const int32 ByteCount = Pixels.Num() * sizeof(FColor);
    const uint8* DataPtr = reinterpret_cast<const uint8*>(Pixels.GetData());

    int32 TotalSent = 0;
    while (TotalSent < ByteCount)
    {
        int32 SentThisTime = 0;
        const bool bSent = FrameSocket->Send(
            DataPtr + TotalSent,
            ByteCount - TotalSent,
            SentThisTime
        );

        if (!bSent || SentThisTime <= 0)
        {
            UE_LOG(LogRTSPCameraActor, Warning,
                TEXT("Socket send failed, disconnecting. Sent=%d/%d"),
                TotalSent, ByteCount);

            DisconnectSocket();
            return;
        }

        TotalSent += SentThisTime;
    }

    if (bVerboseLog)
    {
        UE_LOG(LogRTSPCameraActor, Verbose,
            TEXT("Frame sent successfully: %d bytes"),
            ByteCount);
    }
}

bool ARTSPCameraActor::ConnectToFFmpegSocket()
{
    DisconnectSocket();

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogRTSPCameraActor, Error, TEXT("Socket subsystem not available"));
        return false;
    }

    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();

    bool bIsValidIP = false;
    Addr->SetIp(*TCPHost, bIsValidIP);
    Addr->SetPort(TCPPort);

    if (!bIsValidIP)
    {
        UE_LOG(LogRTSPCameraActor, Error, TEXT("Invalid TCPHost: %s"), *TCPHost);
        return false;
    }

    FrameSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("RTSPCameraActorSocket"), false);
    if (!FrameSocket)
    {
        UE_LOG(LogRTSPCameraActor, Error, TEXT("Failed to create TCP socket"));
        return false;
    }

    FrameSocket->SetNoDelay(true);

    int32 NewSize = 4 * 1024 * 1024;
    int32 OutSize = 0;
    FrameSocket->SetSendBufferSize(NewSize, OutSize);

    FrameSocket->SetNonBlocking(false);

    bSocketConnected = FrameSocket->Connect(*Addr);

    if (!bSocketConnected)
    {
        UE_LOG(LogRTSPCameraActor, Warning,
            TEXT("ConnectToFFmpegSocket failed: %s:%d"),
            *TCPHost, TCPPort);

        DisconnectSocket();
        return false;
    }

    UE_LOG(LogRTSPCameraActor, Log,
        TEXT("Connected to FFmpeg TCP input: %s:%d"),
        *TCPHost, TCPPort);

    return true;
}

void ARTSPCameraActor::DisconnectSocket()
{
    bSocketConnected = false;

    if (FrameSocket)
    {
        FrameSocket->Close();

        ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (SocketSubsystem)
        {
            SocketSubsystem->DestroySocket(FrameSocket);
        }

        FrameSocket = nullptr;
    }
}

bool ARTSPCameraActor::IsFFmpegRunning()
{
    return FFmpegProc.IsValid() && FPlatformProcess::IsProcRunning(FFmpegProc);
}

FString ARTSPCameraActor::BuildFFmpegCommandLine() const
{
    // UE ReadPixels() 读出来的是 BGRA8
    // FFmpeg 作为 TCP 服务端监听，接收原始 BGRA 视频流，再编码推到 RTSP
    return FString::Printf(
        TEXT("-loglevel warning ")
        TEXT("-f rawvideo ")
        TEXT("-pix_fmt bgra ")
        TEXT("-video_size %dx%d ")
        TEXT("-framerate %d ")
        TEXT("-listen 1 ")
        TEXT("-i tcp://%s:%d ")
        TEXT("-an ")
        TEXT("-c:v libx264 ")
        TEXT("-preset ultrafast ")
        TEXT("-tune zerolatency ")
        TEXT("-pix_fmt yuv420p ")
        TEXT("-f rtsp ")
        TEXT("-rtsp_transport tcp ")
        TEXT("\"%s\""),
        CaptureWidth,
        CaptureHeight,
        FPS,
        *TCPHost,
        TCPPort,
        *RTSPUrl
    );
}