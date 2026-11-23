#pragma once

#include "Abilities/GameplayAbility.h"
#include "CoopRPGTauntAbility.generated.h"

class ACoopRPGCharacter;
class ACoopRPGEnemy;

UCLASS()
class COOPRPG_API UCoopRPGTauntAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UCoopRPGTauntAbility();

    static constexpr double RangeCm = 400.0;
    static constexpr double DurationSeconds = 4.0;
    static constexpr double PulseSeconds = 0.48;

protected:
    virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
    static bool IsInRange(const ACoopRPGCharacter& Caster, const ACoopRPGEnemy& Enemy);
};
