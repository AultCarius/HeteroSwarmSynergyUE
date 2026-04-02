// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 摄像头系统 - 全局自由视角
// 作者: Carius
// 日期: 2026-02-26

#include "FreeLookCameraPawn.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Kismet/KismetMathLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogFreeLookCamera, Log, All);

// ========== 构造函数 ==========

AFreeLookCameraPawn::AFreeLookCameraPawn()
    : CurrentMovementSpeed(2000.0f)
    , TargetRotation(FRotator::ZeroRotator)
    , RotationInput(FVector2D::ZeroVector)
    , MovementInput(FVector::ZeroVector)
    , bSpeedBoostActive(false)
    , bSlowMovementActive(false)
    , SmoothMoveTarget(FVector::ZeroVector)
    , SmoothMoveDuration(0.0f)
    , SmoothMoveElapsed(0.0f)
    , bIsSmoothMoving(false)
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // 创建根场景组件
    RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
    RootComponent = RootSceneComponent;

    // 创建摄像机组件
    CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
    CameraComponent->SetupAttachment(RootSceneComponent);
    CameraComponent->bUsePawnControlRotation = false;  // 我们手动控制旋转
    CameraComponent->FieldOfView = CameraFOV;

    // 创建移动组件
    MovementComponent = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("MovementComponent"));
    MovementComponent->UpdatedComponent = RootSceneComponent;
    MovementComponent->MaxSpeed = BaseMovementSpeed;
    MovementComponent->Acceleration = MovementAcceleration;
    MovementComponent->Deceleration = MovementDeceleration;

    // 启用输入
    AutoPossessPlayer = EAutoReceiveInput::Disabled;  // 手动Possess
}

// ========== 生命周期 ==========

void AFreeLookCameraPawn::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // 初始化当前移动速度
    CurrentMovementSpeed = BaseMovementSpeed;

    // 初始化目标旋转
    TargetRotation = GetActorRotation();

    // 应用相机FOV
    if (CameraComponent)
    {
        CameraComponent->FieldOfView = CameraFOV;
    }

    UE_LOG(LogFreeLookCamera, Log, TEXT("FreeLookCameraPawn initialized at location: %s"),
        *GetActorLocation().ToString());
}

void AFreeLookCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    check(PlayerInputComponent);

    // 绑定移动输入
    PlayerInputComponent->BindAxis("MoveForward", this, &AFreeLookCameraPawn::MoveForward);
    PlayerInputComponent->BindAxis("MoveRight", this, &AFreeLookCameraPawn::MoveRight);
    PlayerInputComponent->BindAxis("MoveUp", this, &AFreeLookCameraPawn::MoveUp);

    // 绑定旋转输入
    PlayerInputComponent->BindAxis("Turn", this, &AFreeLookCameraPawn::Turn);
    PlayerInputComponent->BindAxis("LookUp", this, &AFreeLookCameraPawn::LookUp);

    // 绑定缩放输入
    PlayerInputComponent->BindAxis("ZoomCamera", this, &AFreeLookCameraPawn::ZoomCamera);

    // 绑定速度修改器
    PlayerInputComponent->BindAction("SpeedModifier", IE_Pressed, this, &AFreeLookCameraPawn::StartSpeedBoost);
    PlayerInputComponent->BindAction("SpeedModifier", IE_Released, this, &AFreeLookCameraPawn::StopSpeedBoost);
    PlayerInputComponent->BindAction("SlowModifier", IE_Pressed, this, &AFreeLookCameraPawn::StartSlowMovement);
    PlayerInputComponent->BindAction("SlowModifier", IE_Released, this, &AFreeLookCameraPawn::StopSlowMovement);

    UE_LOG(LogFreeLookCamera, Log, TEXT("Input component setup complete"));
}

void AFreeLookCameraPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 更新移动速度
    UpdateMovementSpeed(DeltaTime);

    // 更新旋转
    UpdateRotation(DeltaTime);

    // 处理平滑移动
    if (bIsSmoothMoving)
    {
        ProcessSmoothMove(DeltaTime);
    }
    else
    {
        // 正常移动控制
        ApplyMovementInput(DeltaTime);
    }
}

// ========== 输入处理 ==========

void AFreeLookCameraPawn::MoveForward(float Value)
{
    MovementInput.X = FMath::Clamp(Value, -1.0f, 1.0f);
}

void AFreeLookCameraPawn::MoveRight(float Value)
{
    MovementInput.Y = FMath::Clamp(Value, -1.0f, 1.0f);
}

void AFreeLookCameraPawn::MoveUp(float Value)
{
    MovementInput.Z = FMath::Clamp(Value, -1.0f, 1.0f);
}

void AFreeLookCameraPawn::Turn(float Value)
{
    RotationInput.X = Value * MouseSensitivity;
}

void AFreeLookCameraPawn::LookUp(float Value)
{
    RotationInput.Y = Value * MouseSensitivity;
}

void AFreeLookCameraPawn::ZoomCamera(float Value)
{
    if (FMath::Abs(Value) > KINDA_SMALL_NUMBER)
    {
        // 计算新的速度
        float SpeedChange = BaseMovementSpeed * ZoomSpeedStep * FMath::Sign(Value);
        float NewSpeed = BaseMovementSpeed + SpeedChange;

        // 限制在范围内
        BaseMovementSpeed = FMath::Clamp(NewSpeed, MinMovementSpeed, MaxMovementSpeed);

        UE_LOG(LogFreeLookCamera, Verbose, TEXT("Base speed adjusted to: %.1f"), BaseMovementSpeed);
    }
}

void AFreeLookCameraPawn::StartSpeedBoost()
{
    bSpeedBoostActive = true;
    UE_LOG(LogFreeLookCamera, Verbose, TEXT("Speed boost activated"));
}

void AFreeLookCameraPawn::StopSpeedBoost()
{
    bSpeedBoostActive = false;
    UE_LOG(LogFreeLookCamera, Verbose, TEXT("Speed boost deactivated"));
}

void AFreeLookCameraPawn::StartSlowMovement()
{
    bSlowMovementActive = true;
    UE_LOG(LogFreeLookCamera, Verbose, TEXT("Slow movement activated"));
}

void AFreeLookCameraPawn::StopSlowMovement()
{
    bSlowMovementActive = false;
    UE_LOG(LogFreeLookCamera, Verbose, TEXT("Slow movement deactivated"));
}

// ========== 内部函数 ==========

void AFreeLookCameraPawn::UpdateMovementSpeed(float DeltaTime)
{
    // 计算目标速度
    float TargetSpeed = BaseMovementSpeed;

    if (bSpeedBoostActive)
    {
        TargetSpeed *= SpeedBoostMultiplier;
    }
    else if (bSlowMovementActive)
    {
        TargetSpeed *= SlowMultiplier;
    }

    // 限制速度
    TargetSpeed = FMath::Clamp(TargetSpeed, MinMovementSpeed, MaxMovementSpeed);

    // 平滑插值到目标速度
    CurrentMovementSpeed = FMath::FInterpTo(CurrentMovementSpeed, TargetSpeed, DeltaTime, 10.0f);

    // 更新移动组件的最大速度
    if (MovementComponent)
    {
        MovementComponent->MaxSpeed = CurrentMovementSpeed;
    }
}

void AFreeLookCameraPawn::UpdateRotation(float DeltaTime)
{
    // 更新目标旋转
    TargetRotation.Yaw += RotationInput.X;
    TargetRotation.Pitch += RotationInput.Y;

    // 限制Pitch角度
    TargetRotation.Pitch = FMath::Clamp(TargetRotation.Pitch, MinPitchAngle, MaxPitchAngle);

    // 标准化Yaw角度
    TargetRotation.Yaw = FMath::UnwindDegrees(TargetRotation.Yaw);

    // 平滑插值到目标旋转
    FRotator CurrentRotation = GetActorRotation();
    FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotationSmoothSpeed);

    SetActorRotation(NewRotation);
}

void AFreeLookCameraPawn::ProcessSmoothMove(float DeltaTime)
{
    SmoothMoveElapsed += DeltaTime;

    if (SmoothMoveElapsed >= SmoothMoveDuration)
    {
        // 移动完成
        SetActorLocation(SmoothMoveTarget);
        bIsSmoothMoving = false;

        UE_LOG(LogFreeLookCamera, Log, TEXT("Smooth move completed to: %s"), *SmoothMoveTarget.ToString());
    }
    else
    {
        // 计算插值alpha
        float Alpha = SmoothMoveElapsed / SmoothMoveDuration;
        Alpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);  // 使用平滑曲线

        // 插值到目标位置
        FVector CurrentLocation = GetActorLocation();
        FVector NewLocation = FMath::Lerp(CurrentLocation, SmoothMoveTarget, Alpha * DeltaTime * 5.0f);

        SetActorLocation(NewLocation);
    }
}

void AFreeLookCameraPawn::ApplyMovementInput(float DeltaTime)
{
    if (!MovementComponent || MovementInput.IsNearlyZero())
    {
        return;
    }

    // 获取前向和右向向量
    FVector Forward = GetActorForwardVector();
    FVector Right = GetActorRightVector();
    FVector Up = FVector::UpVector;  // 世界空间的上方向

    // 计算移动方向(世界空间)
    FVector MovementDirection = FVector::ZeroVector;
    MovementDirection += Forward * MovementInput.X;
    MovementDirection += Right * MovementInput.Y;
    MovementDirection += Up * MovementInput.Z;

    // 标准化方向
    if (!MovementDirection.IsNearlyZero())
    {
        MovementDirection.Normalize();
    }

    // 应用移动
    AddMovementInput(MovementDirection, 1.0f);
}

// ========== 蓝图接口 ==========

void AFreeLookCameraPawn::SetMovementSpeed(float NewSpeed)
{
    BaseMovementSpeed = FMath::Clamp(NewSpeed, MinMovementSpeed, MaxMovementSpeed);
    UE_LOG(LogFreeLookCamera, Log, TEXT("Movement speed set to: %.1f"), BaseMovementSpeed);
}

void AFreeLookCameraPawn::ResetToDefaultTransform(FVector DefaultLocation, FRotator DefaultRotation)
{
    SetActorLocation(DefaultLocation);
    SetActorRotation(DefaultRotation);
    TargetRotation = DefaultRotation;

    UE_LOG(LogFreeLookCamera, Log, TEXT("Reset to default transform - Location: %s, Rotation: %s"),
        *DefaultLocation.ToString(), *DefaultRotation.ToString());
}

void AFreeLookCameraPawn::SmoothMoveTo(FVector TargetLocation, float Duration)
{
    if (Duration <= 0.0f)
    {
        SetActorLocation(TargetLocation);
        return;
    }

    SmoothMoveTarget = TargetLocation;
    SmoothMoveDuration = Duration;
    SmoothMoveElapsed = 0.0f;
    bIsSmoothMoving = true;

    UE_LOG(LogFreeLookCamera, Log, TEXT("Starting smooth move to: %s over %.2fs"),
        *TargetLocation.ToString(), Duration);
}

void AFreeLookCameraPawn::SmoothRotateTo(FRotator TargetRotationValue, float Duration)
{
    if (Duration <= 0.0f)
    {
        SetActorRotation(TargetRotationValue);
        TargetRotation = TargetRotationValue;
        return;
    }

    TargetRotation = TargetRotationValue;

    UE_LOG(LogFreeLookCamera, Log, TEXT("Starting smooth rotation to: %s"),
        *TargetRotationValue.ToString());
}