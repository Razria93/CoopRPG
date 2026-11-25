#pragma once

#include "GameplayEffect.h"
#include "CoopRPGRecoveryEffect.generated.h"

namespace CoopRPG::Recovery
{
    inline constexpr int32 SupplyAmount = 30;
    inline constexpr int32 PersonalAmount = 40;

    void SetAmount(FGameplayEffectSpec& Spec, float Amount);
}

UCLASS()
class COOPRPG_API UCoopRPGRecoveryEffect : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGRecoveryEffect();

};

UCLASS()
class COOPRPG_API UCoopRPGStaminaRecoveryEffect : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGStaminaRecoveryEffect();

};
