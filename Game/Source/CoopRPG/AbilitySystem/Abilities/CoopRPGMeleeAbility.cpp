#include "CoopRPGMeleeAbility.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGGameMode.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGMelee, Log, All);

namespace
{
    constexpr float MeleeRangeCm = 250.0f;
    constexpr float ProbeWindupSeconds = 0.65f;
    constexpr float SweepLengthCm = 145.0f;
    constexpr float SweepRadiusCm = 37.0f;

    using CoopRPG::Debug::DescribePlayerRequestContext;

    const TCHAR* SweepForEnemy(const AActor* Attacker, const AActor* Target, const FVector& Direction, uint32 Swing)
    {
        const FVector Start = Attacker->GetActorLocation();
        const FVector TargetDelta = Target->GetActorLocation() - Start;
        if (FVector::DotProduct(Direction, TargetDelta) <= 0.0)
        {
            return TEXT("OutsideSwing");
        }

        UWorld* World = Attacker->GetWorld();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CoopRPGMelee), false, Attacker);
        Query.bFindInitialOverlaps = true;
        FCollisionObjectQueryParams Objects;
        Objects.AddObjectTypesToQuery(ECC_Pawn);
        Objects.AddObjectTypesToQuery(ECC_WorldStatic);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);

        const FVector End = Start + Direction * SweepLengthCm;
        TArray<FHitResult> Contacts;
        World->SweepMultiByObjectType(Contacts, Start, End, FQuat::Identity,
            Objects, FCollisionShape::MakeSphere(SweepRadiusCm), Query);

        // Resolve the earliest contact per actor and let world geometry win ties at an obstacle.
        int32 EnemyContacts = 0;
        float EnemyTime = TNumericLimits<float>::Max();
        float ObstacleTime = TNumericLimits<float>::Max();
        for (const FHitResult& Contact : Contacts)
        {
            if (Contact.GetActor() == Target)
            {
                ++EnemyContacts;
                EnemyTime = FMath::Min(EnemyTime, Contact.Time);
            }
            else if (const UPrimitiveComponent* Component = Contact.GetComponent();
                Component && Component->GetCollisionObjectType() != ECC_Pawn
                && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
            {
                ObstacleTime = FMath::Min(ObstacleTime, Contact.Time);
            }
        }

        UE_CLOG(EnemyContacts > 1, LogCoopRPGMelee, Log,
            TEXT("MeleeMultipleContacts Target=%s Components=%d"), *GetNameSafe(Target), EnemyContacts);
        const bool bObstructed = ObstacleTime < TNumericLimits<float>::Max() && ObstacleTime <= EnemyTime;
        const bool bHit = EnemyTime < TNumericLimits<float>::Max() && !bObstructed;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
        if (FParse::Param(FCommandLine::Get(), TEXT("CoopMeleeSweepDebug")))
        {
            if (auto* State = World->GetGameState<ACoopRPGGameState>())
            {
                FCoopRPGMeleeSweepTrace Trace;
                Trace.Start = Start;
                Trace.End = End;
                Trace.Radius = SweepRadiusCm;
                const auto* Character = Cast<ACoopRPGCharacter>(Attacker);
                Trace.Slot = Character ? Character->GetDisplaySlot() : 0;
                Trace.Swing = Swing;
                Trace.Result = bHit ? ECoopRPGMeleeSweepResult::Hit
                    : bObstructed ? ECoopRPGMeleeSweepResult::Obstructed : ECoopRPGMeleeSweepResult::Missed;
                Trace.ContactTime = bHit ? EnemyTime : bObstructed ? ObstacleTime : 0.0f;
                UE_LOG(LogCoopRPGMelee, Log, TEXT("MeleeSweepTrace Slot=%d Swing=%u Start=%s End=%s Radius=%.2f Result=%s Contact=%.4f"),
                    Trace.Slot, Trace.Swing, *Trace.Start.ToCompactString(), *Trace.End.ToCompactString(), Trace.Radius,
                    *StaticEnum<ECoopRPGMeleeSweepResult>()->GetNameStringByValue(static_cast<int64>(Trace.Result)), Trace.ContactTime);
                State->MulticastMeleeSweepTrace(Trace);
            }
        }
#endif
        if (bObstructed)
        {
            return TEXT("Obstructed");
        }
        return bHit ? nullptr : TEXT("OutsideSwing");
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

    if (const auto* State = Attacker->GetWorld()->GetGameState<ACoopRPGGameState>(); State && State->HasRoundResult())
    {
        LogFailedCheck(ActorInfo, TEXT("NotPlaying"), ANSI_TO_TCHAR(__FUNCTION__));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    auto* GameMode = Attacker->GetWorld()->GetAuthGameMode<ACoopRPGGameMode>();
    AActor* Target = GameMode ? GameMode->GetMeleeTarget() : nullptr;
    const UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target, false);
    const FGameplayAttribute HealthAttribute = UCoopRPGCombatAttributes::GetHealthAttribute();
    if (!IsValid(TargetASC) || TargetASC->GetNumericAttribute(HealthAttribute) <= 0.0f)
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackRejected Attacker=%s Reason=InvalidTarget %s"),
            *GetNameSafe(Attacker), *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
        return;
    }

    const float Distance = FVector::Dist2D(Attacker->GetActorLocation(), Target->GetActorLocation());
    const float HealthBefore = TargetASC->GetNumericAttribute(HealthAttribute);
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
    AttackSequence = 0;
    float Windup = ProbeWindupSeconds;
    if (Target->IsA<ACoopRPGEnemy>())
    {
        const float Yaw = Attacker->GetActorRotation().Yaw;
        Windup = Attacker->GetMeleeWindup();
        PendingRound = Attacker->GetWorld()->GetGameState<ACoopRPGGameState>()->GetRoundId();
        AttackDirection = FRotator(0.0f, Yaw, 0.0f).Vector();
        AttackSequence = Attacker->StartMeleePresentation(Yaw, Windup);
    }
    bHitPending = true;
    Attacker->GetWorldTimerManager().SetTimer(HitTimer, this, &UCoopRPGMeleeAbility::ResolveHit, Windup, false);
    const float StaminaAfter = Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetStaminaAttribute());
    UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackStarted Attacker=%s Target=%s Windup=%.2f Stamina=%.2f->%.2f %s Swing=%u"),
        *GetNameSafe(Attacker), *GetNameSafe(Target), Windup, StaminaBefore, StaminaAfter,
        *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)), AttackSequence);
}

void UCoopRPGMeleeAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    AActor* Attacker = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
    const bool bCancelledBeforeHit = bHitPending && bWasCancelled;
    if (UWorld* World = GetWorld())
    {
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
        if (bCancelledBeforeHit && AttackSequence != 0 && FParse::Param(FCommandLine::Get(), TEXT("CoopMeleeSweepDebug")))
        {
            UE_LOG(LogCoopRPGMelee, Log, TEXT("MeleeSweepCancelled Swing=%u TimerRemainingMs=%.2f %s"), AttackSequence,
                World->GetTimerManager().GetTimerRemaining(HitTimer) * 1000.0,
                *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)));
        }
#endif
        World->GetTimerManager().ClearTimer(HitTimer);
    }
    bHitPending = false;
    PendingTarget.Reset();
    PendingRound.Invalidate();

    if (auto* Character = Cast<ACoopRPGCharacter>(Attacker); Character && AttackSequence != 0)
    {
        Character->FinishMeleeWindup(bCancelledBeforeHit);
    }

    if (bCancelledBeforeHit)
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackCancelled Attacker=%s Damage=0 Committed=1 %s Swing=%u"),
            *GetNameSafe(Attacker), *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)), AttackSequence);
    }
    AttackSequence = 0;
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
    AActor* Target = PendingTarget.Get();
    UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target, false);
    const FGameplayAttribute HealthAttribute = UCoopRPGCombatAttributes::GetHealthAttribute();
    const auto* GameMode = IsValid(Attacker) ? Attacker->GetWorld()->GetAuthGameMode<ACoopRPGGameMode>() : nullptr;

    auto* State = IsValid(Attacker) ? Attacker->GetWorld()->GetGameState<ACoopRPGGameState>() : nullptr;

    const TCHAR* MissReason = nullptr;
    float Distance = 0.0f;
    if (!IsValid(Attacker) || !IsValid(Source)
        || Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetHealthAttribute()) <= 0.0f)
    {
        MissReason = TEXT("AttackerDead");
    }
    else if ((State && State->HasRoundResult())
        || (PendingRound.IsValid() && (!State || !State->IsRoundPlaying() || State->GetRoundId() != PendingRound)))
    {
        MissReason = TEXT("NotPlaying");
    }
    else if (!IsValid(TargetASC) || !GameMode || GameMode->GetMeleeTarget() != Target
        || TargetASC->GetNumericAttribute(HealthAttribute) <= 0.0f)
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
        else if (AttackSequence != 0)
        {
            MissReason = SweepForEnemy(Attacker, Target, AttackDirection, AttackSequence);
        }
    }

    if (MissReason)
    {
        UE_LOG(LogCoopRPGMelee, Log, TEXT("AttackMissed Attacker=%s Target=%s Reason=%s Damage=0 Committed=1 %s Swing=%u"),
            *GetNameSafe(Attacker), *GetNameSafe(Target), MissReason,
            *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)), AttackSequence);
        if (AttackSequence != 0 && (!IsValid(Attacker) || !IsValid(Source)
            || Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetHealthAttribute()) <= 0.0f
            || !State || !State->IsRoundPlaying()))
        {
            if (auto* Character = Cast<ACoopRPGCharacter>(ActorInfo->AvatarActor.Get()))
            {
                Character->FinishMeleeWindup(true);
            }
            AttackSequence = 0;
        }
        EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
        return;
    }

    if (const auto* Enemy = Cast<ACoopRPGEnemy>(Target); Enemy && Enemy->IsShieldEncounter())
    {
        const double Angle = Enemy->GetShieldAngle(Attacker->GetActorLocation());
        const bool bRaised = Enemy->IsShieldRaised();
        // Allow roundoff at the inclusive 60-degree boundary.
        const bool bBlocked = bRaised && Angle <= ACoopRPGEnemy::ShieldHalfAngleDegrees + UE_DOUBLE_KINDA_SMALL_NUMBER;
        UE_LOG(LogCoopRPGMelee, Log, TEXT("ShieldMeleeEvaluated Angle=%.4f Raised=%d Outcome=%s %s Swing=%u"),
            Angle, bRaised, bBlocked ? TEXT("Blocked") : TEXT("Hit"),
            *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)), AttackSequence);
        if (bBlocked)
        {
            if (State)
            {
                const auto* Character = Cast<ACoopRPGCharacter>(Attacker);
                FCoopRPGGameplayFeedback Feedback(ECoopRPGFeedbackKind::ShieldBlocked, Target, 0.0f,
                    Character ? Character->GetDisplaySlot() : INDEX_NONE);
                const FVector ShieldForward = FRotator(0.0, Enemy->GetShieldWarning().FacingYaw, 0.0).Vector();
                Feedback.Location += ShieldForward * 56.0 + FVector(0.0, 0.0, 34.0);
                State->MulticastGameplayFeedback(Feedback);
            }
            EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
            return;
        }
    }

    const float HealthBefore = TargetASC->GetNumericAttribute(HealthAttribute);
    const FGameplayEffectSpecHandle Damage = MakeOutgoingGameplayEffectSpec(UCoopRPGMeleeDamage::StaticClass());
    check(Damage.IsValid());
    Source->ApplyGameplayEffectSpecToTarget(*Damage.Data.Get(), TargetASC);
    if (const float DamageAmount = HealthBefore - TargetASC->GetNumericAttribute(HealthAttribute); State && DamageAmount > 0.0f)
    {
        const auto* Character = Cast<ACoopRPGCharacter>(Attacker);
        const int32 Slot = Character && Character->GetDisplaySlot() > 0 ? Character->GetDisplaySlot() : INDEX_NONE;
        State->MulticastGameplayFeedback({ECoopRPGFeedbackKind::Damage, Target, DamageAmount, Slot});
    }
    UE_LOG(LogCoopRPGMelee, Log,
        TEXT("AttackApproved Attacker=%s Target=%s Distance=%.2f Range=%.2f HP=%.2f->%.2f %s Swing=%u"),
        *GetNameSafe(Attacker), *GetNameSafe(Target), Distance, MeleeRangeCm, HealthBefore, TargetASC->GetNumericAttribute(HealthAttribute),
        *DescribePlayerRequestContext(Attacker, ANSI_TO_TCHAR(__FUNCTION__)), AttackSequence);
    EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
