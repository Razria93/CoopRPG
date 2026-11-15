#include "CoopRPGMeleeAbility.h"

#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGGameMode.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGMelee, Log, All);

namespace
{
    constexpr float MeleeRangeCm = 250.0f;
}

UCoopRPGMeleeAbility::UCoopRPGMeleeAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    CostGameplayEffectClass = UCoopRPGMeleeCost::StaticClass();
}

void UCoopRPGMeleeAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    auto* Attacker = Cast<APawn>(ActorInfo->AvatarActor.Get());
    UAbilitySystemComponent* Source = ActorInfo->AbilitySystemComponent.Get();
    if (!IsValid(Attacker) || !Attacker->HasAuthority() || !IsValid(Source))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    auto* GameMode = Attacker->GetWorld()->GetAuthGameMode<ACoopRPGGameMode>();
    ACoopRPGAttackTarget* Target = GameMode ? GameMode->GetAttackTarget() : nullptr;
    if (!IsValid(Target) || Target->GetHealth() <= 0.0f)
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackRejected Attacker=%s Reason=InvalidTarget %s"),
            *GetNameSafe(Attacker), *CoopRPG::Debug::DescribeLogContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    const float Distance = FVector::Dist2D(Attacker->GetActorLocation(), Target->GetActorLocation());
    const float HealthBefore = Target->GetHealth();
    const float StaminaBefore = Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetStaminaAttribute());
    if (Distance > MeleeRangeCm)
    {
        UE_LOG(LogCoopRPGMelee, Log,
            TEXT("AttackRejected Attacker=%s Target=%s Reason=OutOfRange Distance=%.2f Range=%.2f HP=%.2f Stamina=%.2f %s"),
            *GetNameSafe(Attacker), *GetNameSafe(Target), Distance, MeleeRangeCm, HealthBefore, StaminaBefore,
            *CoopRPG::Debug::DescribeLogContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackRejected Attacker=%s Reason=CommitFailed %s"),
            *GetNameSafe(Attacker), *CoopRPG::Debug::DescribeLogContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    const FGameplayEffectSpecHandle Damage = MakeOutgoingGameplayEffectSpec(UCoopRPGMeleeDamage::StaticClass());
    check(Damage.IsValid());
    Source->ApplyGameplayEffectSpecToTarget(*Damage.Data.Get(), Target->GetAbilitySystemComponent());
    UE_LOG(LogCoopRPGMelee, Log,
        TEXT("AttackApproved Attacker=%s Target=%s Distance=%.2f Range=%.2f HP=%.2f->%.2f Stamina=%.2f->%.2f %s"),
        *GetNameSafe(Attacker), *GetNameSafe(Target), Distance, MeleeRangeCm, HealthBefore, Target->GetHealth(),
        StaminaBefore, Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetStaminaAttribute()),
        *CoopRPG::Debug::DescribeLogContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
    EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
