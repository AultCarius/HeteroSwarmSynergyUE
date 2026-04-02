// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 通信层
// 作者：Carius
// 日期：2026-02-09
// 修改：2026-03-13  v2.0 — MAVLink v2 + JSON 混合协议处理

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Containers/Queue.h"
// UDP通信相关头文件（TUniquePtr需要完整定义）
#include "UDPProtocolTypes.h"
#include "FUDPReceiverRunnable.h"
#include "PacketBufferPool.h"
#include "MessageDispatcher.h"

// MAVLink v2 官方生成头文件
#include "mavlink/heteroswarm/mavlink.h"

#include "UDPManager.generated.h"

// 前置声明
class IUDPMessageHandler;
class FSocket;
class FRunnableThread;

DECLARE_MULTICAST_DELEGATE(FOnUDPConfigurationChanged);

// ========== MAVLink 业务层 Delegate 参数结构 ==========

/**
 * MAVLink HEARTBEAT 消息解析结果
 *
 * 对应 MAVLink common HEARTBEAT (msg_id=0)
 * 用于通知HeartbeatManager：某个系统ID的智能体发来了心跳
 */
USTRUCT(BlueprintType)
struct FMAVLinkHeartbeatData
{
    GENERATED_BODY()

    /** MAVLink系统ID（1-255，每个智能体唯一） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 SystemID = 0;

    /** MAVLink组件ID */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 ComponentID = 0;

    /** 自定义模式（对应设备类型，见EDeviceType） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 CustomMode = 0;

    /** MAVLink飞行器类型（MAV_TYPE枚举值） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 MAVType = 0;

    /** 自动驾驶仪类型 */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 Autopilot = 0;

    /** 系统状态（MAV_STATE枚举值） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 SystemStatus = 0;

    /** 接收时间戳（秒） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    float ReceiveTime = 0.0;
};

/**
 * MAVLink LINKS_MOBILE_ROBOT_STATES 消息解析结果
 *
 * 对应 heteroswarm 自定义消息 msg_id=13001
 * 包含单个智能体的完整运动状态
 */
USTRUCT(BlueprintType)
struct FMAVLinkDeviceStateData
{
    GENERATED_BODY()

    /** MAVLink系统ID */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 SystemID = 0;

    /** 设备类型（对应EDeviceType） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 DeviceType = 0;

    /** 系统状态（0:未解锁 1:已解锁） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 Status = 0;

    /** 电量百分比（-1表示未知） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 BatteryRemaining = -1;

    /** GPS定位类型（0-3） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 GPSFixType = 0;

    /** 可见卫星数 */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 SatellitesVisible = 0;

    /** GPS纬度（度） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    float GPSLatitude = 0.0;

    /** GPS经度（度） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    float GPSLongitude = 0.0;

    /** GPS海拔（米） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    float GPSAltitude = 0.0f;

    /** NED位置（米）：X=North Y=East Z=Down */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    FVector NEDPosition = FVector::ZeroVector;

    /** NED速度（米/秒）：X=North Y=East Z=Down */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    FVector NEDVelocity = FVector::ZeroVector;

    /**
     * 姿态四元数（UE顺序：X,Y,Z,W）
     * MAVLink原始为 q[0]=w q[1]=x q[2]=y q[3]=z，解析时已转换
     */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    FQuat Quaternion = FQuat::Identity;

    /** 角速度（rad/s）：X=Roll速率 Y=Pitch速率 Z=Yaw速率 */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    FVector AngularVelocity = FVector::ZeroVector;

    /**
     * 执行器状态数组（最多24个）
     * 四旋翼：[0-3]=螺旋桨RPM [4-6]=云台角度(deg)
     * 机器狗：[0-11]=关节角度(deg) [12-14]=云台角度(deg)
     */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    TArray<float> ActuatorStates;

    /** 接收时间戳（秒） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    float ReceiveTime = 0.0;
};

/**
 * MAVLink LINKS_WAYPOINTS 消息解析结果
 *
 * 对应 heteroswarm 自定义消息 msg_id=13002
 * 包含单个智能体的期望轨迹点列表（NED坐标）
 */
USTRUCT(BlueprintType)
struct FMAVLinkWaypointsData
{
    GENERATED_BODY()

    /** MAVLink系统ID */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    int32 SystemID = 0;

    /**
     * 轨迹点列表（NED坐标，米）
     * X=North  Y=East  Z=Down
     */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    TArray<FVector> Waypoints;

    /** 接收时间戳（秒） */
    UPROPERTY(BlueprintReadOnly, Category = "MAVLink")
    float ReceiveTime = 0.0;
};

// ========== Delegate 类型声明 ==========

/** 收到MAVLink心跳时广播（每个心跳包触发一次） */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMAVLinkHeartbeat, const FMAVLinkHeartbeatData&);

/** 收到MAVLink设备状态时广播 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMAVLinkDeviceState, const FMAVLinkDeviceStateData&);

/** 收到MAVLink轨迹点时广播 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMAVLinkWaypoints, const FMAVLinkWaypointsData&);

// ========== UDP 网络统计 ==========

/**
 * UDP网络统计信息
 */
USTRUCT(BlueprintType)
struct FUDPNetworkStatistics
{
    GENERATED_BODY()

    /** 累计接收包数量 */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    int32 PacketsReceived = 0;

    /** 累计丢弃包数量（魔数校验失败） */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    int32 PacketsDropped = 0;

    /** Socket错误次数 */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    int32 SocketErrors = 0;

    /** 丢包率（百分比，0-100） */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    float PacketLossRate = 0.0f;

    /** 缓冲池当前使用数 */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    int32 BufferPoolInUse = 0;

    /** 缓冲池峰值使用数 */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    int32 BufferPoolPeakUsage = 0;

    /** 缓冲池动态分配次数 */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    int32 BufferPoolDynamicAllocations = 0;

    /** 当前队列中待处理包数量 */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    int32 QueuedPackets = 0;

    /** 活跃的接收Socket数量 */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    int32 ActiveReceiverSockets = 0;

    /** 是否在线 */
    UPROPERTY(BlueprintReadOnly, Category = "UDP Statistics")
    bool bIsOnline = false;
};

/**
 * UDP管理器 - 全局单例
 */
UCLASS()
class HETEROSWARMSYNERGYUE_API UUDPManager : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()

public:
    // ========== 构造/析构 ==========

    /** 构造函数 */
    UUDPManager();

    /** 析构函数 */
    virtual ~UUDPManager();

    // ========== USubsystem 接口 ==========

    /** 子系统初始化（GameInstance创建时调用） */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** 子系统反初始化（GameInstance销毁时调用） */
    virtual void Deinitialize() override;

    // ========== FTickableGameObject 接口 ==========

    /** 每帧Tick函数 */
    virtual void Tick(float DeltaTime) override;

    /** 获取Tick统计ID */
    virtual TStatId GetStatId() const override;

    /** 是否允许Tick（仅在初始化后Tick） */
    virtual bool IsTickable() const override;

    // ========== 蓝图接口 ==========

    /**
     * 初始化UDP系统（简化版本 - 单端口）
     *
     * 适用于简单场景，只需要单个接收端口
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager")
    bool InitializeUDP(const FString& ListenIP = TEXT("0.0.0.0"),
        int32 ListenPort = 8888,
        int32 BufferPoolSize = 100);

    /**
     * 初始化UDP系统（完整版本 - 多端口配置）
     *
     * 使用FUDPConfiguration进行详细配置
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager")
    bool InitializeUDPWithConfig(const FUDPConfiguration& Config, int32 BufferPoolSize = 100);

    /**
     * 关闭UDP系统
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager")
    void ShutdownUDP();

    /**
     * 获取网络统计信息
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager")
    void GetNetworkStatistics(FUDPNetworkStatistics& OutStatistics);

    /**
     * 设置每帧最大处理包数量
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager")
    void SetMaxPacketsPerFrame(int32 MaxCount);

    /**
     * 获取当前配置
     */
    UFUNCTION(BlueprintPure, Category = "UDP Manager")
    FUDPConfiguration GetCurrentConfiguration() const { return CurrentConfig; }

    /**
     * 是否已初始化
     */
    UFUNCTION(BlueprintPure, Category = "UDP Manager")
    bool IsInitialized() const { return bIsInitialized; }

    /**
     * 打印统计信息到日志
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager")
    void PrintStatistics();

    // ========== 动态配置接口 ==========

    /**
     * 添加UDP接收器（运行时热插拔）
     *
     * @param Port 端口号
     * @param Description 接收器描述
     * @return true表示添加成功
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager|Dynamic Config")
    bool AddReceiver(int32 Port, const FString& Description = TEXT("New Receiver"));

    /**
     * 移除UDP接收器（运行时热插拔）
     *
     * @param Port 要移除的端口号
     * @return true表示移除成功
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager|Dynamic Config")
    bool RemoveReceiver(int32 Port);

    /**
     * 启用或禁用指定端口的接收器
     *
     * @param Port 端口号
     * @param bEnabled 是否启用
     * @return true表示操作成功
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager|Dynamic Config")
    bool SetReceiverEnabled(int32 Port, bool bEnabled);

    /**
     * 批量更新接收器配置（会完全重启UDP系统）
     *
     * @param NewConfig 新的完整配置
     * @return true表示更新成功
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager|Dynamic Config")
    bool UpdateConfiguration(const FUDPConfiguration& NewConfig);

    /**
     * 获取所有接收器的配置列表
     *
     * @return 当前接收器配置数组
     */
    UFUNCTION(BlueprintPure, Category = "UDP Manager|Dynamic Config")
    TArray<FUDPReceiverConfig> GetReceivers() const;

    /**
     * 获取指定端口的接收器配置
     *
     * @param Port 端口号
     * @param OutConfig 输出的接收器配置
     * @return true表示找到该端口
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager|Dynamic Config")
    bool GetReceiverByPort(int32 Port, FUDPReceiverConfig& OutConfig) const;

    /**
     * 验证端口号的有效性
     *
     * @param Port 端口号
     * @param OutErrorMessage 错误信息输出
     * @return true表示端口有效
     */
    UFUNCTION(BlueprintCallable, Category = "UDP Manager|Dynamic Config")
    bool ValidatePort(int32 Port, FString& OutErrorMessage) const;

    // ========== 事件委托 ==========

    /**
     * 获取配置变更委托
     *
     * 业务层可以绑定此委托，在UDP配置变更时收到通知
     *
     * @return 配置变更委托引用
     */
    FOnUDPConfigurationChanged& OnConfigurationChanged() { return ConfigurationChangedDelegate; }

    // ========== C++ 接口 ==========
    //TODO: 把messageType换成枚举
    /**
     * 注册消息处理器
     */
    void RegisterMessageHandler(uint16 MessageType, IUDPMessageHandler* Handler);

    /**
     * 取消注册消息处理器
     */
    void UnregisterMessageHandler(uint16 MessageType);

    /**
     * 获取缓冲区对象池指针
     */
    FPacketBufferPool* GetBufferPool() const { return BufferPool.Get(); }

    /**
     * 获取消息分发器指针
     */
    FMessageDispatcher* GetMessageDispatcher() const { return MessageDispatcher.Get(); }

    /**
     * 将JSON字符串封装为自定义UDP帧并发送到指定端口
     *
     * 帧结构：[FUDPMessageHeader 8B][JSON UTF-8 字节]
     *   MagicNumber   = 0xABCD
     *   MessageType   = 传入值（如 0x1003 = JSONEventStatus）
     *   PayloadLength = JSON字节数
     *
     * SendSocket 在首次调用时懒初始化，之后复用，在 CleanupResources() 中销毁。
     *
     * @param MessageType  消息类型码
     * @param JsonString   要发送的 JSON 字符串（FString，内部转 UTF-8）
     * @param TargetPort   目标端口（通常为 10004）
     * @param TargetIP     目标IP（默认广播地址 "255.255.255.255"）
     * @return             true 表示发送成功
     */
    bool SendJSONMessage(uint16 MessageType,
        const FString& JsonString,
        int32 TargetPort,
        const FString& TargetIP = TEXT("255.255.255.255"));

    // ========== MAVLink 业务 Delegate（供GameInstance绑定） ==========

    /**
     * 收到MAVLink HEARTBEAT时广播
     * HeartbeatManager 绑定此委托，管理智能体生命周期
     */
    FOnMAVLinkHeartbeat OnMAVLinkHeartbeat;

    /**
     * 收到MAVLink LINKS_MOBILE_ROBOT_STATES (13001) 时广播
     * DeviceStateManager 绑定此委托，更新Actor位置/姿态
     */
    FOnMAVLinkDeviceState OnMAVLinkDeviceState;

    /**
     * 收到MAVLink LINKS_WAYPOINTS (13002) 时广播
     * TrajectoryManager 绑定此委托，显示期望轨迹
     */
    FOnMAVLinkWaypoints OnMAVLinkWaypoints;

private:
    // ========== 核心组件 ==========

    /** 接收线程 */
    FRunnableThread* ReceiverThread;

    /** 接收线程Runnable对象（管理多个Socket） */
    TUniquePtr<FUDPReceiverRunnable> ReceiverRunnable;

    /** 缓冲区对象池 */
    TUniquePtr<FPacketBufferPool> BufferPool;

    /** 消息分发器 */
    TUniquePtr<FMessageDispatcher> MessageDispatcher;

    /** 原始数据包队列（线程安全） */
    TQueue<FRawPacket> RawPacketQueue;

    /**
     * JSON广播发送Socket（首次调用 SendJSONMessage 时懒初始化）
     * 生命周期：由 CleanupResources() 负责关闭和销毁
     */
    FSocket* SendSocket;


    // ========== 配置参数 ==========

    /** 当前UDP配置 */
    FUDPConfiguration CurrentConfig;

    /** 每帧最大处理包数量 */
    int32 MaxPacketsPerFrame;

    /** 缓冲池大小 */
    int32 BufferPoolSize;

    // ========== 状态标志 ==========

    /** 是否已初始化 */
    bool bIsInitialized;

    // ========== 统计信息 ==========

    /** 累计处理包数量 */
    int32 TotalPacketsProcessed;

    /** 上次打印统计的时间 */
    double LastStatsPrintTime;

    /** 统计打印间隔（秒） */
    float StatsPrintInterval;

    // ========== 内部辅助函数 ==========

    /**
     * 顶层包分发：根据 Packet.ProtocolType 路由到对应处理函数
     */
    void ProcessPacket(FRawPacket& Packet);

    /**
     * 处理自定义UDP包（0xABCD魔数）
     *
     * 内部调用 MessageDispatcher::Dispatch()，
     * 其中 JSON 类型（0x1001/0x1003）由 EventMarkerManager 注册的 Handler 处理。
     */
    void ProcessCustomUDPPacket(FRawPacket& Packet);

    /**
     * 处理 MAVLink v2 包（0xFD起始）
     *
     * 使用 mavlink_parse_char() 逐字节解析，支持粘包/分包。
     * 解析成功的消息传递给 HandleMAVLinkMessage()。
     *
     * @param Packet 原始数据包
     */
    void ProcessMAVLinkPacket(FRawPacket& Packet);

    /**
     * 根据 MAVLink msg_id 分发到对应业务 Delegate
     *
     * 已支持的消息ID：
     *   0     - HEARTBEAT           → OnMAVLinkHeartbeat
     *   13001 - LINKS_MOBILE_ROBOT_STATES → OnMAVLinkDeviceState
     *   13002 - LINKS_WAYPOINTS     → OnMAVLinkWaypoints
     *
     * @param Message    已完整解析的 mavlink_message_t
     * @param SystemID   发送方系统ID（来自 message.sysid）
     * @param ReceiveTime 接收时间戳（秒）
     */
    void HandleMAVLinkMessage(const mavlink_message_t& Message, uint8 SystemID, double ReceiveTime);

    /**
     * 更新统计信息（定期打印日志）
     */
    void UpdateStatistics(double CurrentTime);

    /**
     * 清理所有资源
     */
    void CleanupResources();

    /**
     * 内部初始化实现（被InitializeUDP和InitializeUDPWithConfig调用）
     */
    bool InitializeInternal(const FUDPConfiguration& Config, int32 InBufferPoolSize);

    // ========== 委托 ==========

    /** 配置变更委托（通过 OnConfigurationChanged() 对外暴露） */
    FOnUDPConfigurationChanged ConfigurationChangedDelegate;
};


