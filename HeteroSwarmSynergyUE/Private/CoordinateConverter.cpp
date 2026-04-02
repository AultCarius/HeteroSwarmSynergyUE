// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoordinateConverter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoordinateConverter, Log, All);

// ========== 批量转换实现 ==========

void UCoordinateConverter::BatchConvertNEDToUE(
    const FNEDVector* InputNED,
    FVector* OutputUE,
    int32 Count
)
{
    check(InputNED && OutputUE && Count > 0);

    for (int32 i = 0; i < Count; ++i)
    {
        OutputUE[i] = NEDToUE(InputNED[i]);
    }

    UE_LOG(LogCoordinateConverter, Verbose,
        TEXT("Batch converted %d NED → UE"), Count);
}

void UCoordinateConverter::BatchConvertUEToNED(
    const FVector* InputUE,
    FNEDVector* OutputNED,
    int32 Count
)
{
    check(InputUE && OutputNED && Count > 0);

    for (int32 i = 0; i < Count; ++i)
    {
        OutputNED[i] = UEToNED(InputUE[i]);
    }

    UE_LOG(LogCoordinateConverter, Verbose,
        TEXT("Batch converted %d UE → NED"), Count);
}

// ========== 辅助函数实现 ==========

bool UCoordinateConverter::IsValidNED(const FNEDVector& NED, float MaxDistance)
{
    if (!FMath::IsFinite(NED.North) ||
        !FMath::IsFinite(NED.East) ||
        !FMath::IsFinite(NED.Down))
    {
        return false;
    }

    const float DistSq = NED.North * NED.North +
        NED.East * NED.East +
        NED.Down * NED.Down;

    return DistSq <= (MaxDistance * MaxDistance);
}

bool UCoordinateConverter::IsValidAttitude(const FAttitude& Attitude)
{
    if (!FMath::IsFinite(Attitude.Roll) ||
        !FMath::IsFinite(Attitude.Pitch) ||
        !FMath::IsFinite(Attitude.Yaw))
    {
        return false;
    }

    // Roll & Pitch: [-π, π], Yaw: [0, 2π]
    return (Attitude.Roll >= -PI && Attitude.Roll <= PI) &&
        (Attitude.Pitch >= -PI && Attitude.Pitch <= PI) &&
        (Attitude.Yaw >= 0.0f && Attitude.Yaw <= TWO_PI);
}

float UCoordinateConverter::NEDDistance(const FNEDVector& NED1, const FNEDVector& NED2)
{
    const float DN = NED2.North - NED1.North;
    const float DE = NED2.East - NED1.East;
    const float DD = NED2.Down - NED1.Down;

    return FMath::Sqrt(DN * DN + DE * DE + DD * DD);
}

float UCoordinateConverter::NormalizeYaw(float Yaw)
{
    Yaw = FMath::Fmod(Yaw, TWO_PI);
    if (Yaw < 0.0f) Yaw += TWO_PI;
    return Yaw;
}