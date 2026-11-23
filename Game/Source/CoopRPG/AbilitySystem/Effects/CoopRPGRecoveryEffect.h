#pragma once

#include "GameplayEffect.h"
#include "CoopRPGRecoveryEffect.generated.h"

UCLASS()
class COOPRPG_API UCoopRPGRecoveryEffect : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGRecoveryEffect();

public:
    static constexpr float HealthRestored = 30.0f;
};

UCLASS()
class COOPRPG_API UCoopRPGStaminaRecoveryEffect : public UGameplayEffect
{
    GENERATED_BODY()

public:
    UCoopRPGStaminaRecoveryEffect();

public:
    static constexpr float StaminaRestored = 30.0f;
};
