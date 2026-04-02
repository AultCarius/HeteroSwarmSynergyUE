// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 摄像头系统 - 跟随视角
// 作者: Carius
// 日期: 2026-02-26

#include "FollowCameraPawn.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetMathLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogFollowCamera, Log, All);

// ========== 构造函数 ==========

AFollowCameraPawn::AFollowCameraPawn()
    : FollowTarget(nullptr)
    , CurrentTargetIndex(-1)
    , CurrentOrbitRotation(FRotator(-30.0f, 0.0f, 0.0f))
    , TargetOrbitRotation(FRotator(-30.0f, 0.0f, 0.0f))
    , CurrentFollowDistance(500.0f)
    , TargetFollowDistance(500.0f)
    , OrbitInput(FVector2D::ZeroVector)
    , TargetSwitchTimer(0.0f)
    , bIsTransitioningTarget(false)
    , TransitionStartLocation(FVector::ZeroVector)
    , TransitionStartRotation(FRotator::ZeroRotator)
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // 创建根场景组件
    RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
    RootComponent = RootSceneComponent;

    // 创建弹簧臂组件
    SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmComponent"));
    SpringArmComponent->SetupAttachment(RootSceneComponent);
    SpringArmComponent->TargetArmLength = DefaultFollowDistance;
    SpringArmComponent->bDoCollisionTest = bEnableCollisionTest;
    SpringArmComponent->bUsePawnControlRotation = false;
    SpringArmComponent->bInheritPitch = false;
    SpringArmComponent->bInheritYaw = false;
    SpringArmComponent->bInheritRoll = false;
    SpringArmComponent->bEnableCameraLag = true;
    SpringArmComponent->CameraLagSpeed = PositionSmoothSpeed;
    SpringArmComponent->bEnableCameraRotationLag = true;
    SpringArmComponent->CameraRotationLagSpeed = RotationSmoothSpeed;

    // 创建摄像机组件
    CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
    CameraComponent->SetupAttachment(SpringArmComponent, USpringArmComponent::SocketName);
    CameraComponent->bUsePawnControlRotation = false;
    CameraComponent->FieldOfView = CameraFOV;

    // 启用输入
    AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

// ========== 生命周期 ==========

void AFollowCameraPawn::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // 初始化距离
    CurrentFollowDistance = DefaultFollowDistance;
    TargetFollowDistance = DefaultFollowDistance;

    // 应用相机FOV
    if (CameraComponent)
    {
        CameraComponent->FieldOfView = CameraFOV;
    }

    // 应用弹簧臂设置
    if (SpringArmComponent)
    {
        SpringArmComponent->TargetArmLength = DefaultFollowDistance;
        SpringArmComponent->bDoCollisionTest = bEnableCollisionTest;
        SpringArmComponent->CameraLagSpeed = PositionSmoothSpeed;
        SpringArmComponent->CameraRotationLagSpeed = RotationSmoothSpeed;
        SpringArmComponent->SocketOffset = FollowOffset;
    }

    UE_LOG(LogFollowCamera, Log, TEXT("FollowCameraPawn initialized"));
}

void AFollowCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    check(PlayerInputComponent);

    // 绑定环绕输入
    PlayerInputComponent->BindAxis("OrbitYaw", this, &AFollowCameraPawn::OrbitYaw);
    PlayerInputComponent->BindAxis("OrbitPitch", this, &AFollowCameraPawn::OrbitPitch);

    // 绑定缩放输入
    PlayerInputComponent->BindAxis("ZoomCamera", this, &AFollowCameraPawn::ZoomCamera);

    // 绑定目标切换
    PlayerInputComponent->BindAction("NextTarget", IE_Pressed, this, &AFollowCameraPawn::InputNextTarget);
    PlayerInputComponent->BindAction("PreviousTarget", IE_Pressed, this, &AFollowCameraPawn::InputPreviousTarget);

    UE_LOG(LogFollowCamera, Log, TEXT("Input component setup complete"));
}

void AFollowCameraPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 更新跟随距离
    UpdateFollowDistance(DeltaTime);

    // 如果正在过渡,处理过渡逻辑
    if (bIsTransitioningTarget)
    {
        ProcessTargetTransition(DeltaTime);
    }
    else
    {
        // 正常更新摄像头
        UpdateCameraTransform(DeltaTime);
    }
}

// ========== 输入处理 ==========

void AFollowCameraPawn::OrbitYaw(float Value)
{
    if (FMath::Abs(Value) > KINDA_SMALL_NUMBER)
    {
        TargetOrbitRotation.Yaw += Value * OrbitRotationSpeed * MouseOrbitSensitivity * GetWorld()->GetDeltaSeconds();
        TargetOrbitRotation.Yaw = FMath::UnwindDegrees(TargetOrbitRotation.Yaw);
    }
}

void AFollowCameraPawn::OrbitPitch(float Value)
{
    if (FMath::Abs(Value) > KINDA_SMALL_NUMBER)
    {
        TargetOrbitRotation.Pitch += Value * OrbitRotationSpeed * MouseOrbitSensitivity * GetWorld()->GetDeltaSeconds();
        TargetOrbitRotation.Pitch = FMath::Clamp(TargetOrbitRotation.Pitch, MinOrbitPitch, MaxOrbitPitch);
    }
}

void AFollowCameraPawn::ZoomCamera(float Value)
{
    if (FMath::Abs(Value) > KINDA_SMALL_NUMBER)
    {
        float DistanceChange = CurrentFollowDistance * ZoomDistanceStep * FMath::Sign(Value);
        TargetFollowDistance = FMath::Clamp(
            TargetFollowDistance - DistanceChange,
            MinFollowDistance,
            MaxFollowDistance
        );

        UE_LOG(LogFollowCamera, Verbose, TEXT("Target distance adjusted to: %.1f"), TargetFollowDistance);
    }
}

void AFollowCameraPawn::InputNextTarget()
{
    SwitchToNextTarget();
}

void AFollowCameraPawn::InputPreviousTarget()
{
    SwitchToPreviousTarget();
}

// ========== 内部函数 ==========

void AFollowCameraPawn::UpdateCameraTransform(float DeltaTime)
{
    if (!IsTargetValid(FollowTarget))
    {
        return;
    }

    // 平滑插值环绕角度
    CurrentOrbitRotation = FMath::RInterpTo(
        CurrentOrbitRotation,
        TargetOrbitRotation,
        DeltaTime,
        RotationSmoothSpeed
    );

    // 计算目标位置(包含偏移)
    FVector TargetLocation = CalculateTargetLocation();

    // 更新Pawn位置
    SetActorLocation(TargetLocation);

    // 更新弹簧臂旋转
    if (SpringArmComponent)
    {
        SpringArmComponent->SetWorldRotation(CurrentOrbitRotation);
    }
}

void AFollowCameraPawn::UpdateFollowDistance(float DeltaTime)
{
    // 平滑插值到目标距离
    CurrentFollowDistance = FMath::FInterpTo(
        CurrentFollowDistance,
        TargetFollowDistance,
        DeltaTime,
        ZoomSmoothSpeed
    );

    // 更新弹簧臂长度
    if (SpringArmComponent)
    {
        SpringArmComponent->TargetArmLength = CurrentFollowDistance;
    }
}

void AFollowCameraPawn::ProcessTargetTransition(float DeltaTime)
{
    TargetSwitchTimer += DeltaTime;

    if (TargetSwitchTimer >= TargetSwitchDuration)
    {
        // 过渡完成
        bIsTransitioningTarget = false;
        TargetSwitchTimer = 0.0f;

        UE_LOG(LogFollowCamera, Log, TEXT("Target transition complete"));
    }
    else
    {
        // 计算插值alpha
        float Alpha = TargetSwitchTimer / TargetSwitchDuration;
        Alpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);

        if (IsTargetValid(FollowTarget))
        {
            // 插值到新目标
            FVector TargetLocation = CalculateTargetLocation();
            FVector NewLocation = FMath::Lerp(TransitionStartLocation, TargetLocation, Alpha);
            SetActorLocation(NewLocation);

            // 插值旋转
            FRotator NewRotation = FMath::RInterpTo(
                TransitionStartRotation,
                CurrentOrbitRotation,
                DeltaTime,
                RotationSmoothSpeed * 2.0f
            );

            if (SpringArmComponent)
            {
                SpringArmComponent->SetWorldRotation(NewRotation);
            }
        }
    }
}

bool AFollowCameraPawn::IsTargetValid(AActor* Target) const
{
    return Target != nullptr && !Target->IsPendingKill();
}

FVector AFollowCameraPawn::CalculateTargetLocation() const
{
    if (!IsTargetValid(FollowTarget))
    {
        return GetActorLocation();
    }

    // 获取目标位置
    FVector BaseLocation = FollowTarget->GetActorLocation();

    // 应用局部空间偏移
    FVector WorldOffset = FollowTarget->GetActorTransform().TransformVector(FollowOffset);

    return BaseLocation + WorldOffset;
}

// ========== 蓝图接口 - 目标控制 ==========

void AFollowCameraPawn::SetFollowTarget(AActor* NewTarget, bool bSmoothTransition)
{
    if (!IsTargetValid(NewTarget))
    {
        UE_LOG(LogFollowCamera, Warning, TEXT("Invalid target provided"));
        return;
    }

    AActor* OldTarget = FollowTarget;
    FollowTarget = NewTarget;

    if (bSmoothTransition && OldTarget != nullptr)
    {
        // 启动平滑过渡
        bIsTransitioningTarget = true;
        TargetSwitchTimer = 0.0f;
        TransitionStartLocation = GetActorLocation();
        TransitionStartRotation = SpringArmComponent ? SpringArmComponent->GetComponentRotation() : GetActorRotation();

        UE_LOG(LogFollowCamera, Log, TEXT("Smooth transition to new target started"));
    }
    else
    {
        // 立即切换
        if (IsTargetValid(FollowTarget))
        {
            SetActorLocation(CalculateTargetLocation());
        }

        UE_LOG(LogFollowCamera, Log, TEXT("Immediate target switch"));
    }

    // 更新目标列表索引
    if (TargetList.Num() > 0)
    {
        CurrentTargetIndex = TargetList.Find(NewTarget);
    }

    // 触发蓝图事件
    OnFollowTargetChanged(NewTarget, OldTarget);

    UE_LOG(LogFollowCamera, Log, TEXT("Follow target changed to: %s"),
        NewTarget ? *NewTarget->GetName() : TEXT("None"));
}

void AFollowCameraPawn::ClearFollowTarget()
{
    AActor* OldTarget = FollowTarget;
    FollowTarget = nullptr;
    CurrentTargetIndex = -1;
    bIsTransitioningTarget = false;

    OnFollowTargetChanged(nullptr, OldTarget);

    UE_LOG(LogFollowCamera, Log, TEXT("Follow target cleared"));
}

void AFollowCameraPawn::SetTargetList(const TArray<AActor*>& NewTargetList)
{
    TargetList = NewTargetList;
    CurrentTargetIndex = -1;

    UE_LOG(LogFollowCamera, Log, TEXT("Target list updated with %d targets"), TargetList.Num());
}

void AFollowCameraPawn::AddTargetToList(AActor* Target)
{
    if (IsTargetValid(Target) && !TargetList.Contains(Target))
    {
        TargetList.Add(Target);
        UE_LOG(LogFollowCamera, Log, TEXT("Added target to list: %s"), *Target->GetName());
    }
}

void AFollowCameraPawn::RemoveTargetFromList(AActor* Target)
{
    if (TargetList.Contains(Target))
    {
        TargetList.Remove(Target);

        // 如果移除的是当前目标,更新索引
        if (Target == FollowTarget)
        {
            CurrentTargetIndex = -1;
        }

        UE_LOG(LogFollowCamera, Log, TEXT("Removed target from list: %s"), *Target->GetName());
    }
}

void AFollowCameraPawn::SwitchToNextTarget()
{
    if (TargetList.Num() == 0)
    {
        UE_LOG(LogFollowCamera, Warning, TEXT("Target list is empty"));
        return;
    }

    CurrentTargetIndex = (CurrentTargetIndex + 1) % TargetList.Num();
    SetFollowTarget(TargetList[CurrentTargetIndex], true);

    UE_LOG(LogFollowCamera, Log, TEXT("Switched to next target (index: %d)"), CurrentTargetIndex);
}

void AFollowCameraPawn::SwitchToPreviousTarget()
{
    if (TargetList.Num() == 0)
    {
        UE_LOG(LogFollowCamera, Warning, TEXT("Target list is empty"));
        return;
    }

    CurrentTargetIndex = (CurrentTargetIndex - 1 + TargetList.Num()) % TargetList.Num();
    SetFollowTarget(TargetList[CurrentTargetIndex], true);

    UE_LOG(LogFollowCamera, Log, TEXT("Switched to previous target (index: %d)"), CurrentTargetIndex);
}

void AFollowCameraPawn::SwitchToTargetAtIndex(int32 Index)
{
    if (!TargetList.IsValidIndex(Index))
    {
        UE_LOG(LogFollowCamera, Warning, TEXT("Invalid target index: %d"), Index);
        return;
    }

    CurrentTargetIndex = Index;
    SetFollowTarget(TargetList[CurrentTargetIndex], true);

    UE_LOG(LogFollowCamera, Log, TEXT("Switched to target at index: %d"), Index);
}

// ========== 蓝图接口 - 相机控制 ==========

void AFollowCameraPawn::SetFollowDistance(float NewDistance)
{
    TargetFollowDistance = FMath::Clamp(NewDistance, MinFollowDistance, MaxFollowDistance);
    UE_LOG(LogFollowCamera, Log, TEXT("Follow distance set to: %.1f"), TargetFollowDistance);
}

float AFollowCameraPawn::GetFollowDistance() const
{
    return CurrentFollowDistance;
}

void AFollowCameraPawn::SetFollowOffset(FVector NewOffset)
{
    FollowOffset = NewOffset;

    if (SpringArmComponent)
    {
        SpringArmComponent->SocketOffset = FollowOffset;
    }

    UE_LOG(LogFollowCamera, Log, TEXT("Follow offset set to: %s"), *FollowOffset.ToString());
}

void AFollowCameraPawn::ResetOrbitRotation(FRotator DefaultRotation)
{
    TargetOrbitRotation = DefaultRotation;
    CurrentOrbitRotation = DefaultRotation;

    if (SpringArmComponent)
    {
        SpringArmComponent->SetWorldRotation(DefaultRotation);
    }

    UE_LOG(LogFollowCamera, Log, TEXT("Orbit rotation reset to: %s"), *DefaultRotation.ToString());
}

void AFollowCameraPawn::SetOrbitYaw(float Yaw)
{
    TargetOrbitRotation.Yaw = FMath::UnwindDegrees(Yaw);
    UE_LOG(LogFollowCamera, Verbose, TEXT("Orbit Yaw set to: %.1f"), Yaw);
}

void AFollowCameraPawn::SetOrbitPitch(float Pitch)
{
    TargetOrbitRotation.Pitch = FMath::Clamp(Pitch, MinOrbitPitch, MaxOrbitPitch);
    UE_LOG(LogFollowCamera, Verbose, TEXT("Orbit Pitch set to: %.1f"), Pitch);
}