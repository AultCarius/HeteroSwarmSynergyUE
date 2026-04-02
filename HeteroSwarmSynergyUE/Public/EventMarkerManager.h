// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 业务逻辑层
// 作者：Carius
// 日期：2026-02-09
// 修改：2026-03-13  v2.0 — 新增JSON事件控制接收路径 + 事件状态定时广播

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "IUDPMessageHandler.h"
#include "UDPProtocolTypes.h"
#include "JSONMessageTypes.h"        // 新增：JSON消息类型定义
#include "EventMarkerManager.generated.h"

// 前置声明
class UUDPManager;

/**
 * 事件标记运行时状态（蓝图可用）
 *
 * 此结构体包含事件标记在UE中的运行时状态信息
 * 所有坐标都已转换为UE坐标系
 */
USTRUCT(BlueprintType)
struct FEventMarkerRuntimeState
{
    GENERATED_BODY()

    /** 事件唯一ID */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    int32 EventID = 0;

    /** 事件类型（见EEventType枚举） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    uint8 EventType = 0;

    /** 事件状态（0=初始, 1=高亮, 2=消失） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    uint8 State = 0;

    /** 事件位置（UE坐标，厘米） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    FVector Location = FVector::ZeroVector;

    /**
     * 事件位置（NED坐标，米）
     * 保存原始NED值，供广播回外部系统时使用
     */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    FVector NEDPosition = FVector::ZeroVector;

    /** 姿态角（弧度：Roll/Pitch/Yaw），来自 JSON pose[3..5] */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    FVector Attitude = FVector::ZeroVector;

    /** 创建时间（秒，相对于程序启动） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    float CreationTime = 0.0f;

    /** 最后更新时间（秒） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    float LastUpdateTime = 0.0f;

    /** 状态更新次数 */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker")
    int32 StateChangeCount = 0;
};

/**
 * 事件标记统计信息（蓝图可用）
 */
USTRUCT(BlueprintType)
struct FEventMarkerStatistics
{
    GENERATED_BODY()

    /** 活跃事件总数（未消失的事件） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 ActiveEventCount = 0;

    /** 已消失事件总数 */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 DisappearedEventCount = 0;

    /** 历史事件总数 */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 TotalEventCount = 0;

    /** 累计处理的消息数 */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 TotalMessagesProcessed = 0;

    /** 累计状态更新数 */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 TotalStateUpdates = 0;

    /** 按事件类型统计数量（道路巡检类） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 RoadEventCount = 0;

    /** 按事件类型统计数量（环境勘探类） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 EnvironmentEventCount = 0;

    /** 按事件类型统计数量（室内楼宇类） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 IndoorEventCount = 0;

    /** 按事件类型统计数量（室外类） */
    UPROPERTY(BlueprintReadOnly, Category = "Event Marker Statistics")
    int32 OutdoorEventCount = 0;
};

// ========== 蓝图事件委托 ==========

/**
 * 事件状态变化事件
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnEventStateChanged,
    int32, EventID,
    const FEventMarkerRuntimeState&, State
);

/**
 * 新事件创建事件
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnEventCreated,
    int32, EventID,
    uint8, EventType,
    FVector, Location
);

/**
 * 事件高亮事件
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnEventHighlighted,
    int32, EventID,
    FVector, Location
);

/**
 * 事件消失事件
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnEventDisappeared,
    int32, EventID,
    FVector, Location
);

/**
 * 批量事件更新完成事件
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnBatchEventUpdateComplete,
    int32, UpdatedCount
);

/**
 * 场景控制指令收到事件（来自JSON msgid=2001）
 *
 * 参数：
 * - SceneID:    场景ID
 * - Operation:  操作字符串（"start"/"stop"/"pause"/"resume"）
 * - bEmergency: 是否紧急停止
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnSceneControlReceived,
    int32, SceneID,
    const FString&, Operation,
    bool, bEmergency
);

/**
 * 事件标记管理器
 *
 * 职责：
 * - 接收并解析事件标记UDP消息（0x0002单个、0x0012批量）     [旧二进制路径，保留]
 * - 接收并解析JSON事件控制消息（0x1001，端口10003）         [新JSON路径]
 * - 接收并解析JSON场景控制消息（0x1001，端口10003）         [新JSON路径]
 * - 将NED坐标转换为UE坐标系
 * - 维护事件运行时状态缓存
 * - 管理事件状态流转（Initial → Highlighted → Disappeared）
 * - 触发蓝图事件通知状态变化
 * - 定时将事件状态广播回外部系统（端口10004，JSON msgid=1003）[新发送路径]
 *
 * 消息类型路由（均通过 MessageDispatcher/UDPManager 注册）：
 *   0x0002 → ProcessSingleEventMarker()      [二进制单事件]
 *   0x0012 → ProcessBatchEventMarker()       [二进制批量事件]
 *   0x1001 → HandleJSONEventControl()        [JSON事件/场景控制]
 *
 * 定时广播：
 *   每隔 BroadcastIntervalSeconds（默认2秒），
 *   对缓存中所有活跃事件发送一次状态广播（msgid=1003，端口10004）
 */
UCLASS(BlueprintType)
class HETEROSWARMSYNERGYUE_API UEventMarkerManager : public UObject, public IUDPMessageHandler
{
    GENERATED_BODY()

public:
    // ========== 构造与生命周期 ==========

    UEventMarkerManager();
    virtual ~UEventMarkerManager();

    /**
     * 初始化管理器
     *
     * @param InUDPManager UDP管理器指针
     * @return true表示初始化成功
     */
    bool Initialize(UUDPManager* InUDPManager);

    /**
     * 关闭管理器
     */
    void Shutdown();

    /**
     * 每帧Tick（由 HeteroSwarmGameInstance::Tick 调用）
     *
     * 用于：
     * - 检查是否到达广播间隔并触发事件状态广播
     *
     * @param DeltaTime 帧间隔（秒）
     */
    void Tick(float DeltaTime);

    // ========== IUDPMessageHandler 接口 ==========

    /**
     * 处理UDP消息（由MessageDispatcher调用）
     *
     * 根据消息类型路由到对应处理函数：
     *   二进制消息（0x0002/0x0012）：按长度识别后转入旧路径
     *   JSON消息（0x1001）：转入 HandleJSONEventControl()
     *
     * @param Data 消息体数据（不含消息头）
     * @param Length 消息体长度
     */
    virtual void HandleMessage(const uint8* Data, uint32 Length) override;

    // ========== 蓝图接口 - 查询 ==========

    /**
     * 获取事件当前状态
     *
     * @param EventID 事件ID
     * @param OutState 输出：事件状态
     * @return true表示事件存在
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Get Event State"))
    bool GetEventState(int32 EventID, FEventMarkerRuntimeState& OutState) const;

    /**
     * 获取所有活跃事件ID列表（未消失的事件）
     *
     * @return 事件ID数组
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Get All Active Event IDs"))
    TArray<int32> GetAllActiveEventIDs() const;

    /**
     * 获取指定类型的事件ID列表
     *
     * @param EventType 事件类型
     * @param bOnlyActive 是否只返回活跃事件
     * @return 事件ID数组
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Get Events By Type"))
    TArray<int32> GetEventsByType(uint8 EventType, bool bOnlyActive = true) const;

    /**
     * 检查事件是否存在
     *
     * @param EventID 事件ID
     * @return true表示事件存在
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Does Event Exist"))
    bool DoesEventExist(int32 EventID) const;

    /**
     * 检查事件是否活跃（未消失）
     *
     * @param EventID 事件ID
     * @return true表示活跃
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Is Event Active"))
    bool IsEventActive(int32 EventID) const;

    /**
     * 获取活跃事件数量
     *
     * @return 活跃事件数
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Get Active Event Count"))
    int32 GetActiveEventCount() const;

    /**
     * 获取统计信息
     *
     * @param OutStatistics 输出：统计信息
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Get Event Statistics"))
    void GetStatistics(FEventMarkerStatistics& OutStatistics) const;

    // ========== 蓝图接口 - 管理 ==========

    /**
     * 清除所有事件
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Clear All Events"))
    void ClearAllEvents();

    /**
     * 清除已消失的事件（释放内存）
     *
     * @return 清除的事件数量
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Clear Disappeared Events"))
    int32 ClearDisappearedEvents();

    /**
     * 手动移除指定事件
     *
     * @param EventID 事件ID
     * @return true表示移除成功
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Remove Event"))
    bool RemoveEvent(int32 EventID);

    /**
     * 立即触发一次全量事件状态广播（端口10004，JSON msgid=1003）
     *
     * 对缓存中所有活跃事件发送状态消息。
     * 通常由定时器驱动，也可手动触发（如场景初始化时）。
     *
     * @return 广播的事件数量
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Broadcast Event Statuses Now"))
    int32 BroadcastEventStatusesNow();

    /**
     * 设置事件状态广播间隔
     *
     * @param IntervalSeconds 广播间隔（秒，0表示禁用广播，推荐1.0-5.0）
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Set Broadcast Interval"))
    void SetBroadcastInterval(float IntervalSeconds);

    /**
     * 打印统计信息到日志
     */
    UFUNCTION(BlueprintCallable, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Print Statistics"))
    void PrintStatistics() const;

    // ========== 静态工具函数 ==========

    /**
     * 获取事件类型的友好名称
     *
     * @param EventType 事件类型
     * @return 类型名称（中文）
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Get Event Type Name"))
    static FString GetEventTypeName(uint8 EventType);

    /**
     * 获取事件状态的友好名称
     *
     * @param State 事件状态
     * @return 状态名称
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Get Event State Name"))
    static FString GetEventStateName(uint8 State);

    /**
     * 获取事件分类（道路/环境/室内/室外）
     *
     * @param EventType 事件类型
     * @return 分类名称
     */
    UFUNCTION(BlueprintPure, Category = "UDP|Event Marker Manager",
        meta = (DisplayName = "Get Event Category"))
    static FString GetEventCategory(uint8 EventType);

    // ========== 蓝图事件 ==========

    /** 事件状态变化事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Event Marker Manager")
    FOnEventStateChanged OnEventStateChanged;

    /** 新事件创建事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Event Marker Manager")
    FOnEventCreated OnEventCreated;

    /** 事件高亮事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Event Marker Manager")
    FOnEventHighlighted OnEventHighlighted;

    /** 事件消失事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Event Marker Manager")
    FOnEventDisappeared OnEventDisappeared;

    /** 批量更新完成事件 */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Event Marker Manager")
    FOnBatchEventUpdateComplete OnBatchEventUpdateComplete;

    /**
     * 收到JSON场景控制消息（msgid=2001）时广播
     * 蓝图可绑定此委托，响应"启动/停止/暂停/恢复"场景指令
     */
    UPROPERTY(BlueprintAssignable, Category = "UDP|Event Marker Manager")
    FOnSceneControlReceived OnSceneControlReceived;

    // ========== C++ 接口 ==========

    /**
     * 获取事件状态（C++版本，返回指针）
     *
     * @param EventID 事件ID
     * @return 事件状态指针，不存在则返回nullptr
     */
    const FEventMarkerRuntimeState* GetEventStatePtr(int32 EventID) const;

    /**
     * 获取所有事件状态（只读）
     *
     * @return 事件缓存的只读引用
     */
    const TMap<int32, FEventMarkerRuntimeState>& GetAllEventStates() const { return EventCache; }

private:
    // ========== 核心数据 ==========

    /** UDP管理器指针 */
    UPROPERTY()
    UUDPManager* UDPManager;

    /** 事件状态缓存（EventID -> State） */
    UPROPERTY()
    TMap<int32, FEventMarkerRuntimeState> EventCache;

    /** 是否已初始化 */
    bool bIsInitialized;

    // ========== 广播定时器 ==========

    /**
     * 事件状态广播间隔（秒）
     * 0 表示禁用广播。默认 2.0 秒。
     */
    float BroadcastIntervalSeconds;

    /** 距上次广播已积累的时间（秒） */
    float BroadcastAccumulatedTime;

    // ========== 统计信息 ==========

    /** 累计处理的消息数 */
    int32 TotalMessagesProcessed;

    /** 累计状态更新数 */
    int32 TotalStateUpdates;

    /** 累计创建的事件数 */
    int32 TotalEventsCreated;

    /** 累计高亮次数 */
    int32 TotalHighlights;

    /** 累计消失次数 */
    int32 TotalDisappearances;

    /** 累计发送的广播次数 */
    int32 TotalBroadcastsSent;

    // ========== 内部处理函数 - 旧二进制路径 ==========

    /**
     * 处理单个事件标记消息（0x0002）
     *
     * @param Data 消息数据
     * @param Length 消息长度
     */
    void ProcessSingleEventMarker(const uint8* Data, uint32 Length);

    /**
     * 处理批量事件标记消息（0x0012）
     *
     * @param Data 消息数据
     * @param Length 消息长度
     */
    void ProcessBatchEventMarker(const uint8* Data, uint32 Length);

    /**
     * 处理单个事件标记更新（内部公共路径）
     *
     * 被旧二进制路径和新JSON路径共同调用。
     *
     * @param EventID    事件ID
     * @param EventType  事件类型码
     * @param NewState   新状态码（0/1/2）
     * @param NEDPos     NED坐标（米）
     * @param Attitude   姿态角（弧度），二进制路径传 FVector::ZeroVector
     */
    void ProcessSingleUpdate(int32 EventID, uint8 EventType, uint8 NewState,
        const FVector& NEDPos, const FVector& Attitude);

    /**
     * 验证事件标记数据包的合法性（旧二进制路径专用）
     *
     * @param Packet 事件标记数据包
     * @return true表示数据合法
     */
    bool ValidateEventMarkerPacket(const FEventMarkerPacket& Packet) const;

    /**
     * 处理状态转换，触发对应的蓝图委托
     *
     * @param EventID 事件ID
     * @param OldState 旧状态
     * @param NewState 新状态
     * @param Location 事件位置（UE坐标）
     */
    void HandleStateTransition(int32 EventID, uint8 OldState, uint8 NewState, const FVector& Location);

    // ========== 内部处理函数 - 新JSON路径 ==========

    /**
     * 处理JSON事件/场景控制消息（MessageType=0x1001）
     *
     * 从 UDP Payload 中提取 JSON 字符串，根据内层 msgid 路由：
     *   msgid=1001 → HandleJSONEventControlMessage()
     *   msgid=2001 → HandleJSONSceneControlMessage()
     *
     * @param Data   UDP Payload 数据（JSON UTF-8字符串，无BOM）
     * @param Length Payload长度（字节）
     */
    void HandleJSONEventControl(const uint8* Data, uint32 Length);

    /**
     * 处理已解析的JSON事件控制消息（msgid=1001）
     *
     * 遍历 events 数组，对每个事件条目调用 ProcessSingleUpdate()
     *
     * @param Message 已解析的事件控制消息
     */
    void HandleJSONEventControlMessage(const FJSONEventControlMessage& Message);

    /**
     * 处理已解析的JSON场景控制消息（msgid=2001）
     *
     * 触发 OnSceneControlReceived 委托，通知蓝图层
     *
     * @param Message 已解析的场景控制消息
     */
    void HandleJSONSceneControlMessage(const FJSONSceneControlMessage& Message);

    /**
     * 将单个事件的当前状态序列化并通过UDPManager发送（端口10004）
     *
     * 被 BroadcastEventStatusesNow() 循环调用。
     *
     * @param State 事件运行时状态
     * @return true表示发送成功
     */
    bool SendEventStatusBroadcast(const FEventMarkerRuntimeState& State);
};