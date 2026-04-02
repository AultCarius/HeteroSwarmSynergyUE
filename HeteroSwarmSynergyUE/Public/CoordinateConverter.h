// Copyright Epic Games, Inc. All Rights Reserved.
// 项目：室内外异构编队协同演示验证系统
// 模块：UDP通信 - 工具层
// 作者：Carius
// 日期：2026-02-09

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UDPProtocolTypes.h"
#include "CoordinateConverter.generated.h"

/**
 * 坐标系转换工具类
 *
 * 核心职责：
 * - 将UDP协议的NED坐标转换为UE的FVector
 * - 将UDP协议的姿态角转换为UE的FRotator
 *
 * 设计理念：
 * - C++代码直接使用静态内联函数（零开销）
 * - 蓝图不需要访问此类（业务层已经是FVector/FRotator）
 * - 转换只在协议边界发生一次
 *
 * 坐标系转换规则：
 * - NED_North  → UE_X（前方）
 * - NED_East   → UE_Y（右方）
 * - NED_Down   → -UE_Z（向下为正 → 向上为正）
 * - 单位转换：米 → 厘米（×100）
 *
 * 姿态转换规则：
 * - Roll（横滚）、Pitch（俯仰）、Yaw（航向）
 * - 单位转换：弧度 → 角度（×180/π）
 */
UCLASS()
class HETEROSWARMSYNERGYUE_API UCoordinateConverter : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ========== C++ 静态内联函数（主要接口） ==========

    /**
     * NED向量 → UE向量
     *
     * @param NED NED坐标（米）
     * @return UE坐标（厘米）
     */
    static FORCEINLINE FVector NEDToUE(const FNEDVector& NED)
    {
        return FVector(
            NED.North * 100.0f,    // 北 → X
            NED.East * 100.0f,     // 东 → Y
            -NED.Down * 100.0f     // 地向(负) → 天向(正)
        );
    }

    /**
     * NED姿态 → UE旋转
     *
     * @param Attitude 姿态角（弧度）
     * @return UE旋转器（度）
     */
    static FORCEINLINE FRotator AttitudeToUE(const FAttitude& Attitude)
    {
        constexpr float RadToDeg = 180.0f / PI;

        return FRotator(
            Attitude.Pitch * RadToDeg,  // 俯仰
            Attitude.Yaw * RadToDeg,    // 航向
            Attitude.Roll * RadToDeg    // 横滚
        );
    }

    /**
     * UE向量 → NED向量
     *
     * @param UE UE坐标（厘米）
     * @return NED坐标（米）
     */
    static FORCEINLINE FNEDVector UEToNED(const FVector& UE)
    {
        FNEDVector NED;
        NED.North = UE.X * 0.01f;   // X → 北
        NED.East = UE.Y * 0.01f;    // Y → 东
        NED.Down = -UE.Z * 0.01f;   // -Z → 地向
        return NED;
    }

    /**
     * UE旋转 → NED姿态
     *
     * @param Rotation UE旋转器（度）
     * @return 姿态角（弧度）
     */
    static FORCEINLINE FAttitude UEToAttitude(const FRotator& Rotation)
    {
        constexpr float DegToRad = PI / 180.0f;

        FAttitude Attitude;
        Attitude.Roll = Rotation.Roll * DegToRad;
        Attitude.Pitch = Rotation.Pitch * DegToRad;
        Attitude.Yaw = Rotation.Yaw * DegToRad;
        return Attitude;
    }

    // ========== 批量转换（高性能） ==========

    /**
     * 批量NED → UE转换（无内存分配）
     *
     * @param InputNED NED数组指针
     * @param OutputUE UE数组指针（预分配）
     * @param Count 数量
     */
    static void BatchConvertNEDToUE(
        const FNEDVector* InputNED,
        FVector* OutputUE,
        int32 Count
    );

    /**
     * 批量UE → NED转换（无内存分配）
     *
     * @param InputUE UE数组指针
     * @param OutputNED NED数组指针（预分配）
     * @param Count 数量
     */
    static void BatchConvertUEToNED(
        const FVector* InputUE,
        FNEDVector* OutputNED,
        int32 Count
    );

    // ========== 辅助工具函数（C++专用） ==========

    /**
     * 验证NED向量是否合法
     *
     * @param NED NED向量
     * @param MaxDistance 最大距离（米）
     * @return true表示合法
     */
    static bool IsValidNED(const FNEDVector& NED, float MaxDistance = 1000.0f);

    /**
     * 验证姿态角是否合法
     *
     * @param Attitude 姿态角
     * @return true表示合法
     */
    static bool IsValidAttitude(const FAttitude& Attitude);

    /**
     * 计算两个NED位置的距离
     *
     * @param NED1 位置1
     * @param NED2 位置2
     * @return 距离（米）
     */
    static float NEDDistance(const FNEDVector& NED1, const FNEDVector& NED2);

    /**
     * 归一化航向角到[0, 2π]
     *
     * @param Yaw 航向角（弧度）
     * @return 归一化后的航向角
     */
    static float NormalizeYaw(float Yaw);

private:
    static constexpr float RAD_TO_DEG = 180.0f / PI;
    static constexpr float DEG_TO_RAD = PI / 180.0f;
    static constexpr float METER_TO_CM = 100.0f;
    static constexpr float CM_TO_METER = 0.01f;
    static constexpr float TWO_PI = PI * 2.0f;
};