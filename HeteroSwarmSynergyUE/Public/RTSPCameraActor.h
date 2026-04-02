#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "RTSPCameraActor.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class FSocket;

UCLASS()
class HETEROSWARMSYNERGYUE_API ARTSPCameraActor : public AActor
{
    GENERATED_BODY()

public:
    ARTSPCameraActor();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    /** 场景捕获组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTSP")
    USceneCaptureComponent2D* SceneCapture;

    /** 输出渲染目标，可为空；为空时自动创建 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    UTextureRenderTarget2D* RenderTarget;

    /** 捕获宽度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    int32 CaptureWidth = 1280;

    /** 捕获高度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    int32 CaptureHeight = 720;

    /** 发送帧率 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    int32 FPS = 30;

    /** ffmpeg 可执行文件路径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    FString FFmpegPath = TEXT("D:/Program Files/ffmpeg-master-latest-win64-gpl-shared/bin/ffmpeg.exe");

    /** 输出 RTSP 地址 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    FString RTSPUrl = TEXT("rtsp://127.0.0.1:8554/uecam");

    /** ffmpeg 监听 TCP Host */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    FString TCPHost = TEXT("127.0.0.1");

    /** ffmpeg 监听 TCP Port */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    int32 TCPPort = 9000;

    /** ffmpeg 启动后，UE 侧等待多久再尝试连接（秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    float InitialConnectDelay = 1.0f;

    /** 断开后重连间隔（秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    float ConnectRetryInterval = 1.0f;

    /** 是否在 BeginPlay 自动启动 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    bool bAutoStartOnBeginPlay = true;

    /** 是否输出详细日志 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTSP")
    bool bVerboseLog = true;

private:
    void InitRenderTarget();
    bool StartFFmpeg();
    void StopFFmpeg();
    void CaptureAndSendFrame();
    bool ConnectToFFmpegSocket();
    void DisconnectSocket();

    bool IsFFmpegRunning();
    FString BuildFFmpegCommandLine() const;

private:
    FProcHandle FFmpegProc;
    FSocket* FrameSocket = nullptr;

    float TimeAccumulator = 0.0f;
    float ConnectRetryAccumulator = 0.0f;
    bool bSocketConnected = false;
    bool bStarted = false;
};