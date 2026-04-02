// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - JSON消息解析器
// 作者：Carius
// 日期：2026-03-13
//
// 说明：
//   纯静态工具类，负责 JSON 字符串 ↔ 消息结构体的双向转换。
//   不持有状态，不依赖 UObject 体系，仅在 Game 线程调用。
//   使用 UE 内置 FJsonObject / FJsonSerializer（需在 Build.cs 中引用 "Json" 模块）。

#pragma once

#include "CoreMinimal.h"
#include "JSONMessageTypes.h"

// FJsonObject / FJsonValue 前置声明，实现文件中再包含 Json.h
class FJsonObject;
class FJsonValue;

/**
 * JSON消息解析器（纯静态工具类）
 *
 * 提供两类接口：
 *   Parse*()    : UDP Payload（UTF-8 JSON 字符串）→ 结构体
 *   Generate*() : 结构体 → JSON 字符串（用于发送）
 */
class HETEROSWARMSYNERGYUE_API FJSONMessageParser
{
public:
    // ========== 解析接口（JSON字符串 → 结构体）==========

    /**
     * 解析 msgid=1001 事件控制消息
     *
     * args 为数组格式，一条消息可含多个事件条目。
     *
     * @param JsonString    UTF-8 JSON 字符串
     * @param OutMessage    输出：解析结果
     * @return              true 表示解析成功且至少含一个有效事件条目
     */
    static bool ParseEventControl(const FString& JsonString, FJSONEventControlMessage& OutMessage);

    /**
     * 解析 msgid=2001 场景控制消息
     *
     * args 为对象格式（非数组）。
     *
     * @param JsonString    UTF-8 JSON 字符串
     * @param OutMessage    输出：解析结果
     * @return              true 表示解析成功
     */
    static bool ParseSceneControl(const FString& JsonString, FJSONSceneControlMessage& OutMessage);

    // ========== 生成接口（结构体 → JSON字符串）==========

    /**
     * 生成 msgid=1003 事件状态广播消息 JSON 字符串
     *
     * 使用紧凑格式（无缩进）以节省带宽。
     * 若 Message.Timestamp 为空，自动填充当前 UTC 时间。
     *
     * @param Message       消息结构体
     * @param OutJsonString 输出：序列化后的 JSON 字符串
     * @return              true 表示序列化成功
     */
    static bool GenerateEventStatus(const FJSONEventStatusMessage& Message, FString& OutJsonString);

private:
    /**
     * 从 JsonObject 解析单个事件条目（args 数组中的一个元素）
     *
     * id / type / operation 为必填字段；pose 为可选（缺失时填零值，不拒绝整条消息）。
     */
    static bool ParseEventEntry(
        const TSharedPtr<FJsonObject>& EntryObject,
        FJSONEventEntry& OutEntry);

    /**
     * 将 6 元素 pose 数组解析为 NED 位置和姿态角
     *
     * pose 格式：[North(m), East(m), Down(m), Roll(rad), Pitch(rad), Yaw(rad)]
     */
    static bool ParsePoseArray(
        const TArray<TSharedPtr<FJsonValue>>& PoseArray,
        FVector& OutPosition,
        FVector& OutAttitude);

    /**
     * 生成当前 UTC 时间的 ISO 8601 字符串
     *
     * 格式：2026-03-13T12:00:00Z
     */
    static FString GetCurrentISO8601Timestamp();

    // 禁止实例化
    FJSONMessageParser() = delete;
};