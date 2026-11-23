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
