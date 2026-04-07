#include "DeviceFirstPersonCamera.h"

#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "RtspStreamComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeviceCamera, Log, All);

UDeviceFirstPersonCamera::UDeviceFirstPersonCamera()
    : CurrentGimbalPitch(0.0f)
    , TargetGimbalPitch(0.0f)
    , bIsStreamingVideo(false)
    , CachedRTSPStreamComponent(nullptr)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    FieldOfView = CameraFOV;
}

void UDeviceFirstPersonCamera::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoApplyTypePreset)
    {
        ApplyCameraTypePreset();
    }

    FieldOfView = CameraFOV;
    UpdateCameraTransform();

    if (bEnableCustomPostProcess)
    {
        ApplyPostProcessSettings();
    }

    if (URTSPStreamComponent* StreamComponent = FindRTSPStreamComponent())
    {
        if (bAutoAttachRTSPStreamToCamera)
        {
            StreamComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
            StreamComponent->SetRelativeLocation(FVector::ZeroVector);
            StreamComponent->SetRelativeRotation(FRotator::ZeroRotator);
        }

        SyncRTSPStreamSettings();
    }

    if (bEnableVideoStream)
    {
        StartVideoStream();
    }

    const AActor* OwnerActor = GetOwner();
    UE_LOG(LogDeviceCamera, Log,
        TEXT("DeviceFirstPersonCamera initialized on %s - Type: %d"),
        OwnerActor ? *OwnerActor->GetName() : TEXT("<NoOwner>"),
        static_cast<int32>(CameraType));
}

void UDeviceFirstPersonCamera::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bEnableGimbalEffect)
    {
        UpdateGimbalRotation(DeltaTime);
    }

    UpdateCameraTransform();
}

void UDeviceFirstPersonCamera::UpdateGimbalRotation(float DeltaTime)
{
    CurrentGimbalPitch = FMath::FInterpTo(
        CurrentGimbalPitch,
        TargetGimbalPitch,
        DeltaTime,
        GimbalSmoothSpeed);

    CurrentGimbalPitch = FMath::Clamp(CurrentGimbalPitch, GimbalMinPitch, GimbalMaxPitch);
}

void UDeviceFirstPersonCamera::UpdateCameraTransform()
{
    SetRelativeLocation(CameraOffset);

    FRotator FinalRotation = CameraRotationOffset;
    if (bEnableGimbalEffect)
    {
        FinalRotation.Pitch += CurrentGimbalPitch;
    }

    SetRelativeRotation(FinalRotation);
    FieldOfView = CameraFOV;

    if (URTSPStreamComponent* StreamComponent = FindRTSPStreamComponent())
    {
        if (StreamComponent->SceneCapture)
        {
            StreamComponent->SceneCapture->FOVAngle = CameraFOV;
        }
    }
}

void UDeviceFirstPersonCamera::ApplyPostProcessSettings()
{
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
        return 90.0f;
    case EDeviceCameraType::UAV_Gimbal:
        return 80.0f;
    case EDeviceCameraType::UGV_Front:
        return 100.0f;
    case EDeviceCameraType::RobotDog_Head:
        return 95.0f;
    case EDeviceCameraType::Custom:
    default:
        return 90.0f;
    }
}

FVector UDeviceFirstPersonCamera::GetPresetOffset(EDeviceCameraType Type) const
{
    switch (Type)
    {
    case EDeviceCameraType::UAV_Front:
        return FVector(30.0f, 0.0f, 0.0f);
    case EDeviceCameraType::UAV_Gimbal:
        return FVector(0.0f, 0.0f, -20.0f);
    case EDeviceCameraType::UGV_Front:
        return FVector(80.0f, 0.0f, 40.0f);
    case EDeviceCameraType::RobotDog_Head:
        return FVector(30.0f, 0.0f, 60.0f);
    case EDeviceCameraType::Custom:
    default:
        return FVector(50.0f, 0.0f, 0.0f);
    }
}

FRotator UDeviceFirstPersonCamera::GetPresetRotationOffset(EDeviceCameraType Type) const
{
    switch (Type)
    {
    case EDeviceCameraType::UAV_Front:
        return FRotator(0.0f, 0.0f, 0.0f);
    case EDeviceCameraType::UAV_Gimbal:
        return FRotator(-15.0f, 0.0f, 0.0f);
    case EDeviceCameraType::UGV_Front:
        return FRotator(-5.0f, 0.0f, 0.0f);
    case EDeviceCameraType::RobotDog_Head:
        return FRotator(-10.0f, 0.0f, 0.0f);
    case EDeviceCameraType::Custom:
    default:
        return FRotator::ZeroRotator;
    }
}

void UDeviceFirstPersonCamera::ApplyCameraTypePreset()
{
    CameraFOV = GetPresetFOV(CameraType);
    FieldOfView = CameraFOV;

    CameraOffset = GetPresetOffset(CameraType);
    CameraRotationOffset = GetPresetRotationOffset(CameraType);

    const bool bShouldEnableGimbal = (CameraType == EDeviceCameraType::UAV_Gimbal);
    bEnableGimbalEffect = bShouldEnableGimbal;

    if (bEnableGimbalEffect)
    {
        GimbalMinPitch = -90.0f;
        GimbalMaxPitch = 30.0f;
    }
    else
    {
        CurrentGimbalPitch = 0.0f;
        TargetGimbalPitch = 0.0f;
    }

    UpdateCameraTransform();
    SyncRTSPStreamSettings();

    UE_LOG(LogDeviceCamera, Log,
        TEXT("Applied camera type preset: %d - FOV: %.1f, Offset: %s"),
        static_cast<int32>(CameraType), CameraFOV, *CameraOffset.ToString());
}

void UDeviceFirstPersonCamera::SetCameraType(EDeviceCameraType NewType)
{
    CameraType = NewType;
    ApplyCameraTypePreset();

    UE_LOG(LogDeviceCamera, Log, TEXT("Camera type changed to: %d"), static_cast<int32>(NewType));
}

void UDeviceFirstPersonCamera::SetCameraOffset(FVector NewOffset)
{
    CameraOffset = NewOffset;
    UpdateCameraTransform();
    SyncRTSPStreamSettings();

    UE_LOG(LogDeviceCamera, Verbose, TEXT("Camera offset set to: %s"), *NewOffset.ToString());
}

void UDeviceFirstPersonCamera::SetCameraRotationOffset(FRotator NewRotationOffset)
{
    CameraRotationOffset = NewRotationOffset;
    UpdateCameraTransform();
    SyncRTSPStreamSettings();

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

URTSPStreamComponent* UDeviceFirstPersonCamera::FindRTSPStreamComponent()
{
    if (CachedRTSPStreamComponent && IsValid(CachedRTSPStreamComponent))
    {
        return CachedRTSPStreamComponent;
    }

    AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return nullptr;
    }

    TArray<URTSPStreamComponent*> StreamComponents;
    OwnerActor->GetComponents<URTSPStreamComponent>(StreamComponents);

    if (StreamComponents.Num() == 0)
    {
        CachedRTSPStreamComponent = nullptr;
        return nullptr;
    }

    if (!RTSPStreamComponentName.IsNone())
    {
        for (URTSPStreamComponent* Component : StreamComponents)
        {
            if (Component && Component->GetFName() == RTSPStreamComponentName)
            {
                CachedRTSPStreamComponent = Component;
                return CachedRTSPStreamComponent;
            }
        }

        UE_LOG(LogDeviceCamera, Warning,
            TEXT("RTSPStreamComponent named '%s' was not found on actor %s"),
            *RTSPStreamComponentName.ToString(), *OwnerActor->GetName());
    }

    CachedRTSPStreamComponent = StreamComponents[0];
    return CachedRTSPStreamComponent;
}

void UDeviceFirstPersonCamera::SyncRTSPStreamSettings()
{
    URTSPStreamComponent* StreamComponent = FindRTSPStreamComponent();
    if (!StreamComponent)
    {
        return;
    }

    StreamComponent->CaptureWidth = StreamResolutionWidth;
    StreamComponent->CaptureHeight = StreamResolutionHeight;
    StreamComponent->FPS = StreamFrameRate;

    if (StreamComponent->SceneCapture)
    {
        StreamComponent->SceneCapture->FOVAngle = CameraFOV;
    }
}

void UDeviceFirstPersonCamera::StartVideoStream()
{
    if (bIsStreamingVideo)
    {
        UE_LOG(LogDeviceCamera, Warning, TEXT("Video stream already started"));
        return;
    }

    URTSPStreamComponent* StreamComponent = FindRTSPStreamComponent();
    if (!StreamComponent)
    {
        UE_LOG(LogDeviceCamera, Error,
            TEXT("StartVideoStream failed: no RtspStreamComponent found on actor %s"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("<NoOwner>"));
        return;
    }

    if (bAutoAttachRTSPStreamToCamera)
    {
        StreamComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        StreamComponent->SetRelativeLocation(FVector::ZeroVector);
        StreamComponent->SetRelativeRotation(FRotator::ZeroRotator);
    }

    SyncRTSPStreamSettings();

    bIsStreamingVideo = StreamComponent->StartStreaming();
    if (!bIsStreamingVideo)
    {
        UE_LOG(LogDeviceCamera, Error,
            TEXT("Video stream start failed on actor %s"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("<NoOwner>"));
        return;
    }

    OnVideoStreamStarted();

    UE_LOG(LogDeviceCamera, Log,
        TEXT("Video stream started - Resolution: %dx%d @ %d fps"),
        StreamResolutionWidth, StreamResolutionHeight, StreamFrameRate);
}

void UDeviceFirstPersonCamera::StopVideoStream()
{
    URTSPStreamComponent* StreamComponent = FindRTSPStreamComponent();
    if (StreamComponent)
    {
        StreamComponent->StopStreaming();
    }

    if (!bIsStreamingVideo)
    {
        return;
    }

    bIsStreamingVideo = false;
    OnVideoStreamStopped();

    UE_LOG(LogDeviceCamera, Log, TEXT("Video stream stopped"));
}

AActor* UDeviceFirstPersonCamera::GetOwnerDevice() const
{
    return GetOwner();
}
