// Copyright Epic Games, Inc. All Rights Reserved.

#include "PacketBufferPool.h"

DEFINE_LOG_CATEGORY_STATIC(LogPacketBufferPool, Log, All);

// ========== 构造与析构 ==========

FPacketBufferPool::FPacketBufferPool(int32 InitialSize, int32 InBufferSize)
    : InitialCapacity(InitialSize)
    , BufferSize(InBufferSize)
{
    check(InitialSize > 0);
    check(InBufferSize > 0 && InBufferSize <= MAX_UDP_PACKET_SIZE);

    UE_LOG(LogPacketBufferPool, Log,
        TEXT("Initializing packet buffer pool: %d buffers x %d bytes = %.2f MB"),
        InitialSize,
        BufferSize,
        (InitialSize * BufferSize) / (1024.0f * 1024.0f)
    );

    // 预分配所有缓冲区
    for (int32 i = 0; i < InitialSize; ++i)
    {
        TSharedPtr<TArray<uint8>> Buffer = CreateBuffer();
        FreeBuffers.Enqueue(Buffer);
    }

    UE_LOG(LogPacketBufferPool, Log,
        TEXT("Packet buffer pool initialized successfully")
    );
}

FPacketBufferPool::~FPacketBufferPool()
{
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("Destroying packet buffer pool...")
    );

    PrintStatistics();

    // 释放所有空闲缓冲区
    int32 FreedCount = 0;
    TSharedPtr<TArray<uint8>> Buffer;

    while (FreeBuffers.Dequeue(Buffer))
    {
        if (Buffer.IsValid())
        {
            Buffer.Reset(); // 释放共享指针
            FreedCount++;
        }
    }

    UE_LOG(LogPacketBufferPool, Log,
        TEXT("Packet buffer pool destroyed. Freed %d buffers."),
        FreedCount
    );

    const int32 LeakedBuffers = InUseCount.GetValue();
    if (LeakedBuffers > 0)
    {
        UE_LOG(LogPacketBufferPool, Error,
            TEXT("MEMORY LEAK DETECTED: %d buffers were not released!"),
            LeakedBuffers
        );
    }
}

// ========== 核心接口实现 ==========

TSharedPtr<TArray<uint8>> FPacketBufferPool::Acquire()
{
    TotalAcquires.Increment();

    TSharedPtr<TArray<uint8>> Buffer;

    // 临界区：从队列取缓冲区
    {
        FScopeLock Lock(&CriticalSection);
        FreeBuffers.Dequeue(Buffer);
    }

    // 如果池已耗尽，动态分配新缓冲区
    if (!Buffer.IsValid())
    {
        Buffer = CreateBuffer();

        const int32 DynamicCount = DynamicAllocations.Increment();

        UE_LOG(LogPacketBufferPool, Warning,
            TEXT("Buffer pool exhausted! Dynamically allocated buffer #%d. Consider increasing pool size."),
            DynamicCount
        );
    }

    // 更新使用统计
    const int32 CurrentUsage = InUseCount.Increment();
    UpdatePeakUsage(CurrentUsage);

    return Buffer;
}

void FPacketBufferPool::Release(TSharedPtr<TArray<uint8>> Buffer)
{
    if (!Buffer.IsValid())
    {
        UE_LOG(LogPacketBufferPool, Warning,
            TEXT("Attempted to release invalid buffer, ignoring")
        );
        return;
    }

    TotalReleases.Increment();
    InUseCount.Decrement();

    // 临界区：归还缓冲区到队列
    {
        FScopeLock Lock(&CriticalSection);
        FreeBuffers.Enqueue(Buffer);
    }
}

// ========== 统计接口实现 ==========

int32 FPacketBufferPool::GetAvailableCount() const
{
    FScopeLock Lock(&CriticalSection);

    const int32 TotalBuffers = InitialCapacity + DynamicAllocations.GetValue();
    const int32 Available = TotalBuffers - InUseCount.GetValue();

    return FMath::Max(0, Available);
}

void FPacketBufferPool::PrintStatistics() const
{
    const int32 TotalBuffers = InitialCapacity + DynamicAllocations.GetValue();
    const int32 CurrentUsage = InUseCount.GetValue();
    const int32 Available = GetAvailableCount();
    const float UsagePercent = (TotalBuffers > 0)
        ? (CurrentUsage * 100.0f / TotalBuffers)
        : 0.0f;

    UE_LOG(LogPacketBufferPool, Log,
        TEXT("===== Packet Buffer Pool Statistics =====")
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  Initial Capacity    : %d buffers"),
        InitialCapacity
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  Buffer Size         : %d bytes"),
        BufferSize
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  Total Buffers       : %d"),
        TotalBuffers
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  In Use              : %d (%.1f%%)"),
        CurrentUsage,
        UsagePercent
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  Available           : %d"),
        Available
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  Peak Usage          : %d"),
        PeakUsage.GetValue()
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  Dynamic Allocations : %d"),
        DynamicAllocations.GetValue()
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  Total Acquires      : %d"),
        TotalAcquires.GetValue()
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("  Total Releases      : %d"),
        TotalReleases.GetValue()
    );
    UE_LOG(LogPacketBufferPool, Log,
        TEXT("========================================")
    );
}

void FPacketBufferPool::ResetStatistics()
{
    DynamicAllocations.Reset();
    PeakUsage.Reset();
    TotalAcquires.Reset();
    TotalReleases.Reset();

    UE_LOG(LogPacketBufferPool, Log,
        TEXT("Buffer pool statistics reset")
    );
}

// ========== 内部辅助函数 ==========

TSharedPtr<TArray<uint8>> FPacketBufferPool::CreateBuffer()
{
    TSharedPtr<TArray<uint8>> Buffer = MakeShared<TArray<uint8>>();

    // 预分配内存
    Buffer->SetNumUninitialized(BufferSize);

    return Buffer;
}

void FPacketBufferPool::UpdatePeakUsage(int32 CurrentUsage)
{
    int32 CurrentPeak = PeakUsage.GetValue();
    while (CurrentUsage > CurrentPeak)
    {
        if (PeakUsage.Set(CurrentUsage) == CurrentPeak)
        {
            break;
        }
        CurrentPeak = PeakUsage.GetValue();
    }
}