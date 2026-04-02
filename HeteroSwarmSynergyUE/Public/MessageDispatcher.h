// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 通信层
// 作者：Carius
// 日期：2026-02-09

#pragma once

#include "CoreMinimal.h"
#include "UDPProtocolTypes.h"

// 前置声明
class IUDPMessageHandler;  // 修改：IMessageHandler -> IUDPMessageHandler

/**
 * 消息分发器
 *
 * 职责：
 * - 解析UDP消息头（魔数、类型、长度）
 * - 根据MessageType路由到对应的Handler
 * - 执行消息完整性校验（魔数验证、长度验证）
 * - 处理未知消息类型的容错
 * - 统计消息分发情况
 *
 * 消息格式：
 * +------------------+------------------+
 * |   消息头 (8字节)  |   消息体 (变长)   |
 * +------------------+------------------+
 * | MagicNumber(2B) |                  |
 * | MessageType(2B) |    Payload       |
 * | PayloadLen (4B) |                  |
 * +------------------+------------------+
 *
 * 线程安全：
 * - 注册/取消注册需在Game线程调用
 * - Dispatch函数在Game线程调用（由UUDPManager::Tick驱动）
 * - 内部不使用锁（单线程访问）
 *
 * 性能特点：
 * - O(1)查找Handler（TMap）
 * - 零拷贝分发（直接传递指针）
 * - 内联校验函数
 *
 * 使用示例：
 * @code
 * FMessageDispatcher Dispatcher;
 *
 * // 注册处理器
 * Dispatcher.RegisterHandler(0x0001, DeviceStateManager);
 * Dispatcher.RegisterHandler(0x0002, EventMarkerManager);
 *
 * // 分发消息
 * Dispatcher.Dispatch(PacketData, PacketLength);
 * @endcode
 */
class HETEROSWARMSYNERGYUE_API FMessageDispatcher
{
public:
    /** 构造函数 */
    FMessageDispatcher();

    /** 析构函数 */
    ~FMessageDispatcher();

    // ========== 核心接口 ==========

    /**
     * 分发消息到对应的Handler
     *
     * @param Data 原始数据指针（包含消息头+消息体）
     * @param Length 数据总长度（字节）
     *
     * 执行流程：
     * 1. 校验数据长度 >= 8字节（消息头大小）
     * 2. 解析消息头（魔数、类型、负载长度）
     * 3. 校验魔数 = 0xABCD
     * 4. 校验长度一致性（头部声明 == 实际长度）
     * 5. 查找Handler
     * 6. 调用Handler->HandleMessage(Payload, PayloadLength)
     *
     * 线程安全：必须在Game线程调用
     */
    void Dispatch(const uint8* Data, uint32 Length);

    /**
     * 注册消息处理器
     *
     * @param MessageType 消息类型码（例如 0x0001）
     * @param Handler 消息处理器指针（必须实现IUDPMessageHandler接口）
     *
     * 注意：
     * - Handler的生命周期由调用者管理
     * - 重复注册会覆盖之前的Handler并警告
     * - 传入nullptr会被忽略并记录错误
     *
     * 线程安全：必须在Game线程调用
     */
    void RegisterHandler(uint16 MessageType, IUDPMessageHandler* Handler);

    /**
     * 取消注册消息处理器
     *
     * @param MessageType 消息类型码
     *
     * 线程安全：必须在Game线程调用
     */
    void UnregisterHandler(uint16 MessageType);

    /**
     * 检查是否已注册某个消息类型的处理器
     *
     * @param MessageType 消息类型码
     * @return true表示已注册
     */
    bool IsHandlerRegistered(uint16 MessageType) const;

    // ========== 统计接口 ==========

    /**
     * 获取累计分发的消息总数
     * @return 成功分发的消息数量
     */
    int32 GetTotalDispatched() const { return TotalDispatched; }

    /**
     * 获取校验失败的消息数量
     * @return 魔数或长度校验失败的消息数量
     */
    int32 GetValidationFailures() const { return ValidationFailures; }

    /**
     * 获取未知消息类型数量
     * @return 没有注册Handler的消息类型数量
     */
    int32 GetUnknownTypes() const { return UnknownTypes; }

    /**
     * 获取注册的Handler数量
     * @return 当前注册的消息类型数量
     */
    int32 GetRegisteredHandlerCount() const { return HandlerRegistry.Num(); }

    /**
     * 打印统计信息到日志
     */
    void PrintStatistics() const;

    /**
     * 重置统计计数器
     */
    void ResetStatistics();

    /**
     * 获取所有已注册的消息类型列表
     * @return 消息类型码数组
     */
    TArray<uint16> GetRegisteredMessageTypes() const;

private:
    // ========== 内部数据 ==========

    /** Handler注册表（MessageType -> Handler指针） */
    TMap<uint16, IUDPMessageHandler*> HandlerRegistry;  // 修改：IMessageHandler -> IUDPMessageHandler

    // ========== 统计计数器 ==========

    /** 累计成功分发的消息数 */
    int32 TotalDispatched = 0;

    /** 校验失败计数（魔数/长度错误） */
    int32 ValidationFailures = 0;

    /** 未知消息类型计数 */
    int32 UnknownTypes = 0;

    /** 每种消息类型的分发次数统计（用于调试） */
    TMap<uint16, int32> MessageTypeCounters;

    // ========== 常量定义 ==========

    /** 协议魔数 */
    static constexpr uint16 MAGIC_NUMBER = 0xABCD;

    /** 消息头最小长度（字节） */
    static constexpr uint32 HEADER_SIZE = 8;

    // ========== 内部辅助函数 ==========

    /**
     * 校验消息头的完整性
     *
     * @param Header 消息头指针
     * @param TotalLength 数据总长度
     * @return true表示校验通过
     */
    FORCEINLINE bool ValidateHeader(const FUDPMessageHeader* Header, uint32 TotalLength) const;

    /**
     * 记录校验失败日志
     *
     * @param Reason 失败原因
     * @param Data 数据指针（用于打印16进制）
     * @param Length 数据长度
     */
    void LogValidationFailure(const FString& Reason, const uint8* Data, uint32 Length);

    /**
     * 记录未知消息类型
     *
     * @param MessageType 消息类型码
     */
    void LogUnknownMessageType(uint16 MessageType);

    /**
     * 更新消息类型统计计数
     *
     * @param MessageType 消息类型码
     */
    void UpdateMessageTypeCounter(uint16 MessageType);
};