// Copyright Epic Games, Inc. All Rights Reserved.
// 项目: 室内外异构编队协同演示验证系统
// 模块: 摄像头系统 - 跟随视角
// 作者: Carius
// 日期: 2026-02-26

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "FollowCameraPawn.generated.h"

/**
 * 跟随视角摄像头Pawn
 *
 * 功能特性:
 * - 锁定目标Actor进行跟随
 * - 鼠标拖拽/键盘环绕目标旋转
 * - 鼠标滚轮缩放距离
 * - 自动避障(通过SpringArm)
 * - 可配置的跟随偏移和距离
 * - 平滑过渡到新目标
 * - 支持多目标快速切换
 *
 * 使用方式:
 * 1. 在蓝图中继承此类(BP_FollowCamera)
 * 2. 在蓝图中调整跟随参数、弹簧臂长度等
 * 3. 调用SetFollowTarget设置跟随目标
 * 4. 可通过蓝图事件响应目标切换
 *
 * 输入绑定要求(在项目设置中配置):
 * - OrbitYaw: 鼠标X轴或A/D键
 * - OrbitPitch: 鼠标Y轴或W/S键
 * - ZoomCamera: 鼠标滚轮
 * - NextTarget: Tab键
 * - PreviousTarget: Shift+Tab键
 */
UCLASS(Blueprintable, BlueprintType, Category = "Hetero Swarm|Camera")
class HETEROSWARMSYNERGYUE_API AFollowCameraPawn : public APawn
{
    GENERATED_BODY()

public:
    // ========== 构造与生命周期 ==========

    AFollowCameraPawn();

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

    /** 弹簧臂组件(提供自动避障和平滑跟随) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USpringArmComponent* SpringArmComponent;

    /** 摄像机组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCameraComponent* CameraComponent;

    // ========== 跟随目标 ==========

protected:
    /** 当前跟随的目标Actor */
    UPROPERTY(BlueprintReadOnly, Category = "Camera|Target")
    AActor* FollowTarget;

    /** 目标列表(用于多目标切换) */
    UPROPERTY(BlueprintReadOnly, Category = "Camera|Target")
    TArray<AActor*> TargetList;

    /** 当前目标在列表中的索引 */
    UPROPERTY(BlueprintReadOnly, Category = "Camera|Target")
    int32 CurrentTargetIndex;

    // ========== 跟随参数配置 ==========

public:
    /** 默认跟随距离(弹簧臂长度,厘米) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Follow",
        meta = (ClampMin = "100.0", ClampMax = "10000.0", UIMin = "100.0", UIMax = "10000.0"))
    float DefaultFollowDistance = 500.0f;

    /** 最小跟随距离 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Follow",
        meta = (ClampMin = "50.0", ClampMax = "5000.0", UIMin = "50.0", UIMax = "5000.0"))
    float MinFollowDistance = 100.0f;

    /** 最大跟随距离 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Follow",
        meta = (ClampMin = "500.0", ClampMax = "50000.0", UIMin = "500.0", UIMax = "50000.0"))
    float MaxFollowDistance = 5000.0f;

    /** 跟随位置偏移(相对于目标,局部空间) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Follow")
    FVector FollowOffset = FVector(0.0f, 0.0f, 100.0f);

    /** 是否启用弹簧臂碰撞检测(自动避障) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Follow")
    bool bEnableCollisionTest = true;

    /** 位置平滑插值速度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Follow",
        meta = (ClampMin = "1.0", ClampMax = "30.0", UIMin = "1.0", UIMax = "30.0"))
    float PositionSmoothSpeed = 8.0f;

    // ========== 环绕参数配置 ==========

    /** 环绕旋转速度(度/秒) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Orbit",
        meta = (ClampMin = "10.0", ClampMax = "360.0", UIMin = "10.0", UIMax = "360.0"))
    float OrbitRotationSpeed = 90.0f;

    /** 鼠标环绕灵敏度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Orbit",
        meta = (ClampMin = "0.1", ClampMax = "5.0", UIMin = "0.1", UIMax = "5.0"))
    float MouseOrbitSensitivity = 1.0f;

    /** Pitch角度限制(最小值,向下看) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Orbit",
        meta = (ClampMin = "-89.0", ClampMax = "0.0", UIMin = "-89.0", UIMax = "0.0"))
    float MinOrbitPitch = -80.0f;

    /** Pitch角度限制(最大值,向上看) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Orbit",
        meta = (ClampMin = "0.0", ClampMax = "89.0", UIMin = "0.0", UIMax = "89.0"))
    float MaxOrbitPitch = 80.0f;

    /** 旋转平滑插值速度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Orbit",
        meta = (ClampMin = "1.0", ClampMax = "30.0", UIMin = "1.0", UIMax = "30.0"))
    float RotationSmoothSpeed = 10.0f;

    // ========== 缩放参数配置 ==========

    /** 滚轮缩放步长(距离变化百分比) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Zoom",
        meta = (ClampMin = "0.05", ClampMax = "0.5", UIMin = "0.05", UIMax = "0.5"))
    float ZoomDistanceStep = 0.15f;

    /** 缩放平滑插值速度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Zoom",
        meta = (ClampMin = "1.0", ClampMax = "30.0", UIMin = "1.0", UIMax = "30.0"))
    float ZoomSmoothSpeed = 8.0f;

    // ========== 相机参数配置 ==========

    /** 相机视野角度(FOV) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Camera",
        meta = (ClampMin = "30.0", ClampMax = "120.0", UIMin = "30.0", UIMax = "120.0"))
    float CameraFOV = 90.0f;

    /** 目标切换过渡时间 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings|Camera",
        meta = (ClampMin = "0.1", ClampMax = "3.0", UIMin = "0.1", UIMax = "3.0"))
    float TargetSwitchDuration = 0.5f;

    // ========== 蓝图事件 ==========

    /** 目标切换时触发 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Camera Events",
        meta = (DisplayName = "On Follow Target Changed"))
    void OnFollowTargetChanged(AActor* NewTarget, AActor* OldTarget);

    /** 摄像头激活时触发 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Camera Events",
        meta = (DisplayName = "On Camera Activated"))
    void OnCameraActivated();

    /** 摄像头失活时触发 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Camera Events",
        meta = (DisplayName = "On Camera Deactivated"))
    void OnCameraDeactivated();

    // ========== 蓝图接口 - 目标控制 ==========

    /** 设置跟随目标 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Target")
    void SetFollowTarget(AActor* NewTarget, bool bSmoothTransition = true);

    /** 清除跟随目标 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Target")
    void ClearFollowTarget();

    /** 获取当前跟随目标 */
    UFUNCTION(BlueprintPure, Category = "Camera|Target")
    AActor* GetFollowTarget() const { return FollowTarget; }

    /** 设置目标列表(用于多目标切换) */
    UFUNCTION(BlueprintCallable, Category = "Camera|Target")
    void SetTargetList(const TArray<AActor*>& NewTargetList);

    /** 添加目标到列表 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Target")
    void AddTargetToList(AActor* Target);

    /** 从列表移除目标 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Target")
    void RemoveTargetFromList(AActor* Target);

    /** 切换到下一个目标 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Target")
    void SwitchToNextTarget();

    /** 切换到上一个目标 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Target")
    void SwitchToPreviousTarget();

    /** 切换到指定索引的目标 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Target")
    void SwitchToTargetAtIndex(int32 Index);

    // ========== 蓝图接口 - 相机控制 ==========

    /** 设置跟随距离 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Control")
    void SetFollowDistance(float NewDistance);

    /** 获取当前跟随距离 */
    UFUNCTION(BlueprintPure, Category = "Camera|Control")
    float GetFollowDistance() const;

    /** 设置跟随偏移 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Control")
    void SetFollowOffset(FVector NewOffset);

    /** 重置环绕角度到默认值 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Control")
    void ResetOrbitRotation(FRotator DefaultRotation = FRotator(-30.0f, 0.0f, 0.0f));

    /** 设置环绕Yaw角度 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Control")
    void SetOrbitYaw(float Yaw);

    /** 设置环绕Pitch角度 */
    UFUNCTION(BlueprintCallable, Category = "Camera|Control")
    void SetOrbitPitch(float Pitch);

    /** 获取相机组件 */
    UFUNCTION(BlueprintPure, Category = "Camera|Components")
    UCameraComponent* GetCameraComponent() const { return CameraComponent; }

    /** 获取弹簧臂组件 */
    UFUNCTION(BlueprintPure, Category = "Camera|Components")
    USpringArmComponent* GetSpringArmComponent() const { return SpringArmComponent; }

protected:
    // ========== 输入处理函数 ==========

    /** 环绕Yaw输入 */
    void OrbitYaw(float Value);

    /** 环绕Pitch输入 */
    void OrbitPitch(float Value);

    /** 缩放输入 */
    void ZoomCamera(float Value);

    /** 下一个目标 */
    void InputNextTarget();

    /** 上一个目标 */
    void InputPreviousTarget();

    // ========== 内部状态 ==========

    /** 当前环绕角度 */
    FRotator CurrentOrbitRotation;

    /** 目标环绕角度 */
    FRotator TargetOrbitRotation;

    /** 当前跟随距离 */
    float CurrentFollowDistance;

    /** 目标跟随距离 */
    float TargetFollowDistance;

    /** 环绕输入向量 */
    FVector2D OrbitInput;

    /** 目标切换过渡计时器 */
    float TargetSwitchTimer;

    /** 是否正在进行目标切换过渡 */
    bool bIsTransitioningTarget;

    /** 过渡起始位置 */
    FVector TransitionStartLocation;

    /** 过渡起始旋转 */
    FRotator TransitionStartRotation;

    // ========== 内部函数 ==========

    /** 更新摄像头位置和旋转 */
    void UpdateCameraTransform(float DeltaTime);

    /** 更新跟随距离 */
    void UpdateFollowDistance(float DeltaTime);

    /** 处理目标切换过渡 */
    void ProcessTargetTransition(float DeltaTime);

    /** 验证目标有效性 */
    bool IsTargetValid(AActor* Target) const;

    /** 计算目标位置 */
    FVector CalculateTargetLocation() const;
};