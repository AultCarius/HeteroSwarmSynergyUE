// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DeviceStateManager.h"
#include "HeteroSwarmAgentBase.generated.h"

/**
 * 设备Actor基类（状态驱动层）
 *
 * 职责：
 * - 保存 DeviceID / DeviceType / Online 状态
 * - 缓存最近一帧 FDeviceRuntimeState
 * - 在 Tick 中按状态平滑更新自身 Transform
 * - 为后续执行器可视化驱动预留接口
 */
UCLASS(Blueprintable)
class HETEROSWARMSYNERGYUE_API AHeteroSwarmAgentBase : public AActor
{
    GENERATED_BODY()

public:
    AHeteroSwarmAgentBase();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaSeconds) override;

public:
    /** 由 GameInstance 在 Spawn 后初始化 */
    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Agent")
    void InitializeAgent(int32 InDeviceID, int32 InDeviceType);

    /** 设置在线状态 */
    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Agent")
    void SetAgentOnline(bool bInOnline);

    /** 应用运行时状态（由 GameInstance 在收到 DeviceState 更新时调用） */
    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Agent")
    virtual void ApplyRuntimeState(const FDeviceRuntimeState& InState);

    /** 获取设备ID */
    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Agent")
    int32 GetDeviceID() const { return DeviceID; }

    /** 获取设备类型 */
    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Agent")
    int32 GetDeviceType() const { return DeviceType; }

    /** 是否在线 */
    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Agent")
    bool IsAgentOnline() const { return bIsOnline; }

    /** 是否已收到至少一帧有效运行时状态 */
    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Agent")
    bool HasValidRuntimeState() const { return bHasValidRuntimeState; }

    /** 获取当前缓存的运行时状态 */
    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Agent")
    FDeviceRuntimeState GetCurrentRuntimeState() const { return CurrentRuntimeState; }

protected:
    /** 根组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USceneComponent* RootSceneComponent;

    /** 设备ID（MAVLink SystemID） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Agent")
    int32 DeviceID;

    /** 设备类型（统一设备类型码） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Agent")
    int32 DeviceType;

    /** 是否在线 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Agent")
    bool bIsOnline;

    /** 当前缓存的运行时状态 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Runtime State")
    FDeviceRuntimeState CurrentRuntimeState;

    /** 是否已有有效运行时状态 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Runtime State")
    bool bHasValidRuntimeState;

    /** 是否在下一次状态更新时直接贴到目标位姿 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Runtime State")
    bool bSnapToRuntimeStateOnNextUpdate;

    /** 位置插值速度 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Movement",
        meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "50.0", UIMax = "50.0"))
    float LocationInterpSpeed;

    /** 旋转插值速度 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Movement",
        meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "50.0", UIMax = "50.0"))
    float RotationInterpSpeed;

    /** 调试显示名称 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Agent")
    FString DebugDisplayName;

protected:
    /** 按 CurrentRuntimeState 更新自身 Transform */
    virtual void UpdateTransformFromRuntimeState(float DeltaSeconds);

    /**
     * 每帧驱动执行器可视化。
     *
     * 默认实现会转发到 Blueprint 事件 BP_UpdateActuatorVisuals，
     * 这样不同设备蓝图可以直接读取 CurrentRuntimeState.ActuatorStates
     * 去驱动螺旋桨、关节、云台等。
     *
     * 设备专用 C++ 子类也可以直接覆写此函数。
     */
    virtual void UpdateActuatorVisuals(float DeltaSeconds);

protected:
    /** 蓝图扩展：初始化完成 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Agent")
    void BP_OnAgentInitialized();

    /** 蓝图扩展：在线状态切换 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Agent")
    void BP_OnAgentOnlineStateChanged(bool bNewOnline);

    /** 蓝图扩展：收到新的运行时状态 */
    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Agent")
    void BP_OnRuntimeStateApplied(const FDeviceRuntimeState& NewState);

    /**
     * 蓝图扩展：每帧驱动执行器可视化。
     *
     * 建议在不同设备蓝图中按约定读取 RuntimeState.ActuatorStates：
     * - 四旋翼: [0-3] 螺旋桨RPM, [4-6] 云台 Pitch/Roll/Yaw
     * - 机器狗: [0-11] 12关节角, [12-14] 云台角
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Agent",
        meta = (DisplayName = "Update Actuator Visuals"))
    void BP_UpdateActuatorVisuals(const FDeviceRuntimeState& RuntimeState, float DeltaSeconds);
};
