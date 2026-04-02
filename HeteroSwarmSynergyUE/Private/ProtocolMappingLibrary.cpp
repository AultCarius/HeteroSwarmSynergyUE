#include "ProtocolMappingLibrary.h"

namespace
{
    static FString NormalizeToken(const FString& InRaw)
    {
        FString Result = InRaw.TrimStartAndEnd().ToLower();
        Result.ReplaceInline(TEXT(" "), TEXT(""));
        Result.ReplaceInline(TEXT("_"), TEXT(""));
        Result.ReplaceInline(TEXT("-"), TEXT(""));
        return Result;
    }
}

EUnifiedDeviceType FProtocolMapping::ToUnifiedDeviceType(int32 RawDeviceTypeOrCustomMode)
{
    switch (RawDeviceTypeOrCustomMode)
    {
    case 1:
        return EUnifiedDeviceType::IndoorQuadcopter;

    case 2:
        return EUnifiedDeviceType::OutdoorQuadcopter;

    case 3:
        return EUnifiedDeviceType::IndoorUGV;

    case 4:
        return EUnifiedDeviceType::OutdoorUGV;

    case 5:
        return EUnifiedDeviceType::RobotDog;

    default:
        return EUnifiedDeviceType::Unknown;
    }
}

uint8 FProtocolMapping::NormalizeDeviceTypeCode(int32 RawDeviceTypeOrCustomMode)
{
    return static_cast<uint8>(ToUnifiedDeviceType(RawDeviceTypeOrCustomMode));
}

FString FProtocolMapping::GetDeviceTypeName(uint8 DeviceTypeCode)
{
    switch (static_cast<EUnifiedDeviceType>(DeviceTypeCode))
    {
    case EUnifiedDeviceType::IndoorQuadcopter:
        return TEXT("IndoorQuadcopter");

    case EUnifiedDeviceType::OutdoorQuadcopter:
        return TEXT("OutdoorQuadcopter");

    case EUnifiedDeviceType::IndoorUGV:
        return TEXT("IndoorUGV");

    case EUnifiedDeviceType::OutdoorUGV:
        return TEXT("OutdoorUGV");

    case EUnifiedDeviceType::RobotDog:
        return TEXT("RobotDog");

    case EUnifiedDeviceType::Unknown:
    default:
        return TEXT("Unknown");
    }
}

ESceneControlOperationType FProtocolMapping::ToSceneOperation(const FString& RawOperation)
{
    const FString Token = NormalizeToken(RawOperation);

    if (Token == TEXT("start") || Token == TEXT("begin") || Token == TEXT("run"))
    {
        return ESceneControlOperationType::Start;
    }

    if (Token == TEXT("stop") || Token == TEXT("end") || Token == TEXT("finish"))
    {
        return ESceneControlOperationType::Stop;
    }

    if (Token == TEXT("pause") || Token == TEXT("hold"))
    {
        return ESceneControlOperationType::Pause;
    }

    if (Token == TEXT("resume") || Token == TEXT("continue"))
    {
        return ESceneControlOperationType::Resume;
    }

    if (Token == TEXT("reset") || Token == TEXT("restart") || Token == TEXT("reinit"))
    {
        return ESceneControlOperationType::Reset;
    }

    return ESceneControlOperationType::Unknown;
}

FString FProtocolMapping::NormalizeSceneOperationString(const FString& RawOperation)
{
    switch (ToSceneOperation(RawOperation))
    {
    case ESceneControlOperationType::Start:
        return TEXT("start");

    case ESceneControlOperationType::Stop:
        return TEXT("stop");

    case ESceneControlOperationType::Pause:
        return TEXT("pause");

    case ESceneControlOperationType::Resume:
        return TEXT("resume");

    case ESceneControlOperationType::Reset:
        return TEXT("reset");

    case ESceneControlOperationType::Unknown:
    default:
        return TEXT("unknown");
    }
}

EEventControlOperationType FProtocolMapping::ToEventOperation(const FString& RawOperation)
{
    const FString Token = NormalizeToken(RawOperation);

    if (Token == TEXT("create") || Token == TEXT("spawn") || Token == TEXT("add"))
    {
        return EEventControlOperationType::Create;
    }

    if (Token == TEXT("activate") || Token == TEXT("highlight") || Token == TEXT("focus"))
    {
        return EEventControlOperationType::Activate;
    }

    if (Token == TEXT("close") || Token == TEXT("remove") || Token == TEXT("delete") || Token == TEXT("disappear"))
    {
        return EEventControlOperationType::Close;
    }

    return EEventControlOperationType::Unknown;
}

uint8 FProtocolMapping::EventOperationToMarkerState(const FString& RawOperation)
{
    switch (ToEventOperation(RawOperation))
    {
    case EEventControlOperationType::Create:
        return 0;

    case EEventControlOperationType::Activate:
        return 1;

    case EEventControlOperationType::Close:
        return 2;

    case EEventControlOperationType::Unknown:
    default:
        return 0xFF;
    }
}

uint8 FProtocolMapping::EventOperationToMarkerState(EJSONEventOperation Operation)
{
    switch (Operation)
    {
    case EJSONEventOperation::Create:
        return 0;

    case EJSONEventOperation::Activate:
        return 1;

    case EJSONEventOperation::Close:
        return 2;

    default:
        return 0xFF;
    }
}