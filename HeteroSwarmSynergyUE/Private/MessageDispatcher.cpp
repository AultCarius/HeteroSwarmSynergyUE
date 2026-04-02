// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageDispatcher.h"
#include "IUDPMessageHandler.h"

// 日志分类
DEFINE_LOG_CATEGORY_STATIC(LogMessageDispatcher, Log, All);

// ========== 构造与析构 ==========

FMessageDispatcher::FMessageDispatcher()
{
    UE_LOG(LogMessageDispatcher, Log, TEXT("MessageDispatcher created"));
}

FMessageDispatcher::~FMessageDispatcher()
{
    UE_LOG(LogMessageDispatcher, Log, TEXT("MessageDispatcher destroyed"));

    // 打印最终统计
    PrintStatistics();

    // 清空注册表
    HandlerRegistry.Empty();
    MessageTypeCounters.Empty();
}

// ========== 核心接口实现 ==========

void FMessageDispatcher::Dispatch(const uint8* Data, uint32 Length)
{
    // 1. 基础长度校验
    if (Length < HEADER_SIZE)
    {
        LogValidationFailure(
            FString::Printf(TEXT("Packet too small: %d bytes (minimum %d)"), Length, HEADER_SIZE),
            Data,
            Length
        );
        ValidationFailures++;
        return;
    }

    // 2. 解析消息头
    const FUDPMessageHeader* Header = reinterpret_cast<const FUDPMessageHeader*>(Data);

    // 3. 校验消息头
    if (!ValidateHeader(Header, Length))
    {
        ValidationFailures++;
        return;
    }

    // 4. 查找对应的Handler
    IUDPMessageHandler** HandlerPtr = HandlerRegistry.Find(Header->MessageType);

    if (HandlerPtr == nullptr || *HandlerPtr == nullptr)
    {
        LogUnknownMessageType(Header->MessageType);
        UnknownTypes++;
        return;
    }

    // 5. 提取消息体（跳过消息头）
    const uint8* Payload = Data + HEADER_SIZE;
    const uint32 PayloadLength = Header->PayloadLength;

    // 6. 分发到Handler
    IUDPMessageHandler* Handler = *HandlerPtr;

    UE_LOG(LogMessageDispatcher, Verbose,
        TEXT("Dispatching message: Type=0x%04X, PayloadLength=%d bytes"),
        Header->MessageType,
        PayloadLength
    );

    // 调用Handler处理消息
    Handler->HandleMessage(Payload, PayloadLength);

    // 7. 更新统计
    TotalDispatched++;
    UpdateMessageTypeCounter(Header->MessageType);
}

void FMessageDispatcher::RegisterHandler(uint16 MessageType, IUDPMessageHandler* Handler)
{
    if (Handler == nullptr)
    {
        UE_LOG(LogMessageDispatcher, Error,
            TEXT("Attempted to register null handler for message type 0x%04X"),
            MessageType
        );
        return;
    }

    // 检查是否已注册
    if (HandlerRegistry.Contains(MessageType))
    {
        UE_LOG(LogMessageDispatcher, Warning,
            TEXT("Handler for message type 0x%04X already registered, overwriting"),
            MessageType
        );
    }

    // 注册Handler
    HandlerRegistry.Add(MessageType, Handler);

    UE_LOG(LogMessageDispatcher, Log,
        TEXT("Registered handler for message type 0x%04X (Total handlers: %d)"),
        MessageType,
        HandlerRegistry.Num()
    );
}

void FMessageDispatcher::UnregisterHandler(uint16 MessageType)
{
    const int32 RemovedCount = HandlerRegistry.Remove(MessageType);

    if (RemovedCount > 0)
    {
        UE_LOG(LogMessageDispatcher, Log,
            TEXT("Unregistered handler for message type 0x%04X (Remaining handlers: %d)"),
            MessageType,
            HandlerRegistry.Num()
        );

        // 同时移除统计计数
        MessageTypeCounters.Remove(MessageType);
    }
    else
    {
        UE_LOG(LogMessageDispatcher, Warning,
            TEXT("Attempted to unregister non-existent handler for message type 0x%04X"),
            MessageType
        );
    }
}

bool FMessageDispatcher::IsHandlerRegistered(uint16 MessageType) const
{
    return HandlerRegistry.Contains(MessageType);
}

// ========== 统计接口实现 ==========

void FMessageDispatcher::PrintStatistics() const
{
    UE_LOG(LogMessageDispatcher, Log, TEXT("========== Message Dispatcher Statistics =========="));
    UE_LOG(LogMessageDispatcher, Log, TEXT("  Registered Handlers : %d"), HandlerRegistry.Num());
    UE_LOG(LogMessageDispatcher, Log, TEXT("  Total Dispatched    : %d"), TotalDispatched);
    UE_LOG(LogMessageDispatcher, Log, TEXT("  Validation Failures : %d"), ValidationFailures);
    UE_LOG(LogMessageDispatcher, Log, TEXT("  Unknown Types       : %d"), UnknownTypes);

    // 打印每种消息类型的统计
    if (MessageTypeCounters.Num() > 0)
    {
        UE_LOG(LogMessageDispatcher, Log, TEXT("  Message Type Breakdown:"));

        // 按消息类型排序
        TArray<uint16> SortedTypes;
        MessageTypeCounters.GetKeys(SortedTypes);
        SortedTypes.Sort();

        for (uint16 MessageType : SortedTypes)
        {
            const int32 Count = MessageTypeCounters[MessageType];
            const float Percentage = (TotalDispatched > 0)
                ? (Count * 100.0f / TotalDispatched)
                : 0.0f;

            UE_LOG(LogMessageDispatcher, Log,
                TEXT("    0x%04X : %6d messages (%.1f%%)"),
                MessageType,
                Count,
                Percentage
            );
        }
    }

    UE_LOG(LogMessageDispatcher, Log, TEXT("==================================================="));
}

void FMessageDispatcher::ResetStatistics()
{
    TotalDispatched = 0;
    ValidationFailures = 0;
    UnknownTypes = 0;
    MessageTypeCounters.Empty();

    UE_LOG(LogMessageDispatcher, Log, TEXT("Message dispatcher statistics reset"));
}

TArray<uint16> FMessageDispatcher::GetRegisteredMessageTypes() const
{
    TArray<uint16> MessageTypes;
    HandlerRegistry.GetKeys(MessageTypes);
    MessageTypes.Sort();
    return MessageTypes;
}

// ========== 内部辅助函数 ==========

bool FMessageDispatcher::ValidateHeader(const FUDPMessageHeader* Header, uint32 TotalLength) const
{
    // 1. 校验魔数
    if (Header->MagicNumber != MAGIC_NUMBER)
    {
        UE_LOG(LogMessageDispatcher, Warning,
            TEXT("Invalid magic number: Expected 0x%04X, Got 0x%04X"),
            MAGIC_NUMBER,
            Header->MagicNumber
        );
        return false;
    }

    // 2. 校验长度一致性
    const uint32 ExpectedTotalLength = HEADER_SIZE + Header->PayloadLength;
    if (ExpectedTotalLength != TotalLength)
    {
        UE_LOG(LogMessageDispatcher, Warning,
            TEXT("Length mismatch: Header declares %d bytes (header %d + payload %d), but packet is %d bytes"),
            ExpectedTotalLength,
            HEADER_SIZE,
            Header->PayloadLength,
            TotalLength
        );
        return false;
    }

    // 3. 校验负载长度合理性（最大64KB）
    if (Header->PayloadLength > 65535)
    {
        UE_LOG(LogMessageDispatcher, Error,
            TEXT("Payload length too large: %d bytes (max 65535)"),
            Header->PayloadLength
        );
        return false;
    }

    return true;
}

void FMessageDispatcher::LogValidationFailure(const FString& Reason, const uint8* Data, uint32 Length)
{
    UE_LOG(LogMessageDispatcher, Warning,
        TEXT("Message validation failed: %s"),
        *Reason
    );

    // 在Verbose级别输出16进制数据（用于调试）
    if (UE_LOG_ACTIVE(LogMessageDispatcher, Verbose))
    {
        FString HexDump;
        const uint32 DumpLength = FMath::Min(Length, 64u); // 最多打印64字节

        for (uint32 i = 0; i < DumpLength; ++i)
        {
            HexDump += FString::Printf(TEXT("%02X "), Data[i]);

            // 每16字节换行
            if ((i + 1) % 16 == 0)
            {
                HexDump += TEXT("\n");
            }
        }

        if (Length > DumpLength)
        {
            HexDump += TEXT("...");
        }

        UE_LOG(LogMessageDispatcher, Verbose,
            TEXT("Packet hex dump (%d bytes):\n%s"),
            Length,
            *HexDump
        );
    }
}

void FMessageDispatcher::LogUnknownMessageType(uint16 MessageType)
{
    // 使用Verbose级别，因为未知消息类型可能是正常的（例如外部系统发送了新版本协议）
    UE_LOG(LogMessageDispatcher, Verbose,
        TEXT("No handler registered for message type 0x%04X"),
        MessageType
    );
}

void FMessageDispatcher::UpdateMessageTypeCounter(uint16 MessageType)
{
    int32& Counter = MessageTypeCounters.FindOrAdd(MessageType, 0);
    Counter++;
}