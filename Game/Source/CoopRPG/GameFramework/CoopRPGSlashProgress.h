#pragma once

#include "CoreMinimal.h"
#include "CoopRPGSlashProgress.generated.h"

UENUM()
enum class ECoopRPGActiveSkill : uint8
{
    None,
    Slash,
    Taunt
};

USTRUCT()
struct FCoopRPGActiveSkillSlots
{
    GENERATED_BODY()

    UPROPERTY()
    uint8 Slash = 0;

    UPROPERTY()
    uint8 Taunt = 0;
};

USTRUCT()
struct FCoopRPGSkillProgress
{
    GENERATED_BODY()

    UPROPERTY()
    bool bHasCapsule = false;

    UPROPERTY()
    bool bRegistered = false;
};

USTRUCT()
struct FCoopRPGSkillCapsuleProgress
{
    GENERATED_BODY()

    UPROPERTY()
    FCoopRPGSkillProgress Slash;

    UPROPERTY()
    FCoopRPGSkillProgress Taunt;
};
