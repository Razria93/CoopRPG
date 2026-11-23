#include "CoopRPGTauntAbility.h"

#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/AbilitySystem/CoopRPGAreaTargeting.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Debug/Logging/CoopRPGTauntLog.h"
#include "CoopRPG/GameFramework/CoopRPGExpeditionMaps.h"
#include "CoopRPG/GameFramework/CoopRPGGameMode.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace TauntLog = CoopRPG::Debug::TauntLog;

UCoopRPGTauntAbility::UCoopRPGTauntAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    CostGameplayEffectClass = UCoopRPGTauntCost::StaticClass();
    CooldownGameplayEffectClass = UCoopRPGTauntCooldown::StaticClass();
}

bool UCoopRPGTauntAbility::CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayTagContainer* OptionalRelevantTags) const
{
    const bool bAllowed = Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags);
    if (!bAllowed && ActorInfo)
    {
        TauntLog::Rejected(ActorInfo->AvatarActor.Get(), TEXT("LowStamina"));
    }
    return bAllowed;
}

bool UCoopRPGTauntAbility::CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayTagContainer* OptionalRelevantTags) const
{
    const bool bAllowed = Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags);
    if (!bAllowed && ActorInfo)
    {
        TauntLog::Rejected(ActorInfo->AvatarActor.Get(), TEXT("Cooldown"));
    }
    return bAllowed;
}

void UCoopRPGTauntAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData*)
{
    auto* Caster = Cast<ACoopRPGCharacter>(ActorInfo->AvatarActor.Get());
    UWorld* World = Caster ? Caster->GetWorld() : nullptr;
    const auto* State = World ? World->GetGameState<ACoopRPGGameState>() : nullptr;
    const auto* Mode = World ? World->GetAuthGameMode<ACoopRPGGameMode>() : nullptr;
    const bool bSkirmish = CoopRPG::Expedition::IsSkirmish(World);
    const bool bCombat = bSkirmish || CoopRPG::Expedition::IsBoss(World);
    const TCHAR* Rejection = !Caster || !Caster->HasAuthority() || !State || !State->IsRoundPlaying() || !Mode || !bCombat
        ? TEXT("NotPlaying") : !Caster->IsGuardian() ? TEXT("WrongRole") : !Caster->HasShieldEquipped()
        ? TEXT("NoShield") : Caster->GetHealth() <= 0.0f || Caster->IsMeleeWindupActive() ? TEXT("Busy") : nullptr;
    if (Rejection)
    {
        TauntLog::Rejected(Caster, Rejection);
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    TArray<ACoopRPGEnemy*, TInlineAllocator<4>> Targets;
    const auto Consider = [&](ACoopRPGEnemy* Enemy)
    {
        if (IsValid(Enemy) && !Enemy->IsDead() && IsInRange(*Caster, *Enemy))
        {
            Targets.Add(Enemy);
        }
    };
    if (bSkirmish)
    {
        for (ACoopRPGEnemy* Enemy : Mode->GetSkirmishEnemies())
        {
            Consider(Enemy);
        }
    }
    else
    {
        Consider(Cast<ACoopRPGEnemy>(Mode->GetMeleeTarget()));
    }
    if (Targets.IsEmpty())
    {
        TauntLog::Rejected(Caster, TEXT("NoTargets"));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    const float Before = Caster->GetStamina();
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    const double EndsAt = State->GetServerWorldTimeSeconds() + DurationSeconds;
    TauntLog::Cast(State->GetRoundId(), *Caster, Targets.Num(), Before, Caster->GetStamina(), EndsAt);
    for (ACoopRPGEnemy* Enemy : Targets)
    {
        Enemy->ApplyTaunt(*Caster, EndsAt);
    }
    const double GroundZ = Caster->GetActorLocation().Z - Caster->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Origin(Caster->GetActorLocation().X, Caster->GetActorLocation().Y, GroundZ + 3.0);
    Caster->MulticastTauntPulse(Origin);
    EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

bool UCoopRPGTauntAbility::IsInRange(const ACoopRPGCharacter& Caster, const ACoopRPGEnemy& Enemy)
{
    const auto* Capsule = Enemy.GetCapsuleComponent();
    const double EnemyReach = RangeCm + Capsule->GetScaledCapsuleRadius();
    const double CasterFloor = Caster.GetActorLocation().Z - Caster.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const double EnemyFloor = Enemy.GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
    return FMath::Abs(CasterFloor - EnemyFloor) <= 100.0
        && FVector::DistSquared2D(Caster.GetActorLocation(), Enemy.GetActorLocation()) <= FMath::Square(EnemyReach)
        && !CoopRPG::AreaTargeting::IsObstructed(Caster, Enemy);
}
