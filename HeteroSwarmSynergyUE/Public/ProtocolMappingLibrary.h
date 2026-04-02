#pragma once

#include "CoreMinimal.h"
#include "JSONMessageTypes.h"
#include "ProtocolMappingLibrary.generated.h"

UENUM(BlueprintType)
enum class EUnifiedDeviceType : uint8
{
    Unknown = 0   UMETA(DisplayName = "Unknown"),
    IndoorQuadcopter = 1   UMETA(DisplayName = "IndoorQuadcopter"),
    OutdoorQuadcopter = 2   UMETA(DisplayName = "OutdoorQuadcopter"),
    IndoorUGV = 3   UMETA(DisplayName = "IndoorUGV"),
    OutdoorUGV = 4   UMETA(DisplayName = "OutdoorUGV"),
    RobotDog = 5   UMETA(DisplayName = "RobotDog")
};

UENUM(BlueprintType)
enum class ESceneControlOperationType : uint8
{
    Unknown = 0 UMETA(DisplayName = "Unknown"),
    Start = 1 UMETA(DisplayName = "Start"),
    Stop = 2 UMETA(DisplayName = "Stop"),
    Pause = 3 UMETA(DisplayName = "Pause"),
    Resume = 4 UMETA(DisplayName = "Resume"),
    Reset = 5 UMETA(DisplayName = "Reset")
};

UENUM(BlueprintType)
enum class EEventControlOperationType : uint8
{
    Unknown = 0 UMETA(DisplayName = "Unknown"),
    Create = 1 UMETA(DisplayName = "Create"),
    Activate = 2 UMETA(DisplayName = "Activate"),
    Close = 3 UMETA(DisplayName = "Close")
};

struct FProtocolMapping
{
public:
    static EUnifiedDeviceType ToUnifiedDeviceType(int32 RawDeviceTypeOrCustomMode);
    static uint8 NormalizeDeviceTypeCode(int32 RawDeviceTypeOrCustomMode);
    static FString GetDeviceTypeName(uint8 DeviceTypeCode);

    static ESceneControlOperationType ToSceneOperation(const FString& RawOperation);
    static FString NormalizeSceneOperationString(const FString& RawOperation);

    static EEventControlOperationType ToEventOperation(const FString& RawOperation);
    static uint8 EventOperationToMarkerState(const FString& RawOperation);

    // 新增：直接支持 JSON 解析后的枚举
    static uint8 EventOperationToMarkerState(EJSONEventOperation Operation);
};