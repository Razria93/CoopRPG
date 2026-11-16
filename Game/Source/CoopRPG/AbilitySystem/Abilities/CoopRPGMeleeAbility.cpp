#include "CoopRPGMeleeAbility.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGGameMode.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGMelee, Log, All);

namespace
{
    constexpr float MeleeRangeCm = 250.0f;
    constexpr float WindupSeconds = 0.65f;

    using CoopRPG::Debug::DescribePlayerRequestContext;

    void LogFailedCheck(const FGameplayAbilityActorInfo* ActorInfo, const TCHAR* Reason, const TCHAR* Function)
    {
        const AActor* Actor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
        const UAbilitySystemComponent* Source = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
        if (!IsValid(Actor) || !IsValid(Source))
        {
            return;
        }

        UE_LOG(LogCoopRPGMelee, Log, TEXT("%s Attacker=%s Reason=%s Stamina=%.2f Cooldown=%d %s"),
            Actor->HasAuthority() ? TEXT("AttackRejected") : TEXT("AttackBlocked"), *GetNameSafe(Actor), Reason,
            Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetStaminaAttribute()),
            Source->HasMatchingGameplayTag(UCoopRPGMeleeCooldown::GetCooldownTag()) ? 1 : 0,
            *DescribePlayerRequestContext(Actor, Function));
    }
}

UCoopRPGMeleeAbility::UCoopRPGMeleeAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    CostGameplayEffectClass = UCoopRPGMeleeCost::StaticClass();
    CooldownGameplayEffectClass = UCoopRPGMeleeCooldown::StaticClass();
}

bool UCoopRPGMeleeAbility::CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayTagContainer* OptionalRelevantTags) const
{
    const bool bAllowed = Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags);
    if (!bAllowed)
    {
        LogFailedCheck(ActorInfo, TEXT("Cost"), ANSI_TO_TCHAR(__FUNCTION__));
    }
    return bAllowed;
}

bool UCoopRPGMeleeAbility::CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayTagContainer* OptionalRelevantTags) const
{
    const bool bAllowed = Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags);
    if (!bAllowed)
    {
        LogFailedCheck(ActorInfo, TEXT("Cooldown"), ANSI_TO_TCHAR(__FUNCTION__));
    }
    return bAllowed;
}

void UCoopRPGMeleeAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    auto* Attacker = Cast<ACoopRPGCharacter>(ActorInfo->AvatarActor.Get());
    UAbilitySystemComponent* Source = ActorInfo->AbilitySystemComponent.Get();
    if (!IsValid(Attacker) || !Attacker->HasAuthority() || !IsValid(Source))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    if (Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetHealthAttribute()) <= 0.0f)
    {
        LogFailedCheck(ActorInfo, TEXT("AttackerDead"), ANSI_TO_TCHAR(__FUNCTION__));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    if (!Attacker->HasMeleeWeapon())
    {
        LogFailedCheck(ActorInfo, TEXT("Unarmed"), ANSI_TO_TCHAR(__FUNCTION__));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    auto* GameMode = Attacker->GetWorld()->GetAuthGameMode<ACoopRPGGameMode>();
    ACoopRPGAttackTarget* Target = GameMode ? GameMode->GetAttackTarget() : nullptr;
    if (!IsValid(Target) || Target->GetHealth() <= 0.0f)
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackRejected Attacker=%s Reason=InvalidTarget %s"),
            *GetNameSafe(Attacker), *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
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
            *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackRejected Attacker=%s Reason=CommitFailed %s"),
            *GetNameSafe(Attacker), *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    PendingTarget = Target;
    bHitPending = true;
    Attacker->GetWorldTimerManager().SetTimer(HitTimer, this, &UCoopRPGMeleeAbility::ResolveHit, WindupSeconds, false);
    const float StaminaAfter = Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetStaminaAttribute());
    UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackStarted Attacker=%s Target=%s Windup=%.2f Stamina=%.2f->%.2f %s"),
        *GetNameSafe(Attacker), *GetNameSafe(Target), WindupSeconds, StaminaBefore, StaminaAfter,
        *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
}

void UCoopRPGMeleeAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    AActor* Attacker = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
    const bool bCancelledBeforeHit = bHitPending && bWasCancelled;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(HitTimer);
    }
    bHitPending = false;
    PendingTarget.Reset();

    if (bCancelledBeforeHit)
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackCancelled Attacker=%s Damage=0 Committed=1 %s"),
            *GetNameSafe(Attacker), *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
    }
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCoopRPGMeleeAbility::ResolveHit()
{
    if (!bHitPending)
    {
        return;
    }
    bHitPending = false;

    const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
    const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
    const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
    const AActor* Attacker = ActorInfo->AvatarActor.Get();
    UAbilitySystemComponent* Source = ActorInfo->AbilitySystemComponent.Get();
    ACoopRPGAttackTarget* Target = PendingTarget.Get();

    const TCHAR* MissReason = nullptr;
    float Distance = 0.0f;
    if (!IsValid(Attacker) || !IsValid(Source)
        || Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetHealthAttribute()) <= 0.0f)
    {
        MissReason = TEXT("AttackerDead");
    }
    else if (!IsValid(Target) || Target->GetHealth() <= 0.0f)
    {
        MissReason = TEXT("InvalidTarget");
    }
    else
    {
        Distance = FVector::Dist2D(Attacker->GetActorLocation(), Target->GetActorLocation());
        if (Distance > MeleeRangeCm)
        {
            MissReason = TEXT("OutOfRange");
        }
    }

    if (MissReason)
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackMissed Attacker=%s Target=%s Reason=%s Damage=0 Committed=1 %s"),
            *GetNameSafe(Attacker), *GetNameSafe(Target), MissReason,
            *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
        return;
    }

    const float HealthBefore = Target->GetHealth();
    const FGameplayEffectSpecHandle Damage = MakeOutgoingGameplayEffectSpec(UCoopRPGMeleeDamage::StaticClass());
    check(Damage.IsValid());
    Source->ApplyGameplayEffectSpecToTarget(*Damage.Data.Get(), Target->GetAbilitySystemComponent());
    UE_LOG(LogCoopRPGMelee, Log,
        TEXT("AttackApproved Attacker=%s Target=%s Distance=%.2f Range=%.2f HP=%.2f->%.2f %s"),
        *GetNameSafe(Attacker), *GetNameSafe(Target), Distance, MeleeRangeCm, HealthBefore, Target->GetHealth(),
        *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
    EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
