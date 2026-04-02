// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 摄像头系统 - 设备第一人称视角
// 作者: Carius
// 日期: 2026-02-26

#include "DeviceFirstPersonCamera.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeviceCamera, Log, All);

// ========== 构造函数 ==========

UDeviceFirstPersonCamera::UDeviceFirstPersonCamera()
    : CurrentGimbalPitch(0.0f)
    , TargetGimbalPitch(0.0f)
    , bIsStreamingVideo(false)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    // 默认不使用Pawn控制旋转
    //bUsePawnControlRotation = false;

    // 设置默认FOV
    FieldOfView = CameraFOV;
}

// ========== 生命周期 ==========

void UDeviceFirstPersonCamera::BeginPlay()
{
    Super::BeginPlay();

    // 如果启用自动应用预设
    if (bAutoApplyTypePreset)
    {
        ApplyCameraTypePreset();
    }

    // 应用相机设置
    FieldOfView = CameraFOV;

    // 应用后处理设置
    if (bEnableCustomPostProcess)
    {
        ApplyPostProcessSettings();
    }

    // 如果启用视频流
    if (bEnableVideoStream)
    {
        StartVideoStream();
    }

    UE_LOG(LogDeviceCamera, Log, TEXT("DeviceFirstPersonCamera initialized on %s - Type: %d"),
        *GetOwner()->GetName(), (int32)CameraType);
}

void UDeviceFirstPersonCamera::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // 更新云台旋转
    if (bEnableGimbalEffect)
    {
        UpdateGimbalRotation(DeltaTime);
    }

    // 更新相机变换
    UpdateCameraTransform();
}

// ========== 内部函数 ==========

void UDeviceFirstPersonCamera::UpdateGimbalRotation(float DeltaTime)
{
    // 平滑插值到目标Pitch
    CurrentGimbalPitch = FMath::FInterpTo(
        CurrentGimbalPitch,
        TargetGimbalPitch,
        DeltaTime,
        GimbalSmoothSpeed
    );

    // 限制Pitch范围
    CurrentGimbalPitch = FMath::Clamp(CurrentGimbalPitch, GimbalMinPitch, GimbalMaxPitch);
}

void UDeviceFirstPersonCamera::UpdateCameraTransform()
{
    // 设置相对位置
    SetRelativeLocation(CameraOffset);

    // 计算最终旋转(基础旋转偏移 + 云台旋转)
    FRotator FinalRotation = CameraRotationOffset;

    if (bEnableGimbalEffect)
    {
        FinalRotation.Pitch += CurrentGimbalPitch;
    }

    SetRelativeRotation(FinalRotation);
}

void UDeviceFirstPersonCamera::ApplyPostProcessSettings()
{
    // 启用后处理设置覆盖
    PostProcessSettings.bOverride_AutoExposureBias = true;
    PostProcessSettings.AutoExposureBias = TonemapperExposure;

    PostProcessSettings.bOverride_VignetteIntensity = true;
    PostProcessSettings.VignetteIntensity = VignetteIntensity;

    UE_LOG(LogDeviceCamera, Log, TEXT("Post process settings applied"));
}

float UDeviceFirstPersonCamera::GetPresetFOV(EDeviceCameraType Type) const
{
    switch (Type)
    {
    case EDeviceCameraType::UAV_Front:
        return 90.0f;  // 无人机前向摄像头,较宽视野

    case EDeviceCameraType::UAV_Gimbal:
        return 80.0f;  // 云台摄像头,稍窄以便观察细节

    case EDeviceCameraType::UGV_Front:
        return 100.0f;  // 无人车前向,需要更宽视野

    case EDeviceCameraType::RobotDog_Head:
        return 95.0f;  // 机器狗头部摄像头

    case EDeviceCameraType::Custom:
    default:
        return 90.0f;  // 默认值
    }
}

FVector UDeviceFirstPersonCamera::GetPresetOffset(EDeviceCameraType Type) const
{
    switch (Type)
    {
    case EDeviceCameraType::UAV_Front:
        return FVector(30.0f, 0.0f, 0.0f);  // 机头前方30cm

    case EDeviceCameraType::UAV_Gimbal:
        return FVector(0.0f, 0.0f, -20.0f);  // 机身下方20cm(云台位置)

    case EDeviceCameraType::UGV_Front:
        return FVector(80.0f, 0.0f, 40.0f);  // 车头前方且稍高

    case EDeviceCameraType::RobotDog_Head:
        return FVector(30.0f, 0.0f, 60.0f);  // 头部位置

    case EDeviceCameraType::Custom:
    default:
        return FVector(50.0f, 0.0f, 0.0f);  // 默认前方50cm
    }
}

FRotator UDeviceFirstPersonCamera::GetPresetRotationOffset(EDeviceCameraType Type) const
{
    switch (Type)
    {
    case EDeviceCameraType::UAV_Front:
        return FRotator(0.0f, 0.0f, 0.0f);  // 水平前向

    case EDeviceCameraType::UAV_Gimbal:
        return FRotator(-15.0f, 0.0f, 0.0f);  // 稍微向下倾斜

    case EDeviceCameraType::UGV_Front:
        return FRotator(-5.0f, 0.0f, 0.0f);  // 稍微向下看路面

    case EDeviceCameraType::RobotDog_Head:
        return FRotator(-10.0f, 0.0f, 0.0f);  // 略微向下

    case EDeviceCameraType::Custom:
    default:
        return FRotator::ZeroRotator;
    }
}

// ========== 蓝图接口 ==========

void UDeviceFirstPersonCamera::ApplyCameraTypePreset()
{
    // 应用预设FOV
    CameraFOV = GetPresetFOV(CameraType);
    FieldOfView = CameraFOV;

    // 应用预设偏移
    CameraOffset = GetPresetOffset(CameraType);

    // 应用预设旋转偏移
    CameraRotationOffset = GetPresetRotationOffset(CameraType);

    // 云台摄像头特殊设置
    if (CameraType == EDeviceCameraType::UAV_Gimbal)
    {
        bEnableGimbalEffect = true;
        GimbalMinPitch = -90.0f;
        GimbalMaxPitch = 30.0f;
    }

    UE_LOG(LogDeviceCamera, Log,
        TEXT("Applied camera type preset: %d - FOV: %.1f, Offset: %s"),
        (int32)CameraType, CameraFOV, *CameraOffset.ToString());
}

void UDeviceFirstPersonCamera::SetCameraType(EDeviceCameraType NewType)
{
    CameraType = NewType;
    ApplyCameraTypePreset();

    UE_LOG(LogDeviceCamera, Log, TEXT("Camera type changed to: %d"), (int32)NewType);
}

void UDeviceFirstPersonCamera::SetCameraOffset(FVector NewOffset)
{
    CameraOffset = NewOffset;
    UpdateCameraTransform();

    UE_LOG(LogDeviceCamera, Verbose, TEXT("Camera offset set to: %s"), *NewOffset.ToString());
}

void UDeviceFirstPersonCamera::SetCameraRotationOffset(FRotator NewRotationOffset)
{
    CameraRotationOffset = NewRotationOffset;
    UpdateCameraTransform();

    UE_LOG(LogDeviceCamera, Verbose, TEXT("Camera rotation offset set to: %s"),
        *NewRotationOffset.ToString());
}

void UDeviceFirstPersonCamera::SetGimbalPitch(float Pitch)
{
    if (!bEnableGimbalEffect)
    {
        UE_LOG(LogDeviceCamera, Warning, TEXT("Gimbal effect is not enabled"));
        return;
    }

    TargetGimbalPitch = FMath::Clamp(Pitch, GimbalMinPitch, GimbalMaxPitch);

    UE_LOG(LogDeviceCamera, Verbose, TEXT("Gimbal pitch set to: %.1f"), TargetGimbalPitch);
}

void UDeviceFirstPersonCamera::StartVideoStream()
{
    if (bIsStreamingVideo)
    {
        UE_LOG(LogDeviceCamera, Warning, TEXT("Video stream already started"));
        return;
    }

    // TODO: 实现实际的视频流推送逻辑
    // 这里需要配合视频流系统进行集成
    // 可能涉及SceneCapture2D、MediaOutput等

    bIsStreamingVideo = true;

    OnVideoStreamStarted();

    UE_LOG(LogDeviceCamera, Log,
        TEXT("Video stream started - Resolution: %dx%d @ %d fps"),
        StreamResolutionWidth, StreamResolutionHeight, StreamFrameRate);
}

void UDeviceFirstPersonCamera::StopVideoStream()
{
    if (!bIsStreamingVideo)
    {
        return;
    }

    // TODO: 停止视频流推送

    bIsStreamingVideo = false;

    OnVideoStreamStopped();

    UE_LOG(LogDeviceCamera, Log, TEXT("Video stream stopped"));
}

AActor* UDeviceFirstPersonCamera::GetOwnerDevice() const
{
    return GetOwner();
}