#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIGPUReadback.h"
#include "RtspStreamComponent.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class FSocket;

// ============================================================
//  帧数据容器
// ============================================================
struct FFrameData
{
    TArray<uint8> Pixels; // BGRA raw bytes，紧密排列无 padding
    int32 Width = 0;
    int32 Height = 0;
};

// ============================================================
//  独立发送线程
// ============================================================
class FRTSPSendThread : public FRunnable
{
public:
    FRTSPSendThread(const FString& InHost, int32 InPort, float InRetryInterval);
    virtual ~FRTSPSendThread();

    virtual bool   Init() override;
    virtual uint32 Run()  override;
    virtual void   Stop() override;

    void EnqueueFrame(TSharedPtr<FFrameData> Frame);
    void Shutdown();
    bool IsConnected() const { return bSocketConnected; }

private:
    bool TryConnect();
    void Disconnect();
    bool SendAll(const uint8* Data, int32 ByteCount);

    FString Host;
    int32   Port;
    float   RetryInterval;

    FThreadSafeBool bStopRequested{ false };
    FThreadSafeBool bSocketConnected{ false };

    TQueue<TSharedPtr<FFrameData>, EQueueMode::Spsc> FrameQueue;

    FSocket* Socket = nullptr;
    FRunnableThread* Thread = nullptr;
};

// ============================================================
//  URTSPStreamComponent
//
//  用法：挂载到无人机 Actor，替换原有摄像头 SceneComponent。
//  默认不推流，不产生 GPU / 线程开销。
//  调用 StartStreaming() 后开始采集并推流。
// ============================================================
UCLASS(ClassGroup = (RTSP), meta = (BlueprintSpawnableComponent))
class HETEROSWARMSYNERGYUE_API URTSPStreamComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    URTSPStreamComponent();

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // ── 内嵌 SceneCapture，作为子组件挂载在本组件下 ──────────
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTSP")
    USceneCaptureComponent2D* SceneCapture;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    UTextureRenderTarget2D* RenderTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    int32 CaptureWidth = 1280;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    int32 CaptureHeight = 720;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    int32 FPS = 30;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    FString FFmpegPath = TEXT("D:/Environment/ffmpeg/bin/ffmpeg.exe");

    /** 推流目标，多组件时每个填不同路径，如 /cam1  /cam2 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    FString RTSPUrl = TEXT("rtsp://127.0.0.1:8554/uecam");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    FString TCPHost = TEXT("127.0.0.1");

    /** 多组件时每个使用不同端口，如 9001 / 9002 / 9003 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    int32 TCPPort = 9001;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    float InitialConnectDelay = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    float ConnectRetryInterval = 1.0f;

    /** 默认关闭：外部显式调用 StartStreaming() 才启动 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    bool bAutoStartOnBeginPlay = false;

    /** 开启后每 5 秒输出一次采集 / 发送统计 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    bool bVerboseLog = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP|Server")
    bool bLaunchMediaMTX = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP|Server")
    FString MediaMTXPath = TEXT("D:/Environment/mediamtx/mediamtx.exe");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP|Server")
    FString MediaMTXArguments;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP|Server")
    float MediaMTXStartupDelay = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP|Runtime")
    bool bRestartFFmpegOnUnexpectedExit = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP|Runtime")
    bool bUseNVENC = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP|Runtime",
        meta = (EditCondition = "bUseNVENC", ClampMin = "1", ClampMax = "50"))
    int32 NVENCBitrateMbps = 6;

    UFUNCTION(BlueprintCallable, Category = "RTSP")
    bool StartStreaming();

    UFUNCTION(BlueprintCallable, Category = "RTSP")
    void StopStreaming();

    UFUNCTION(BlueprintPure, Category = "RTSP")
    bool IsStreaming() const { return bStarted; }

private:
    void    InitRenderTarget();
    FString BuildFFmpegCommandLine() const;

    bool StartMediaMTX();
    void StopMediaMTX();
    bool IsMediaMTXRunning();

    bool StartFFmpeg();
    void StopFFmpeg();
    bool IsFFmpegRunning();

    void RequestFrameCapture();
    void OnReadbackReady(int32 SlotIndex);

    FString GetLogPrefix() const;

private:
    FProcHandle MediaMTXProc;
    FProcHandle FFmpegProc;
    bool bOwnsMediaMTXProcess = false;

    // ── 3 槽 Readback，轮询方式 ──────────────────────────────
    static constexpr int32 ReadbackBufferCount = 3;

    struct FReadbackSlot
    {
        TUniquePtr<FRHIGPUTextureReadback> Readback;
        bool  bPending = false;
        int32 Width = 0;
        int32 Height = 0;
    };

    FReadbackSlot ReadbackSlots[ReadbackBufferCount];
    int32 NextWriteSlot = 0;

    float TimeAccumulator = 0.0f;
    float ConnectDelayRemaining = 0.0f;
    bool  bStarted = false;

    int32 DbgFramesCaptured = 0;
    int32 DbgFramesSent = 0;
    float DbgLogTimer = 0.0f;

    TUniquePtr<FRTSPSendThread> SendThread;
};
