// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 业务逻辑层
// 作者：Carius
// 日期：2026-02-09

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "IUDPMessageHandler.h"
#include "UDPProtocolTypes.h"
#include "DeviceStateManager.h"
#include "TrajectoryManager.generated.h"

// 前置声明
class UUDPManager;
class UWorld;
struct FMAVLinkWaypointsData;

/**
 * 轨迹点（带时间戳）
 */
USTRUCT(BlueprintType)
struct FTrajectoryPoint
{
    GENERATED_BODY()

    /** 轨迹点位置（UE坐标，厘米） */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Point")
    FVector Location = FVector::ZeroVector;

    /** 添加时间（秒，相对于程序启动） */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Point")
    float Timestamp = 0.0f;

    /** 默认构造 */
    FTrajectoryPoint() = default;

    /** 带参数构造 */
    FTrajectoryPoint(const FVector& InLocation, float InTimestamp)
        : Location(InLocation)
        , Timestamp(InTimestamp)
    {
    }
};

/**
 * 轨迹配置（蓝图可编辑）
 */
USTRUCT(BlueprintType)
struct FTrajectoryConfig
{
    GENERATED_BODY()

    /** 期望轨迹颜色 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config")
    FLinearColor PlannedColor = FLinearColor(0.0f, 1.0f, 0.0f, 1.0f);  // 绿色

    /** 实际轨迹颜色 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config")
    FLinearColor ActualColor = FLinearColor(1.0f, 0.5f, 0.0f, 1.0f);   // 橙色

    /** 轨迹生存时间（秒），0表示永久保留 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config",
        meta = (ClampMin = "0.0", ClampMax = "300.0"))
    float LifetimeSeconds = 10.0f;

    /** 轨迹线宽（厘米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config",
        meta = (ClampMin = "1.0", ClampMax = "50.0"))
    float LineWidth = 5.0f;

    /** 轨迹透明度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Opacity = 0.8f;

    /** 是否启用淡出效果（越旧的点越透明） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config")
    bool bEnableFadeOut = true;

    /** 最大轨迹点数量（防止内存溢出） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config",
        meta = (ClampMin = "10", ClampMax = "10000"))
    int32 MaxPointsPerTrajectory = 500;

    /** 是否在场景中绘制调试轨迹 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug")
    bool bEnableDebugVisualization = true;

    /** 是否在轨迹末端绘制设备标签 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug")
    bool bDrawDeviceLabel = true;

    /** 轨迹点调试球半径（厘米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug",
        meta = (ClampMin = "2.0", ClampMax = "100.0"))
    float DebugPointSize = 18.0f;

    /** 调试轨迹整体抬升高度（厘米），避免与地面/点云完全重叠 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug",
        meta = (ClampMin = "0.0", ClampMax = "500.0"))
    float DebugVerticalOffsetCm = 40.0f;

    /** 实际轨迹调试抬升高度（厘米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug",
        meta = (ClampMin = "0.0", ClampMax = "500.0"))
    float ActualDebugVerticalOffsetCm = 0.0f;

    /** 是否绘制轨迹方向箭头 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug")
    bool bDrawDirectionArrows = true;

    /** 轨迹方向箭头大小（厘米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug",
        meta = (ClampMin = "10.0", ClampMax = "500.0"))
    float DirectionArrowSizeCm = 90.0f;

    /** 轨迹方向箭头间距（厘米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug",
        meta = (ClampMin = "10.0", ClampMax = "5000.0"))
    float DirectionArrowSpacingCm = 280.0f;

    /** 是否在轨迹末端绘制更醒目的方向头 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug")
    bool bDrawTerminalDirectionHead = true;

    /** 轨迹末端方向头大小（厘米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug",
        meta = (ClampMin = "20.0", ClampMax = "800.0"))
    float TerminalDirectionHeadSizeCm = 180.0f;

    /** 是否显示原始轨迹点标记 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Debug")
    bool bDrawPointMarkers = false;

    /** 是否对折线做平滑重采样 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Render")
    bool bSmoothCurve = true;

    /** 每段平滑插值的细分数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Render",
        meta = (ClampMin = "1", ClampMax = "32"))
    int32 SmoothSubstepsPerSegment = 8;

    /** 是否启用头细尾粗或头粗尾细的宽度渐变 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Render")
    bool bUseWidthTaper = true;

    /** 轨迹最老端宽度倍率 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Render",
        meta = (ClampMin = "0.05", ClampMax = "4.0"))
    float OldestWidthScale = 0.45f;

    /** 轨迹最新端宽度倍率 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Render",
        meta = (ClampMin = "0.05", ClampMax = "4.0"))
    float NewestWidthScale = 1.0f;

    /** 是否启用轨迹头尾亮度/透明度渐变 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Render")
    bool bUseOpacityTaper = true;

    /** 轨迹最老端亮度倍率 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Render",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OldestOpacityScale = 0.22f;

    /** 轨迹最新端亮度倍率 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Render",
        meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float NewestOpacityScale = 1.0f;

    /** 是否根据设备状态自动沉淀实际轨迹 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Actual Tracking")
    bool bAutoTrackActualTrajectory = true;

    /** 实际轨迹最小采样距离（厘米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Actual Tracking",
        meta = (ClampMin = "1.0", ClampMax = "5000.0"))
    float ActualTrackMinDistanceCm = 80.0f;

    /** 实际轨迹最小采样时间间隔（秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Config|Actual Tracking",
        meta = (ClampMin = "0.01", ClampMax = "5.0"))
    float ActualTrackMinIntervalSeconds = 0.15f;
};


/**
 * 设备轨迹数据（蓝图可用）
 */
USTRUCT(BlueprintType)
struct FDeviceTrajectory
{
    GENERATED_BODY()

    /** 设备ID */
    UPROPERTY(BlueprintReadOnly, Category = "Device Trajectory")
    int32 DeviceID = 0;

    /** 轨迹类型 */
    UPROPERTY(BlueprintReadOnly, Category = "Device Trajectory")
    ETrajectoryType TrajectoryType = ETrajectoryType::Planned;

    /** 轨迹点列表 */
    UPROPERTY(BlueprintReadOnly, Category = "Device Trajectory")
    TArray<FTrajectoryPoint> Points;

    /** 轨迹总点数（历史累计） */
    UPROPERTY(BlueprintReadOnly, Category = "Device Trajectory")
    int32 TotalPointsReceived = 0;

    /** 最后更新时间 */
    UPROPERTY(BlueprintReadOnly, Category = "Device Trajectory")
    float LastUpdateTime = 0.0f;
};

/**
 * 轨迹统计信息（蓝图可用）
 */
USTRUCT(BlueprintType)
struct FTrajectoryStatistics
{
    GENERATED_BODY()

    /** 活跃的设备轨迹数量 */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Statistics")
    int32 ActiveTrajectoryCount = 0;

    /** 期望轨迹数量 */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Statistics")
    int32 PlannedTrajectoryCount = 0;

    /** 实际轨迹数量 */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Statistics")
    int32 ActualTrajectoryCount = 0;

    /** 累计轨迹点总数 */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Statistics")
    int32 TotalPointsCount = 0;

    /** 累计处理的消息数 */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Statistics")
    int32 TotalMessagesProcessed = 0;

    /** 累计接收的轨迹数据数 */
    UPROPERTY(BlueprintReadOnly, Category = "Trajectory Statistics")
    int32 TotalTrajectoriesReceived = 0;
};

// ========== 蓝图事件委托 ==========

/**
 * 轨迹更新事件
 *
 * 参数：
 * - DeviceID: 设备ID
 * - TrajectoryType: 轨迹类型
 * - NewPointsCount: 新增的轨迹点数量
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnTrajectoryUpdated,
    int32, DeviceID,
    ETrajectoryType, TrajectoryType,
    int32, NewPointsCount
);

/**
 * 新轨迹创建事件
 *
 * 参数：
 * - DeviceID: 设备ID
 * - TrajectoryType: 轨迹类型
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnTrajectoryCreated,
    int32, DeviceID,
    ETrajectoryType, TrajectoryType
);

/**
 * 轨迹清除事件
 *
 * 参数：
 * - DeviceID: 设备ID
 * - TrajectoryType: 轨迹类型
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnTrajectoryCleared,
    int32, DeviceID,
    ETrajectoryType, TrajectoryType
);

/**
 * 轨迹管理器
 *
 * 职责：
 * - 接收并解析轨迹数据UDP消息（0x0003）
 * - 将NED坐标转换为UE坐标系
 * - 分离管理期望轨迹和实际轨迹
 * - 管理轨迹生存时间（自动清理过期点）
 * - 支持配置文件定制轨迹显示参数
 * - 触发蓝图事件通知轨迹变化
 *
 * 轨迹类型：
 * - Planned (期望轨迹): 规划的路径
 * - Actual (实际轨迹): 设备实际运行的轨迹
 *
 * 使用方式：
 * - C++: 通过GameInstance获取实例
 * - 蓝图: 绑定事件委托，响应轨迹更新
 */
UCLASS(BlueprintType)
class HETEROSWARMSYNERGYUE_API UTrajectoryManager : public UObject, public IUDPMessageHandler
{
    GENERATED_BODY()

public:
    // ========== 构造与生命周期 ==========

    UTrajectoryManager();
    virtual ~UTrajectoryManager();

    /**
     * 初始化管理器
     *
     * @param InUDPManager UDP管理器指针
     * @return true表示初始化成功
     */
    bool Initialize(UUDPManager* InUDPManager, UWorld* InWorld);

    /**
     * 关闭管理器
     */
    void Shutdown();

    /**
     * 每帧更新（清理过期轨迹点）
     *
     * @param DeltaTime 帧间隔时间
     */
    void Tick(float DeltaTime);

    // ========== IUDPMessageHandler 接口 ==========

    /**
     * 处理UDP消息（由MessageDispatcher调用）
     *
     * @param Data 消息体数据（不含消息头）
     * @param Length 消息体长度
     */
    virtual void HandleMessage(const uint8* Data, uint32 Length) override;

    /**
     * 处理 MAVLink LINKS_WAYPOINTS (13002) 轨迹数据
     *
     * 该消息表示当前设备最新的规划航点集合，因此会覆盖缓存中的 Planned 轨迹。
     *
     * @param WaypointData UDPManager 解析后的 MAVLink 轨迹数据
     */
    void HandleMAVLinkWaypoints(const FMAVLinkWaypointsData& WaypointData);

    /** 根据设备实时状态自动沉淀 Actual 轨迹，便于对比规划轨迹与实际运行轨迹 */
    UFUNCTION()
    void HandleDeviceStateForActualTrajectory(int32 DeviceID, const FDeviceRuntimeState& State);

    // ========== 蓝图接口 - 查询 ==========

    /**
     * 获取设备的轨迹数据
     *
     * @param DeviceID 设备ID
     * @param TrajectoryType 轨迹类型
     * @param OutTrajectory 输出：轨迹数据
     * @return true表示轨迹存在
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Get Device Trajectory"))
    bool GetDeviceTrajectory(int32 DeviceID, ETrajectoryType TrajectoryType, FDeviceTrajectory& OutTrajectory) const;

    /**
     * 获取设备的轨迹点列表
     *
     * @param DeviceID 设备ID
     * @param TrajectoryType 轨迹类型
     * @return 轨迹点数组（仅包含位置）
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Get Trajectory Points"))
    TArray<FVector> GetTrajectoryPoints(int32 DeviceID, ETrajectoryType TrajectoryType) const;

    /**
     * 获取所有有轨迹的设备ID列表
     *
     * @return 设备ID数组
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Get All Device IDs With Trajectory"))
    TArray<int32> GetAllDeviceIDsWithTrajectory() const;

    /**
     * 检查设备是否有轨迹数据
     *
     * @param DeviceID 设备ID
     * @param TrajectoryType 轨迹类型
     * @return true表示有轨迹
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Has Trajectory"))
    bool HasTrajectory(int32 DeviceID, ETrajectoryType TrajectoryType) const;

    /**
     * 获取轨迹点数量
     *
     * @param DeviceID 设备ID
     * @param TrajectoryType 轨迹类型
     * @return 轨迹点数量
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Get Trajectory Point Count"))
    int32 GetTrajectoryPointCount(int32 DeviceID, ETrajectoryType TrajectoryType) const;

    /**
     * 获取统计信息
     *
     * @param OutStatistics 输出：统计信息
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Get Trajectory Statistics"))
    void GetStatistics(FTrajectoryStatistics& OutStatistics) const;

    // ========== 蓝图接口 - 配置 ==========

    /**
     * 获取当前轨迹配置
     *
     * @return 轨迹配置
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Get Trajectory Config"))
    FTrajectoryConfig GetTrajectoryConfig() const { return Config; }

    /**
     * 设置轨迹配置
     *
     * @param NewConfig 新配置
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Set Trajectory Config"))
    void SetTrajectoryConfig(const FTrajectoryConfig& NewConfig);

    /**
     * 从配置文件加载配置
     *
     * @param ConfigFilePath 配置文件路径（相对于项目Config目录）
     * @return true表示加载成功
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Load Config From File"))
    bool LoadConfigFromFile(const FString& ConfigFilePath);

    /**
     * 保存配置到文件
     *
     * @param ConfigFilePath 配置文件路径（相对于项目Config目录）
     * @return true表示保存成功
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Save Config To File"))
    bool SaveConfigToFile(const FString& ConfigFilePath) const;

    // ========== 蓝图接口 - 管理 ==========

    /**
     * 清除指定设备的轨迹
     *
     * @param DeviceID 设备ID
     * @param TrajectoryType 轨迹类型
     * @return true表示清除成功
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Clear Device Trajectory"))
    bool ClearDeviceTrajectory(int32 DeviceID, ETrajectoryType TrajectoryType);

    /**
     * 清除指定设备的所有轨迹（期望+实际）
     *
     * @param DeviceID 设备ID
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Clear All Device Trajectories"))
    void ClearAllDeviceTrajectories(int32 DeviceID);

    /**
     * 清除所有轨迹
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Clear All Trajectories"))
    void ClearAllTrajectories();

    /**
     * 手动清理过期的轨迹点
     *
     * @return 清理的轨迹点数量
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Cleanup Expired Points"))
    int32 CleanupExpiredPoints();

    /**
     * 打印统计信息到日志
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Trajectory Manager",
        meta = (DisplayName = "Print Statistics"))
    void PrintStatistics() const;

    // ========== 蓝图事件 ==========

    /** 轨迹更新事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Trajectory Manager")
    FOnTrajectoryUpdated OnTrajectoryUpdated;

    /** 新轨迹创建事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Trajectory Manager")
    FOnTrajectoryCreated OnTrajectoryCreated;

    /** 轨迹清除事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Trajectory Manager")
    FOnTrajectoryCleared OnTrajectoryCleared;

    // ========== C++ 接口 ==========

    /**
     * 获取设备轨迹（C++版本，返回指针）
     *
     * @param DeviceID 设备ID
     * @param TrajectoryType 轨迹类型
     * @return 轨迹数据指针，不存在则返回nullptr
     */
    const FDeviceTrajectory* GetDeviceTrajectoryPtr(int32 DeviceID, ETrajectoryType TrajectoryType) const;

private:
    // ========== 核心数据 ==========

    /** UDP管理器指针 */
    UPROPERTY()
    UUDPManager* UDPManager;

    /** World 上下文，用于场景调试绘制 */
    UPROPERTY()
    UWorld* World;

    /**
     * 轨迹缓存
     * Key: (DeviceID << 1) | TrajectoryType
     * 使用位运算合并DeviceID和TrajectoryType作为唯一键
     */
    UPROPERTY()
    TMap<int32, FDeviceTrajectory> TrajectoryCache;

    /** 轨迹配置 */
    UPROPERTY()
    FTrajectoryConfig Config;

    /** 是否已初始化 */
    bool bIsInitialized;

    // ========== 清理控制 ==========

    /** 上次清理过期点的时间 */
    float LastCleanupTime;

    /** 清理间隔（秒） */
    float CleanupInterval;

    // ========== 统计信息 ==========

    /** 累计处理的消息数 */
    int32 TotalMessagesProcessed;

    /** 累计接收的轨迹数据数 */
    int32 TotalTrajectoriesReceived;

    /** 累计清理的过期点数量 */
    int32 TotalExpiredPointsCleared;

    // ========== 内部辅助函数 ==========

    /**
     * 生成轨迹缓存键
     *
     * @param DeviceID 设备ID
     * @param TrajectoryType 轨迹类型
     * @return 缓存键
     */
    static FORCEINLINE int32 MakeTrajectoryKey(int32 DeviceID, ETrajectoryType TrajectoryType)
    {
        return (DeviceID << 1) | static_cast<int32>(TrajectoryType);
    }

    /**
     * 处理轨迹数据消息
     *
     * @param Data 消息数据
     * @param Length 消息长度
     */
    void ProcessTrajectoryData(const uint8* Data, uint32 Length);

    /**
     * 验证轨迹数据的合法性
     *
     * @param DeviceID 设备ID
     * @param TrajectoryType 轨迹类型
     * @param PointCount 轨迹点数量
     * @return true表示数据合法
     */
    bool ValidateTrajectoryData(int32 DeviceID, uint8 TrajectoryType, int32 PointCount) const;

    /**
     * 清理指定轨迹的过期点
     *
     * @param Trajectory 轨迹数据引用
     * @return 清理的点数量
     */
    int32 CleanupExpiredPointsForTrajectory(FDeviceTrajectory& Trajectory);

    bool EnsureTrajectoryEntry(int32 DeviceID, ETrajectoryType TrajectoryType, FDeviceTrajectory*& OutTrajectoryPtr);
    bool AppendPointToTrajectory(FDeviceTrajectory& Trajectory, const FVector& UELocation, float Timestamp);
    bool ShouldAppendActualPoint(const FDeviceTrajectory& Trajectory, const FVector& UELocation, float Timestamp) const;
    void BuildSmoothedTrajectorySamples(
        const FDeviceTrajectory& Trajectory,
        const FVector& TrajectoryOffset,
        TArray<FVector>& OutPositions,
        TArray<float>& OutTimestamps) const;

    /** 在场景中绘制轨迹调试可视化 */
    void DrawDebugTrajectories() const;
};
