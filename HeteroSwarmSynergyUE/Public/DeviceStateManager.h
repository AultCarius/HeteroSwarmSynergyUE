// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 业务逻辑层
// 作者：Carius
// 日期：2026-02-09
// 修改：2026-03-13  v2.0 — MAVLink适配，扩展设备状态（四元数/GPS/电量/执行器）

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "IUDPMessageHandler.h"
#include "UDPProtocolTypes.h"
// FMAVLinkDeviceStateData 定义在 UDPManager.h
#include "UDPManager.h"
#include "DeviceStateManager.generated.h"

// 前置声明
class UUDPManager;

// ========== 设备运行时状态 ==========

/**
 * 设备运行时状态（蓝图可用）
 *
 * v2.0 扩展：在原有位置/姿态/速度基础上，新增 MAVLink 特有字段：
 * GPS 信息、电量、卫星数、四元数姿态、角速度、执行器状态。
 * 所有坐标和旋转均已转换为 UE 坐标系。
 */
USTRUCT(BlueprintType)
struct FDeviceRuntimeState
{
    GENERATED_BODY()

    // ── 基础信息 ─────────────────────────────────────────────────────────

    /** MAVLink 系统ID（与 HeartbeatManager 保持一致） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State")
    int32 DeviceID = 0;

    /**
     * 设备类型（对应 EDeviceType 枚举值）
     * 1=室内四旋翼 2=室外四旋翼 3=室内UGV 4=室外UGV 5=机器狗
     */
    UPROPERTY(BlueprintReadOnly, Category = "Device State")
    uint8 DeviceType = 0;

    /** 是否在线（由 HeartbeatManager 超时检测权威管理，此处由MAVLink消息到来时设为true） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State")
    bool bIsOnline = false;

    // ── 运动状态（UE 坐标系） ─────────────────────────────────────────────

    /** 设备位置（UE 坐标，厘米）：X=North*100  Y=East*100  Z=-Down*100 */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|Motion")
    FVector Location = FVector::ZeroVector;

    /**
     * 设备旋转（UE FRotator，度）
     * 由四元数转换而来（若使用 MAVLink 协议），避免万向节锁
     */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|Motion")
    FRotator Rotation = FRotator::ZeroRotator;

    /** 姿态四元数（UE 顺序 X,Y,Z,W），精度高于 FRotator，供动画系统使用 */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|Motion")
    FQuat Quaternion = FQuat::Identity;

    /** 设备速度（UE 坐标，厘米/秒） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|Motion")
    FVector Velocity = FVector::ZeroVector;

    /** 角速度（rad/s）：X=Roll速率  Y=Pitch速率  Z=Yaw速率（NED坐标系，未转换） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|Motion")
    FVector AngularVelocity = FVector::ZeroVector;

    // ── GPS 信息 ──────────────────────────────────────────────────────────

    /** GPS 纬度（度，WGS84），-90~+90 */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|GPS")
    float GPSLatitude = 0.0;

    /** GPS 经度（度，WGS84），-180~+180 */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|GPS")
    float GPSLongitude = 0.0;

    /** GPS 海拔高度（米，MSL） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|GPS")
    float GPSAltitude = 0.0f;

    /** GPS 定位类型（0=无定位 1=无GPS 2=2D 3=3D） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|GPS")
    int32 GPSFixType = 0;

    /** 可见卫星数 */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|GPS")
    int32 SatellitesVisible = 0;

    // ── 系统状态 ──────────────────────────────────────────────────────────

    /** 系统状态（0=未解锁 1=已解锁） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|System")
    int32 ArmStatus = 0;

    /** 电量百分比（0-100，-1 表示未知） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|System")
    int32 BatteryRemaining = -1;

    // ── 执行器状态 ────────────────────────────────────────────────────────

    /**
     * 执行器状态数组（最多 24 个）
     *
     * 四旋翼：[0-3]=螺旋桨RPM  [4-6]=云台角度(deg: Pitch/Roll/Yaw)
     * 机器狗：[0-11]=12个关节角度(deg)  [12-14]=云台角度(deg)
     * 无人车：待定
     */
    UPROPERTY(BlueprintReadOnly, Category = "Device State|Actuator")
    TArray<float> ActuatorStates;

    // ── 元信息 ────────────────────────────────────────────────────────────

    /** 最后更新时间戳（FPlatformTime::Seconds()） */
    UPROPERTY(BlueprintReadOnly, Category = "Device State")
    float LastUpdateTime = 0.0;

    /** 累计接收的状态更新次数 */
    UPROPERTY(BlueprintReadOnly, Category = "Device State")
    int32 UpdateCount = 0;
};

// ========== 设备统计信息 ==========

/**
 * 设备统计信息（蓝图可用）
 */
USTRUCT(BlueprintType)
struct FDeviceStatistics
{
    GENERATED_BODY()

    /** 在线设备总数 */
    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 OnlineDeviceCount = 0;

    /** 离线设备总数 */
    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 OfflineDeviceCount = 0;

    /** 累计处理的消息数 */
    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 TotalMessagesProcessed = 0;

    /** 累计处理的设备状态更新数 */
    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 TotalStateUpdates = 0;

    // 按设备类型统计在线数量（对应 EDeviceType 枚举值）
    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 IndoorQuadcopterCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 OutdoorQuadcopterCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 IndoorUGVCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 OutdoorUGVCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 RobotDogCount = 0;

    // Legacy Blueprint compatibility fields kept so existing Break nodes in maps
    // can still compile while the project migrates to the more detailed counters.
    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 UAVCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Device Statistics")
    int32 UGVCount = 0;
};

// ========== 蓝图事件委托 ==========

/**
 * 设备状态更新事件
 * DeviceID: MAVLink SystemID；State: 设备运行时状态
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnDeviceStateChanged,
    int32, DeviceID,
    const FDeviceRuntimeState&, State
);

/**
 * 新设备首次出现事件
 * DeviceID: MAVLink SystemID；DeviceType: 对应 EDeviceType 数值
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnDeviceAdded,
    int32, DeviceID,
    uint8, DeviceType
);

/**
 * 设备手动移除事件（通过 RemoveDevice() 触发）
 * DeviceID: MAVLink SystemID；LastLocation: 移除时的最后 UE 坐标（厘米）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnDeviceRemoved,
    int32, DeviceID,
    FVector, LastLocation
);

/**
 * 旧协议批量更新完成事件
 * UpdatedCount: 本次有效更新的设备数量
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnBatchUpdateComplete,
    int32, UpdatedCount
);

// ========== DeviceStateManager ==========

/**
 * 设备状态管理器
 *
 * 职责：
 * - 接收 MAVLink LINKS_MOBILE_ROBOT_STATES (13001) 消息，更新设备运动状态
 * - 兼容旧自定义UDP协议（0x0001/0x0011），供过渡期使用
 * - 将 NED 坐标转换为 UE 坐标系，四元数转换为 FRotator
 * - 维护设备运行时状态缓存（位置/姿态/GPS/电量/执行器）
 * - 触发蓝图事件通知状态变化，供 Actor Transform 更新使用
 *
 * 与 HeartbeatManager 的分工：
 * - HeartbeatManager：管理智能体是否在线（生命周期/Actor Spawn/Destroy）
 * - DeviceStateManager：管理智能体的运动状态（Transform/GPS/电量更新）
 *
 * 注意：DeviceStateManager 不做超时检测，在线状态由 HeartbeatManager 权威管理。
 */
UCLASS(BlueprintType)
class HETEROSWARMSYNERGYUE_API UDeviceStateManager : public UObject, public IUDPMessageHandler
{
    GENERATED_BODY()

public:
    // ========== 构造与生命周期 ==========

    UDeviceStateManager();
    virtual ~UDeviceStateManager();

    /**
     * 初始化管理器
     *
     * @param InUDPManager UDP管理器指针（用于注册旧协议 Handler）
     * @return true 表示成功
     */
    bool Initialize(UUDPManager* InUDPManager);

    /**
     * 关闭管理器
     */
    void Shutdown();

    /**
     * 每帧 Tick（由 GameInstance 调用）
     * v2.0：超时检测已移交 HeartbeatManager，此处暂为空实现，保留供未来扩展
     */
    void Tick(float DeltaTime);

    // ========== IUDPMessageHandler 接口（旧协议兼容） ==========

    /**
     * 处理旧自定义 UDP 消息（由 MessageDispatcher 调用）
     * 处理 MessageType 0x0001（单设备）和 0x0011（批量）
     */
    virtual void HandleMessage(const uint8* Data, uint32 Length) override;

    // ========== MAVLink 处理接口（v2.0） ==========

    /**
     * 处理 MAVLink LINKS_MOBILE_ROBOT_STATES (13001) 消息
     *
     * 由 GameInstance 在初始化时绑定到 UDPManager::OnMAVLinkDeviceState。
     * 解析四元数、GPS、电量、执行器状态并写入缓存，触发 OnDeviceStateChanged。
     *
     * @param StateData 来自 UDPManager 的已解析设备状态数据
     */
    UFUNCTION()
    void HandleMAVLinkDeviceState(const FMAVLinkDeviceStateData& StateData);

    // ========== 蓝图查询接口 ==========

    /**
     * 获取设备当前状态
     *
     * @param DeviceID MAVLink SystemID
     * @param OutState 输出：设备状态
     * @return true 表示设备存在（含离线设备）
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Get Device State"))
    bool GetDeviceState(int32 DeviceID, FDeviceRuntimeState& OutState) const;

    /**
     * 获取所有在线设备的 SystemID 列表
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Get All Online Device IDs"))
    TArray<int32> GetAllOnlineDeviceIDs() const;

    /**
     * 获取指定类型的设备 SystemID 列表（仅在线）
     *
     * @param DeviceType 对应 EDeviceType 的数值（1-5）
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Get Devices By Type"))
    TArray<int32> GetDevicesByType(uint8 DeviceType) const;

    /**
     * 检查设备是否在线
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Is Device Online"))
    bool IsDeviceOnline(int32 DeviceID) const;

    /**
     * 获取在线设备数量
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Get Online Device Count"))
    int32 GetOnlineDeviceCount() const;

    /**
     * 获取统计信息
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Get Device Statistics"))
    void GetStatistics(FDeviceStatistics& OutStatistics) const;

    /**
     * 清除所有设备状态缓存
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Clear All Devices"))
    void ClearAllDevices();

    /**
     * 手动移除指定设备，并触发 OnDeviceRemoved 事件
     *
     * @param DeviceID MAVLink SystemID
     * @return true 表示移除成功（设备存在）
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Remove Device"))
    bool RemoveDevice(int32 DeviceID);

    /**
     * 打印统计信息到日志
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Device State Manager",
        meta = (DisplayName = "Print Statistics"))
    void PrintStatistics() const;

    // ========== 蓝图事件 ==========

    /** 设备运动状态更新事件（每次收到 MAVLink 状态包均触发） */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Device State Manager")
    FOnDeviceStateChanged OnDeviceStateChanged;

    /** 新设备首次出现事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Device State Manager")
    FOnDeviceAdded OnDeviceAdded;

    /** 设备手动移除事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Device State Manager")
    FOnDeviceRemoved OnDeviceRemoved;

    /** 旧协议批量更新完成事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Device State Manager")
    FOnBatchUpdateComplete OnBatchUpdateComplete;

    // ========== C++ 接口 ==========

    /**
     * 获取设备状态（C++ 版本，返回指针，不存在则返回 nullptr）
     */
    const FDeviceRuntimeState* GetDeviceStatePtr(int32 DeviceID) const;

    /**
     * 获取所有设备状态只读引用
     */
    const TMap<int32, FDeviceRuntimeState>& GetAllDeviceStates() const { return DeviceCache; }

private:
    // ========== 核心数据 ==========

    /** UDP管理器指针（用于注册旧协议 Handler） */
    UPROPERTY()
    UUDPManager* UDPManager;

    /** 设备状态缓存（SystemID → FDeviceRuntimeState） */
    UPROPERTY()
    TMap<int32, FDeviceRuntimeState> DeviceCache;

    /** 是否已初始化 */
    bool bIsInitialized;

    // ========== 统计信息 ==========

    /** 累计处理的旧协议消息数 */
    int32 TotalMessagesProcessed;

    /** 累计处理的设备状态更新数（MAVLink + 旧协议） */
    int32 TotalStateUpdates;

    // ========== 旧协议内部处理函数（兼容保留） ==========

    void ProcessSingleDeviceState(const uint8* Data, uint32 Length);
    void ProcessBatchDeviceState(const uint8* Data, uint32 Length);
    void ProcessSingleUpdate(const FDeviceStatePacket& Packet);
    bool ValidateDeviceStatePacket(const FDeviceStatePacket& Packet) const;

    // ========== MAVLink 内部处理函数 ==========

    /**
     * 将 FMAVLinkDeviceStateData 中的 NED 坐标/四元数转换并写入 FDeviceRuntimeState
     *
     * @param StateData 来自 UDPManager 的原始解析结果
     * @param OutState  目标状态结构（原地修改）
     */
    void ApplyMAVLinkStateToRuntime(
        const FMAVLinkDeviceStateData& StateData,
        FDeviceRuntimeState& OutState) const;

    /**
     * 将执行器状态数组从 FMAVLinkDeviceStateData 复制到 FDeviceRuntimeState
     *
     * @param StateData 来自 UDPManager 的原始解析结果
     * @param OutState  目标状态结构（原地修改）
     */
    void UpdateActuatorStates(
        const FMAVLinkDeviceStateData& StateData,
        FDeviceRuntimeState& OutState) const;
};
