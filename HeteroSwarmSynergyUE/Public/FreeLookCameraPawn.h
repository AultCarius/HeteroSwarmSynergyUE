// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 摄像头系统 - 全局自由视角
// 作者: Carius
// 日期: 2026-02-26

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "FreeLookCameraPawn.generated.h"

/**
 * 全局自由视角摄像头Pawn
 *
 * 功能特性:
 * - WASD键盘移动控制
 * - 鼠标右键旋转视角
 * - 鼠标滚轮缩放速度
 * - Shift加速/Ctrl减速
 * - Q/E键上升下降
 * - 平滑插值移动
 * - 可配置的移动速度和加速度
 *
 * 使用方式:
 * 1. 在蓝图中继承此类(BP_FreeLookCamera)
 * 2. 在蓝图中调整移动参数、相机FOV等
 * 3. 在GameMode中设置为默认Pawn或通过PlayerController切换
 *
 * 输入绑定要求(在项目设置中配置):
 * - MoveForward: W/S键或手柄左摇杆Y轴
 * - MoveRight: A/D键或手柄左摇杆X轴
 * - MoveUp: Q/E键
 * - Turn: 鼠标X轴
 * - LookUp: 鼠标Y轴
 * - ZoomCamera: 鼠标滚轮
 * - SpeedModifier: Shift键
 * - SlowModifier: Ctrl键
 */
UCLASS(Blueprintable, BlueprintType, Category = "Hetero Swarm|Camera")
class HETEROSWARMSYNERGYUE_API AFreeLookCameraPawn : public APawn
{
    GENERATED_BODY()

public:
    // ========== 构造与生命周期 ==========

    AFreeLookCameraPawn();

    /** 初始化组件(PostInitializeComponents调用) */
    virtual void PostInitializeComponents() override;

    /** 设置玩家输入绑定 */
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    /** 每帧更新 */
    virtual void Tick(float DeltaTime) override;

    // ========== 组件 ==========

protected:
    /** 根组件(场景组件) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class USceneComponent* RootSceneComponent;

    /** 摄像机组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCameraComponent* CameraComponent;

    /** 移动组件(提供平滑移动) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UFloatingPawnMovement* MovementComponent;

    // ========== 移动参数配置 ==========

public:
    /** 基础移动速度(厘米/秒) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Movement",
        meta = (ClampMin = "100.0", ClampMax = "50000.0", UIMin = "100.0", UIMax = "50000.0"))
    float BaseMovementSpeed = 2000.0f;

    /** Shift加速倍数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Movement",
        meta = (ClampMin = "1.0", ClampMax = "10.0", UIMin = "1.0", UIMax = "10.0"))
    float SpeedBoostMultiplier = 3.0f;

    /** Ctrl减速倍数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Movement",
        meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
    float SlowMultiplier = 0.3f;

    /** 移动加速度(厘米/秒²) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Movement",
        meta = (ClampMin = "100.0", ClampMax = "20000.0", UIMin = "100.0", UIMax = "20000.0"))
    float MovementAcceleration = 5000.0f;

    /** 移动减速度(厘米/秒²) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Movement",
        meta = (ClampMin = "100.0", ClampMax = "20000.0", UIMin = "100.0", UIMax = "20000.0"))
    float MovementDeceleration = 8000.0f;

    // ========== 旋转参数配置 ==========

    /** 鼠标灵敏度(度/像素) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Rotation",
        meta = (ClampMin = "0.01", ClampMax = "2.0", UIMin = "0.01", UIMax = "2.0"))
    float MouseSensitivity = 0.15f;

    /** Pitch角度限制(最小值,向下看) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Rotation",
        meta = (ClampMin = "-90.0", ClampMax = "0.0", UIMin = "-90.0", UIMax = "0.0"))
    float MinPitchAngle = -89.0f;

    /** Pitch角度限制(最大值,向上看) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Rotation",
        meta = (ClampMin = "0.0", ClampMax = "90.0", UIMin = "0.0", UIMax = "90.0"))
    float MaxPitchAngle = 89.0f;

    /** 旋转平滑插值速度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Rotation",
        meta = (ClampMin = "1.0", ClampMax = "30.0", UIMin = "1.0", UIMax = "30.0"))
    float RotationSmoothSpeed = 10.0f;

    // ========== 缩放参数配置 ==========

    /** 最小移动速度(厘米/秒) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Zoom",
        meta = (ClampMin = "50.0", ClampMax = "10000.0", UIMin = "50.0", UIMax = "10000.0"))
    float MinMovementSpeed = 200.0f;

    /** 最大移动速度(厘米/秒) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Zoom",
        meta = (ClampMin = "1000.0", ClampMax = "100000.0", UIMin = "1000.0", UIMax = "100000.0"))
    float MaxMovementSpeed = 20000.0f;

    /** 滚轮缩放步长(速度变化百分比) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Zoom",
        meta = (ClampMin = "0.05", ClampMax = "0.5", UIMin = "0.05", UIMax = "0.5"))
    float ZoomSpeedStep = 0.15f;

    // ========== 相机参数配置 ==========

    /** 相机视野角度(FOV) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Camera",
        meta = (ClampMin = "30.0", ClampMax = "120.0", UIMin = "30.0", UIMax = "120.0"))
    float CameraFOV = 90.0f;

    /** 是否启用平滑移动 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Camera")
    bool bEnableSmoothMovement = true;

    // ========== 蓝图事件 ==========

    /** 摄像头激活时触发 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Camera Events",
        meta = (DisplayName = "On Camera Activated"))
    void OnCameraActivated();

    /** 摄像头失活时触发 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Camera Events",
        meta = (DisplayName = "On Camera Deactivated"))
    void OnCameraDeactivated();

    // ========== 蓝图接口 ==========

    /** 获取当前移动速度 */
    UFUNCTION(BlueprintPure, Category = "Camera|Movement")
    float GetCurrentMovementSpeed() const { return CurrentMovementSpeed; }

    /** 设置移动速度 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Movement")
    void SetMovementSpeed(float NewSpeed);

    /** 获取相机组件 */
    UFUNCTION(BlueprintPure, Category = "Camera|Components")
    UCameraComponent* GetCameraComponent() const { return CameraComponent; }

    /** 重置到默认位置和旋转 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Control")
    void ResetToDefaultTransform(FVector DefaultLocation, FRotator DefaultRotation);

    /** 平滑移动到目标位置 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Control")
    void SmoothMoveTo(FVector TargetLocation, float Duration = 1.0f);

    /** 平滑旋转到目标角度 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Control")
    void SmoothRotateTo(FRotator TargetRotation, float Duration = 1.0f);

protected:
    // ========== 输入处理函数 ==========

    /** 前后移动输入 */
    void MoveForward(float Value);

    /** 左右移动输入 */
    void MoveRight(float Value);

    /** 上下移动输入 */
    void MoveUp(float Value);

    /** 水平旋转输入 */
    void Turn(float Value);

    /** 垂直旋转输入 */
    void LookUp(float Value);

    /** 鼠标滚轮缩放 */
    void ZoomCamera(float Value);

    /** 速度修改器(Shift加速) */
    void StartSpeedBoost();
    void StopSpeedBoost();

    /** 减速修改器(Ctrl减速) */
    void StartSlowMovement();
    void StopSlowMovement();

    // ========== 内部状态 ==========

    /** 当前移动速度 */
    float CurrentMovementSpeed;

    /** 目标旋转(用于平滑插值) */
    FRotator TargetRotation;

    /** 当前旋转输入 */
    FVector2D RotationInput;

    /** 移动输入向量 */
    FVector MovementInput;

    /** 是否启用加速 */
    bool bSpeedBoostActive;

    /** 是否启用减速 */
    bool bSlowMovementActive;

    /** 平滑移动目标位置 */
    FVector SmoothMoveTarget;

    /** 平滑移动持续时间 */
    float SmoothMoveDuration;

    /** 平滑移动已用时间 */
    float SmoothMoveElapsed;

    /** 是否正在进行平滑移动 */
    bool bIsSmoothMoving;

    // ========== 内部函数 ==========

    /** 更新移动速度(根据加速/减速状态) */
    void UpdateMovementSpeed(float DeltaTime);

    /** 更新旋转(平滑插值) */
    void UpdateRotation(float DeltaTime);

    /** 处理平滑移动 */
    void ProcessSmoothMove(float DeltaTime);

    /** 应用移动输入 */
    void ApplyMovementInput(float DeltaTime);
};