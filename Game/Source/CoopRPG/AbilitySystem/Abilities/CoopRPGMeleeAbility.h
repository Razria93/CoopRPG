#pragma once

#include "Abilities/GameplayAbility.h"
#include "CoopRPGMeleeAbility.generated.h"

UCLASS()
class COOPRPG_API UCoopRPGMeleeAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UCoopRPGMeleeAbility();

protected:
    virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
