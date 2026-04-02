// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"

/**
 * UDP数据包缓冲区对象池
 */
class HETEROSWARMSYNERGYUE_API FPacketBufferPool
{
public:
    explicit FPacketBufferPool(int32 InitialSize = 100, int32 InBufferSize = 65507);
    ~FPacketBufferPool();

    // ========== 核心接口 ==========

    /**
     * 从池中获取一个缓冲区
     * @return 缓冲区共享指针
     */
    TSharedPtr<TArray<uint8>> Acquire();

    /**
     * 归还缓冲区到池中
     * @param Buffer 要归还的缓冲区共享指针
     */
    void Release(TSharedPtr<TArray<uint8>> Buffer);

    // ========== 统计接口 ==========

    int32 GetInitialCapacity() const { return InitialCapacity; }
    int32 GetAvailableCount() const;
    int32 GetDynamicAllocations() const { return DynamicAllocations.GetValue(); }
    int32 GetPeakUsage() const { return PeakUsage.GetValue(); }
    int32 GetInUseCount() const { return InUseCount.GetValue(); }
    int32 GetBufferSize() const { return BufferSize; }

    void PrintStatistics() const;
    void ResetStatistics();

private:
    /** 空闲缓冲区队列 */
    TQueue<TSharedPtr<TArray<uint8>>> FreeBuffers;

    /** 保护队列操作的临界区 */
    mutable FCriticalSection CriticalSection;

    int32 InitialCapacity;
    int32 BufferSize;

    FThreadSafeCounter DynamicAllocations;
    FThreadSafeCounter InUseCount;
    FThreadSafeCounter PeakUsage;
    FThreadSafeCounter TotalAcquires;
    FThreadSafeCounter TotalReleases;

    static constexpr int32 MAX_UDP_PACKET_SIZE = 65507;

    TSharedPtr<TArray<uint8>> CreateBuffer();
    void UpdatePeakUsage(int32 CurrentUsage);

    FPacketBufferPool(const FPacketBufferPool&) = delete;
    FPacketBufferPool& operator=(const FPacketBufferPool&) = delete;
};