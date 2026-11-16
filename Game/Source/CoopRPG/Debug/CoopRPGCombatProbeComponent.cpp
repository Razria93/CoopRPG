#include "CoopRPGCombatProbeComponent.h"

#include "CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGCombatProbe, Log, All);

namespace
{
    bool IsProbeEnabled(const UWorld* World)
    {
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
        return World && World->IsGameWorld() && FParse::Param(FCommandLine::Get(), TEXT("CoopCombatTest"));
#else
        return false;
#endif
    }

    UAbilitySystemComponent* GetSource(const UActorComponent* Component)
    {
        const auto* Character = Cast<ACoopRPGCharacter>(Component->GetOwner());
        return Character ? Character->GetAbilitySystemComponent() : nullptr;
    }

    ACoopRPGAttackTarget* GetTarget(UWorld* World)
    {
        for (TActorIterator<ACoopRPGAttackTarget> It(World); It; ++It)
        {
            return *It;
        }
        return nullptr;
    }

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    UCoopRPGCombatProbeComponent* GetLocalProbe(UWorld* World)
    {
        APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
        APawn* Pawn = PC && PC->IsLocalController() ? PC->GetPawn() : nullptr;
        return Pawn ? Pawn->FindComponentByClass<UCoopRPGCombatProbeComponent>() : nullptr;
    }

    // One key event, unlike Input.+key which forces input every frame even after focus loss.
    FAutoConsoleCommandWithWorldAndArgs MoveKeyCommand(TEXT("Coop.Test.MoveKey"),
        TEXT("Coop.Test.MoveKey down|up: one simulated W event for focus-loss tests. Requires -CoopCombatTest -CoopManualInput."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (!IsProbeEnabled(World) || !FParse::Param(FCommandLine::Get(), TEXT("CoopManualInput")) || Args.Num() != 1)
            {
                return;
            }
            APlayerController* PC = World->GetFirstPlayerController();
            if (PC && PC->IsLocalController() && (Args[0] == TEXT("down") || Args[0] == TEXT("up")))
            {
                const bool bDown = Args[0] == TEXT("down");
                PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, bDown ? IE_Pressed : IE_Released, bDown ? 1.0f : 0.0f));
                UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("ManualMoveKey Event=%s"), *Args[0]);
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs PrepareCommand(TEXT("Coop.Test.Prepare"),
        TEXT("Coop.Test.Prepare <stamina 0..100>: reset own cooldown/stamina and shared target HP to 100. Requires -CoopCombatTest."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World); Probe && Args.Num() == 1 && Args[0].IsNumeric())
            {
                Probe->Prepare(FCString::Atof(*Args[0]));
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs RequestCommand(TEXT("Coop.Test.Request"),
        TEXT("Send existing GAS activation RPC without client precheck; server checks stay intact. Requires -CoopCombatTest."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World))
            {
                Probe->RequestActivation();
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs VitalsCommand(TEXT("Coop.Test.Vitals"),
        TEXT("Coop.Test.Vitals <target health> <own health>: set test HP on the server."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World);
                Probe && Args.Num() == 2 && Args[0].IsNumeric() && Args[1].IsNumeric())
            {
                Probe->SetVitals(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs ConfirmTwiceCommand(TEXT("Coop.Test.ConfirmTwice"),
        TEXT("Deliver the same attack's hit confirmation twice while a test attack is pending."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World))
            {
                Probe->ConfirmTwice();
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs SnapshotCommand(TEXT("Coop.Test.Snapshot"),
        TEXT("Coop.Test.Snapshot <label>: log local and server HP/stamina/cooldown."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World))
            {
                Probe->Snapshot(Args.IsEmpty() ? TEXT("Manual") : Args[0].Left(48));
            }
        }));
#endif
}

UCoopRPGCombatProbeComponent::UCoopRPGCombatProbeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UCoopRPGCombatProbeComponent::Prepare(float Stamina)
{
    if (IsProbeEnabled(GetWorld()) && FMath::IsFinite(Stamina) && Stamina >= 0.0f && Stamina <= 100.0f)
    {
        ServerPrepare(Stamina);
    }
}

void UCoopRPGCombatProbeComponent::RequestActivation()
{
    UAbilitySystemComponent* Source = GetSource(this);
    if (!IsProbeEnabled(GetWorld()) || !Source || GetOwner()->HasAuthority())
    {
        return;
    }
    if (FGameplayAbilitySpec* Spec = Source->FindAbilitySpecFromClass(UCoopRPGMeleeAbility::StaticClass()))
    {
        UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestActivationRequest Actor=%s ClientPrecheck=Skipped ServerChecks=Enabled %s"),
            *GetNameSafe(GetOwner()), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        Source->CallServerTryActivateAbility(Spec->Handle, true, FPredictionKey());
    }
}

void UCoopRPGCombatProbeComponent::Snapshot(const FString& Label)
{
    if (IsProbeEnabled(GetWorld()))
    {
        WriteSnapshot(Label.Left(48));
        ServerSnapshot(Label.Left(48));
    }
}

void UCoopRPGCombatProbeComponent::SetVitals(float TargetHealth, float AttackerHealth)
{
    if (IsProbeEnabled(GetWorld()))
    {
        ServerSetVitals(TargetHealth, AttackerHealth);
    }
}

void UCoopRPGCombatProbeComponent::ConfirmTwice()
{
    if (IsProbeEnabled(GetWorld()))
    {
        ServerConfirmTwice();
    }
}

void UCoopRPGCombatProbeComponent::BeginPlay()
{
    Super::BeginPlay();
    if (UAbilitySystemComponent* Source = GetSource(this); Source && IsProbeEnabled(GetWorld()))
    {
        Source->RegisterGameplayTagEvent(UCoopRPGMeleeCooldown::GetCooldownTag(), EGameplayTagEventType::NewOrRemoved)
            .AddUObject(this, &UCoopRPGCombatProbeComponent::OnCooldownChanged);
    }
}

void UCoopRPGCombatProbeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UAbilitySystemComponent* Source = GetSource(this))
    {
        Source->RegisterGameplayTagEvent(UCoopRPGMeleeCooldown::GetCooldownTag(), EGameplayTagEventType::NewOrRemoved).RemoveAll(this);
    }
    Super::EndPlay(EndPlayReason);
}

void UCoopRPGCombatProbeComponent::ServerPrepare_Implementation(float Stamina)
{
    if (!IsProbeEnabled(GetWorld()) || !FMath::IsFinite(Stamina) || Stamina < 0.0f || Stamina > 100.0f)
    {
        UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestPrepareRejected Actor=%s Reason=DisabledOrInvalid"), *GetNameSafe(GetOwner()));
        return;
    }
    UAbilitySystemComponent* Source = GetSource(this);
    ACoopRPGAttackTarget* Target = GetTarget(GetWorld());
    if (Source && Target)
    {
        Source->RemoveActiveGameplayEffectBySourceEffect(UCoopRPGMeleeCooldown::StaticClass(), Source);
        Source->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetStaminaAttribute(), Stamina);
        Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetHealthAttribute(), 100.0f);
        GetOwner()->ForceNetUpdate();
        Target->ForceNetUpdate();
        WriteSnapshot(TEXT("Prepared"));
    }
}

void UCoopRPGCombatProbeComponent::ServerSnapshot_Implementation(const FString& Label)
{
    if (IsProbeEnabled(GetWorld()))
    {
        WriteSnapshot(Label.Left(48));
    }
}

void UCoopRPGCombatProbeComponent::ServerSetVitals_Implementation(float TargetHealth, float AttackerHealth)
{
    if (!IsProbeEnabled(GetWorld())
        || !FMath::IsFinite(TargetHealth) || !FMath::IsWithinInclusive(TargetHealth, 0.0f, 100.0f)
        || !FMath::IsFinite(AttackerHealth) || !FMath::IsWithinInclusive(AttackerHealth, 0.0f, 100.0f))
    {
        return;
    }
    UAbilitySystemComponent* Source = GetSource(this);
    ACoopRPGAttackTarget* Target = GetTarget(GetWorld());
    if (Source && Target)
    {
        Source->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetHealthAttribute(), AttackerHealth);
        Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetHealthAttribute(), TargetHealth);
        GetOwner()->ForceNetUpdate();
        Target->ForceNetUpdate();
        UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestVitals Actor=%s HP=%.2f AttackerHP=%.2f"),
            *GetNameSafe(GetOwner()), TargetHealth, AttackerHealth);
    }
}

void UCoopRPGCombatProbeComponent::ServerConfirmTwice_Implementation()
{
    UAbilitySystemComponent* Source = GetSource(this);
    if (!IsProbeEnabled(GetWorld()) || !Source)
    {
        return;
    }
    const FGameplayAbilitySpec* Spec = Source->FindAbilitySpecFromClass(UCoopRPGMeleeAbility::StaticClass());
    auto* Melee = Spec ? Cast<UCoopRPGMeleeAbility>(Spec->GetPrimaryInstance()) : nullptr;
    if (Melee && Melee->IsActive())
    {
        UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestDuplicateConfirmation Actor=%s Attempts=2"), *GetNameSafe(GetOwner()));
        Melee->ResolveHit();
        Melee->ResolveHit();
    }
}

void UCoopRPGCombatProbeComponent::OnCooldownChanged(FGameplayTag Tag, int32 Count)
{
    UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("CooldownChanged Actor=%s Count=%d Time=%.3f %s"),
        *GetNameSafe(GetOwner()), Count, GetWorld()->GetTimeSeconds(),
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void UCoopRPGCombatProbeComponent::WriteSnapshot(const FString& Label) const
{
    const UAbilitySystemComponent* Source = GetSource(this);
    const ACoopRPGAttackTarget* Target = GetTarget(GetWorld());
    if (!Source || !Target)
    {
        return;
    }
    const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
        FGameplayTagContainer(UCoopRPGMeleeCooldown::GetCooldownTag()));
    const TArray<float> Remaining = Source->GetActiveEffectsTimeRemaining(Query);
    UE_LOG(LogCoopRPGCombatProbe, Log,
        TEXT("CombatSnapshot Label=%s Actor=%s HP=%.2f Stamina=%.2f Cooldown=%d Remaining=%.3f Time=%.3f %s"),
        *Label, *GetNameSafe(GetOwner()), Target->GetHealth(),
        Source->GetNumericAttribute(UCoopRPGCombatAttributes::GetStaminaAttribute()),
        Source->HasMatchingGameplayTag(UCoopRPGMeleeCooldown::GetCooldownTag()) ? 1 : 0,
        Remaining.IsEmpty() ? -1.0f : Remaining[0], GetWorld()->GetTimeSeconds(),
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("MovementSnapshot Label=%s Actor=%s Position=%s Velocity=%s %s"),
        *Label, *GetNameSafe(GetOwner()), *GetOwner()->GetActorLocation().ToString(), *GetOwner()->GetVelocity().ToString(),
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}
