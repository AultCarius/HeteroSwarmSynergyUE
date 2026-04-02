// Copyright Epic Games, Inc. All Rights Reserved.

#include "JSONMessageParser.h"
#include "Json.h"   // FJsonObject, FJsonSerializer, TJsonReaderFactory, TJsonWriterFactory

DEFINE_LOG_CATEGORY_STATIC(LogJSONParser, Log, All);

// ============================================================
// 公开接口 - 解析
// ============================================================

bool FJSONMessageParser::ParseEventControl(const FString& JsonString, FJSONEventControlMessage& OutMessage)
{
    if (JsonString.IsEmpty())
    {
        UE_LOG(LogJSONParser, Warning, TEXT("ParseEventControl: empty input"));
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseEventControl: JSON parse failed. Input: %s"), *JsonString.Left(200));
        return false;
    }

    // 验证 msgid
    int32 MsgID = 0;
    if (!RootObject->TryGetNumberField(TEXT("msgid"), MsgID))
    {
        UE_LOG(LogJSONParser, Warning, TEXT("ParseEventControl: missing 'msgid' field"));
        return false;
    }
    if (MsgID != 1001)
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseEventControl: unexpected msgid=%d (expected 1001)"), MsgID);
        return false;
    }

    OutMessage.MsgID = MsgID;
    RootObject->TryGetStringField(TEXT("publisher"), OutMessage.Publisher);
    RootObject->TryGetStringField(TEXT("time"), OutMessage.Timestamp);

    // 提取 args 数组
    const TArray<TSharedPtr<FJsonValue>>* ArgsArray = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("args"), ArgsArray) || !ArgsArray)
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseEventControl: missing or invalid 'args' array"));
        return false;
    }

    OutMessage.Events.Reset();
    OutMessage.Events.Reserve(ArgsArray->Num());

    int32 ValidCount = 0;
    for (int32 i = 0; i < ArgsArray->Num(); ++i)
    {
        const TSharedPtr<FJsonValue>& Entry = (*ArgsArray)[i];
        if (!Entry.IsValid() || Entry->Type != EJson::Object)
        {
            UE_LOG(LogJSONParser, Warning,
                TEXT("ParseEventControl: args[%d] is not an object, skipping"), i);
            continue;
        }

        FJSONEventEntry EventEntry;
        if (ParseEventEntry(Entry->AsObject(), EventEntry))
        {
            OutMessage.Events.Add(EventEntry);
            ValidCount++;
        }
        else
        {
            UE_LOG(LogJSONParser, Warning,
                TEXT("ParseEventControl: args[%d] parse failed, skipping"), i);
        }
    }

    if (ValidCount == 0)
    {
        UE_LOG(LogJSONParser, Warning, TEXT("ParseEventControl: no valid event entries found"));
        return false;
    }

    UE_LOG(LogJSONParser, Verbose,
        TEXT("ParseEventControl: OK publisher=%s %d/%d events"),
        *OutMessage.Publisher, ValidCount, ArgsArray->Num());

    return true;
}

bool FJSONMessageParser::ParseSceneControl(const FString& JsonString, FJSONSceneControlMessage& OutMessage)
{
    if (JsonString.IsEmpty())
    {
        UE_LOG(LogJSONParser, Warning, TEXT("ParseSceneControl: empty input"));
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseSceneControl: JSON parse failed. Input: %s"), *JsonString.Left(200));
        return false;
    }

    // 验证 msgid
    int32 MsgID = 0;
    if (!RootObject->TryGetNumberField(TEXT("msgid"), MsgID))
    {
        UE_LOG(LogJSONParser, Warning, TEXT("ParseSceneControl: missing 'msgid' field"));
        return false;
    }
    if (MsgID != 2001)
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseSceneControl: unexpected msgid=%d (expected 2001)"), MsgID);
        return false;
    }

    OutMessage.MsgID = MsgID;
    RootObject->TryGetStringField(TEXT("publisher"), OutMessage.Publisher);
    RootObject->TryGetStringField(TEXT("time"), OutMessage.Timestamp);

    // 提取 args 对象（非数组）
    const TSharedPtr<FJsonObject>* ArgsObject = nullptr;
    if (!RootObject->TryGetObjectField(TEXT("args"), ArgsObject) || !ArgsObject || !ArgsObject->IsValid())
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseSceneControl: missing or invalid 'args' object"));
        return false;
    }

    (*ArgsObject)->TryGetNumberField(TEXT("id"), OutMessage.SceneID);
    (*ArgsObject)->TryGetStringField(TEXT("name"), OutMessage.SceneName);
    (*ArgsObject)->TryGetStringField(TEXT("operation"), OutMessage.Operation);
    (*ArgsObject)->TryGetBoolField(TEXT("emergencystop"), OutMessage.bEmergencyStop);

    if (OutMessage.Operation.IsEmpty())
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseSceneControl: missing 'operation' field in args"));
        return false;
    }

    UE_LOG(LogJSONParser, Verbose,
        TEXT("ParseSceneControl: OK scene=%d '%s' op=%s emergency=%s"),
        OutMessage.SceneID, *OutMessage.SceneName,
        *OutMessage.Operation,
        OutMessage.bEmergencyStop ? TEXT("YES") : TEXT("no"));

    return true;
}

// ============================================================
// 公开接口 - 生成
// ============================================================

bool FJSONMessageParser::GenerateEventStatus(const FJSONEventStatusMessage& Message, FString& OutJsonString)
{
    // 构造 args 对象
    TSharedRef<FJsonObject> ArgsObject = MakeShared<FJsonObject>();
    ArgsObject->SetNumberField(TEXT("id"), Message.EventStatus.EventID);
    ArgsObject->SetNumberField(TEXT("type"), Message.EventStatus.EventType);
    ArgsObject->SetStringField(TEXT("status"), Message.EventStatus.Status);

    // pose 数组：[North, East, Down, Roll, Pitch, Yaw]
    TArray<TSharedPtr<FJsonValue>> PoseArray;
    PoseArray.Add(MakeShared<FJsonValueNumber>(Message.EventStatus.NEDPosition.X)); // North
    PoseArray.Add(MakeShared<FJsonValueNumber>(Message.EventStatus.NEDPosition.Y)); // East
    PoseArray.Add(MakeShared<FJsonValueNumber>(Message.EventStatus.NEDPosition.Z)); // Down
    PoseArray.Add(MakeShared<FJsonValueNumber>(Message.EventStatus.Attitude.X));    // Roll
    PoseArray.Add(MakeShared<FJsonValueNumber>(Message.EventStatus.Attitude.Y));    // Pitch
    PoseArray.Add(MakeShared<FJsonValueNumber>(Message.EventStatus.Attitude.Z));    // Yaw
    ArgsObject->SetArrayField(TEXT("pose"), PoseArray);

    // 构造根对象
    TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();

    const FString Timestamp = Message.Timestamp.IsEmpty()
        ? GetCurrentISO8601Timestamp()
        : Message.Timestamp;

    RootObject->SetStringField(TEXT("time"), Timestamp);
    RootObject->SetNumberField(TEXT("msgid"), Message.MsgID);
    RootObject->SetStringField(TEXT("publisher"), Message.Publisher);
    RootObject->SetObjectField(TEXT("args"), ArgsObject);

    // 序列化为紧凑 JSON（无缩进，节省带宽）
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);

    if (!FJsonSerializer::Serialize(RootObject, Writer))
    {
        UE_LOG(LogJSONParser, Error,
            TEXT("GenerateEventStatus: serialization failed for EventID=%d"),
            Message.EventStatus.EventID);
        OutJsonString.Empty();
        return false;
    }

    UE_LOG(LogJSONParser, VeryVerbose,
        TEXT("GenerateEventStatus: EventID=%d status=%s len=%d"),
        Message.EventStatus.EventID, *Message.EventStatus.Status, OutJsonString.Len());

    return true;
}

// ============================================================
// 私有辅助函数
// ============================================================

bool FJSONMessageParser::ParseEventEntry(
    const TSharedPtr<FJsonObject>& EntryObject,
    FJSONEventEntry& OutEntry)
{
    if (!EntryObject.IsValid())
    {
        return false;
    }

    // id（必填，必须 > 0）
    int32 EventID = 0;
    if (!EntryObject->TryGetNumberField(TEXT("id"), EventID) || EventID <= 0)
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseEventEntry: missing or invalid 'id' field"));
        return false;
    }
    OutEntry.EventID = EventID;

    // type（必填）
    int32 EventType = 0;
    if (!EntryObject->TryGetNumberField(TEXT("type"), EventType))
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseEventEntry: missing 'type' field for id=%d"), EventID);
        return false;
    }
    OutEntry.EventType = static_cast<uint8>(EventType);

    // operation（必填）
    FString OpStr;
    if (!EntryObject->TryGetStringField(TEXT("operation"), OpStr) || OpStr.IsEmpty())
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseEventEntry: missing 'operation' field for id=%d"), EventID);
        return false;
    }
    OutEntry.Operation = ParseOperationString(OpStr);
    if (OutEntry.Operation == EJSONEventOperation::Unknown)
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParseEventEntry: unknown operation='%s' for id=%d"), *OpStr, EventID);
        return false;
    }

    // pose（可选；解析失败时用零值，不拒绝整条消息）
    const TArray<TSharedPtr<FJsonValue>>* PoseArrayPtr = nullptr;
    if (EntryObject->TryGetArrayField(TEXT("pose"), PoseArrayPtr) && PoseArrayPtr)
    {
        if (!ParsePoseArray(*PoseArrayPtr, OutEntry.NEDPosition, OutEntry.Attitude))
        {
            UE_LOG(LogJSONParser, Warning,
                TEXT("ParseEventEntry: pose parse failed for id=%d, using zero"), EventID);
            OutEntry.NEDPosition = FVector::ZeroVector;
            OutEntry.Attitude = FVector::ZeroVector;
        }
    }

    return true;
}

bool FJSONMessageParser::ParsePoseArray(
    const TArray<TSharedPtr<FJsonValue>>& PoseArray,
    FVector& OutPosition,
    FVector& OutAttitude)
{
    if (PoseArray.Num() < 6)
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParsePoseArray: expected 6 elements, got %d"), PoseArray.Num());
        return false;
    }

    double North = 0.0, East = 0.0, Down = 0.0;
    double Roll = 0.0, Pitch = 0.0, Yaw = 0.0;

    if (!PoseArray[0]->TryGetNumber(North) ||
        !PoseArray[1]->TryGetNumber(East) ||
        !PoseArray[2]->TryGetNumber(Down) ||
        !PoseArray[3]->TryGetNumber(Roll) ||
        !PoseArray[4]->TryGetNumber(Pitch) ||
        !PoseArray[5]->TryGetNumber(Yaw))
    {
        UE_LOG(LogJSONParser, Warning,
            TEXT("ParsePoseArray: non-numeric value in pose array"));
        return false;
    }

    OutPosition = FVector(static_cast<float>(North),
        static_cast<float>(East),
        static_cast<float>(Down));

    OutAttitude = FVector(static_cast<float>(Roll),
        static_cast<float>(Pitch),
        static_cast<float>(Yaw));
    return true;
}

FString FJSONMessageParser::GetCurrentISO8601Timestamp()
{
    const FDateTime Now = FDateTime::UtcNow();
    return FString::Printf(
        TEXT("%04d-%02d-%02dT%02d:%02d:%02dZ"),
        Now.GetYear(), Now.GetMonth(), Now.GetDay(),
        Now.GetHour(), Now.GetMinute(), Now.GetSecond()
    );
}