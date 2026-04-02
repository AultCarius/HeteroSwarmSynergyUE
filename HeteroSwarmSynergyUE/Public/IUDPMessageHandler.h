// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 业务逻辑层
// 作者：Carius
// 日期：2026-02-09

#pragma once

#include "CoreMinimal.h"

/**
 * UDP消息处理器接口
 *
 * 所有业务管理器（DeviceStateManager、EventMarkerManager等）都必须实现此接口
 *
 * 职责：
 * - 提供统一的消息处理入口
 * - 由FMessageDispatcher调用
 *
 * 实现要求：
 * - HandleMessage必须是线程安全的（在Game线程调用）
 * - 不应阻塞执行（快速处理并返回）
 * - 应处理所有可能的数据格式错误
 *
 * 使用示例：
 * @code
 * class UDeviceStateManager : public UObject, public IUDPMessageHandler
 * {
 * public:
 *     virtual void HandleMessage(const uint8* Data, uint32 Length) override
 *     {
 *         // 解析并处理设备状态数据
 *     }
 * };
 * @endcode
 */
class HETEROSWARMSYNERGYUE_API IUDPMessageHandler
{
public:
    /** 虚析构函数 */
    virtual ~IUDPMessageHandler() = default;

    /**
     * 处理消息
     *
     * @param Data 消息体数据指针（不包含消息头）
     * @param Length 消息体长度（字节）
     *
     * 注意：
     * - Data指向的内存由调用者管理，不要保存指针
     * - 如需保存数据，必须拷贝到自己的内存中
     * - 此函数必须快速返回，不要执行耗时操作
     * - 必须处理所有可能的数据格式错误（长度不足、数据非法等）
     */
    virtual void HandleMessage(const uint8* Data, uint32 Length) = 0;
};