// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 协议定义
// 作者：Carius
// 日期：2026-02-09
// 修改：2026-03-13  v2.0 — 添加MAVLink/JSON混合协议支持

#pragma once

#include "CoreMinimal.h"
#include "UDPProtocolTypes.generated.h"

/**
 * UDP通信协议数据结构定义
 *
 * 协议特性（v2.0）：
 * - 支持三种协议并存：MAVLink v2 / 自定义UDP二进制 / 自定义UDP封装JSON
 * - MAVLink v2：以 0xFD 起始，使用官方 mavlink_parse_char() 解析
 * - 自定义UDP：以 0xABCD 起始，小端序，NED坐标系，紧密打包
 * - JSON消息：以 0xABCD 起始，MessageType=0x1001/0x1003，Payload为JSON字符串
 * - 单位：米、米/秒、弧度
 */

 // ========== 协议类型枚举 ==========

 /**
  * 接收到的UDP包所属协议类型
  *
  * 由接收线程（FUDPReceiverRunnable）识别后写入 FRawPacket，
  * UUDPManager::Tick() 根据此字段决定如何分发处理。
  *
  * 识别规则：
  *   首字节 == 0xFD              → MAVLink
  *   首2字节 == 0xABCD（小端序） → CustomUDP（二进制或JSON封装）
  *   其他                        → Unknown，直接丢弃
  */
enum class EProtocolType : uint8
{
    /** 未知/无法识别的协议，丢弃 */
    Unknown = 0,

    /** MAVLink v2 协议（首字节 0xFD） */
    MAVLink = 1,

    /** 自定义UDP协议（首2字节 0xABCD），含二进制消息和JSON封装消息 */
    CustomUDP = 2
};

// ========== 消息头定义 ==========

#pragma pack(push, 1)

/**
 * UDP消息头（8字节）
 *
 * 所有UDP消息的统一头部格式
 */
struct FUDPMessageHeader
{
    /** 魔数，固定为 0xABCD（用于快速校验） */
    uint16 MagicNumber;

    /** 消息类型码（见 EUDPMessageType 枚举） */
    uint16 MessageType;

    /** 消息体长度（字节），不包含消息头本身 */
    uint32 PayloadLength;
};

#pragma pack(pop)

// 编译期检查结构体大小
static_assert(sizeof(FUDPMessageHeader) == 8, "FUDPMessageHeader must be 8 bytes");

// ========== 消息类型枚举 ==========

/**
 * UDP消息类型枚举
 *
 * 0x0001-0x00FF：原有自定义二进制消息类型（保留兼容）
 * 0x1001-0x10FF：新增JSON封装消息类型
 */
enum class EUDPMessageType : uint16
{
    /** 单设备状态 */
    DeviceState = 0x0001,

    /** 批量设备状态（推荐使用，减少UDP包数量） */
    DeviceStateBatch = 0x0011,

    /** 单事件标记 */
    EventMarker = 0x0002,

    /** 批量事件标记 */
    EventMarkerBatch = 0x0012,

    /** 轨迹数据 */
    TrajectoryData = 0x0003,

    /** 点云数据 */
    PointCloudData = 0x0004,

    /** 视频帧数据 */
    VideoFrame = 0x0005,

    /** 控制指令 */
    CommandControl = 0x0006,

    /** 配置更新 */
    ConfigUpdate = 0x0007,

    /** 心跳包（旧自定义协议，MAVLink心跳改为由MAVLink HEARTBEAT消息处理） */
    Heartbeat = 0x00FF,

    // ── JSON封装消息（自定义UDP头部 + JSON字符串Payload）──

    /**
     * JSON场景事件控制消息（接收，端口10003）
     * Payload: JSON字符串，内层 msgid=1001
     * 处理者: EventMarkerManager::HandleJSONEventControl()
     */
    JSONEventControl = 0x1001,

    /**
     * JSON场景事件状态广播消息（发送，端口10004）
     * Payload: JSON字符串，内层 msgid=1003
     * 处理者: EventMarkerManager::BroadcastEventStatuses()
     */
    JSONEventStatus = 0x1003
};

// ========== 基础数据类型 ==========

#pragma pack(push, 1)

/**
 * NED坐标向量（12字节）
 *
 * NED坐标系：
 * - North: 北向（米）
 * - East:  东向（米）
 * - Down:  地向（米，向下为正）
 */
struct FNEDVector
{
    float North;    // 北向坐标（米）
    float East;     // 东向坐标（米）
    float Down;     // 地向坐标（米）
};

/**
 * 姿态角（12字节）
 *
 * 单位：弧度
 */
struct FAttitude
{
    float Roll;     // 横滚角（弧度）
    float Pitch;    // 俯仰角（弧度）
    float Yaw;      // 航向角（弧度）
};

#pragma pack(pop)

// 编译期检查结构体大小
static_assert(sizeof(FNEDVector) == 12, "FNEDVector must be 12 bytes");
static_assert(sizeof(FAttitude) == 12, "FAttitude must be 12 bytes");

// ========== 设备相关 ==========

/**
 * 设备类型枚举
 *
 * 对应 MAVLink LINKS_MOBILE_ROBOT_STATES 中的 type 字段（uint8）。
 * 室内/室外同类设备在渲染上可能有不同模型，因此分开定义。
 */
enum class EDeviceType : uint8
{
    /** 室内四旋翼无人机 (MAVLink type=1) */
    IndoorQuadcopter = 1,

    /** 室外四旋翼无人机 (MAVLink type=2) */
    OutdoorQuadcopter = 2,

    /** 室内无人车 UGV (MAVLink type=3) */
    IndoorUGV = 3,

    /** 室外无人车 UGV (MAVLink type=4) */
    OutdoorUGV = 4,

    /** 机器狗 (MAVLink type=5) */
    RobotDog = 5,

    /** 未知/未支持类型 */
    Unknown = 255
};

#pragma pack(push, 1)

/**
 * 设备状态数据包（44字节）
 *
 * 包含单个设备的完整状态信息
 */
struct FDeviceStatePacket
{
    /** 设备唯一ID */
    uint32 DeviceID;

    /** 设备类型 */
    uint8 DeviceType;

    /** 保留字段1 */
    uint8 Reserved1;

    /** 保留字段2 */
    uint8 Reserved2;

    /** 保留字段3 */
    uint8 Reserved3;

    /** 位置（NED坐标，米） */
    FNEDVector Position;

    /** 姿态（弧度） */
    FAttitude Attitude;

    /** 速度（NED坐标，米/秒） */
    FNEDVector Velocity;
};

#pragma pack(pop)

// 编译期检查结构体大小
static_assert(sizeof(FDeviceStatePacket) == 44, "FDeviceStatePacket must be 44 bytes");

// ========== 事件相关 ==========

/**
 * 事件类型枚举
 */
enum class EEventType : uint8
{
    // 道路巡检 (0-9)
    RoadWaterlogging = 0,      // 积水
    RoadObstacle = 1,      // 障碍物
    IllegalParking = 2,      // 违章停车

    // 环境勘探 (10-19)
    IllegalDischarge = 10,     // 非法排污口
    DustSource = 11,     // 裸露扬尘源
    ConstructionWaste = 12,     // 建筑垃圾

    // 室内楼宇 (20-29)
    IndoorMaterialTransport = 20,   // 物资运输需求
    IndoorInspection = 21,   // 巡检需求
    IndoorCleaning = 22,   // 清洁需求

    // 室外 (30-39)
    OutdoorMaterialTransport = 30,  // 物资运输
    OutdoorInspection = 31,  // 巡检需求

    Unknown = 255               // 未知类型
};

/**
 * 事件状态枚举
 */
enum class EEventState : uint8
{
    /** 初始状态 */
    Initial = 0,

    /** 高亮状态 */
    Highlighted = 1,

    /** 消失状态 */
    Disappeared = 2
};

#pragma pack(push, 1)

/**
 * 事件标记数据包（24字节）
 */
struct FEventMarkerPacket
{
    /** 事件唯一ID */
    uint32 EventID;

    /** 事件类型 */
    uint8 EventType;

    /** 事件状态 */
    uint8 State;

    /** 保留字段1 */
    uint8 Reserved1;

    /** 保留字段2 */
    uint8 Reserved2;

    /** 事件位置（NED坐标，米） */
    FNEDVector Position;

    /** 保留字段3 */
    uint32 Reserved3;
};

#pragma pack(pop)

// 编译期检查结构体大小
static_assert(sizeof(FEventMarkerPacket) == 24, "FEventMarkerPacket must be 24 bytes");

// ========== 轨迹相关 ==========

/**
 * 轨迹类型枚举（蓝图可用）
 */
UENUM(BlueprintType)
enum class ETrajectoryType : uint8
{
    /** 期望轨迹（规划路径） */
    Planned = 0 UMETA(DisplayName = "期望轨迹"),

    /** 实际轨迹（真实运行） */
    Actual = 1 UMETA(DisplayName = "实际轨迹")
};

#pragma pack(push, 1)

/**
 * 轨迹点数据包（12字节）
 *
 * 用于UDP网络传输，NED坐标系
 */
struct FTrajectoryPointPacket
{
    /** 轨迹点位置（NED坐标，米） */
    FNEDVector Position;
};

#pragma pack(pop)

// 编译期检查结构体大小
static_assert(sizeof(FTrajectoryPointPacket) == 12, "FTrajectoryPointPacket must be 12 bytes");



// ========== 点云相关 ==========

#pragma pack(push, 1)

/**
 * 点云点（16字节）
 */
struct FPointCloudPoint
{
    /** 点位置（NED坐标，米） */
    FNEDVector Position;

    /** 强度值（0.0-1.0） */
    float Intensity;
};

#pragma pack(pop)

// 编译期检查结构体大小
static_assert(sizeof(FPointCloudPoint) == 16, "FPointCloudPoint must be 16 bytes");

// ========== 队列传递用的运行时结构 ==========

/**
 * 原始数据包结构（队列传递用）
 * 用于从Receiver线程传递到Game线程
 *
 * 注意：使用TSharedPtr管理缓冲区，支持拷贝但保持所有权语义
 */
struct FRawPacket
{
    /** 数据缓冲区指针（共享所有权，但逻辑上单一所有者） */
    TSharedPtr<TArray<uint8>> Data;

    /** 数据长度（字节） */
    int32 Length = 0;

    /** 发送方地址（IP:Port格式） */
    FString SenderAddress;

    /** 接收时间戳（秒） */
    double ReceiveTime = 0.0;

    /**
     * 协议类型（由接收线程识别后填入）
     *
     * UUDPManager::Tick() 根据此字段决定分发路径：
     *   MAVLink   → ProcessMAVLinkPacket()
     *   CustomUDP → MessageDispatcher（二进制）或 JSON解析
     *   Unknown   → 直接丢弃，不进入业务逻辑
     */
    EProtocolType ProtocolType = EProtocolType::Unknown;

    /** 默认构造 */
    FRawPacket() = default;

    /** 拷贝构造（允许，但共享所有权） */
    FRawPacket(const FRawPacket& Other) = default;

    /** 拷贝赋值 */
    FRawPacket& operator=(const FRawPacket& Other) = default;

    /** 移动构造 */
    FRawPacket(FRawPacket&& Other) noexcept = default;

    /** 移动赋值 */
    FRawPacket& operator=(FRawPacket&& Other) noexcept = default;
};

// ========== 辅助函数 ==========

/**
 * 将消息类型枚举转换为字符串（用于日志）
 */
inline FString UDPMessageTypeToString(EUDPMessageType Type)
{
    switch (Type)
    {
    case EUDPMessageType::DeviceState:       return TEXT("DeviceState");
    case EUDPMessageType::DeviceStateBatch:  return TEXT("DeviceStateBatch");
    case EUDPMessageType::EventMarker:       return TEXT("EventMarker");
    case EUDPMessageType::EventMarkerBatch:  return TEXT("EventMarkerBatch");
    case EUDPMessageType::TrajectoryData:    return TEXT("TrajectoryData");
    case EUDPMessageType::PointCloudData:    return TEXT("PointCloudData");
    case EUDPMessageType::VideoFrame:        return TEXT("VideoFrame");
    case EUDPMessageType::CommandControl:    return TEXT("CommandControl");
    case EUDPMessageType::ConfigUpdate:      return TEXT("ConfigUpdate");
    case EUDPMessageType::Heartbeat:         return TEXT("Heartbeat(Legacy)");
    case EUDPMessageType::JSONEventControl:  return TEXT("JSONEventControl");
    case EUDPMessageType::JSONEventStatus:   return TEXT("JSONEventStatus");
    default:                                  return TEXT("Unknown");
    }
}

/**
 * 将协议类型枚举转换为字符串（用于日志）
 */
inline FString ProtocolTypeToString(EProtocolType Type)
{
    switch (Type)
    {
    case EProtocolType::MAVLink:    return TEXT("MAVLink");
    case EProtocolType::CustomUDP:  return TEXT("CustomUDP");
    case EProtocolType::Unknown:    return TEXT("Unknown");
    default:                        return TEXT("Invalid");
    }
}

/**
 * 将设备类型枚举转换为字符串
 */
inline FString DeviceTypeToString(EDeviceType Type)
{
    switch (Type)
    {
    case EDeviceType::IndoorQuadcopter:  return TEXT("IndoorQuadcopter");
    case EDeviceType::OutdoorQuadcopter: return TEXT("OutdoorQuadcopter");
    case EDeviceType::IndoorUGV:         return TEXT("IndoorUGV");
    case EDeviceType::OutdoorUGV:        return TEXT("OutdoorUGV");
    case EDeviceType::RobotDog:          return TEXT("RobotDog");
    case EDeviceType::Unknown:           return TEXT("Unknown");
    default:                             return TEXT("Invalid");
    }
}

/**
 * 将事件类型枚举转换为字符串
 */
inline FString EventTypeToString(EEventType Type)
{
    switch (Type)
    {
    case EEventType::RoadWaterlogging:          return TEXT("RoadWaterlogging");
    case EEventType::RoadObstacle:              return TEXT("RoadObstacle");
    case EEventType::IllegalParking:            return TEXT("IllegalParking");
    case EEventType::IllegalDischarge:          return TEXT("IllegalDischarge");
    case EEventType::DustSource:                return TEXT("DustSource");
    case EEventType::ConstructionWaste:         return TEXT("ConstructionWaste");
    case EEventType::IndoorMaterialTransport:   return TEXT("IndoorMaterialTransport");
    case EEventType::IndoorInspection:          return TEXT("IndoorInspection");
    case EEventType::IndoorCleaning:            return TEXT("IndoorCleaning");
    case EEventType::OutdoorMaterialTransport:  return TEXT("OutdoorMaterialTransport");
    case EEventType::OutdoorInspection:         return TEXT("OutdoorInspection");
    case EEventType::Unknown:                   return TEXT("Unknown");
    default:                                    return TEXT("Invalid");
    }
}

/**
 * 将事件状态枚举转换为字符串
 */
inline FString EventStateToString(EEventState State)
{
    switch (State)
    {
    case EEventState::Initial:      return TEXT("Initial");
    case EEventState::Highlighted:  return TEXT("Highlighted");
    case EEventState::Disappeared:  return TEXT("Disappeared");
    default:                        return TEXT("Invalid");
    }
}

// ========== UDP通信配置 ==========

/**
 * UDP接收端口配置
 *
 * 用于配置多个UDP接收端口，每个端口可以接收来自任意IP的数据
 */
USTRUCT(BlueprintType)
struct FUDPReceiverConfig
{
    GENERATED_BODY()

    /** 本地监听端口 */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    int32 LocalPort;

    /** 用途描述（用于日志和调试） */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    FString Description;

    /** 是否启用此接收端口 */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    bool bEnabled;

    /** 接收缓冲区大小（字节，默认64KB） */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    int32 BufferSize;

    /** 默认构造 */
    FUDPReceiverConfig()
        : LocalPort(0)
        , Description(TEXT(""))
        , bEnabled(true)
        , BufferSize(65536)
    {
    }

    /** 带参数构造 */
    FUDPReceiverConfig(int32 InPort, const FString& InDescription, bool bInEnabled = true)
        : LocalPort(InPort)
        , Description(InDescription)
        , bEnabled(bInEnabled)
        , BufferSize(65536)
    {
    }
};

/**
 * UDP发送目标配置
 *
 * 用于配置多个UDP发送目标，支持向不同IP:Port发送数据
 */
USTRUCT(BlueprintType)
struct FUDPSenderConfig
{
    GENERATED_BODY()

    /** 目标IP地址 */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    FString TargetIP;

    /** 目标端口 */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    int32 TargetPort;

    /** 用途描述（用于日志和调试） */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    FString Description;

    /** 是否启用此发送目标 */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    bool bEnabled;

    /** 默认构造 */
    FUDPSenderConfig()
        : TargetIP(TEXT(""))
        , TargetPort(0)
        , Description(TEXT(""))
        , bEnabled(true)
    {
    }

    /** 带参数构造 */
    FUDPSenderConfig(const FString& InIP, int32 InPort, const FString& InDescription, bool bInEnabled = true)
        : TargetIP(InIP)
        , TargetPort(InPort)
        , Description(InDescription)
        , bEnabled(bInEnabled)
    {
    }
};

/**
 * UDP通信总配置
 *
 * 包含所有接收端口和发送目标的配置
 */
USTRUCT(BlueprintType)
struct FUDPConfiguration
{
    GENERATED_BODY()

    /** 接收端口配置列表 */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    TArray<FUDPReceiverConfig> Receivers;

    /** 发送目标配置列表 */
    UPROPERTY(BlueprintReadWrite, Category = "UDP Config")
    TArray<FUDPSenderConfig> Senders;

    /** 默认构造 */
    FUDPConfiguration() = default;

    /**
     * 添加接收端口
     */
    void AddReceiver(int32 Port, const FString& Description, bool bEnabled = true)
    {
        Receivers.Add(FUDPReceiverConfig(Port, Description, bEnabled));
    }

    /**
     * 添加发送目标
     */
    void AddSender(const FString& IP, int32 Port, const FString& Description, bool bEnabled = true)
    {
        Senders.Add(FUDPSenderConfig(IP, Port, Description, bEnabled));
    }

    /**
     * 获取启用的接收端口数量
     */
    int32 GetEnabledReceiverCount() const
    {
        int32 Count = 0;
        for (const FUDPReceiverConfig& Config : Receivers)
        {
            if (Config.bEnabled)
            {
                Count++;
            }
        }
        return Count;
    }

    /**
     * 获取启用的发送目标数量
     */
    int32 GetEnabledSenderCount() const
    {
        int32 Count = 0;
        for (const FUDPSenderConfig& Config : Senders)
        {
            if (Config.bEnabled)
            {
                Count++;
            }
        }
        return Count;
    }
};

// ========== Socket上下文结构 ==========

/**
 * Socket上下文信息
 *
 * 用于接收线程管理单个Socket及其相关数据
 */
struct FSocketContext
{
    /** Socket指针 */
    FSocket* Socket;

    /** 绑定的本地端口 */
    int32 LocalPort;

    /** 用途描述 */
    FString Description;

    /** 接收缓冲区 */
    TSharedPtr<TArray<uint8>> ReceiveBuffer;

    /** 该Socket是否已初始化 */
    bool bIsInitialized;

    /** 默认构造 */
    FSocketContext()
        : Socket(nullptr)
        , LocalPort(0)
        , Description(TEXT(""))
        , bIsInitialized(false)
    {
        ReceiveBuffer = MakeShared<TArray<uint8>>();
    }

    /** 带参数构造 */
    FSocketContext(FSocket* InSocket, int32 InPort, const FString& InDescription, int32 BufferSize)
        : Socket(InSocket)
        , LocalPort(InPort)
        , Description(InDescription)
        , bIsInitialized(true)
    {
        ReceiveBuffer = MakeShared<TArray<uint8>>();
        ReceiveBuffer->SetNumUninitialized(BufferSize);
    }

    /** 析构时不自动释放Socket（由管理器负责） */
    ~FSocketContext() = default;
};