#pragma once

#include "Abilities/GameplayAbility.h"
#include "Engine/TimerHandle.h"
#include "CoopRPGSlashAbility.generated.h"

UCLASS()
class COOPRPG_API UCoopRPGSlashAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UCoopRPGSlashAbility();

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
    void ResolveHit();

public:
    static constexpr int32 MaxTargets = 3;
    static constexpr double RangeCm = 230.0;
    static constexpr double HalfAngleDegrees = 55.0;
    static constexpr float DamagePerTarget = 18.0f;

private:
    FGuid Round;
    FTimerHandle HitTimer;
    FVector Facing = FVector::ForwardVector;
    uint32 Swing = 0;
};
