#pragma once

#include "Abilities/GameplayAbility.h"
#include "Engine/TimerHandle.h"
#include "CoopRPGMeleeAbility.generated.h"

class AActor;
class UAbilitySystemComponent;
class UCoopRPGCombatProbeComponent;

UCLASS()
class COOPRPG_API UCoopRPGMeleeAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UCoopRPGMeleeAbility();
    static bool CanPayPresentationCost(const UAbilitySystemComponent& Source);

protected:
    virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
    friend class UCoopRPGCombatProbeComponent;

    void ResolveHit();

private:
    TWeakObjectPtr<AActor> PendingTarget;
    FGuid PendingRound;
    FVector AttackDirection = FVector::ForwardVector;
    uint32 AttackSequence = 0;
    FTimerHandle HitTimer;
    bool bHitPending = false;
};
