#pragma once

#include "CoreMinimal.h"
#include "CoopRPGStatProgress.generated.h"

UENUM()
enum class ECoopRPGScrollStat : uint8
{
    MartialPower,
    Protection
};

USTRUCT()
struct FCoopRPGScrollProgress
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Level = 0;

    UPROPERTY()
    int32 ScrollCount = 0;
};

USTRUCT()
struct FCoopRPGStatProgress
{
    GENERATED_BODY()

    static constexpr int32 PointsPerScroll = 1;
    static constexpr float DamagePerMartialPower = 1.0f;

    UPROPERTY()
    FCoopRPGScrollProgress MartialPower;

    UPROPERTY()
    FCoopRPGScrollProgress Protection;

    FCoopRPGScrollProgress* Find(ECoopRPGScrollStat Stat)
    {
        switch (Stat)
        {
        case ECoopRPGScrollStat::MartialPower: return &MartialPower;
        case ECoopRPGScrollStat::Protection: return &Protection;
        default: return nullptr;
        }
    }
};
