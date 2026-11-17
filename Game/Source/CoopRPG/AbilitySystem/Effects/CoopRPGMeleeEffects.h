#pragma once

#include "GameplayEffect.h"
#include "CoopRPGMeleeEffects.generated.h"

UCLASS()
class COOPRPG_API UCoopRPGMeleeDamage : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGMeleeDamage();
};

UCLASS()
class COOPRPG_API UCoopRPGMeleeCost : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGMeleeCost();
};

UCLASS()
class COOPRPG_API UCoopRPGMeleeCooldown : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGMeleeCooldown();

    static FGameplayTag GetCooldownTag();
};
