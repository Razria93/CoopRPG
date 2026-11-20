#include "CoopRPGCombatProbeComponent.h"

#include "CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGExtractionZone.h"
#include "CoopRPG/GameFramework/CoopRPGGameMode.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"
#include "CoopRPG/GameFramework/CoopRPGPlayerController.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/BoxComponent.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
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

    FAutoConsoleCommandWithWorldAndArgs MeleeLayoutCommand(TEXT("Coop.Test.MeleeLayout"),
        TEXT("Set a controlled front, away, wall, far or multi-contact layout for the melee regression."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World); Probe && Args.Num() == 1)
            {
                Probe->PrepareMeleeLayout(FName(*Args[0]));
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs LootPositionCommand(TEXT("Coop.Test.LootPosition"),
        TEXT("Coop.Test.LootPosition near|far: place the owned Pawn for an enemy loot range test."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World);
                Probe && Args.Num() == 1 && (Args[0] == TEXT("near") || Args[0] == TEXT("far")))
            {
                Probe->PositionForEnemyLoot(Args[0] == TEXT("near"));
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs ExtractionPositionCommand(TEXT("Coop.Test.ExtractionPosition"),
        TEXT("Coop.Test.ExtractionPosition inside|outside: position the owned Pawn for the zone test."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World);
                Probe && Args.Num() == 1 && (Args[0] == TEXT("inside") || Args[0] == TEXT("outside")))
            {
                Probe->PositionForExtraction(Args[0] == TEXT("inside"));
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs RoundEndCommand(TEXT("Coop.Test.RoundEnd"),
        TEXT("Coop.Test.RoundEnd timeout|death-timeout|timeout-extraction|death-on-logout"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World); Probe && Args.Num() == 1)
            {
                Probe->PrepareRoundEnd(FName(*Args[0]));
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs RoundSnapshotCommand(TEXT("Coop.Test.RoundSnapshot"),
        TEXT("Coop.Test.RoundSnapshot <label>: compare local and server round state."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World); Probe && Args.Num() == 1)
            {
                Probe->SnapshotRound(FName(*Args[0].Left(32)));
            }
        }));

    FAutoConsoleCommandWithWorldAndArgs StalePickupCommand(TEXT("Coop.Test.StalePickup"),
        TEXT("Coop.Test.StalePickup <round guid>: send an old round ID through the normal pickup RPC."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            FGuid RoundId;
            if (UCoopRPGCombatProbeComponent* Probe = GetLocalProbe(World);
                Probe && Args.Num() == 1 && FGuid::Parse(Args[0], RoundId))
            {
                Probe->RequestStalePickup(RoundId);
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

void UCoopRPGCombatProbeComponent::PrepareMeleeLayout(FName Layout)
{
    if (IsProbeEnabled(GetWorld()))
    {
        ServerPrepareMeleeLayout(Layout);
    }
}

void UCoopRPGCombatProbeComponent::PositionForEnemyLoot(bool bNear)
{
    if (IsProbeEnabled(GetWorld()))
    {
        ServerPositionForEnemyLoot(bNear);
    }
}

void UCoopRPGCombatProbeComponent::PositionForExtraction(bool bInside)
{
    if (IsProbeEnabled(GetWorld()))
    {
        ServerPositionForExtraction(bInside);
    }
}

void UCoopRPGCombatProbeComponent::PrepareRoundEnd(FName Scenario)
{
    if (IsProbeEnabled(GetWorld()))
    {
        ServerPrepareRoundEnd(Scenario);
    }
}

void UCoopRPGCombatProbeComponent::SnapshotRound(FName Label)
{
    if (IsProbeEnabled(GetWorld()))
    {
        WriteRoundSnapshot(Label);
        ServerSnapshotRound(Label);
    }
}

void UCoopRPGCombatProbeComponent::RequestStalePickup(FGuid RoundId)
{
    if (!IsProbeEnabled(GetWorld()))
    {
        return;
    }

    const auto* Pawn = Cast<ACoopRPGCharacter>(GetOwner());
    auto* Controller = Pawn ? Cast<ACoopRPGPlayerController>(Pawn->GetController()) : nullptr;
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (Controller && Controller->IsLocalController() && State && State->GetSharedChest()
        && RoundId.IsValid() && State->GetRoundId().IsValid() && State->GetRoundId() != RoundId)
    {
        UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestStalePickup OldRound=%s Round=%s"),
            *RoundId.ToString(), *State->GetRoundId().ToString());
        Controller->ServerTakeChestItem(State->GetSharedChest(), RoundId, 1, ECoopRPGChestItem::Recovery);
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
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    FGameModeEvents::OnGameModeLogoutEvent().RemoveAll(this);
#endif
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
    const auto* Mode = GetWorld()->GetAuthGameMode<ACoopRPGGameMode>();
    AActor* Target = Mode ? Mode->GetMeleeTarget() : GetTarget(GetWorld());
    UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target, false);
    if (Source && TargetASC)
    {
        Source->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetHealthAttribute(), AttackerHealth);
        TargetASC->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetHealthAttribute(), TargetHealth);
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

void UCoopRPGCombatProbeComponent::ServerPrepareMeleeLayout_Implementation(FName Layout)
{
    if (!IsProbeEnabled(GetWorld())
        || (Layout != TEXT("front") && Layout != TEXT("away") && Layout != TEXT("wall")
            && Layout != TEXT("far") && Layout != TEXT("multi")))
    {
        return;
    }

    const auto* Mode = GetWorld()->GetAuthGameMode<ACoopRPGGameMode>();
    auto* Character = Cast<ACoopRPGCharacter>(GetOwner());
    auto* Enemy = Mode ? Cast<ACoopRPGEnemy>(Mode->GetMeleeTarget()) : nullptr;
    if (!Character || !Enemy)
    {
        return;
    }

    if (AActor* Previous = MeleeTestObstacle.Get())
    {
        Previous->Destroy();
    }
    Enemy->SetActorTickEnabled(false);
    Enemy->GetCharacterMovement()->StopMovementImmediately();
    Character->GetCharacterMovement()->StopMovementImmediately();
    const FVector EnemyPosition(850.0, 0.0, 90.15);
    const FVector PlayerPosition(Layout == TEXT("far") ? 565.0 : 720.0,
        Character->GetDisplaySlot() == 1 ? -50.0 : 50.0, 90.15);
    FRotator Facing = (EnemyPosition - PlayerPosition).Rotation();
    Facing.Yaw += Layout == TEXT("away") ? 180.0f : 0.0f;
    Enemy->TeleportTo(EnemyPosition, FRotator::ZeroRotator, false, true);
    Character->TeleportTo(PlayerPosition, Facing, false, true);

    if (Layout == TEXT("multi") && !FindObject<UBoxComponent>(Enemy, TEXT("MeleeProbeContact")))
    {
        auto* ExtraContact = NewObject<UBoxComponent>(Enemy, TEXT("MeleeProbeContact"));
        Enemy->AddInstanceComponent(ExtraContact);
        ExtraContact->SetupAttachment(Enemy->GetRootComponent());
        ExtraContact->SetBoxExtent(FVector(26.0, 26.0, 50.0));
        ExtraContact->SetCollisionObjectType(ECC_Pawn);
        ExtraContact->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        ExtraContact->SetCollisionResponseToAllChannels(ECR_Overlap);
        ExtraContact->RegisterComponent();
    }
    if (Layout == TEXT("wall"))
    {
        AActor* Obstacle = GetWorld()->SpawnActor<AActor>();
        auto* Wall = NewObject<UBoxComponent>(Obstacle);
        Obstacle->AddInstanceComponent(Wall);
        Obstacle->SetRootComponent(Wall);
        Wall->SetBoxExtent(FVector(4.0, 90.0, 95.0));
        Wall->SetCollisionObjectType(ECC_WorldStatic);
        Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Wall->SetCollisionResponseToAllChannels(ECR_Block);
        Wall->RegisterComponent();
        Obstacle->SetActorLocation(FVector(785.0, 0.0, 90.15));
        MeleeTestObstacle = Obstacle;
    }

    Character->ForceNetUpdate();
    Enemy->ForceNetUpdate();
    UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("MeleeTestLayout Layout=%s Player=%d Yaw=%.1f"),
        *Layout.ToString(), Character->GetDisplaySlot(), Facing.Yaw);
}

void UCoopRPGCombatProbeComponent::ServerPositionForEnemyLoot_Implementation(bool bNear)
{
    if (!IsProbeEnabled(GetWorld()))
    {
        return;
    }

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const ACoopRPGEnemyLoot* Loot = State ? State->GetEnemyLoot() : nullptr;
    auto* Character = Cast<ACoopRPGCharacter>(GetOwner());
    const APlayerState* PlayerState = Character ? Character->GetPlayerState() : nullptr;
    if (!IsValid(Loot) || !PlayerState || Character->GetHealth() <= 0.0f)
    {
        return;
    }

    const float Side = PlayerState->GetPlayerId() % 2 == 0 ? -1.0f : 1.0f;
    const FVector Origin = Loot->GetActorLocation();
    const FVector Destination(Origin.X, Origin.Y + Side * (bNear ? 120.0f : 450.0f), Character->GetActorLocation().Z);
    if (Character->TeleportTo(Destination, Character->GetActorRotation()))
    {
        UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestEnemyLootPosition Position=%s DropId=%s Distance=%.2f %s"),
            bNear ? TEXT("Near") : TEXT("Far"), *Loot->GetDropId().ToString(),
            FVector::Dist(Character->GetActorLocation(), Origin),
            *CoopRPG::Debug::DescribePlayerRequestContext(Character, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void UCoopRPGCombatProbeComponent::ServerPositionForExtraction_Implementation(bool bInside)
{
    if (!IsProbeEnabled(GetWorld()))
    {
        return;
    }

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const ACoopRPGExtractionZone* Zone = State ? State->GetExtractionZone() : nullptr;
    auto* Character = Cast<ACoopRPGCharacter>(GetOwner());
    const APlayerState* PlayerState = Character ? Character->GetPlayerState() : nullptr;
    if (!IsValid(Zone) || !PlayerState || Character->GetHealth() <= 0.0f)
    {
        return;
    }

    const float Side = PlayerState->GetPlayerId() % 2 == 0 ? -1.0f : 1.0f;
    const FVector Origin = Zone->GetActorLocation();
    const FVector Destination(Origin.X + (bInside ? 60.0f : 245.0f), Origin.Y + Side * 55.0f,
        Character->GetActorLocation().Z);
    if (Character->TeleportTo(Destination, Character->GetActorRotation()))
    {
        UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestExtractionPosition Inside=%d %s"),
            Zone->ContainsPawn(*Character) ? 1 : 0,
            *CoopRPG::Debug::DescribePlayerRequestContext(Character, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void UCoopRPGCombatProbeComponent::ServerPrepareRoundEnd_Implementation(FName Scenario)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (!IsProbeEnabled(GetWorld()))
    {
        return;
    }
    auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    UAbilitySystemComponent* Source = GetSource(this);
    if (!State || !State->IsRoundPlaying() || !IsValid(State->GetExtractionZone()) || !Source)
    {
        return;
    }

    if (Scenario == TEXT("timeout"))
    {
        const double Now = State->GetServerWorldTimeSeconds();
        if (State->RoundStatus.RoundDeadline - Now <= 0.6)
        {
            return;
        }
        State->RoundStatus.RoundDeadline = Now + 0.6;
        State->ForceNetUpdate();
    }
    else if (Scenario == TEXT("death-timeout"))
    {
        State->RoundStatus.RoundDeadline = State->GetServerWorldTimeSeconds();
        Source->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetHealthAttribute(), 0.0f);
    }
    else if (Scenario == TEXT("timeout-extraction") && State->IsExtractionInProgress())
    {
        State->RoundStatus.RoundDeadline = State->RoundStatus.ExtractionDeadline;
        State->ForceNetUpdate();
    }
    else if (Scenario == TEXT("death-on-logout"))
    {
        auto& Logout = FGameModeEvents::OnGameModeLogoutEvent();
        Logout.RemoveAll(this);
        Logout.AddUObject(this, &UCoopRPGCombatProbeComponent::OnPeerLogout);
    }
    else
    {
        return;
    }

    UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestRoundEndPrepared Scenario=%s Round=%s Frame=%llu Deadline=%.3f"),
        *Scenario.ToString().ToLower(), *State->GetRoundId().ToString(), static_cast<unsigned long long>(GFrameCounter),
        State->RoundStatus.RoundDeadline);
#endif
}

void UCoopRPGCombatProbeComponent::ServerSnapshotRound_Implementation(FName Label)
{
    if (IsProbeEnabled(GetWorld()))
    {
        WriteRoundSnapshot(Label);
    }
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void UCoopRPGCombatProbeComponent::OnPeerLogout(AGameModeBase* Mode, AController* Exiting)
{
    const auto* Character = Cast<ACoopRPGCharacter>(GetOwner());
    const auto* State = Mode && Mode->GetWorld() == GetWorld() ? Mode->GetGameState<ACoopRPGGameState>() : nullptr;
    if (Character && State && State->IsRoundPlaying() && Exiting != Character->GetController())
    {
        FGameModeEvents::OnGameModeLogoutEvent().RemoveAll(this);
        if (UAbilitySystemComponent* Source = GetSource(this))
        {
            Source->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetHealthAttribute(), 0.0f);
            UE_LOG(LogCoopRPGCombatProbe, Log, TEXT("TestPeerLogoutDeath Round=%s Frame=%llu"),
                *State->GetRoundId().ToString(), static_cast<unsigned long long>(GFrameCounter));
        }
    }
}
#endif

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

void UCoopRPGCombatProbeComponent::WriteRoundSnapshot(FName Label) const
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const auto* Pawn = Cast<ACoopRPGCharacter>(GetOwner());
    const auto* Controller = Pawn ? Cast<ACoopRPGPlayerController>(Pawn->GetController()) : nullptr;
    const UAbilitySystemComponent* ASC = GetSource(this);
    if (!State || !Controller || !ASC || !State->GetSharedChest() || !State->GetRoundId().IsValid())
    {
        return;
    }

    float EnemyHP = -1.0f;
    for (TActorIterator<ACoopRPGEnemy> It(GetWorld()); It; ++It)
    {
        EnemyHP = It->GetAbilitySystemComponent()->GetNumericAttribute(UCoopRPGCombatAttributes::GetHealthAttribute());
        break;
    }
    const FVector Location = Pawn->GetActorLocation();
    UE_LOG(LogCoopRPGCombatProbe, Log,
        TEXT("TestRoundSnapshot Label=%s Map=%s Round=%s Actor=%s Phase=%s Ready=%d Open=%d HP=%.2f Stamina=%.2f Cooldown=%d Equipped=%d Recovery=%d ChestRecovery=%d ChestWeapons=%d EnemyHP=%.2f TeamLoot=%d Score=%d Clock=%d Extraction=%d X=%.2f Y=%.2f %s"),
        *Label.ToString(), *UGameplayStatics::GetCurrentLevelName(this), *State->GetRoundId().ToString(),
        *GetNameSafe(Pawn), State->IsRoundPlaying() ? TEXT("Playing") : State->HasRoundResult() ? TEXT("Result") : TEXT("Waiting"),
        Controller->IsRoundReady(), Controller->GetOpenedChest() != nullptr, Pawn->GetHealth(),
        ASC->GetNumericAttribute(UCoopRPGCombatAttributes::GetStaminaAttribute()),
        ASC->GetTagCount(UCoopRPGMeleeCooldown::GetCooldownTag()), Pawn->HasMeleeWeapon(), Controller->GetRecoveryCount(),
        State->GetSharedChest()->GetRecoveryStock(), State->GetSharedChest()->GetWeaponStock(), EnemyHP,
        State->GetTeamLootCount(), State->GetTeamScore(), State->GetRoundRemainingSeconds() > 0.0,
        State->IsExtractionInProgress(), Location.X, Location.Y,
        *CoopRPG::Debug::DescribePlayerRequestContext(Pawn, ANSI_TO_TCHAR(__FUNCTION__)));
}
