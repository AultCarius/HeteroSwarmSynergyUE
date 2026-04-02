// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 接收层
// 作者：Carius
// 日期：2026-02-09
// 修改：2026-02-12  多Socket版本
// 修改：2026-03-13  v2.0 — 混合协议识别（MAVLink + CustomUDP）

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Containers/Queue.h"

// 前置声明
class FPacketBufferPool;
struct FRawPacket;
struct FUDPReceiverConfig;
struct FSocketContext;
enum class EProtocolType : uint8;

/**
 * UDP接收工作线程（多Socket + 混合协议识别版本）
 *
 * 职责：
 * - 在单个工作线程中管理多个UDP接收Socket
 * - 使用多路复用技术轮询所有Socket，避免线程过多
 * - 识别协议类型（MAVLink v2 / 自定义UDP / 未知）并写入FRawPacket
 * - 将有效数据包推入线程安全队列供Game线程处理
 * - 使用对象池管理缓冲区，避免频繁内存分配
 *
 * 协议识别规则（仅接收层，不解析内容）：
 * - 首字节 == 0xFD            → EProtocolType::MAVLink
 * - 首2字节 == 0xABCD（小端） → EProtocolType::CustomUDP
 * - 其他                      → EProtocolType::Unknown，丢弃
 *
 * 注意：MAVLink包不再校验最小长度为8字节（MAVLink v2头部为10字节），
 * 但接收层只做协议识别，具体校验由UUDPManager的mavlink_parse_char()完成。
 *
 * 线程安全原则：
 * - 严格不访问任何UObject
 * - 仅通过TQueue与Game线程通信
 * - 使用FThreadSafeBool控制停止标志
 */
class HETEROSWARMSYNERGYUE_API FUDPReceiverRunnable : public FRunnable
{
public:
    /**
     * 构造函数（多Socket版本）
     *
     * @param InReceiverConfigs 接收端口配置列表
     * @param InOutputQueue 输出队列，用于向Game线程传递接收到的数据包
     * @param InBufferPool 缓冲区对象池，用于管理接收缓冲区内存
     */
    FUDPReceiverRunnable(
        const TArray<FUDPReceiverConfig>& InReceiverConfigs,
        TQueue<FRawPacket>* InOutputQueue,
        FPacketBufferPool* InBufferPool
    );

    /** 析构函数 */
    virtual ~FUDPReceiverRunnable();

    // ========== FRunnable 接口 ==========

    /**
     * 初始化线程（在工作线程启动前调用）
     * @return true表示初始化成功，false将阻止线程启动
     */
    virtual bool Init() override;

    /**
     * 线程主循环（在工作线程中执行）
     * @return 线程退出码（0表示正常退出）
     */
    virtual uint32 Run() override;

    /**
     * 停止线程（在Game线程中调用，设置停止标志）
     */
    virtual void Stop() override;

    /**
     * 线程退出清理（在工作线程退出后调用）
     */
    virtual void Exit() override;

    // ========== 统计接口 ==========

    /**
     * 获取累计接收包数量
     * @return 线程启动以来接收到的有效包总数
     */
    int32 GetPacketsReceived() const { return PacketsReceived.GetValue(); }

    /**
     * 获取累计丢弃包数量（魔数校验失败）
     * @return 线程启动以来丢弃的无效包总数
     */
    int32 GetPacketsDropped() const { return PacketsDropped.GetValue(); }

    /**
     * 获取Socket错误计数
     * @return RecvFrom失败次数
     */
    int32 GetSocketErrors() const { return SocketErrors.GetValue(); }

    /**
     * 获取活跃的Socket数量
     * @return 当前正在监听的Socket总数
     */
    int32 GetActiveSocketCount() const { return SocketContexts.Num(); }

private:
    // ========== 核心组件 ==========

    /** Socket上下文数组（管理多个接收Socket） */
    TArray<FSocketContext> SocketContexts;

    /** 输出队列指针（线程安全队列，由UUDPManager管理） */
    TQueue<FRawPacket>* OutputQueue;

    /** 缓冲区对象池指针（由UUDPManager管理） */
    FPacketBufferPool* BufferPool;

    // ========== 线程控制 ==========

    /** 停止标志（线程安全） */
    FThreadSafeBool bStopping;

    /** 线程名称（用于调试） */
    FString ThreadName;

    // ========== 统计计数器 ==========

    /** 接收包计数（线程安全） */
    FThreadSafeCounter PacketsReceived;

    /** 丢弃包计数（线程安全） */
    FThreadSafeCounter PacketsDropped;

    /** Socket错误计数（线程安全） */
    FThreadSafeCounter SocketErrors;

    // ========== 常量定义 ==========

    /** UDP最大负载大小（65535 - 20(IP头) - 8(UDP头) = 65507字节） */
    static constexpr uint32 MAX_PACKET_SIZE = 65507;

    /** 自定义UDP协议魔数（小端序读取为 0xABCD） */
    static constexpr uint16 CUSTOM_UDP_MAGIC = 0xABCD;

    /** MAVLink v2 起始字节 */
    static constexpr uint8 MAVLINK_V2_STX = 0xFD;

    /**
     * 自定义UDP最小有效包大小（8字节）
     * MagicNumber(2) + MessageType(2) + PayloadLength(4)
     */
    static constexpr uint32 MIN_CUSTOM_UDP_SIZE = 8;

    /**
     * MAVLink v2 最小有效包大小（12字节）
     * STX(1)+LEN(1)+INC_FLAGS(1)+CMP_FLAGS(1)+SEQ(1)+SYSID(1)+COMPID(1)+MSGID(3)+CRC(2) = 12
     * (不含Payload，最小消息如HEARTBEAT含Payload共28字节)
     */
    static constexpr uint32 MIN_MAVLINK_SIZE = 12;

    /** RecvFrom失败后的休眠时间（毫秒），避免空转占用CPU */
    static constexpr float RECV_FAIL_SLEEP_MS = 1.0f;

    /** 轮询间隔（毫秒），避免空转 */
    static constexpr float POLL_INTERVAL_MS = 1.0f;

    // ========== 内部辅助函数 ==========

    /**
     * 创建并绑定单个接收Socket
     * @param Config 接收端口配置
     * @param OutContext 输出Socket上下文
     * @return true表示创建成功
     */
    bool CreateReceiverSocket(const FUDPReceiverConfig& Config, FSocketContext& OutContext);

    /**
     * 处理单个Socket的数据接收
     * @param Context Socket上下文
     * @return true表示成功接收到数据
     */
    bool ProcessSocketData(FSocketContext& Context);

    /**
     * 识别数据包所属协议类型
     *
     * 仅检查首字节/首2字节，不解析内容，保持接收层轻量。
     * 识别结果写入 FRawPacket.ProtocolType，Unknown包会被丢弃。
     *
     * @param Buffer 数据缓冲区原始指针
     * @param Length 数据长度（字节）
     * @return 识别到的协议类型
     */
    EProtocolType IdentifyProtocol(const uint8* Buffer, int32 Length) const;

    /**
     * 记录接收错误日志
     * @param ErrorCode Socket错误码
     * @param SocketDescription Socket描述（用于区分不同端口）
     */
    void LogReceiveError(ESocketErrors ErrorCode, const FString& SocketDescription);

    /**
     * 清理所有Socket资源
     */
    void CleanupSockets();
};