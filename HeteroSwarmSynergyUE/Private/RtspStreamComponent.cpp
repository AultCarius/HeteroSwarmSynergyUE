#include "RtspStreamComponent.h"

#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogRTSPStream, Log, All);

// ============================================================
//  FRTSPSendThread
// ============================================================

FRTSPSendThread::FRTSPSendThread(const FString& InHost, int32 InPort, float InRetryInterval)
    : Host(InHost), Port(InPort), RetryInterval(InRetryInterval)
{
    Thread = FRunnableThread::Create(
        this,
        *FString::Printf(TEXT("RTSPSendThread_%d"), Port),
        0, TPri_Normal);
    UE_LOG(LogRTSPStream, Log, TEXT("[SendThread:%d] Created"), Port);
}

FRTSPSendThread::~FRTSPSendThread()
{
    Shutdown();
}

bool FRTSPSendThread::Init() { return true; }

uint32 FRTSPSendThread::Run()
{
    UE_LOG(LogRTSPStream, Log, TEXT("[SendThread:%d] Run() start"), Port);
    float RetryTimer = 0.0f;
    int32 TotalSent = 0;

    while (!bStopRequested)
    {
        if (!bSocketConnected)
        {
            if (RetryTimer <= 0.0f)
            {
                if (TryConnect())
                {
                    RetryTimer = 0.0f;
                }
                else
                {
                    RetryTimer = RetryInterval;
                    UE_LOG(LogRTSPStream, Warning,
                        TEXT("[SendThread:%d] Connect failed, retry in %.1fs"),
                        Port, RetryInterval);
                }
            }
            else
            {
                RetryTimer -= 0.05f;
                FPlatformProcess::Sleep(0.05f);
            }
            continue;
        }

        TSharedPtr<FFrameData> Frame;
        if (!FrameQueue.Dequeue(Frame))
        {
            FPlatformProcess::Sleep(0.001f);
            continue;
        }

        if (!Frame.IsValid() || Frame->Pixels.Num() == 0) { continue; }

        const int32 Bytes = Frame->Pixels.Num();
        if (!SendAll(Frame->Pixels.GetData(), Bytes))
        {
            UE_LOG(LogRTSPStream, Warning,
                TEXT("[SendThread:%d] SendAll failed after %d frames"), Port, TotalSent);
            Disconnect();
            RetryTimer = RetryInterval;
        }
        else
        {
            ++TotalSent;
        }
    }

    Disconnect();
    UE_LOG(LogRTSPStream, Log,
        TEXT("[SendThread:%d] Run() exit, total=%d"), Port, TotalSent);
    return 0;
}

void FRTSPSendThread::Stop() { bStopRequested = true; }

void FRTSPSendThread::EnqueueFrame(TSharedPtr<FFrameData> Frame)
{
    if (Frame.IsValid()) { FrameQueue.Enqueue(Frame); }
}

void FRTSPSendThread::Shutdown()
{
    if (Thread)
    {
        bStopRequested = true;
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
}

bool FRTSPSendThread::TryConnect()
{
    Disconnect();
    ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SS) { return false; }

    TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
    bool bValid = false;
    Addr->SetIp(*Host, bValid);
    Addr->SetPort(Port);
    if (!bValid) { return false; }

    Socket = SS->CreateSocket(NAME_Stream, TEXT("RTSPSendSocket"), false);
    if (!Socket) { return false; }

    Socket->SetNoDelay(true);
    Socket->SetNonBlocking(false);
    int32 OutSize = 0;
    Socket->SetSendBufferSize(8 * 1024 * 1024, OutSize);

    if (!Socket->Connect(*Addr))
    {
        SS->DestroySocket(Socket);
        Socket = nullptr;
        return false;
    }

    bSocketConnected = true;
    UE_LOG(LogRTSPStream, Log,
        TEXT("[SendThread:%d] Connected to %s:%d (bufsize=%d)"),
        Port, *Host, Port, OutSize);
    return true;
}

void FRTSPSendThread::Disconnect()
{
    bSocketConnected = false;
    if (Socket)
    {
        Socket->Close();
        if (ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
        {
            SS->DestroySocket(Socket);
        }
        Socket = nullptr;
    }
}

bool FRTSPSendThread::SendAll(const uint8* Data, int32 ByteCount)
{
    int32 Sent = 0;
    while (Sent < ByteCount)
    {
        int32 ThisTime = 0;
        if (!Socket->Send(Data + Sent, ByteCount - Sent, ThisTime) || ThisTime <= 0)
        {
            return false;
        }
        Sent += ThisTime;
    }
    return true;
}

// ============================================================
//  URTSPStreamComponent
// ============================================================

URTSPStreamComponent::URTSPStreamComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
    SceneCapture->SetupAttachment(this);
    SceneCapture->bCaptureEveryFrame = false;
    SceneCapture->bCaptureOnMovement = false;
    SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
    SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
}

FString URTSPStreamComponent::GetLogPrefix() const
{
    const AActor* Owner = GetOwner();
    return FString::Printf(TEXT("[%s/%s]"),
        Owner ? *Owner->GetName() : TEXT("?"),
        *GetName());
}

void URTSPStreamComponent::BeginPlay()
{
    Super::BeginPlay();

    // 初始化 Readback 槽（轻量，只分配对象，不产生 GPU 资源）
    for (int32 i = 0; i < ReadbackBufferCount; ++i)
    {
        ReadbackSlots[i].Readback = MakeUnique<FRHIGPUTextureReadback>(
            *FString::Printf(TEXT("RTSPReadback_%s_%d"), *GetName(), i));
        ReadbackSlots[i].bPending = false;
        ReadbackSlots[i].Width = 0;
        ReadbackSlots[i].Height = 0;
    }

    if (bAutoStartOnBeginPlay) { StartStreaming(); }
}

void URTSPStreamComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bStarted) { return; }

    // ── MediaMTX 守护 ────────────────────────────────────────
    if (bLaunchMediaMTX && !IsMediaMTXRunning())
    {
        UE_LOG(LogRTSPStream, Error,
            TEXT("%s MediaMTX stopped unexpectedly"), *GetLogPrefix());
        StopStreaming();
        return;
    }

    // ── FFmpeg 守护 ──────────────────────────────────────────
    if (!IsFFmpegRunning())
    {
        if (bRestartFFmpegOnUnexpectedExit)
        {
            UE_LOG(LogRTSPStream, Warning,
                TEXT("%s FFmpeg exited unexpectedly, restarting..."), *GetLogPrefix());
            StopFFmpeg();
            if (!StartFFmpeg()) { StopStreaming(); return; }
            ConnectDelayRemaining = InitialConnectDelay;
            return;
        }
        UE_LOG(LogRTSPStream, Error,
            TEXT("%s FFmpeg stopped"), *GetLogPrefix());
        StopStreaming();
        return;
    }

    // ── 轮询所有 Readback 槽（不受延迟倒计时影响）───────────
    for (int32 i = 0; i < ReadbackBufferCount; ++i)
    {
        FReadbackSlot& Slot = ReadbackSlots[i];
        if (!Slot.bPending || !Slot.Readback.IsValid()) { continue; }

        if (Slot.Readback->IsReady())
        {
            OnReadbackReady(i);
            Slot.bPending = false;
        }
    }

    // ── 初始延迟倒计时 ───────────────────────────────────────
    if (ConnectDelayRemaining > 0.0f)
    {
        ConnectDelayRemaining -= DeltaTime;
        return; // 延迟期间不采集新帧
    }

    // ── 帧率控制 ─────────────────────────────────────────────
    const float FrameInterval = (FPS > 0)
        ? 1.0f / static_cast<float>(FPS) : 1.0f / 30.0f;

    TimeAccumulator += DeltaTime;
    if (TimeAccumulator >= FrameInterval)
    {
        TimeAccumulator = FMath::Fmod(TimeAccumulator, FrameInterval);
        RequestFrameCapture();
    }

    // ── 定期诊断日志（仅 bVerboseLog 时输出）────────────────
    if (bVerboseLog)
    {
        DbgLogTimer += DeltaTime;
        if (DbgLogTimer >= 5.0f)
        {
            DbgLogTimer = 0.0f;
            UE_LOG(LogRTSPStream, Log,
                TEXT("%s Status | captured=%d sent=%d | FFmpeg=%s | TCPConnected=%s"),
                *GetLogPrefix(),
                DbgFramesCaptured, DbgFramesSent,
                IsFFmpegRunning() ? TEXT("OK") : TEXT("DEAD"),
                (SendThread && SendThread->IsConnected()) ? TEXT("YES") : TEXT("NO"));
        }
    }
}

void URTSPStreamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopStreaming();
    FlushRenderingCommands();
    for (int32 i = 0; i < ReadbackBufferCount; ++i)
    {
        ReadbackSlots[i].Readback.Reset();
    }
    FlushRenderingCommands();
    Super::EndPlay(EndPlayReason);
}

bool URTSPStreamComponent::StartStreaming()
{
    if (bStarted)
    {
        UE_LOG(LogRTSPStream, Warning,
            TEXT("%s Already streaming"), *GetLogPrefix());
        return true;
    }

    UE_LOG(LogRTSPStream, Log,
        TEXT("%s StartStreaming %dx%d@%dfps TCP=%s:%d RTSP=%s NVENC=%s"),
        *GetLogPrefix(),
        CaptureWidth, CaptureHeight, FPS,
        *TCPHost, TCPPort, *RTSPUrl,
        bUseNVENC ? TEXT("ON") : TEXT("OFF"));

    InitRenderTarget();
    if (!RenderTarget) { return false; }
    if (SceneCapture) { SceneCapture->TextureTarget = RenderTarget; }

    if (bLaunchMediaMTX)
    {
        if (!StartMediaMTX()) { return false; }
        FPlatformProcess::Sleep(MediaMTXStartupDelay);
    }

    SendThread = MakeUnique<FRTSPSendThread>(TCPHost, TCPPort, ConnectRetryInterval);

    if (!StartFFmpeg())
    {
        SendThread.Reset();
        StopMediaMTX();
        return false;
    }

    bStarted = true;
    TimeAccumulator = 0.0f;
    DbgFramesCaptured = 0;
    DbgFramesSent = 0;
    DbgLogTimer = 0.0f;
    ConnectDelayRemaining = FMath::Max(0.0f, InitialConnectDelay);

    UE_LOG(LogRTSPStream, Log,
        TEXT("%s Streaming started, capture begins in %.1fs"),
        *GetLogPrefix(), ConnectDelayRemaining);
    return true;
}

void URTSPStreamComponent::StopStreaming()
{
    if (!bStarted && !SendThread) { return; }

    bStarted = false;
    TimeAccumulator = 0.0f;
    ConnectDelayRemaining = 0.0f;

    if (SendThread) { SendThread->Shutdown(); SendThread.Reset(); }
    StopFFmpeg();
    StopMediaMTX();

    FlushRenderingCommands();

    for (int32 i = 0; i < ReadbackBufferCount; ++i)
    {
        ReadbackSlots[i].bPending = false;
    }

    UE_LOG(LogRTSPStream, Log,
        TEXT("%s StopStreaming complete | captured=%d sent=%d"),
        *GetLogPrefix(), DbgFramesCaptured, DbgFramesSent);
}

void URTSPStreamComponent::InitRenderTarget()
{
    CaptureWidth = FMath::Max(CaptureWidth, 64);
    CaptureHeight = FMath::Max(CaptureHeight, 64);

    if (!RenderTarget)
    {
        RenderTarget = NewObject<UTextureRenderTarget2D>(
            GetOwner(),
            *FString::Printf(TEXT("RTSP_RT_%s"), *GetName()));
        if (!RenderTarget)
        {
            UE_LOG(LogRTSPStream, Error,
                TEXT("%s Failed to create RenderTarget"), *GetLogPrefix());
            return;
        }
    }

    RenderTarget->ClearColor = FLinearColor::Black;
    RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    RenderTarget->InitCustomFormat(CaptureWidth, CaptureHeight, PF_B8G8R8A8, false);
    RenderTarget->UpdateResourceImmediate(true);

    UE_LOG(LogRTSPStream, Log,
        TEXT("%s RenderTarget %dx%d PF_B8G8R8A8"), *GetLogPrefix(), CaptureWidth, CaptureHeight);
}

// ── GPU Readback 提交 ─────────────────────────────────────────

void URTSPStreamComponent::RequestFrameCapture()
{
    if (!RenderTarget || !SceneCapture) { return; }

    // 找空闲槽
    int32 FreeSlot = -1;
    for (int32 i = 0; i < ReadbackBufferCount; ++i)
    {
        int32 Idx = (NextWriteSlot + i) % ReadbackBufferCount;
        if (!ReadbackSlots[Idx].bPending)
        {
            FreeSlot = Idx;
            break;
        }
    }

    if (FreeSlot < 0)
    {
        UE_LOG(LogRTSPStream, Warning,
            TEXT("%s All readback slots busy, dropping frame #%d"),
            *GetLogPrefix(), DbgFramesCaptured);
        return;
    }

    SceneCapture->CaptureScene();

    FTextureRenderTargetResource* RTResource =
        RenderTarget->GameThread_GetRenderTargetResource();
    if (!RTResource)
    {
        UE_LOG(LogRTSPStream, Warning,
            TEXT("%s RTResource null"), *GetLogPrefix());
        return;
    }

    // 传 FRHITexture 引用计数指针，保证渲染线程访问安全
    FTextureRHIRef TextureRHI = RTResource->GetRenderTargetTexture();
    if (!TextureRHI.IsValid())
    {
        UE_LOG(LogRTSPStream, Warning,
            TEXT("%s TextureRHI not valid yet, slot=%d"), *GetLogPrefix(), FreeSlot);
        return;
    }

    ReadbackSlots[FreeSlot].Width = CaptureWidth;
    ReadbackSlots[FreeSlot].Height = CaptureHeight;

    FRHIGPUTextureReadback* ReadbackPtr = ReadbackSlots[FreeSlot].Readback.Get();

    ENQUEUE_RENDER_COMMAND(RTSPEnqueueCopy)(
        [ReadbackPtr, TextureRHI](FRHICommandListImmediate& RHICmdList)
        {
            if (TextureRHI.IsValid())
            {
                ReadbackPtr->EnqueueCopy(RHICmdList, TextureRHI);
            }
        });

    ReadbackSlots[FreeSlot].bPending = true;
    NextWriteSlot = (FreeSlot + 1) % ReadbackBufferCount;
    ++DbgFramesCaptured;
}

// ── GPU Readback 读取 ─────────────────────────────────────────

void URTSPStreamComponent::OnReadbackReady(int32 SlotIndex)
{
    FReadbackSlot& Slot = ReadbackSlots[SlotIndex];

    void* RawData = nullptr;
    int32 RowPitchInPixels = 0;

    ENQUEUE_RENDER_COMMAND(RTSPLockTexture)(
        [&Slot, &RawData, &RowPitchInPixels](FRHICommandListImmediate& RHICmdList)
        {
            Slot.Readback->LockTexture(RHICmdList, RawData, RowPitchInPixels);
        });
    FlushRenderingCommands();

    if (!RawData) { Slot.bPending = false; return; }

    const int32 W = Slot.Width, H = Slot.Height;
    TSharedPtr<FFrameData> Frame = MakeShared<FFrameData>();
    Frame->Width = W;
    Frame->Height = H;
    Frame->Pixels.SetNumUninitialized(W * H * 4);

    const uint8* Src = static_cast<const uint8*>(RawData);
    uint8* Dst = Frame->Pixels.GetData();
    for (int32 Row = 0; Row < H; ++Row)
    {
        FMemory::Memcpy(Dst + Row * W * 4,
            Src + Row * RowPitchInPixels * 4,
            W * 4);
    }

    Slot.Readback->Unlock();
    Slot.bPending = false;

    ++DbgFramesSent;

    if (SendThread)
    {
        SendThread->EnqueueFrame(Frame);
    }
    else
    {
        UE_LOG(LogRTSPStream, Warning,
            TEXT("%s SendThread is null when a GPU readback frame becomes ready, dropping frame"),
            *GetLogPrefix());
    }
}

// ── FFmpeg 命令行 ─────────────────────────────────────────────

FString URTSPStreamComponent::BuildFFmpegCommandLine() const
{
    const FString BR = FString::Printf(TEXT("%dM"), NVENCBitrateMbps);

    FString EncArgs;
    if (bUseNVENC)
    {
        EncArgs = FString::Printf(
            TEXT("-c:v h264_nvenc -preset llhq -tune ll -bf 0 -rc cbr -b:v %s -bufsize %s -zerolatency 1 "),
            *BR, *BR);
    }
    else
    {
        EncArgs = TEXT("-c:v libx264 -preset ultrafast -tune zerolatency ");
    }

    return FString::Printf(
        TEXT("-loglevel warning ")
        TEXT("-fflags nobuffer -flags low_delay ")
        TEXT("-f rawvideo -pix_fmt bgra ")
        TEXT("-video_size %dx%d -framerate %d ")
        TEXT("-i tcp://%s:%d?listen ")
        TEXT("-an -pix_fmt yuv420p ")
        TEXT("%s")
        TEXT("-g %d ")
        TEXT("-f rtsp -rtsp_transport tcp -muxdelay 0 ")
        TEXT("\"%s\""),
        CaptureWidth, CaptureHeight, FPS,
        *TCPHost, TCPPort,
        *EncArgs,
        FPS,
        *RTSPUrl);
}

// ── MediaMTX ─────────────────────────────────────────────────

bool URTSPStreamComponent::StartMediaMTX()
{
    if (!bLaunchMediaMTX) { return true; }
    if (IsMediaMTXRunning()) { return true; }
    if (!FPaths::FileExists(MediaMTXPath))
    {
        UE_LOG(LogRTSPStream, Error,
            TEXT("%s MediaMTX not found: %s"), *GetLogPrefix(), *MediaMTXPath);
        return false;
    }

    uint32 PID = 0;
    const FString WorkDir = FPaths::GetPath(MediaMTXPath);
    MediaMTXProc = FPlatformProcess::CreateProc(
        *MediaMTXPath, *MediaMTXArguments, true, false, false, &PID, 0,
        WorkDir.IsEmpty() ? nullptr : *WorkDir, nullptr);

    if (!MediaMTXProc.IsValid())
    {
        UE_LOG(LogRTSPStream, Error,
            TEXT("%s Failed to launch MediaMTX"), *GetLogPrefix());
        return false;
    }

    bOwnsMediaMTXProcess = true;
    UE_LOG(LogRTSPStream, Log,
        TEXT("%s MediaMTX launched PID=%u"), *GetLogPrefix(), PID);
    return true;
}

void URTSPStreamComponent::StopMediaMTX()
{
    if (!bOwnsMediaMTXProcess) { return; }
    if (MediaMTXProc.IsValid())
    {
        FPlatformProcess::TerminateProc(MediaMTXProc, true);
        FPlatformProcess::CloseProc(MediaMTXProc);
        MediaMTXProc.Reset();
    }
    bOwnsMediaMTXProcess = false;
}

bool URTSPStreamComponent::IsMediaMTXRunning()
{
    return MediaMTXProc.IsValid() && FPlatformProcess::IsProcRunning(MediaMTXProc);
}

// ── FFmpeg ───────────────────────────────────────────────────

bool URTSPStreamComponent::StartFFmpeg()
{
    if (!FPaths::FileExists(FFmpegPath))
    {
        UE_LOG(LogRTSPStream, Error,
            TEXT("%s FFmpeg not found: %s"), *GetLogPrefix(), *FFmpegPath);
        return false;
    }
    if (IsFFmpegRunning()) { return true; }

    const FString Cmd = BuildFFmpegCommandLine();
    const FString WorkDir = FPaths::GetPath(FFmpegPath);
    uint32 PID = 0;

    UE_LOG(LogRTSPStream, Log,
        TEXT("%s Launching FFmpeg:\n  %s %s"), *GetLogPrefix(), *FFmpegPath, *Cmd);

    FFmpegProc = FPlatformProcess::CreateProc(
        *FFmpegPath, *Cmd, true, false, false, &PID, 0,
        WorkDir.IsEmpty() ? nullptr : *WorkDir, nullptr);

    if (!FFmpegProc.IsValid())
    {
        UE_LOG(LogRTSPStream, Error,
            TEXT("%s Failed to launch FFmpeg"), *GetLogPrefix());
        return false;
    }

    UE_LOG(LogRTSPStream, Log,
        TEXT("%s FFmpeg launched PID=%u"), *GetLogPrefix(), PID);
    return true;
}

void URTSPStreamComponent::StopFFmpeg()
{
    if (FFmpegProc.IsValid())
    {
        FPlatformProcess::TerminateProc(FFmpegProc, true);
        FPlatformProcess::CloseProc(FFmpegProc);
        FFmpegProc.Reset();
        UE_LOG(LogRTSPStream, Log,
            TEXT("%s FFmpeg stopped"), *GetLogPrefix());
    }
}

bool URTSPStreamComponent::IsFFmpegRunning()
{
    return FFmpegProc.IsValid() && FPlatformProcess::IsProcRunning(FFmpegProc);
}
