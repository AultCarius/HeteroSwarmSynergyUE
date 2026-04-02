// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - JSON消息类型定义
// 作者：Carius
// 日期：2026-03-13
//
// 说明：
//   定义 JSON 消息的 C++ 数据结构，供 JSONMessageParser 和 EventMarkerManager 使用。
//   所有结构与协议文件 events.json / control.json 中的字段一一对应。
//
//   消息流方向：
//     msgid=1001  外部系统 → UE（EventControl，接收，端口10003）
//     msgid=1003  UE → 外部系统（EventStatus，发送，端口10004）
//     msgid=2001  外部系统 → UE（SceneControl，接收，端口10003）

#pragma once

#include "CoreMinimal.h"

// ========== 事件操作类型枚举 ==========

/**
 * JSON事件操作类型
 *
 * 对应 events.json args[].operation 字段（msgid=1001）
 */
enum class EJSONEventOperation : uint8
{
    /** 未知操作，解析失败或字段缺失 */
    Unknown = 0,

    /** 创建事件标记（"create"） */
    Create = 1,

    /** 激活/高亮事件标记（"activate"） */
    Activate = 2,

    /** 关闭/消失事件标记（"close"） */
    Close = 3
};

// ========== msgid=1001 事件控制消息结构 ==========

/**
 * 单个事件条目
 *
 * 对应 events.json（msgid=1001）的 args 数组中的一个元素：
 * {
 *   "id":        int32,
 *   "type":      uint8,         // 类型码（101/102/103等）
 *   "operation": string,        // "create" / "activate" / "close"
 *   "pose":      [6 float],     // [North, East, Down, Roll, Pitch, Yaw]（米/弧度）
 * }
 */
struct FJSONEventEntry
{
    /** 事件唯一ID */
    int32 EventID = 0;

    /** 事件类型码 */
    uint8 EventType = 0;

    /** 操作类型 */
    EJSONEventOperation Operation = EJSONEventOperation::Unknown;

    /** NED坐标（米）：X=North Y=East Z=Down */
    FVector NEDPosition = FVector::ZeroVector;

    /** 姿态角（弧度）：X=Roll Y=Pitch Z=Yaw */
    FVector Attitude = FVector::ZeroVector;
};

/**
 * JSON事件控制消息（接收方向，端口10003）
 *
 * 对应 events.json 中 msgid=1001 的完整消息：
 * {
 *   "time":      string,
 *   "msgid":     1001,
 *   "publisher": string,
 *   "args":      [ FJSONEventEntry ... ]
 * }
 */
struct FJSONEventControlMessage
{
    /** 消息内部ID（期望=1001） */
    int32 MsgID = 0;

    /** 发布者标识 */
    FString Publisher;

    /** 时间戳字符串（仅记录） */
    FString Timestamp;

    /** 事件条目数组（一条消息可包含多个事件操作） */
    TArray<FJSONEventEntry> Events;
};

// ========== msgid=1003 事件状态广播消息结构 ==========

/**
 * JSON事件状态条目（发送方向，端口10004）
 *
 * 对应 events.json 中 msgid=1003 的 args 字段：
 * {
 *   "id":     int32,
 *   "type":   uint8,
 *   "status": string,  // "created" / "activated" / "closed"
 *   "pose":   [6 float]
 * }
 */
struct FJSONEventStatusEntry
{
    /** 事件ID */
    int32 EventID = 0;

    /** 事件类型码 */
    uint8 EventType = 0;

    /** 状态字符串（"created" / "activated" / "closed"） */
    FString Status;

    /** NED坐标（米） */
    FVector NEDPosition = FVector::ZeroVector;

    /** 姿态角（弧度） */
    FVector Attitude = FVector::ZeroVector;
};

/**
 * JSON事件状态广播消息（发送方向，端口10004）
 *
 * 对应 events.json 中 msgid=1003 的结构。
 * 每次广播发送一个事件的当前状态（args 为对象，非数组）。
 */
struct FJSONEventStatusMessage
{
    /** 消息ID（固定=1003） */
    int32 MsgID = 1003;

    /** 发布者（固定="UEGCS"） */
    FString Publisher = TEXT("UEGCS");

    /** 时间戳（ISO 8601；为空时由序列化函数自动填充当前时间） */
    FString Timestamp;

    /** 单个事件状态 */
    FJSONEventStatusEntry EventStatus;
};

// ========== msgid=2001 场景控制消息结构 ==========

/**
 * JSON场景控制消息（接收方向，端口10003）
 *
 * 对应 control.json 中 msgid=2001：
 * {
 *   "time":      string,
 *   "msgid":     2001,
 *   "publisher": string,
 *   "args": {
 *     "id":            int32,
 *     "name":          string,
 *     "operation":     string,  // "start" / "stop" / "pause" / "resume"
 *     "emergencystop": bool
 *   }
 * }
 */
struct FJSONSceneControlMessage
{
    /** 消息内部ID（期望=2001） */
    int32 MsgID = 0;

    /** 发布者标识 */
    FString Publisher;

    /** 时间戳字符串 */
    FString Timestamp;

    /** 场景ID */
    int32 SceneID = 0;

    /** 场景名称 */
    FString SceneName;

    /** 操作类型："start" / "stop" / "pause" / "resume" */
    FString Operation;

    /** 紧急停止标志 */
    bool bEmergencyStop = false;
};

// ========== 辅助内联转换函数 ==========

/**
 * 将操作字符串解析为 EJSONEventOperation 枚举（大小写不敏感）
 */
inline EJSONEventOperation ParseOperationString(const FString& OpStr)
{
    if (OpStr.Equals(TEXT("create"), ESearchCase::IgnoreCase)) return EJSONEventOperation::Create;
    if (OpStr.Equals(TEXT("activate"), ESearchCase::IgnoreCase)) return EJSONEventOperation::Activate;
    if (OpStr.Equals(TEXT("close"), ESearchCase::IgnoreCase)) return EJSONEventOperation::Close;
    return EJSONEventOperation::Unknown;
}

/**
 * 将 EJSONEventOperation 枚举转换为运行时状态码（uint8）
 *
 *   Create   → 0  (Initial)
 *   Activate → 1  (Highlighted)
 *   Close    → 2  (Disappeared)
 *   Unknown  → 0xFF（调用方需检查此值）
 */
inline uint8 JSONEventOperationToState(EJSONEventOperation Op)
{
    switch (Op)
    {
    case EJSONEventOperation::Create:   return 0;
    case EJSONEventOperation::Activate: return 1;
    case EJSONEventOperation::Close:    return 2;
    default:                            return 0xFF;
    }
}

/**
 * 将运行时状态码转换为 JSON status 字符串
 *
 *   0 → "created"
 *   1 → "activated"
 *   2 → "closed"
 *   其他 → "unknown"
 */
inline FString StateToJSONStatusString(uint8 State)
{
    switch (State)
    {
    case 0:  return TEXT("created");
    case 1:  return TEXT("activated");
    case 2:  return TEXT("closed");
    default: return TEXT("unknown");
    }
}