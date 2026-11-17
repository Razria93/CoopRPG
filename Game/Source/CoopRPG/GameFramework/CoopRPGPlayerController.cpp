#include "CoopRPGPlayerController.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGRecoveryEffect.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"
#include "CoopRPGGameMode.h"
#include "CoopRPGGameState.h"

#include "AbilitySystemComponent.h"
#include "Components/InputComponent.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGManualInput, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGChestRequest, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundReady, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEnemyLootRequest, Log, All);

void ACoopRPGPlayerController::ReceivedPlayer()
{
    Super::ReceivedPlayer();
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("CoopManualInput")))
    {
        bManualInput = true;
        bAwaitFocusClick = true;
        bShowMouseCursor = true;
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        SetInputMode(Mode);
        UE_LOG(LogCoopRPGManualInput, Log, TEXT("ManualInputEnabled Cursor=Visible Lock=None FocusClick=Consumed %s"),
            *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
#endif
}

bool ACoopRPGPlayerController::ShouldFlushKeysWhenViewportFocusChanges() const
{
    return bManualInput || Super::ShouldFlushKeysWhenViewportFocusChanges();
}

void ACoopRPGPlayerController::FlushPressedKeys()
{
    const bool bHeldW = bManualInput && IsInputKeyDown(EKeys::W);
    Super::FlushPressedKeys();
    if (bManualInput)
    {
        bAwaitFocusClick = true;
        bConsumeFocusClickRelease = false;
        // The engine calls this on viewport deactivation/focus loss, including Alt+Tab.
        UE_LOG(LogCoopRPGManualInput, Log, TEXT("ManualInputFlushed Pawn=%s HeldWBefore=%d W=%d %s"),
            *GetNameSafe(GetPawn()), bHeldW ? 1 : 0, IsInputKeyDown(EKeys::W) ? 1 : 0,
            *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

bool ACoopRPGPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (bManualInput && Params.Key == EKeys::LeftMouseButton)
    {
        if (bAwaitFocusClick && (Params.Event == IE_Pressed || Params.Event == IE_DoubleClick))
        {
            bAwaitFocusClick = false;
            bConsumeFocusClickRelease = true;
            UE_LOG(LogCoopRPGManualInput, Log, TEXT("ManualFocusClickConsumed Pawn=%s %s"),
                *GetNameSafe(GetPawn()), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
            return true;
        }
        if (bConsumeFocusClickRelease)
        {
            if (Params.Event == IE_Released)
            {
                bConsumeFocusClickRelease = false;
            }
            return true;
        }
    }
    return Super::InputKey(Params);
}

void ACoopRPGPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, OpenedChest, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, RecoveryCount, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, bRoundReady, COND_OwnerOnly);
}

void ACoopRPGPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ACoopRPGPlayerController::ReadyForRound);
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ACoopRPGPlayerController::OpenSharedChest);
    InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ACoopRPGPlayerController::CloseSharedChest);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACoopRPGPlayerController::TakeRecoveryItem);
    InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ACoopRPGPlayerController::TakeMeleeWeapon);
    InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ACoopRPGPlayerController::UseRecoveryItem);
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ACoopRPGPlayerController::TakeEnemyLoot);
}

void ACoopRPGPlayerController::ReadyForRound()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (State && State->GetRoundId().IsValid())
    {
        UE_LOG(LogCoopRPGRoundReady, Log, TEXT("LootReadyRequested Round=%s %s"),
            *State->GetRoundId().ToString(), *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        ServerReadyForRound(State->GetRoundId());
    }
}

void ACoopRPGPlayerController::OpenSharedChest()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (State && State->GetSharedChest())
    {
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestViewRequested Action=Open Chest=%s Round=%s %s"),
            *GetNameSafe(State->GetSharedChest()), *State->GetRoundId().ToString(),
            *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        ServerSetChestOpen(State->GetSharedChest(), State->GetRoundId(), true);
    }
}

void ACoopRPGPlayerController::CloseSharedChest()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (State && OpenedChest)
    {
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestViewRequested Action=Close Chest=%s Round=%s %s"),
            *GetNameSafe(OpenedChest), *State->GetRoundId().ToString(),
            *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        ServerSetChestOpen(OpenedChest, State->GetRoundId(), false);
    }
}

void ACoopRPGPlayerController::TakeRecoveryItem()
{
    RequestChestItem(ECoopRPGChestItem::Recovery);
}

void ACoopRPGPlayerController::TakeMeleeWeapon()
{
    RequestChestItem(ECoopRPGChestItem::MeleeWeapon);
}

void ACoopRPGPlayerController::UseRecoveryItem()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (State && State->GetRoundId().IsValid())
    {
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("RecoveryUseRequested Round=%s %s"),
            *State->GetRoundId().ToString(), *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        ServerUseRecovery(State->GetRoundId());
    }
}

void ACoopRPGPlayerController::RequestChestItem(ECoopRPGChestItem Item)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (State && OpenedChest)
    {
        ++NextPickupRequest;
        const TCHAR* Event = Item == ECoopRPGChestItem::MeleeWeapon ? TEXT("ChestWeaponRequested") : TEXT("ChestTakeRequested");
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("%s Chest=%s Round=%s Request=%u %s"),
            Event, *GetNameSafe(OpenedChest), *State->GetRoundId().ToString(), NextPickupRequest,
            *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        ServerTakeChestItem(OpenedChest, State->GetRoundId(), NextPickupRequest, Item);
    }
}

void ACoopRPGPlayerController::ServerReadyForRound_Implementation(FGuid RoundId)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (bRoundReady || !GetPawn() || !State || State->HasRoundResult() || !RoundId.IsValid() || State->GetRoundId() != RoundId)
    {
        return;
    }

    bRoundReady = true;
    ForceNetUpdate();
    UE_LOG(LogCoopRPGRoundReady, Log, TEXT("LootPlayerReady Round=%s %s"),
        *RoundId.ToString(), *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
    if (auto* Mode = GetWorld()->GetAuthGameMode<ACoopRPGGameMode>())
    {
        Mode->UpdateLootRoundPhase();
    }
}

void ACoopRPGPlayerController::ServerSetChestOpen_Implementation(ACoopRPGSharedChest* Chest, FGuid RoundId, bool bOpen)
{
    if (!bOpen)
    {
        const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
        if (!State || State->GetRoundId() != RoundId)
        {
            LogChestRejection(TEXT("Close"), TEXT("WrongRound"), Chest, RoundId);
            return;
        }
        if (!OpenedChest)
        {
            LogChestRejection(TEXT("Close"), TEXT("NotOpen"), Chest, RoundId);
            return;
        }
        if (OpenedChest != Chest)
        {
            LogChestRejection(TEXT("Close"), TEXT("InvalidChest"), Chest, RoundId);
            return;
        }

        OpenedChest = nullptr;
        ForceNetUpdate();
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestClosed Chest=%s Round=%s %s"), *GetNameSafe(Chest),
            *RoundId.ToString(), *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    if (const TCHAR* Failure = GetChestRejection(Chest, RoundId))
    {
        LogChestRejection(TEXT("Open"), Failure, Chest, RoundId);
        return;
    }
    if (OpenedChest != Chest)
    {
        OpenedChest = Chest;
        ForceNetUpdate();
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestOpened Chest=%s Round=%s Stock=%d %s"),
            *GetNameSafe(Chest), *RoundId.ToString(), Chest->GetRecoveryStock(),
            *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGPlayerController::ServerTakeChestItem_Implementation(ACoopRPGSharedChest* Chest, FGuid RoundId,
    uint32 Request, ECoopRPGChestItem Item)
{
    const bool bTakingWeapon = Item == ECoopRPGChestItem::MeleeWeapon;
    const TCHAR* Action = bTakingWeapon ? TEXT("TakeWeapon") : TEXT("Take");
    if (!bTakingWeapon && Item != ECoopRPGChestItem::Recovery)
    {
        LogChestRejection(Action, TEXT("InvalidItem"), Chest, RoundId, Request);
        return;
    }
    if (const TCHAR* Failure = GetChestRejection(Chest, RoundId))
    {
        LogChestRejection(Action, Failure, Chest, RoundId, Request);
        return;
    }
    if (OpenedChest != Chest)
    {
        LogChestRejection(Action, TEXT("NotOpen"), Chest, RoundId, Request);
        return;
    }

    if (LastPickupRound != RoundId)
    {
        LastPickupRound = RoundId;
        LastPickupRequest = 0;
    }
    if (Request <= LastPickupRequest)
    {
        LogChestRejection(Action, TEXT("DuplicateRequest"), Chest, RoundId, Request);
        return;
    }
    LastPickupRequest = Request;

    if (bTakingWeapon)
    {
        // GetChestRejection has already checked the possessed character and its health.
        auto* CurrentCharacter = CastChecked<ACoopRPGCharacter>(GetPawn());
        if (CurrentCharacter->HasMeleeWeapon())
        {
            LogChestRejection(Action, TEXT("AlreadyEquipped"), Chest, RoundId, Request);
            return;
        }
        const int32 StockBefore = Chest->GetWeaponStock();
        if (!Chest->TryTakeWeapon())
        {
            LogChestRejection(Action, TEXT("Depleted"), Chest, RoundId, Request);
            return;
        }

        CurrentCharacter->EquipMeleeWeapon();
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestWeaponGranted Chest=%s Round=%s Request=%u Stock=%d->%d Equipped=1 %s"),
            *GetNameSafe(Chest), *RoundId.ToString(), Request, StockBefore, Chest->GetWeaponStock(),
            *CoopRPG::Debug::DescribePlayerRequestContext(CurrentCharacter, ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    const int32 StockBefore = Chest->GetRecoveryStock();
    if (!Chest->TryTakeRecovery())
    {
        LogChestRejection(Action, TEXT("Depleted"), Chest, RoundId, Request);
        return;
    }

    // Both mutations run in this server RPC before replication or the next RPC.
    ++RecoveryCount;
    ForceNetUpdate();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestGranted Chest=%s Round=%s Request=%u Stock=%d->%d Recovery=%d %s"),
        *GetNameSafe(Chest), *RoundId.ToString(), Request, StockBefore, Chest->GetRecoveryStock(), RecoveryCount,
        *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::ServerUseRecovery_Implementation(FGuid RoundId)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId)
    {
        LogRecoveryRejection(TEXT("WrongRound"), RoundId);
        return;
    }
    if (!State->IsRoundPlaying())
    {
        LogRecoveryRejection(TEXT("NotPlaying"), RoundId);
        return;
    }

    auto* CurrentCharacter = Cast<ACoopRPGCharacter>(GetPawn());
    UAbilitySystemComponent* ASC = CurrentCharacter ? CurrentCharacter->GetAbilitySystemComponent() : nullptr;
    if (!ASC || CurrentCharacter->GetHealth() <= 0.0f)
    {
        LogRecoveryRejection(TEXT("NotAlive"), RoundId);
        return;
    }
    if (RecoveryCount <= 0)
    {
        LogRecoveryRejection(TEXT("NoRecovery"), RoundId);
        return;
    }
    const float HealthBefore = CurrentCharacter->GetHealth();
    if (HealthBefore >= UCoopRPGCombatAttributes::MaxHealth)
    {
        LogRecoveryRejection(TEXT("FullHealth"), RoundId);
        return;
    }

    const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UCoopRPGRecoveryEffect::StaticClass(), 1.0f,
        ASC->MakeEffectContext());
    if (!Spec.IsValid())
    {
        LogRecoveryRejection(TEXT("InvalidEffect"), RoundId);
        return;
    }
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    const float HealthAfter = CurrentCharacter->GetHealth();
    if (HealthAfter <= HealthBefore)
    {
        LogRecoveryRejection(TEXT("NoHealing"), RoundId);
        return;
    }

    const int32 CountBefore = RecoveryCount--;
    ForceNetUpdate();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("RecoveryUsed Round=%s HP=%.2f->%.2f Recovery=%d->%d %s"),
        *RoundId.ToString(), HealthBefore, HealthAfter, CountBefore, RecoveryCount,
        *CoopRPG::Debug::DescribePlayerRequestContext(CurrentCharacter, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::TakeEnemyLoot()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const ACoopRPGEnemyLoot* Drop = State ? State->GetEnemyLoot() : nullptr;
    if (IsValid(Drop) && State->GetRoundId().IsValid() && Drop->GetDropId().IsValid())
    {
        UE_LOG(LogCoopRPGEnemyLootRequest, Log, TEXT("EnemyLootTakeRequested Round=%s DropId=%s %s"),
            *State->GetRoundId().ToString(), *Drop->GetDropId().ToString(),
            *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        ServerTakeEnemyLoot(State->GetRoundId(), Drop->GetDropId());
    }
}

void ACoopRPGPlayerController::ServerTakeEnemyLoot_Implementation(FGuid RoundId, FGuid DropId)
{
    auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const int32 Before = State ? State->GetTeamLootCount() : 0;
    const FString Context = CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__));
    const TCHAR* Failure = GetEnemyLootRejection(RoundId, DropId);
    if (!Failure && State->TryGrantEnemyLoot(State->GetEnemyLoot()))
    {
        UE_LOG(LogCoopRPGEnemyLootRequest, Log, TEXT("EnemyLootGranted Round=%s DropId=%s TeamLoot=%d->%d %s"),
            *RoundId.ToString(), *DropId.ToString(), Before, State->GetTeamLootCount(), *Context);
        return;
    }

    UE_LOG(LogCoopRPGEnemyLootRequest, Log, TEXT("EnemyLootRejected Round=%s DropId=%s Reason=%s TeamLoot=%d %s"),
        *RoundId.ToString(), *DropId.ToString(), Failure ? Failure : TEXT("AlreadyClaimed"), Before, *Context);
}

const TCHAR* ACoopRPGPlayerController::GetEnemyLootRejection(FGuid RoundId, FGuid DropId) const
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId)
    {
        return TEXT("WrongRound");
    }
    if (!State->IsRoundPlaying())
    {
        return TEXT("NotPlaying");
    }

    const ACoopRPGEnemyLoot* Drop = State->GetEnemyLoot();
    if (!IsValid(Drop) || !DropId.IsValid() || Drop->GetDropId() != DropId)
    {
        return TEXT("InvalidDrop");
    }

    const auto* CurrentCharacter = Cast<ACoopRPGCharacter>(GetPawn());
    if (!CurrentCharacter || CurrentCharacter->GetHealth() <= 0.0f)
    {
        return TEXT("NotAlive");
    }
    if (FVector::DistSquared(CurrentCharacter->GetActorLocation(), Drop->GetActorLocation())
        > FMath::Square(ACoopRPGEnemyLoot::PickupRangeCm))
    {
        return TEXT("OutOfRange");
    }
    return Drop->IsClaimed() ? TEXT("AlreadyClaimed") : nullptr;
}

void ACoopRPGPlayerController::LogRecoveryRejection(const TCHAR* Reason, FGuid RoundId) const
{
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("RecoveryUseRejected Round=%s Reason=%s Recovery=%d %s"),
        *RoundId.ToString(), Reason, RecoveryCount,
        *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

const TCHAR* ACoopRPGPlayerController::GetChestRejection(const ACoopRPGSharedChest* Chest, FGuid RoundId) const
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId)
    {
        return TEXT("WrongRound");
    }
    if (!State->IsRoundPlaying())
    {
        return TEXT("NotPlaying");
    }
    if (!IsValid(Chest) || State->GetSharedChest() != Chest)
    {
        return TEXT("InvalidChest");
    }

    const APawn* CurrentPawn = GetPawn();
    const auto* AbilityPawn = Cast<ACoopRPGCharacter>(CurrentPawn);
    const UAbilitySystemComponent* ASC = AbilityPawn ? AbilityPawn->GetAbilitySystemComponent() : nullptr;
    if (!ASC || ASC->GetNumericAttribute(UCoopRPGCombatAttributes::GetHealthAttribute()) <= 0.f)
    {
        return TEXT("NotAlive");
    }
    if (FVector::DistSquared(CurrentPawn->GetActorLocation(), Chest->GetActorLocation()) > FMath::Square(220.f))
    {
        return TEXT("OutOfRange");
    }
    return nullptr;
}

void ACoopRPGPlayerController::LogChestRejection(const TCHAR* Action, const TCHAR* Reason,
    const ACoopRPGSharedChest* Chest, FGuid RoundId, uint32 Request) const
{
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestRejected Action=%s Reason=%s Chest=%s Round=%s Request=%u %s"),
        Action, Reason, *GetNameSafe(Chest), *RoundId.ToString(), Request,
        *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::OnRep_OpenedChest()
{
    const FString StockField = OpenedChest ? FString::Printf(TEXT(" Stock=%d"), OpenedChest->GetRecoveryStock()) : FString();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestViewReplicated Chest=%s Open=%d%s %s"),
        *GetNameSafe(OpenedChest), OpenedChest ? 1 : 0, *StockField,
        *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::OnRep_RecoveryCount()
{
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("RecoveryCountReplicated Count=%d %s"),
        RecoveryCount, *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}
