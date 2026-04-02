// Copyright Epic Games, Inc. All Rights Reserved.
// 修改：2026-03-13  v2.0 — 混合协议识别（MAVLink + CustomUDP）

#include "FUDPReceiverRunnable.h"
#include "PacketBufferPool.h"
#include "UDPProtocolTypes.h" // 包含FRawPacket、EProtocolType、FUDPReceiverConfig、FSocketContext定义
#include "HAL/PlatformProcess.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

// 日志分类
DEFINE_LOG_CATEGORY_STATIC(LogUDPReceiver, Log, All);

// ========== 构造与析构 ==========

FUDPReceiverRunnable::FUDPReceiverRunnable(
    const TArray<FUDPReceiverConfig>& InReceiverConfigs,
    TQueue<FRawPacket>* InOutputQueue,
    FPacketBufferPool* InBufferPool
)
    : OutputQueue(InOutputQueue)
    , BufferPool(InBufferPool)
    , bStopping(false)
    , ThreadName(TEXT("UDPMultiReceiverThread"))
{
    check(OutputQueue);
    check(BufferPool);

    // 预分配Socket上下文数组
    SocketContexts.Reserve(InReceiverConfigs.Num());

    // 为每个配置创建Socket
    for (const FUDPReceiverConfig& Config : InReceiverConfigs)
    {
        FSocketContext Context;
        if (CreateReceiverSocket(Config, Context))
        {
            SocketContexts.Add(Context);
        }
    }

    UE_LOG(LogUDPReceiver, Log, TEXT("FUDPReceiverRunnable constructed with %d/%d sockets successfully created"),
        SocketContexts.Num(), InReceiverConfigs.Num());
}

FUDPReceiverRunnable::~FUDPReceiverRunnable()
{
    // 确保线程已停止
    if (!bStopping)
    {
        Stop();
    }

    // 清理所有Socket资源
    CleanupSockets();

    UE_LOG(LogUDPReceiver, Log,
        TEXT("FUDPReceiverRunnable destroyed. Stats - Sockets: %d, Received: %d, Dropped: %d, Errors: %d"),
        SocketContexts.Num(),
        PacketsReceived.GetValue(),
        PacketsDropped.GetValue(),
        SocketErrors.GetValue()
    );
}

// ========== FRunnable 接口实现 ==========

bool FUDPReceiverRunnable::Init()
{
    UE_LOG(LogUDPReceiver, Log, TEXT("Receiver thread initializing with %d sockets..."),
        SocketContexts.Num());

    // 验证队列和对象池
    if (!OutputQueue || !BufferPool)
    {
        UE_LOG(LogUDPReceiver, Error, TEXT("OutputQueue or BufferPool is null"));
        return false;
    }

    // 验证至少有一个Socket上下文
    if (SocketContexts.Num() == 0)
    {
        UE_LOG(LogUDPReceiver, Error, TEXT("No socket contexts available"));
        return false;
    }

    // 验证所有Socket都已初始化
    int32 ValidSockets = 0;
    for (const FSocketContext& Context : SocketContexts)
    {
        if (Context.bIsInitialized && Context.Socket)
        {
            ValidSockets++;
            UE_LOG(LogUDPReceiver, Log, TEXT("  - Port %d: %s [OK]"),
                Context.LocalPort, *Context.Description);
        }
        else
        {
            UE_LOG(LogUDPReceiver, Warning, TEXT("  - Port %d: %s [FAILED]"),
                Context.LocalPort, *Context.Description);
        }
    }

    if (ValidSockets == 0)
    {
        UE_LOG(LogUDPReceiver, Error, TEXT("No valid sockets initialized"));
        return false;
    }

    UE_LOG(LogUDPReceiver, Log, TEXT("Receiver thread initialized successfully with %d/%d valid sockets"),
        ValidSockets, SocketContexts.Num());
    return true;
}

uint32 FUDPReceiverRunnable::Run()
{
    UE_LOG(LogUDPReceiver, Log, TEXT("Receiver thread started, entering main loop with %d sockets..."),
        SocketContexts.Num());

    while (!bStopping)
    {
        bool bReceivedAnyData = false;

        // 轮询所有Socket
        for (FSocketContext& Context : SocketContexts)
        {
            if (!Context.bIsInitialized || !Context.Socket)
            {
                continue;
            }

            // 处理单个Socket的数据
            if (ProcessSocketData(Context))
            {
                bReceivedAnyData = true;
            }
        }

        // 如果所有Socket都没有数据，短暂休眠避免空转
        if (!bReceivedAnyData)
        {
            FPlatformProcess::Sleep(POLL_INTERVAL_MS / 1000.0f);
        }
    }

    UE_LOG(LogUDPReceiver, Log, TEXT("Receiver thread exiting main loop (bStopping = true)"));
    return 0;
}

void FUDPReceiverRunnable::Stop()
{
    UE_LOG(LogUDPReceiver, Log, TEXT("Stop() called, setting bStopping flag..."));
    bStopping = true;
}

void FUDPReceiverRunnable::Exit()
{
    UE_LOG(LogUDPReceiver, Log, TEXT("Receiver thread cleanup complete"));
}

// ========== 内部辅助函数实现 ==========

bool FUDPReceiverRunnable::CreateReceiverSocket(const FUDPReceiverConfig& Config, FSocketContext& OutContext)
{
    if (!Config.bEnabled)
    {
        UE_LOG(LogUDPReceiver, Log, TEXT("Skipping disabled receiver: %s (Port %d)"),
            *Config.Description, Config.LocalPort);
        return false;
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogUDPReceiver, Error, TEXT("Failed to get SocketSubsystem for port %d"), Config.LocalPort);
        return false;
    }

    // 创建UDP Socket
    FSocket* NewSocket = SocketSubsystem->CreateSocket(NAME_DGram,
        *FString::Printf(TEXT("UDPReceiver_%d"), Config.LocalPort), false);

    if (!NewSocket)
    {
        UE_LOG(LogUDPReceiver, Error, TEXT("Failed to create socket for port %d: %s"),
            Config.LocalPort, *Config.Description);
        return false;
    }

    // 设置为非阻塞模式
    NewSocket->SetNonBlocking(true);

    // 设置接收缓冲区大小
    int32 NewSize = Config.BufferSize;
    NewSocket->SetReceiveBufferSize(NewSize, NewSize);

    // 允许地址重用
    NewSocket->SetReuseAddr(true);

    // 创建绑定地址
    TSharedRef<FInternetAddr> BindAddr = SocketSubsystem->CreateInternetAddr();
    BindAddr->SetAnyAddress();
    BindAddr->SetPort(Config.LocalPort);

    // 绑定端口
    if (!NewSocket->Bind(*BindAddr))
    {
        UE_LOG(LogUDPReceiver, Error, TEXT("Failed to bind socket to port %d: %s"),
            Config.LocalPort, *Config.Description);
        SocketSubsystem->DestroySocket(NewSocket);
        return false;
    }

    // 初始化Socket上下文
    OutContext.Socket = NewSocket;
    OutContext.LocalPort = Config.LocalPort;
    OutContext.Description = Config.Description;
    OutContext.ReceiveBuffer = MakeShared<TArray<uint8>>();
    OutContext.ReceiveBuffer->SetNumUninitialized(MAX_PACKET_SIZE);
    OutContext.bIsInitialized = true;

    UE_LOG(LogUDPReceiver, Log, TEXT("Successfully created receiver socket on port %d: %s"),
        Config.LocalPort, *Config.Description);

    return true;
}

bool FUDPReceiverRunnable::ProcessSocketData(FSocketContext& Context)
{
    // 1. 从对象池获取缓冲区
    TSharedPtr<TArray<uint8>> Buffer = BufferPool->Acquire();
    if (!Buffer.IsValid())
    {
        UE_LOG(LogUDPReceiver, Error, TEXT("Failed to acquire buffer from pool for port %d"),
            Context.LocalPort);
        return false;
    }

    // 2. 创建发送者地址
    TSharedRef<FInternetAddr> SenderAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)
        ->CreateInternetAddr();

    // 3. 非阻塞式接收数据
    int32 BytesRead = 0;
    bool bReceived = Context.Socket->RecvFrom(
        Buffer->GetData(),
        MAX_PACKET_SIZE,
        BytesRead,
        *SenderAddr
    );

    // 4. 处理接收结果
    if (bReceived && BytesRead > 0)
    {
        // 4.1 识别协议类型
        const EProtocolType Protocol = IdentifyProtocol(Buffer->GetData(), BytesRead);

        if (Protocol != EProtocolType::Unknown)
        {
            // 4.2 构建原始数据包（含协议类型）
            FRawPacket Packet;
            Packet.Data = Buffer;
            Packet.Length = BytesRead;
            Packet.SenderAddress = SenderAddr->ToString(true);
            Packet.ReceiveTime = FPlatformTime::Seconds();
            Packet.ProtocolType = Protocol;

            // 4.3 推入队列
            OutputQueue->Enqueue(Packet);

            // 4.4 更新统计
            PacketsReceived.Increment();

            UE_LOG(LogUDPReceiver, Verbose,
                TEXT("[Port %d - %s] Received %s packet: %d bytes from %s (Total: %d)"),
                Context.LocalPort,
                *Context.Description,
                Protocol == EProtocolType::MAVLink ? TEXT("MAVLink") : TEXT("CustomUDP"),
                BytesRead,
                *Packet.SenderAddress,
                PacketsReceived.GetValue()
            );

            return true;
        }
        else
        {
            // 协议未知，丢弃
            PacketsDropped.Increment();

            UE_LOG(LogUDPReceiver, Warning,
                TEXT("[Port %d - %s] Unknown protocol in packet (%d bytes) from %s. "
                    "First bytes: 0x%02X 0x%02X (Dropped: %d)"),
                Context.LocalPort,
                *Context.Description,
                BytesRead,
                *SenderAddr->ToString(true),
                BytesRead >= 1 ? Buffer->GetData()[0] : 0,
                BytesRead >= 2 ? Buffer->GetData()[1] : 0,
                PacketsDropped.GetValue()
            );

            // 归还缓冲区
            BufferPool->Release(Buffer);
            return false;
        }
    }
    else
    {
        // 归还缓冲区
        BufferPool->Release(Buffer);

        if (!bReceived)
        {
            ESocketErrors ErrorCode = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)
                ->GetLastErrorCode();

            // 正确处理EWOULDBLOCK
            if (ErrorCode == SE_EWOULDBLOCK || ErrorCode == SE_NO_ERROR)
            {
                // 非阻塞模式下没有数据是正常的
                return false;
            }
            else
            {
                // 真正的错误
                SocketErrors.Increment();
                LogReceiveError(ErrorCode, Context.Description);
                return false;
            }
        }

        return false;
    }
}

void FUDPReceiverRunnable::CleanupSockets()
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        return;
    }

    for (FSocketContext& Context : SocketContexts)
    {
        if (Context.Socket)
        {
            UE_LOG(LogUDPReceiver, Log, TEXT("Closing socket on port %d: %s"),
                Context.LocalPort, *Context.Description);

            SocketSubsystem->DestroySocket(Context.Socket);
            Context.Socket = nullptr;
            Context.bIsInitialized = false;
        }
    }

    SocketContexts.Empty();
}

// ========== 内部辅助函数 ==========

EProtocolType FUDPReceiverRunnable::IdentifyProtocol(const uint8* Buffer, int32 Length) const
{
    if (Length < 1)
    {
        return EProtocolType::Unknown;
    }

    // 检查MAVLink v2（首字节 0xFD）
    if (Buffer[0] == MAVLINK_V2_STX)
    {
        if (Length < static_cast<int32>(MIN_MAVLINK_SIZE))
        {
            // 太短，可能是截断包，丢弃
            return EProtocolType::Unknown;
        }
        return EProtocolType::MAVLink;
    }

    // 检查自定义UDP（首2字节 0xABCD，小端序）
    if (Length >= static_cast<int32>(MIN_CUSTOM_UDP_SIZE))
    {
        const uint16 Magic = *reinterpret_cast<const uint16*>(Buffer);
        if (Magic == CUSTOM_UDP_MAGIC)
        {
            return EProtocolType::CustomUDP;
        }
    }

    return EProtocolType::Unknown;
}

void FUDPReceiverRunnable::LogReceiveError(ESocketErrors ErrorCode, const FString& SocketDescription)
{
    FString ErrorMsg;

    switch (ErrorCode)
    {
    case SE_NO_ERROR:
        ErrorMsg = TEXT("No Error");
        break;
    case SE_EINTR:
        ErrorMsg = TEXT("Interrupted system call");
        break;
    case SE_EBADF:
        ErrorMsg = TEXT("Bad file descriptor");
        break;
    case SE_EACCES:
        ErrorMsg = TEXT("Permission denied");
        break;
    case SE_EFAULT:
        ErrorMsg = TEXT("Bad address");
        break;
    case SE_EINVAL:
        ErrorMsg = TEXT("Invalid argument");
        break;
    case SE_EMFILE:
        ErrorMsg = TEXT("Too many open files");
        break;
    case SE_EWOULDBLOCK:
        ErrorMsg = TEXT("Operation would block");
        break;
    case SE_EINPROGRESS:
        ErrorMsg = TEXT("Operation now in progress");
        break;
    case SE_EALREADY:
        ErrorMsg = TEXT("Operation already in progress");
        break;
    case SE_ENOTSOCK:
        ErrorMsg = TEXT("Socket operation on non-socket");
        break;
    case SE_EDESTADDRREQ:
        ErrorMsg = TEXT("Destination address required");
        break;
    case SE_EMSGSIZE:
        ErrorMsg = TEXT("Message too long");
        break;
    case SE_EPROTOTYPE:
        ErrorMsg = TEXT("Protocol wrong type for socket");
        break;
    case SE_ENOPROTOOPT:
        ErrorMsg = TEXT("Protocol not available");
        break;
    case SE_EPROTONOSUPPORT:
        ErrorMsg = TEXT("Protocol not supported");
        break;
    case SE_ESOCKTNOSUPPORT:
        ErrorMsg = TEXT("Socket type not supported");
        break;
    case SE_EOPNOTSUPP:
        ErrorMsg = TEXT("Operation not supported");
        break;
    case SE_EPFNOSUPPORT:
        ErrorMsg = TEXT("Protocol family not supported");
        break;
    case SE_EAFNOSUPPORT:
        ErrorMsg = TEXT("Address family not supported");
        break;
    case SE_EADDRINUSE:
        ErrorMsg = TEXT("Address already in use");
        break;
    case SE_EADDRNOTAVAIL:
        ErrorMsg = TEXT("Can't assign requested address");
        break;
    case SE_ENETDOWN:
        ErrorMsg = TEXT("Network is down");
        break;
    case SE_ENETUNREACH:
        ErrorMsg = TEXT("Network is unreachable");
        break;
    case SE_ENETRESET:
        ErrorMsg = TEXT("Network dropped connection on reset");
        break;
    case SE_ECONNABORTED:
        ErrorMsg = TEXT("Software caused connection abort");
        break;
    case SE_ECONNRESET:
        ErrorMsg = TEXT("Connection reset by peer");
        break;
    case SE_ENOBUFS:
        ErrorMsg = TEXT("No buffer space available");
        break;
    case SE_EISCONN:
        ErrorMsg = TEXT("Socket is already connected");
        break;
    case SE_ENOTCONN:
        ErrorMsg = TEXT("Socket is not connected");
        break;
    case SE_ESHUTDOWN:
        ErrorMsg = TEXT("Can't send after socket shutdown");
        break;
    case SE_ETOOMANYREFS:
        ErrorMsg = TEXT("Too many references");
        break;
    case SE_ETIMEDOUT:
        ErrorMsg = TEXT("Connection timed out");
        break;
    case SE_ECONNREFUSED:
        ErrorMsg = TEXT("Connection refused");
        break;
    case SE_ELOOP:
        ErrorMsg = TEXT("Too many levels of symbolic links");
        break;
    case SE_ENAMETOOLONG:
        ErrorMsg = TEXT("File name too long");
        break;
    case SE_EHOSTDOWN:
        ErrorMsg = TEXT("Host is down");
        break;
    case SE_EHOSTUNREACH:
        ErrorMsg = TEXT("No route to host");
        break;
    case SE_ENOTEMPTY:
        ErrorMsg = TEXT("Directory not empty");
        break;
    case SE_EPROCLIM:
        ErrorMsg = TEXT("Too many processes");
        break;
    case SE_EUSERS:
        ErrorMsg = TEXT("Too many users");
        break;
    case SE_EDQUOT:
        ErrorMsg = TEXT("Disc quota exceeded");
        break;
    case SE_ESTALE:
        ErrorMsg = TEXT("Stale file handle");
        break;
    case SE_EREMOTE:
        ErrorMsg = TEXT("Too many levels of remote in path");
        break;
    case SE_EDISCON:
        ErrorMsg = TEXT("Graceful disconnect");
        break;
    case SE_SYSNOTREADY:
        ErrorMsg = TEXT("Network subsystem unavailable");
        break;
    case SE_VERNOTSUPPORTED:
        ErrorMsg = TEXT("Winsock version not supported");
        break;
    case SE_NOTINITIALISED:
        ErrorMsg = TEXT("Winsock not initialized");
        break;
    case SE_HOST_NOT_FOUND:
        ErrorMsg = TEXT("Authoritative answer host not found");
        break;
    case SE_TRY_AGAIN:
        ErrorMsg = TEXT("Non-authoritative host not found");
        break;
    case SE_NO_RECOVERY:
        ErrorMsg = TEXT("Non recoverable error");
        break;
    case SE_NO_DATA:
        ErrorMsg = TEXT("Valid name, no data record");
        break;
    default:
        ErrorMsg = FString::Printf(TEXT("Unknown error code: %d"), static_cast<int32>(ErrorCode));
        break;
    }

    UE_LOG(LogUDPReceiver, Error,
        TEXT("[%s] Socket RecvFrom failed: %s (Code: %d, Total Errors: %d)"),
        *SocketDescription,
        *ErrorMsg,
        static_cast<int32>(ErrorCode),
        SocketErrors.GetValue()
    );
}