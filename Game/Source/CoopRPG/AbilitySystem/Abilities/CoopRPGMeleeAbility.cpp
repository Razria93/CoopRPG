#include "CoopRPGMeleeAbility.h"

#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGGameMode.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGMelee, Log, All);

namespace
{
    constexpr float MeleeRangeCm = 250.0f;

    FString DescribeMeleeLogContext(const AActor* Actor, const TCHAR* Function)
    {
        FString Context = CoopRPG::Debug::DescribeLogContext(Actor, Function);
        // The server derives the player from the attacking Pawn's owning connection.
        const auto* Pawn = Cast<APawn>(Actor);
        if (Pawn && Pawn->HasAuthority() && Pawn->GetNetConnection())
        {
            if (const APlayerState* PlayerState = Pawn->GetPlayerState(); PlayerState && PlayerState->GetPlayerId() > 0)
            {
                Context += FString::Printf(TEXT(" RequesterPlayerId=%d"), PlayerState->GetPlayerId());
            }
        }
        return Context;
    }

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
            *DescribeMeleeLogContext(Actor, Function));
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
            *GetNameSafe(Attacker), *DescribeMeleeLogContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
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
            *DescribeMeleeLogContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackRejected Attacker=%s Reason=CommitFailed %s"),
            *GetNameSafe(Attacker), *DescribeMeleeLogContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
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
        *DescribeMeleeLogContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
    EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
