#pragma once

#include "GameplayEffect.h"
#include "CoopRPGMeleeEffects.generated.h"

UCLASS()
class COOPRPG_API UCoopRPGMeleeDamage : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGMeleeDamage();

    static void SetDamage(FGameplayEffectSpec& Spec, float Amount);

public:
    static constexpr float BaseDamage = 10.0f;
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

UCLASS()
class COOPRPG_API UCoopRPGSlashCost : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGSlashCost();

public:
    static constexpr float Stamina = 25.0f;
};

UCLASS()
class COOPRPG_API UCoopRPGSlashCooldown : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGSlashCooldown();
    static FGameplayTag GetCooldownTag();

public:
    static constexpr float Seconds = 6.0f;
};
