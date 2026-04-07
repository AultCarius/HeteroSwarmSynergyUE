#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "DeviceFirstPersonCamera.generated.h"

class URTSPStreamComponent;

/**
 * 设备第一人称摄像头组件类型枚举
 */
UENUM(BlueprintType)
enum class EDeviceCameraType : uint8
{
    /** 无人机机头摄像头(前向) */
    UAV_Front UMETA(DisplayName = "UAV Front Camera"),

    /** 无人机云台摄像头(可俯仰) */
    UAV_Gimbal UMETA(DisplayName = "UAV Gimbal Camera"),

    /** 无人车前向摄像头 */
    UGV_Front UMETA(DisplayName = "UGV Front Camera"),

    /** 机器狗头部摄像头 */
    RobotDog_Head UMETA(DisplayName = "Robot Dog Head Camera"),

    /** 自定义摄像头 */
    Custom UMETA(DisplayName = "Custom Camera")
};

/**
 * 设备第一人称摄像头组件
 *
 * 功能特性:
 * - 添加到设备Actor上提供第一人称视角
 * - 支持多种设备类型预设配置
 * - 可配置的相机位置偏移和旋转
 * - 支持云台效果(平滑旋转)
 * - 可选的画面后处理效果
 * - 可选与 URTSPStreamComponent 自动绑定，实现“相机即推流视角”
 *
 * 使用方式:
 * 1. 在设备Actor蓝图中添加此组件
 * 2. 若需要 RTSP 推流，再在同一个 Actor 上额外添加一个 RtspStreamComponent
 * 3. 可开启 bAutoAttachRTSPStreamToCamera，让 RtspStreamComponent 自动跟随本相机
 * 4. 调用 StartVideoStream / StopVideoStream 即可控制推流
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Camera),
    meta = (BlueprintSpawnableComponent, DisplayName = "Device First Person Camera"))
    class HETEROSWARMSYNERGYUE_API UDeviceFirstPersonCamera : public UCameraComponent
{
    GENERATED_BODY()

public:
    UDeviceFirstPersonCamera();

    /** 组件初始化 */
    virtual void BeginPlay() override;

    /** 每帧更新 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // ========== 摄像头类型配置 ==========

    /** 摄像头类型(影响默认设置) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Type")
    EDeviceCameraType CameraType = EDeviceCameraType::UAV_Front;

    /** 是否在BeginPlay时自动应用类型预设 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Type")
    bool bAutoApplyTypePreset = true;

    // ========== 位置和旋转配置 ==========

    /** 相机位置偏移(相对于父Actor,局部空间) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Transform")
    FVector CameraOffset = FVector(50.0f, 0.0f, 0.0f);

    /** 相机旋转偏移(相对于父Actor,局部空间) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Transform")
    FRotator CameraRotationOffset = FRotator::ZeroRotator;

    // ========== 云台效果配置 ==========

    /** 是否启用云台效果(平滑跟随) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Gimbal")
    bool bEnableGimbalEffect = false;

    /** 云台旋转平滑速度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Gimbal",
        meta = (EditCondition = "bEnableGimbalEffect", ClampMin = "1.0", ClampMax = "30.0"))
    float GimbalSmoothSpeed = 5.0f;

    /** 云台Pitch角度限制(最小值) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Gimbal",
        meta = (EditCondition = "bEnableGimbalEffect", ClampMin = "-90.0", ClampMax = "0.0"))
    float GimbalMinPitch = -90.0f;

    /** 云台Pitch角度限制(最大值) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Gimbal",
        meta = (EditCondition = "bEnableGimbalEffect", ClampMin = "0.0", ClampMax = "90.0"))
    float GimbalMaxPitch = 30.0f;

    // ========== 相机视野配置 ==========

    /** 相机视野角度(FOV) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|View",
        meta = (ClampMin = "30.0", ClampMax = "120.0"))
    float CameraFOV = 90.0f;

    /** 近裁剪面距离(当前仅保留为配置项，若项目后续启用自定义近裁剪可在此接入) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|View",
        meta = (ClampMin = "1.0", ClampMax = "100.0"))
    float NearClipPlane = 10.0f;

    // ========== 视频流配置 ==========

    /** 是否启用视频流推送 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Video Stream")
    bool bEnableVideoStream = false;

    /** 视频流目标分辨率宽度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Video Stream",
        meta = (EditCondition = "bEnableVideoStream", ClampMin = "320", ClampMax = "1920"))
    int32 StreamResolutionWidth = 1280;

    /** 视频流目标分辨率高度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Video Stream",
        meta = (EditCondition = "bEnableVideoStream", ClampMin = "240", ClampMax = "1080"))
    int32 StreamResolutionHeight = 720;

    /** 视频流目标帧率 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Video Stream",
        meta = (EditCondition = "bEnableVideoStream", ClampMin = "15", ClampMax = "60"))
    int32 StreamFrameRate = 30;

    /** 指定要绑定的 RtspStreamComponent 名称；为空时自动查找本 Actor 上第一个该类型组件 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Video Stream")
    FName RTSPStreamComponentName = NAME_None;

    /** 是否在 BeginPlay 时自动把 RtspStreamComponent 挂到本摄像机下 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Video Stream")
    bool bAutoAttachRTSPStreamToCamera = true;

    // ========== 后处理效果 ==========

    /** 是否启用自定义后处理 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Post Process")
    bool bEnableCustomPostProcess = false;

    /** 色调映射(用于HDR效果) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Post Process",
        meta = (EditCondition = "bEnableCustomPostProcess"))
    float TonemapperExposure = 1.0f;

    /** 晕影强度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Camera|Post Process",
        meta = (EditCondition = "bEnableCustomPostProcess", ClampMin = "0.0", ClampMax = "1.0"))
    float VignetteIntensity = 0.2f;

    // ========== 蓝图事件 ==========

    UFUNCTION(BlueprintImplementableEvent, Category = "Device Camera Events",
        meta = (DisplayName = "On Camera Activated"))
    void OnCameraActivated();

    UFUNCTION(BlueprintImplementableEvent, Category = "Device Camera Events",
        meta = (DisplayName = "On Camera Deactivated"))
    void OnCameraDeactivated();

    UFUNCTION(BlueprintImplementableEvent, Category = "Device Camera Events",
        meta = (DisplayName = "On Video Stream Started"))
    void OnVideoStreamStarted();

    UFUNCTION(BlueprintImplementableEvent, Category = "Device Camera Events",
        meta = (DisplayName = "On Video Stream Stopped"))
    void OnVideoStreamStopped();

    // ========== 蓝图接口 ==========

    UFUNCTION(BlueprintCallable, Category = "Device Camera|Control")
    void ApplyCameraTypePreset();

    UFUNCTION(BlueprintCallable, Category = "Device Camera|Control")
    void SetCameraType(EDeviceCameraType NewType);

    UFUNCTION(BlueprintCallable, Category = "Device Camera|Control")
    void SetCameraOffset(FVector NewOffset);

    UFUNCTION(BlueprintCallable, Category = "Device Camera|Control")
    void SetCameraRotationOffset(FRotator NewRotationOffset);

    UFUNCTION(BlueprintCallable, Category = "Device Camera|Control")
    void SetGimbalPitch(float Pitch);

    UFUNCTION(BlueprintPure, Category = "Device Camera|Control")
    float GetGimbalPitch() const { return CurrentGimbalPitch; }

    UFUNCTION(BlueprintCallable, Category = "Device Camera|Video Stream")
    void StartVideoStream();

    UFUNCTION(BlueprintCallable, Category = "Device Camera|Video Stream")
    void StopVideoStream();

    UFUNCTION(BlueprintPure, Category = "Device Camera|Video Stream")
    bool IsStreamingVideo() const { return bIsStreamingVideo; }

    UFUNCTION(BlueprintPure, Category = "Device Camera|Info")
    EDeviceCameraType GetCameraType() const { return CameraType; }

    UFUNCTION(BlueprintPure, Category = "Device Camera|Info")
    AActor* GetOwnerDevice() const;

protected:
    /** 当前云台Pitch角度 */
    float CurrentGimbalPitch;

    /** 目标云台Pitch角度 */
    float TargetGimbalPitch;

    /** 是否正在推送视频流 */
    bool bIsStreamingVideo;

    /** 缓存的流组件 */
    UPROPERTY(Transient)
    URTSPStreamComponent* CachedRTSPStreamComponent;

    void UpdateGimbalRotation(float DeltaTime);
    void UpdateCameraTransform();
    void ApplyPostProcessSettings();

    float GetPresetFOV(EDeviceCameraType Type) const;
    FVector GetPresetOffset(EDeviceCameraType Type) const;
    FRotator GetPresetRotationOffset(EDeviceCameraType Type) const;

    URTSPStreamComponent* FindRTSPStreamComponent();
    void SyncRTSPStreamSettings();
};
