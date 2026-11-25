#include "CoopRPGSlashAbility.h"

#include "CoopRPG/AbilitySystem/CoopRPGAreaTargeting.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGExpeditionMaps.h"
#include "CoopRPG/GameFramework/CoopRPGGameMode.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"
#include "CoopRPG/GameFramework/CoopRPGPlayerController.h"

#include "AbilitySystemComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGSlash, Log, All);

namespace
{
    using CoopRPG::Debug::DescribePlayerRequestContext;
    constexpr double VerticalReachCm = 100.0;

    void LogRejection(const AActor* Hero, const TCHAR* Reason)
    {
        if (IsValid(Hero))
        {
            UE_LOG(LogCoopRPGSlash, Log, TEXT("%s Reason=%s %s"),
                Hero->HasAuthority() ? TEXT("SlashRejected") : TEXT("SlashBlocked"), Reason,
                *DescribePlayerRequestContext(Hero, TEXT("Slash")));
        }
    }
}

UCoopRPGSlashAbility::UCoopRPGSlashAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    CostGameplayEffectClass = UCoopRPGSlashCost::StaticClass();
    CooldownGameplayEffectClass = UCoopRPGSlashCooldown::StaticClass();
}

bool UCoopRPGSlashAbility::CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayTagContainer* OptionalRelevantTags) const
{
    const bool bAllowed = Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags);
    if (!bAllowed)
    {
        LogRejection(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr, TEXT("LowStamina"));
    }
    return bAllowed;
}

bool UCoopRPGSlashAbility::CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayTagContainer* OptionalRelevantTags) const
{
    const bool bAllowed = Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags);
    if (!bAllowed)
    {
        LogRejection(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr, TEXT("Cooldown"));
    }
    return bAllowed;
}

void UCoopRPGSlashAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData*)
{
    auto* Hero = Cast<ACoopRPGCharacter>(ActorInfo->AvatarActor.Get());
    const auto* Player = Hero ? Hero->GetController<ACoopRPGPlayerController>() : nullptr;
    const auto* State = Hero ? Hero->GetWorld()->GetGameState<ACoopRPGGameState>() : nullptr;
    const bool bPlayingCombat = State && State->IsRoundPlaying()
        && (CoopRPG::Expedition::IsSkirmish(Hero->GetWorld()) || CoopRPG::Expedition::IsBoss(Hero->GetWorld()));
    const TCHAR* Reason = !Hero || !Hero->HasAuthority() || !bPlayingCombat ? TEXT("NotPlaying")
        : Hero->IsGuardian() ? TEXT("WrongRole")
        : !Player || !Player->GetActiveSkillSlot(ECoopRPGActiveSkill::Slash) ? TEXT("Unregistered")
        : !Hero->HasMeleeWeapon() ? TEXT("Unarmed")
        : Hero->GetHealth() <= 0.0f || Hero->IsGuarding() || Hero->IsMeleeWindupActive() ? TEXT("Busy") : nullptr;
    if (Reason)
    {
        LogRejection(Hero, Reason);
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    const float StaminaBefore = Hero->GetStamina();
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    Round = State->GetRoundId();
    const float Yaw = Hero->GetActorRotation().Yaw;
    Facing = FRotator(0.0f, Yaw, 0.0f).Vector();
    const float Windup = Hero->GetMeleeWindup();
    Swing = Hero->StartMeleePresentation(Yaw, Windup, true);
    Hero->GetWorldTimerManager().SetTimer(HitTimer, this, &ThisClass::ResolveHit, Windup, false);
    UE_LOG(LogCoopRPGSlash, Log, TEXT("SlashStarted Round=%s Swing=%u Stamina=%.2f->%.2f Cooldown=%.2f %s"),
        *Round.ToString(), Swing, StaminaBefore, Hero->GetStamina(), UCoopRPGSlashCooldown::Seconds,
        *DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
}

void UCoopRPGSlashAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(HitTimer);
    }
    if (auto* Hero = ActorInfo ? Cast<ACoopRPGCharacter>(ActorInfo->AvatarActor.Get()) : nullptr; Hero && Swing != 0)
    {
        Hero->FinishMeleeWindup(bWasCancelled);
        if (bWasCancelled)
        {
            UE_LOG(LogCoopRPGSlash, Log, TEXT("SlashCancelled Round=%s Swing=%u %s"),
                *Round.ToString(), Swing, *DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
        }
    }
    Swing = 0;
    Round.Invalidate();
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCoopRPGSlashAbility::ResolveHit()
{
    if (Swing == 0 || !IsActive())
    {
        return;
    }

    const auto* Info = GetCurrentActorInfo();
    const auto Handle = GetCurrentAbilitySpecHandle();
    const auto Activation = GetCurrentActivationInfo();
    auto* Hero = Info ? Cast<ACoopRPGCharacter>(Info->AvatarActor.Get()) : nullptr;
    auto* State = Hero ? Hero->GetWorld()->GetGameState<ACoopRPGGameState>() : nullptr;
    const auto* Mode = Hero ? Hero->GetWorld()->GetAuthGameMode<ACoopRPGGameMode>() : nullptr;
    if (!Hero || !Hero->HasAuthority() || !Hero->HasMeleeWeapon() || Hero->GetHealth() <= 0.0f
        || !State || State->GetRoundId() != Round || !State->IsRoundPlaying() || !Mode)
    {
        EndAbility(Handle, Info, Activation, false, true);
        return;
    }

    struct FTarget
    {
        ACoopRPGEnemy* Enemy;
        double DistanceSquared;
    };
    TArray<FTarget, TInlineAllocator<8>> Targets;
    const double AngleThreshold = FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
    const auto Consider = [&](ACoopRPGEnemy* Enemy)
    {
        if (!IsValid(Enemy) || Enemy->IsDead() || Enemy->GetHealth() <= 0.0f)
        {
            return;
        }
        const FVector Offset = Enemy->GetActorLocation() - Hero->GetActorLocation();
        const double DistanceSquared = Offset.SizeSquared2D();
        const double Range = RangeCm + Enemy->GetMeleeRangeBonus();
        if (FMath::Abs(Offset.Z) <= VerticalReachCm && DistanceSquared <= FMath::Square(Range)
            && FVector::DotProduct(Facing, Offset.GetSafeNormal2D()) + UE_DOUBLE_SMALL_NUMBER >= AngleThreshold
            && !CoopRPG::AreaTargeting::IsObstructed(*Hero, *Enemy))
        {
            Targets.Add({Enemy, DistanceSquared});
        }
    };
    if (Mode->IsSkirmishEncounter())
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

    Targets.Sort([](const FTarget& Left, const FTarget& Right)
    {
        return Left.DistanceSquared == Right.DistanceSquared
            ? Left.Enemy->GetFName().LexicalLess(Right.Enemy->GetFName()) : Left.DistanceSquared < Right.DistanceSquared;
    });
    const int32 Selected = FMath::Min(MaxTargets, Targets.Num());
    TArray<FCoopRPGGameplayFeedback> FeedbackEvents;
    FeedbackEvents.Reserve(Selected);
    int32 Hits = 0;
    for (int32 Index = 0; Index < Selected; ++Index)
    {
        auto* Enemy = Targets[Index].Enemy;
        if (Enemy->IsShieldRaised() && Enemy->GetShieldAngle(Hero->GetActorLocation())
            <= ACoopRPGEnemy::ShieldHalfAngleDegrees + UE_DOUBLE_KINDA_SMALL_NUMBER)
        {
            const float ShieldYaw = Enemy->GetShieldWarning().FacingYaw;
            FCoopRPGGameplayFeedback Block(ECoopRPGFeedbackKind::ShieldBlocked, Enemy, 0.0f, Hero->GetDisplaySlot());
            Block.Location += FRotator(0.0, ShieldYaw, 0.0).Vector() * 56.0 + FVector(0.0, 0.0, 34.0);
            Block.MeleeEffect = ECoopRPGMeleeEffect::ShieldBlock;
            Block.MeleeEffectLocation = Block.Location;
            Block.MeleeEffectYaw = ShieldYaw;
            Block.MeleeSwing = Swing;
            FeedbackEvents.Add(Block);
            UE_LOG(LogCoopRPGSlash, Log, TEXT("SlashShieldBlocked Round=%s Swing=%u Target=%s %s"),
                *Round.ToString(), Swing, *Enemy->GetName(), *DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
            continue;
        }

        const bool bKeepAttack = Enemy->ShouldPreserveAttackPose();
        const float Before = Enemy->GetHealth();
        const auto Spec = MakeOutgoingGameplayEffectSpec(UCoopRPGMeleeDamage::StaticClass());
        check(Spec.IsValid());
        UCoopRPGMeleeDamage::SetDamage(*Spec.Data.Get(), DamagePerTarget);
        Hero->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), Enemy->GetAbilitySystemComponent());
        const float After = Enemy->GetHealth();
        if (const float Damage = Before - After; Damage > 0.0f)
        {
            ++Hits;
            FCoopRPGGameplayFeedback Feedback(ECoopRPGFeedbackKind::Damage, Enemy, Damage, Hero->GetDisplaySlot());
            Feedback.MeleeEffect = Enemy->IsMushroom() ? ECoopRPGMeleeEffect::MushroomHit : ECoopRPGMeleeEffect::ArmorHit;
            Feedback.MeleeEffectLocation = Feedback.Location;
            Feedback.MeleeEffectYaw = Facing.Rotation().Yaw;
            Feedback.MeleeSwing = Swing;
            Feedback.bPreserveEnemyAttackPose = bKeepAttack;
            // All targets get impact feedback. The owner gets one shake and hit stop per cast.
            Feedback.bTriggerLocalMeleeImpact = Hits == 1;
            FeedbackEvents.Add(Feedback);
        }
        UE_LOG(LogCoopRPGSlash, Log, TEXT("SlashHit Round=%s Swing=%u Target=%s Distance=%.2f HP=%.2f->%.2f %s"),
            *Round.ToString(), Swing, *Enemy->GetName(), FMath::Sqrt(Targets[Index].DistanceSquared),
            Before, After, *DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    }
    if (!FeedbackEvents.IsEmpty())
    {
        // Send all targets in one multicast so the per-update limit cannot drop the third impact.
        State->MulticastSlashFeedback(FeedbackEvents);
    }
    UE_LOG(LogCoopRPGSlash, Log, TEXT("SlashResolved Round=%s Swing=%u Eligible=%d Selected=%d Hits=%d %s"),
        *Round.ToString(), Swing, Targets.Num(), Selected, Hits, *DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    EndAbility(Handle, Info, Activation, false, false);
}
