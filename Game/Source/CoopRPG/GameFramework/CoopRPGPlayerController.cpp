#include "CoopRPGPlayerController.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGRecoveryEffect.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPGGameMode.h"
#include "CoopRPGGameState.h"
#include "CoopRPGSessionEconomy.h"
#include "CoopRPGPlayerState.h"
#include "CoopRPGLootHUD.h"
#include "CoopRPGGameViewportClient.h"

#include "AbilitySystemComponent.h"
#include "Components/InputComponent.h"

#include "Engine/GameInstance.h"
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
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundResultDisplay, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGStorage, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGShop, Log, All);

namespace
{
    bool IsWithinChestRange(const APawn& Pawn, const ACoopRPGSharedChest& Chest)
    {
        return FVector::DistSquared(Pawn.GetActorLocation(), Chest.GetActorLocation()) <= FMath::Square(220.f);
    }
}

void ACoopRPGPlayerController::ReceivedPlayer()
{
    Super::ReceivedPlayer();
    if (IsLocalController() && GetNetMode() == NM_Client)
    {
        if (auto* Viewport = Cast<UCoopRPGGameViewportClient>(GetWorld()->GetGameViewport()))
        {
            Viewport->ClearConnectionNotice();
        }
    }
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
    if (auto* Hero = Cast<ACoopRPGCharacter>(GetPawn()))
    {
        Hero->ReleaseGuardInput();
    }
    if (auto* HUD = GetHUD<ACoopRPGLootHUD>())
    {
        HUD->ReleaseHudPointer();
    }
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
    const bool bPointerPressed = Params.Event == IE_Pressed || Params.Event == IE_DoubleClick;
    if (Params.Key == EKeys::LeftMouseButton)
    {
        if (auto* HUD = GetHUD<ACoopRPGLootHUD>(); HUD && HUD->HandleHudPrimaryButton(*this, Params))
        {
            if (Params.Event == IE_Pressed || Params.Event == IE_DoubleClick)
            {
                bAwaitFocusClick = false;
                bConsumeFocusClickRelease = false;
            }
            return true;
        }
    }
    if (bPointerPressed && (Params.Key == EKeys::LeftMouseButton || Params.Key == EKeys::RightMouseButton))
    {
        if (const auto* HUD = GetHUD<ACoopRPGLootHUD>(); HUD && HUD->IsPointerOverHud(*this))
        {
            return true;
        }
    }
    if (Params.Key == EKeys::MouseScrollUp || Params.Key == EKeys::MouseScrollDown || Params.Key == EKeys::MouseWheelAxis)
    {
        if (auto* HUD = GetHUD<ACoopRPGLootHUD>(); HUD && HUD->HandleHudWheel(*this, Params))
        {
            return true;
        }
    }
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
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, StaminaRecoveryCount, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, RecoveryRegistration, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, StageRecoveryUsesRemaining, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, PotionCapacityBonus, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, StageRecoverySupplies, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, PersonalStorage, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, SessionGold, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, SwordOwnership, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, bPersonalSwordEquipped, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, StatProgress, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, PuzzleReward, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, bExpeditionPowerReward, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, SlashProgress, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGPlayerController, bRoundReady, COND_OwnerOnly);
}

void ACoopRPGPlayerController::AcknowledgeRoundResult(FGuid RoundId)
{
    if (IsLocalController() && RoundId.IsValid() && LastDisplayedResultRound != RoundId)
    {
        LastDisplayedResultRound = RoundId;
        UE_LOG(LogCoopRPGRoundResultDisplay, Log, TEXT("RoundResultDisplayed Round=%s %s"),
            *RoundId.ToString(), *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        ServerAcknowledgeRoundResult(RoundId);
    }
}

bool ACoopRPGPlayerController::FinalizePuzzleReward(ECoopRPGPuzzleReward Reward)
{
    if (!HasAuthority() || PuzzleReward != ECoopRPGPuzzleReward::None)
    {
        return false;
    }
    PuzzleReward = Reward;
    bExpeditionPowerReward |= Reward == ECoopRPGPuzzleReward::ExpeditionPower;
    ForceNetUpdate();
    return true;
}

void ACoopRPGPlayerController::ServerSelectPuzzleReward_Implementation(FGuid RoundId, ECoopRPGPuzzleReward Reward)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<ACoopRPGGameMode>())
    {
        Mode->SelectPuzzleReward(*this, RoundId, Reward);
    }
}

void ACoopRPGPlayerController::ResetLobbyReady()
{
    if (HasAuthority() && bRoundReady)
    {
        bRoundReady = false;
        ForceNetUpdate();
    }
}

void ACoopRPGPlayerController::RestoreTravelInventory(const FCoopRPGRecoverySnapshot& Recovery, const FCoopRPGSwordSnapshot& Sword,
    const FCoopRPGStatProgress& Stats, const FCoopRPGSlashProgress& Slash, bool bPowerReward)
{
    if (HasAuthority())
    {
        SetPersonalRecoveryCount(false, Recovery.Personal.X);
        SetPersonalRecoveryCount(true, Recovery.Personal.Y);
        RecoveryRegistration = Recovery.Registration;
        RestoreSword(Sword);
        StatProgress = Stats;
        SlashProgress = Slash;
        bExpeditionPowerReward = bPowerReward;
    }
}

void ACoopRPGPlayerController::RestoreSword(const FCoopRPGSwordSnapshot& Sword)
{
    check(HasAuthority());
    SwordOwnership = Sword.Ownership;
    bPersonalSwordEquipped = Sword.bEquipped && SwordOwnership != ECoopRPGSwordOwnership::None;
}

void ACoopRPGPlayerController::ApplyExpeditionEquipment()
{
    auto* Hero = Cast<ACoopRPGCharacter>(GetPawn());
    const auto* Identity = GetPlayerState<ACoopRPGPlayerState>();
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!HasAuthority() || !Hero || !Identity || !State || !CoopRPG::Expedition::IsExpedition(GetWorld()))
    {
        return;
    }

    const ECoopRPGHero SelectedHero = Identity->GetSelectedHero();
    const bool bPersonalSword = IsPersonalSwordEquipped();
    const bool bDefaultSword = !bPersonalSword && SelectedHero != ECoopRPGHero::Unassigned;
    const bool bDefaultShield = SelectedHero == ECoopRPGHero::AnimalHero;
    Hero->SetMeleeWeaponEquipped(bPersonalSword || bDefaultSword);
    Hero->SetShieldEquipped(bDefaultShield);
    UE_LOG(LogCoopRPGChestRequest, Log,
        TEXT("DefaultEquipmentApplied Round=%s PersonalSword=%d DefaultSword=%d DefaultShield=%d OwnedSword=%d BoundSword=%d %s"),
        *State->GetRoundId().ToString(), bPersonalSword, bDefaultSword, bDefaultShield, SwordOwnership != ECoopRPGSwordOwnership::None, SwordOwnership == ECoopRPGSwordOwnership::Bound,
        *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::InitializeStageInventory(FGuid RoundId)
{
    auto* Hero = Cast<ACoopRPGCharacter>(GetPawn());
    if (!HasAuthority() || !Hero || !RoundId.IsValid() || StageInventoryRound == RoundId)
    {
        return;
    }

    StageInventoryRound = RoundId;
    EntryRecovery = GetTravelRecovery(false);
    EntrySword = GetTravelSword(false);
    EntryStats = StatProgress;
    EntrySlash = SlashProgress;
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("SlashStageEntered Round=%s Capsule=%d Registered=%d %s"),
        *RoundId.ToString(), HasSlashCapsule(), HasSlashRegistered(),
        *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    StageRecoverySupplies = FIntPoint(1, 1);
    RefreshPotionCapacityBonus();
    StageRecoveryUsesRemaining = FIntPoint(1, 1) + PotionCapacityBonus;
    RefreshPartyRecoveryUses();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("MartialPowerStageEntered Round=%s MartialPower=%d Scrolls=%d %s"),
        *RoundId.ToString(), GetMartialPower(), StatProgress.MartialPower.ScrollCount,
        *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ProtectionStageEntered Round=%s Protection=%d Scrolls=%d %s"),
        *RoundId.ToString(), GetProtection(), StatProgress.Protection.ScrollCount,
        *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("SwordStageEntered Round=%s Owned=%d Bound=%d Equipped=%d %s"),
        *RoundId.ToString(), SwordOwnership != ECoopRPGSwordOwnership::None, SwordOwnership == ECoopRPGSwordOwnership::Bound,
        IsPersonalSwordEquipped(), *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    UE_LOG(LogCoopRPGChestRequest, Log,
        TEXT("RecoveryStageEntered Round=%s PersonalHP=%d SupplyHP=%d SupplySP=%d PersonalSP=%d RegisteredHP=%d RegisteredSP=%d UsesHP=%d UsesSP=%d %s"),
        *RoundId.ToString(), RecoveryCount, StageRecoverySupplies.X, StageRecoverySupplies.Y, StaminaRecoveryCount,
        RecoveryRegistration.bHealth, RecoveryRegistration.bStamina, StageRecoveryUsesRemaining.X, StageRecoveryUsesRemaining.Y,
        *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::RefreshPotionCapacityBonus()
{
    check(HasAuthority());
    const auto* Mode = GetWorld()->GetAuthGameMode<ACoopRPGGameMode>();
    const auto* Identity = GetPlayerState<APlayerState>();
    PotionCapacityBonus = Mode && Identity ? Mode->GetPotionCapacityBonus(Identity->GetUniqueId()) : FIntPoint::ZeroValue;
    ForceNetUpdate();
}

void ACoopRPGPlayerController::LoadSessionGold(FGuid RoundId)
{
    const auto* Identity = GetPlayerState<APlayerState>();
    if (HasAuthority() && Identity && Identity->GetUniqueId().IsValid())
    {
        SessionGold = GetGameInstance()->GetSubsystem<UCoopRPGSessionEconomy>()->GetGold(Identity->GetUniqueId());
        ForceNetUpdate();
        UE_LOG(LogCoopRPGShop, Log, TEXT("SessionGoldLoaded Round=%s Gold=%d %s"), *RoundId.ToString(), SessionGold,
            *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGPlayerController::AwardBossGold(FGuid RoundId)
{
    const auto* Identity = GetPlayerState<APlayerState>();
    if (!HasAuthority() || !RoundId.IsValid() || !Identity || !Identity->GetUniqueId().IsValid())
    {
        return;
    }
    const FUniqueNetIdRepl& OwnerId = Identity->GetUniqueId();
    auto* Economy = GetGameInstance()->GetSubsystem<UCoopRPGSessionEconomy>();
    const int32 PreviousGold = Economy->GetGold(OwnerId);
    if (Economy->AwardBossClear(OwnerId, RoundId))
    {
        SessionGold = Economy->GetGold(OwnerId);
        ForceNetUpdate();
        UE_LOG(LogCoopRPGShop, Log, TEXT("BossGoldAwarded Round=%s Gold=%d->%d %s"),
            *RoundId.ToString(), PreviousGold, SessionGold,
            *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGPlayerController::RequestShopTrade(ECoopRPGStorageItem Item, bool bBuy)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (IsLocalController() && State && State->GetRoundId().IsValid() && CoopRPG::Expedition::IsLobby(GetWorld())
        && !State->IsLobbyDepartureScheduled() && !bShopTradePending)
    {
        bShopTradePending = true;
        ServerShopTrade(State->GetRoundId(), ++NextShopRequest, Item, bBuy);
    }
}

void ACoopRPGPlayerController::LoadPersonalStorage()
{
    RefreshPotionCapacityBonus();
    const auto* Identity = GetPlayerState<APlayerState>();
    if (!HasAuthority() || !Identity || !Identity->GetUniqueId().IsValid())
    {
        return;
    }
    const auto* Storage = GetGameInstance()->GetSubsystem<UCoopRPGLobbyStorage>();
    if (const auto* Stock = Storage->FindPersonal(Identity->GetUniqueId()))
    {
        PersonalStorage = *Stock;
        ForceNetUpdate();
    }
}

int32 ACoopRPGPlayerController::GetStorageBagCount(ECoopRPGStorageItem Item) const
{
    switch (Item)
    {
    case ECoopRPGStorageItem::HealthPotion: return RecoveryCount;
    case ECoopRPGStorageItem::StaminaPotion: return StaminaRecoveryCount;
    case ECoopRPGStorageItem::UnboundSword: return SwordOwnership == ECoopRPGSwordOwnership::Unbound ? 1 : 0;
    case ECoopRPGStorageItem::BoundSword: return SwordOwnership == ECoopRPGSwordOwnership::Bound ? 1 : 0;
    case ECoopRPGStorageItem::MartialScroll: return StatProgress.MartialPower.ScrollCount;
    case ECoopRPGStorageItem::ProtectionScroll: return StatProgress.Protection.ScrollCount;
    case ECoopRPGStorageItem::SlashCapsule: return SlashProgress.bHasCapsule ? 1 : 0;
    default: return 0;
    }
}

int32 ACoopRPGPlayerController::GetBagSlotsUsed() const
{
    int32 Used = 0;
    for (int32 Index = 0; Index < FCoopRPGStorageStock::ItemCount; ++Index)
    {
        const auto Item = static_cast<ECoopRPGStorageItem>(Index);
        Used += CoopRPG::Bag::SlotsUsed(Item, GetStorageBagCount(Item));
    }
    return Used;
}

int32 ACoopRPGPlayerController::GetBagRoom(ECoopRPGStorageItem Item) const
{
    return static_cast<int32>(Item) < FCoopRPGStorageStock::ItemCount
        ? CoopRPG::Bag::RoomFor(Item, GetStorageBagCount(Item), GetBagSlotsUsed()) : 0;
}

void ACoopRPGPlayerController::RequestStorageTransfer(ECoopRPGStorageItem Item, bool bPersonal, bool bDeposit)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (IsLocalController() && State && State->GetRoundId().IsValid() && CoopRPG::Expedition::IsLobby(GetWorld()))
    {
        ServerTransferStorage(State->GetRoundId(), Item, bPersonal, bDeposit);
    }
}

FCoopRPGRecoverySnapshot ACoopRPGPlayerController::GetTravelRecovery(bool bRetry) const
{
    return bRetry ? EntryRecovery : FCoopRPGRecoverySnapshot{FIntPoint(RecoveryCount, StaminaRecoveryCount), RecoveryRegistration};
}

bool ACoopRPGPlayerController::IsRecoveryRegistered(bool bStamina) const
{
    return bStamina ? RecoveryRegistration.bStamina : RecoveryRegistration.bHealth;
}

int32 ACoopRPGPlayerController::GetSelectedRecoveryStock(bool bStamina) const
{
    if (IsRecoveryRegistered(bStamina))
    {
        return bStamina ? StaminaRecoveryCount : RecoveryCount;
    }
    return bStamina ? StageRecoverySupplies.Y : StageRecoverySupplies.X;
}

FIntPoint ACoopRPGPlayerController::GetAvailableRecoveryUses() const
{
    return FIntPoint(FMath::Min(StageRecoveryUsesRemaining.X, GetSelectedRecoveryStock(false)),
        FMath::Min(StageRecoveryUsesRemaining.Y, GetSelectedRecoveryStock(true)));
}

int32 ACoopRPGPlayerController::GetStatScrollCount(ECoopRPGScrollStat Stat) const
{
    return Stat == ECoopRPGScrollStat::Protection ? StatProgress.Protection.ScrollCount : StatProgress.MartialPower.ScrollCount;
}

const FCoopRPGStatProgress& ACoopRPGPlayerController::GetTravelStats(bool bRetry) const
{
    return bRetry ? EntryStats : StatProgress;
}

FCoopRPGSwordSnapshot ACoopRPGPlayerController::GetTravelSword(bool bRetry) const
{
    if (!bRetry)
    {
        return {SwordOwnership, IsPersonalSwordEquipped()};
    }

    FCoopRPGSwordSnapshot Restored = EntrySword;
    // The same item stays bound even when retry restores its previous equip state.
    if (Restored.Ownership != ECoopRPGSwordOwnership::None && SwordOwnership == ECoopRPGSwordOwnership::Bound)
    {
        Restored.Ownership = SwordOwnership;
    }
    return Restored;
}

void ACoopRPGPlayerController::RequestSwordEquipped(bool bEquipped)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (IsLocalController() && State && State->GetRoundId().IsValid())
    {
        ServerSetSwordEquipped(State->GetRoundId(), bEquipped);
    }
}

bool ACoopRPGPlayerController::IsPersonalSwordEquipped() const
{
    return bPersonalSwordEquipped && SwordOwnership != ECoopRPGSwordOwnership::None;
}

void ACoopRPGPlayerController::TakeRecoveryItem()
{
    RequestChestItem(ECoopRPGChestItem::Recovery);
}

void ACoopRPGPlayerController::TakeStaminaRecoveryItem()
{
    RequestChestItem(ECoopRPGChestItem::StaminaRecovery);
}

void ACoopRPGPlayerController::TakeMeleeWeapon()
{
    RequestChestItem(ECoopRPGChestItem::MeleeWeapon);
}

void ACoopRPGPlayerController::CloseOpenedChest(FGuid RoundId, const TCHAR* Reason)
{
    const ACoopRPGSharedChest* Chest = OpenedChest.Get();
    OpenedChest = nullptr;
    ForceNetUpdate();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestClosed Chest=%s Round=%s Reason=%s %s"), *GetNameSafe(Chest),
        *RoundId.ToString(), Reason, *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::RequestRecoveryRegistration(bool bStamina, bool bRegistered)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (IsLocalController() && State && State->GetRoundId().IsValid())
    {
        ServerSetRecoveryRegistration(State->GetRoundId(), bStamina, bRegistered);
    }
}

void ACoopRPGPlayerController::TakeStatScroll(ECoopRPGScrollStat Stat)
{
    RequestChestItem(Stat == ECoopRPGScrollStat::Protection ? ECoopRPGChestItem::ProtectionScroll : ECoopRPGChestItem::MartialPowerScroll);
}

void ACoopRPGPlayerController::TakeSlashCapsule()
{
    RequestChestItem(ECoopRPGChestItem::SlashCapsule);
}

void ACoopRPGPlayerController::RequestUseSlashCapsule()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (IsLocalController() && State && State->GetRoundId().IsValid() && HasSlashCapsule())
    {
        ServerUseSlashCapsule(State->GetRoundId());
    }
}

void ACoopRPGPlayerController::RequestUseStatScroll(ECoopRPGScrollStat Stat)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const int32 Count = GetStatScrollCount(Stat);
    if (IsLocalController() && State && State->GetRoundId().IsValid() && Count > 0)
    {
        ServerUseStatScroll(State->GetRoundId(), Stat, Count);
    }
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
    InputComponent->BindKey(EKeys::J, IE_Pressed, this, &ACoopRPGPlayerController::UseStaminaRecoveryItem);
    InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &ACoopRPGPlayerController::TakeEnemyPotion);
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ACoopRPGPlayerController::TakeEnemyLoot);
}

void ACoopRPGPlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || !IsValid(OpenedChest))
    {
        return;
    }

    if (const APawn* CurrentPawn = GetPawn(); CurrentPawn && !IsWithinChestRange(*CurrentPawn, *OpenedChest))
    {
        if (const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>())
        {
            CloseOpenedChest(State->GetRoundId(), TEXT("OutOfRange"));
        }
    }
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

void ACoopRPGPlayerController::ServerAcknowledgeRoundResult_Implementation(FGuid RoundId)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<ACoopRPGGameMode>())
    {
        Mode->AcknowledgeRoundResult(this, RoundId);
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

void ACoopRPGPlayerController::UseRecoveryItem()
{
    RequestRecoveryUse(false);
}

void ACoopRPGPlayerController::UseStaminaRecoveryItem()
{
    if (CoopRPG::Expedition::IsExpedition(GetWorld()))
    {
        RequestRecoveryUse(true);
    }
}

void ACoopRPGPlayerController::RequestRecoveryUse(bool bStamina)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (State && State->GetRoundId().IsValid())
    {
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("RecoveryUseRequested Round=%s Resource=%s %s"),
            *State->GetRoundId().ToString(), bStamina ? TEXT("SP") : TEXT("HP"),
            *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        ServerUseRecovery(State->GetRoundId(), bStamina);
    }
}

void ACoopRPGPlayerController::RequestChestItem(ECoopRPGChestItem Item)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (State && OpenedChest)
    {
        ++NextPickupRequest;
        const TCHAR* Event = Item == ECoopRPGChestItem::MeleeWeapon ? TEXT("ChestWeaponRequested")
            : Item == ECoopRPGChestItem::StaminaRecovery ? TEXT("ChestStaminaRequested")
            : Item == ECoopRPGChestItem::MartialPowerScroll ? TEXT("ChestMartialPowerScrollRequested")
            : Item == ECoopRPGChestItem::ProtectionScroll ? TEXT("ChestProtectionScrollRequested")
            : Item == ECoopRPGChestItem::SlashCapsule ? TEXT("ChestSlashCapsuleRequested") : TEXT("ChestTakeRequested");
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

        CloseOpenedChest(RoundId, TEXT("Manual"));
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
        Chest->RecordFirstOpen();
        ForceNetUpdate();
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestOpened Chest=%s Round=%s Stock=%d WeaponStock=%d %s"),
            *GetNameSafe(Chest), *RoundId.ToString(), Chest->GetRecoveryStock(), Chest->GetWeaponStock(),
            *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGPlayerController::ServerTakeChestItem_Implementation(ACoopRPGSharedChest* Chest, FGuid RoundId,
    uint32 Request, ECoopRPGChestItem Item)
{
    const bool bTakingWeapon = Item == ECoopRPGChestItem::MeleeWeapon;
    const bool bTakingStamina = Item == ECoopRPGChestItem::StaminaRecovery;
    const bool bTakingCapsule = Item == ECoopRPGChestItem::SlashCapsule;
    const bool bTakingProtection = Item == ECoopRPGChestItem::ProtectionScroll;
    const bool bTakingScroll = Item == ECoopRPGChestItem::MartialPowerScroll || bTakingProtection;
    const TCHAR* Action = bTakingWeapon ? TEXT("TakeWeapon") : bTakingStamina ? TEXT("TakeStamina")
        : bTakingProtection ? TEXT("TakeProtectionScroll") : bTakingScroll ? TEXT("TakeMartialPowerScroll")
        : bTakingCapsule ? TEXT("TakeSlashCapsule") : TEXT("Take");
    if (!bTakingWeapon && Item != ECoopRPGChestItem::Recovery && !bTakingStamina && !bTakingScroll && !bTakingCapsule)
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

    const auto RejectFullBag = [&](ECoopRPGStorageItem BagItem)
    {
        if (GetBagRoom(BagItem) > 0)
        {
            return false;
        }
        LogChestRejection(Action, TEXT("BagFull"), Chest, RoundId, Request);
        ClientGameplayFeedback({ECoopRPGFeedbackKind::BagFull, GetPawn()});
        return true;
    };

    if (bTakingWeapon && CoopRPG::Expedition::IsExpedition(GetWorld()) && RejectFullBag(ECoopRPGStorageItem::UnboundSword))
    {
        return;
    }
    if (bTakingCapsule && RejectFullBag(ECoopRPGStorageItem::SlashCapsule))
    {
        return;
    }

    if (bTakingWeapon)
    {
        // GetChestRejection has already checked the possessed character and its health.
        auto* CurrentCharacter = CastChecked<ACoopRPGCharacter>(GetPawn());
        const bool bGoesIntoBag = CoopRPG::Expedition::IsExpedition(GetWorld());
        if (bGoesIntoBag ? SwordOwnership != ECoopRPGSwordOwnership::None : CurrentCharacter->HasMeleeWeapon())
        {
            LogChestRejection(Action, bGoesIntoBag ? TEXT("AlreadyOwned") : TEXT("AlreadyEquipped"), Chest, RoundId, Request);
            return;
        }
        if (bGoesIntoBag && RejectFullBag(ECoopRPGStorageItem::UnboundSword))
        {
            return;
        }
        const int32 StockBefore = Chest->GetWeaponStock();
        if (!Chest->TryTakeWeapon())
        {
            LogChestRejection(Action, TEXT("Depleted"), Chest, RoundId, Request);
            return;
        }

        if (bGoesIntoBag)
        {
            SwordOwnership = ECoopRPGSwordOwnership::Unbound;
        }
        else
        {
            CurrentCharacter->SetMeleeWeaponEquipped(true);
        }
        ClientGameplayFeedback({ECoopRPGFeedbackKind::WeaponAcquired, CurrentCharacter});
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestWeaponGranted Chest=%s Round=%s Request=%u Stock=%d->%d Equipped=%d Bag=%d Bound=%d %s"),
            *GetNameSafe(Chest), *RoundId.ToString(), Request, StockBefore, Chest->GetWeaponStock(),
            CurrentCharacter->HasMeleeWeapon(), bGoesIntoBag, SwordOwnership == ECoopRPGSwordOwnership::Bound,
            *CoopRPG::Debug::DescribePlayerRequestContext(CurrentCharacter, ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    if (bTakingCapsule)
    {
        if (StageInventoryRound != RoundId || !CoopRPG::Expedition::IsSkirmish(GetWorld()))
        {
            LogChestRejection(Action, TEXT("CapsuleUnavailable"), Chest, RoundId, Request);
            return;
        }
        if (HasSlashCapsule() || HasSlashRegistered())
        {
            LogChestRejection(Action, TEXT("AlreadyOwned"), Chest, RoundId, Request);
            return;
        }
        const int32 StockBefore = Chest->GetSlashCapsuleStock();
        if (!Chest->TryTakeSlashCapsule())
        {
            LogChestRejection(Action, TEXT("Depleted"), Chest, RoundId, Request);
            return;
        }
        SlashProgress.bHasCapsule = true;
        ForceNetUpdate();
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestSlashCapsuleGranted Round=%s Stock=%d->%d %s"),
            *RoundId.ToString(), StockBefore, Chest->GetSlashCapsuleStock(),
            *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    if (bTakingScroll)
    {
        if (StageInventoryRound != RoundId || !CoopRPG::Expedition::IsSkirmish(GetWorld()))
        {
            LogChestRejection(Action, TEXT("ScrollUnavailable"), Chest, RoundId, Request);
            return;
        }
        if (RejectFullBag(bTakingProtection ? ECoopRPGStorageItem::ProtectionScroll : ECoopRPGStorageItem::MartialScroll))
        {
            return;
        }
        const auto Stat = bTakingProtection ? ECoopRPGScrollStat::Protection : ECoopRPGScrollStat::MartialPower;
        const int32 StockBefore = Chest->GetStatScrollStock(Stat);
        if (!Chest->TryTakeStatScroll(Stat))
        {
            LogChestRejection(Action, TEXT("Depleted"), Chest, RoundId, Request);
            return;
        }

        auto& Progress = bTakingProtection ? StatProgress.Protection : StatProgress.MartialPower;
        ++Progress.ScrollCount;
        ForceNetUpdate();
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("%s Round=%s Request=%u Stock=%d->%d Scrolls=%d %s"),
            bTakingProtection ? TEXT("ChestProtectionScrollGranted") : TEXT("ChestMartialPowerScrollGranted"),
            *RoundId.ToString(), Request, StockBefore, Chest->GetStatScrollStock(Stat), Progress.ScrollCount,
            *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    if (bTakingStamina && StageInventoryRound != RoundId)
    {
        LogChestRejection(Action, TEXT("SupplyUnavailable"), Chest, RoundId, Request);
        return;
    }

    if (RejectFullBag(bTakingStamina ? ECoopRPGStorageItem::StaminaPotion : ECoopRPGStorageItem::HealthPotion))
    {
        return;
    }
    const int32 StockBefore = bTakingStamina ? Chest->GetStaminaRecoveryStock() : Chest->GetRecoveryStock();
    if (!Chest->TryTakeRecovery(bTakingStamina))
    {
        LogChestRejection(Action, TEXT("Depleted"), Chest, RoundId, Request);
        return;
    }

    // Both mutations run in this server RPC before replication or the next RPC.
    if (bTakingStamina)
    {
        SetPersonalRecoveryCount(true, StaminaRecoveryCount + 1);
        ClientGameplayFeedback({ECoopRPGFeedbackKind::StaminaRecoveryAcquired, GetPawn()});
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestStaminaGranted Chest=%s Round=%s Request=%u Stock=%d->%d PersonalSP=%d %s"),
            *GetNameSafe(Chest), *RoundId.ToString(), Request, StockBefore, Chest->GetStaminaRecoveryStock(), StaminaRecoveryCount,
            *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }
    SetPersonalRecoveryCount(false, RecoveryCount + 1);
    ClientGameplayFeedback({ECoopRPGFeedbackKind::RecoveryAcquired, GetPawn()});
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestGranted Chest=%s Round=%s Request=%u Stock=%d->%d Recovery=%d %s"),
        *GetNameSafe(Chest), *RoundId.ToString(), Request, StockBefore, Chest->GetRecoveryStock(), RecoveryCount,
        *CoopRPG::Debug::DescribePlayerRequestContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::ServerSetSwordEquipped_Implementation(FGuid RoundId, bool bEquipped)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    auto* Hero = Cast<ACoopRPGCharacter>(GetPawn());
    const bool bLobbyEquipment = State && CoopRPG::Expedition::IsLobby(GetWorld()) && !State->IsLobbyDepartureScheduled();
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId || (StageInventoryRound != RoundId && !bLobbyEquipment)
        || State->HasRoundResult() || !Hero || Hero->GetHealth() <= 0.0f
        || SwordOwnership == ECoopRPGSwordOwnership::None || IsPersonalSwordEquipped() == bEquipped)
    {
        return;
    }

    const bool bNewBinding = bEquipped && SwordOwnership == ECoopRPGSwordOwnership::Unbound;
    if (bNewBinding)
    {
        SwordOwnership = ECoopRPGSwordOwnership::Bound;
    }
    bPersonalSwordEquipped = bEquipped;
    ApplyExpeditionEquipment();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("SwordEquipmentChanged Round=%s Equipped=%d Bound=%d NewBinding=%d %s"),
        *RoundId.ToString(), bEquipped, SwordOwnership == ECoopRPGSwordOwnership::Bound, bNewBinding,
        *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::ServerTransferStorage_Implementation(FGuid RoundId, ECoopRPGStorageItem Item,
    bool bPersonal, bool bDeposit)
{
    auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const auto* Hero = Cast<ACoopRPGCharacter>(GetPawn());
    const auto* Identity = GetPlayerState<APlayerState>();
    const FString ItemName = UEnum::GetValueAsString(Item);
    const TCHAR* Area = bPersonal ? TEXT("Personal") : TEXT("Shared");
    const TCHAR* Action = bDeposit ? TEXT("Deposit") : TEXT("Withdraw");
    const auto Reject = [&](const TCHAR* Reason, const TCHAR* Message)
    {
        ClientStorageFeedback(Message);
        UE_LOG(LogCoopRPGStorage, Log, TEXT("StorageRejected Round=%s Area=%s Action=%s Item=%s Reason=%s %s"),
            *RoundId.ToString(), Area, Action, *ItemName, Reason,
            *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    };
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId || !CoopRPG::Expedition::IsLobby(GetWorld())
        || State->IsLobbyDepartureScheduled() || !Hero || !Identity || !Identity->GetUniqueId().IsValid())
    {
        Reject(TEXT("Unavailable"), TEXT("원정 대기소에서 출발하기 전에 사용할 수 있어요."));
        return;
    }
    if (static_cast<int32>(Item) >= FCoopRPGStorageStock::ItemCount)
    {
        Reject(TEXT("InvalidItem"), TEXT("보관할 수 없는 품목이에요."));
        return;
    }
    if (!bPersonal && Item == ECoopRPGStorageItem::BoundSword)
    {
        Reject(TEXT("BoundToOwner"), TEXT("귀속 장비는 개인 창고에만 보관할 수 있어요."));
        return;
    }

    auto* Storage = GetGameInstance()->GetSubsystem<UCoopRPGLobbyStorage>();
    const FUniqueNetIdRepl& OwnerId = Identity->GetUniqueId();
    const auto* Current = bPersonal ? Storage->FindPersonal(OwnerId) : &Storage->GetShared();
    const int32 Stock = Current ? Current->Get(Item) : 0;
    const int32 Bag = GetStorageBagCount(Item);
    const bool bSword = Item == ECoopRPGStorageItem::UnboundSword || Item == ECoopRPGStorageItem::BoundSword;
    if (bDeposit && bSword && IsPersonalSwordEquipped())
    {
        Reject(TEXT("Equipped"), TEXT("검을 장착 해제한 뒤 넣을 수 있어요."));
        return;
    }
    if (bDeposit && (Bag <= 0 || Stock >= MAX_int32))
    {
        Reject(TEXT("NoDeposit"), TEXT("가방 수량이나 창고 여유를 확인해 주세요."));
        return;
    }
    if (!bDeposit && Stock <= 0)
    {
        Reject(TEXT("Empty"), TEXT("창고에 남은 물품이 없어요."));
        return;
    }
    if (!bDeposit && (Bag >= MAX_int32 || (bSword && SwordOwnership != ECoopRPGSwordOwnership::None)
        || (Item == ECoopRPGStorageItem::SlashCapsule && (HasSlashCapsule() || HasSlashRegistered()))))
    {
        Reject(TEXT("BagLimit"), TEXT("가방에서 해당 품목을 받을 수 없어요."));
        return;
    }

    if (!bDeposit && GetBagRoom(Item) == 0)
    {
        Reject(TEXT("BagFull"), CoopRPG::Bag::FullMessage);
        return;
    }

    const int32 StorageDelta = bDeposit ? 1 : -1;
    FCoopRPGStorageStock& Updated = bPersonal ? Storage->EditPersonal(OwnerId) : Storage->EditShared();
    Updated.Adjust(Item, StorageDelta);
    AdjustStorageBag(Item, -StorageDelta);
    if (bPersonal)
    {
        PersonalStorage = Updated;
    }
    else
    {
        State->UpdateSharedStorage(Updated);
    }
    ForceNetUpdate();
    ClientStorageFeedback(bDeposit ? TEXT("한 개를 창고에 넣었어요.") : TEXT("한 개를 가방에 옮겼어요."));
    UE_LOG(LogCoopRPGStorage, Log, TEXT("StorageTransfer Round=%s Area=%s Action=%s Item=%s Stock=%d->%d Bag=%d->%d %s"),
        *RoundId.ToString(), Area, Action, *ItemName, Stock, Updated.Get(Item), Bag, GetStorageBagCount(Item),
        *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::ClientStorageFeedback_Implementation(const FString& Message)
{
    StorageFeedback = Message;
}

void ACoopRPGPlayerController::AdjustStorageBag(ECoopRPGStorageItem Item, int32 Delta)
{
    switch (Item)
    {
    case ECoopRPGStorageItem::HealthPotion: SetPersonalRecoveryCount(false, RecoveryCount + Delta); break;
    case ECoopRPGStorageItem::StaminaPotion: SetPersonalRecoveryCount(true, StaminaRecoveryCount + Delta); break;
    case ECoopRPGStorageItem::UnboundSword:
        SwordOwnership = Delta > 0 ? ECoopRPGSwordOwnership::Unbound : ECoopRPGSwordOwnership::None;
        break;
    case ECoopRPGStorageItem::BoundSword:
        SwordOwnership = Delta > 0 ? ECoopRPGSwordOwnership::Bound : ECoopRPGSwordOwnership::None;
        break;
    case ECoopRPGStorageItem::MartialScroll: StatProgress.MartialPower.ScrollCount += Delta; break;
    case ECoopRPGStorageItem::ProtectionScroll: StatProgress.Protection.ScrollCount += Delta; break;
    case ECoopRPGStorageItem::SlashCapsule: SlashProgress.bHasCapsule = Delta > 0; break;
    default: checkNoEntry(); break;
    }
}

void ACoopRPGPlayerController::ServerShopTrade_Implementation(FGuid RoundId, uint32 Request,
    ECoopRPGStorageItem Item, bool bBuy)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const auto* Hero = Cast<ACoopRPGCharacter>(GetPawn());
    const auto* Identity = GetPlayerState<APlayerState>();
    const FString ItemName = UEnum::GetValueAsString(Item);
    const TCHAR* Action = bBuy ? TEXT("Buy") : TEXT("Sell");
    const FString Context = CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__));
    const auto Reject = [&](const TCHAR* Reason, const TCHAR* Message)
    {
        ClientFinishShopTrade(Request, Message);
        UE_LOG(LogCoopRPGShop, Log, TEXT("ShopRejected Round=%s Request=%u Action=%s Item=%s Reason=%s Gold=%d Bag=%d SupplyHP=%d %s"),
            *RoundId.ToString(), Request, Action, *ItemName, Reason, SessionGold, GetStorageBagCount(Item),
            StageRecoverySupplies.X, *Context);
    };
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId || !CoopRPG::Expedition::IsLobby(GetWorld())
        || State->IsLobbyDepartureScheduled() || !Hero || !Identity || !Identity->GetUniqueId().IsValid())
    {
        Reject(TEXT("Unavailable"), TEXT("원정 대기소에서 출발하기 전에 거래할 수 있어요."));
        return;
    }
    if (Request == 0 || Request <= LastShopRequest)
    {
        Reject(TEXT("Duplicate"), TEXT("이미 처리한 거래 요청이에요."));
        return;
    }
    LastShopRequest = Request;
    const int32 Price = bBuy ? CoopRPG::Shop::BuyPrice(Item) : CoopRPG::Shop::SellPrice(Item);
    if (Price <= 0)
    {
        Reject(TEXT("NotForTrade"), TEXT("상점에서 거래할 수 없는 품목이에요."));
        return;
    }
    const int32 BeforeBag = GetStorageBagCount(Item);
    if (!bBuy && BeforeBag <= 0)
    {
        Reject(TEXT("NoPersonalItem"), TEXT("가방에 판매할 개인 물품이 없어요."));
        return;
    }
    if (bBuy && (BeforeBag == MAX_int32
        || (Item == ECoopRPGStorageItem::SlashCapsule && (HasSlashCapsule() || HasSlashRegistered()))))
    {
        Reject(TEXT("BagLimit"), TEXT("가방에 더 담을 수 없거나 이미 배운 스킬이에요."));
        return;
    }
    if (bBuy && GetBagRoom(Item) == 0)
    {
        Reject(TEXT("BagFull"), CoopRPG::Bag::FullMessage);
        return;
    }

    auto* Economy = GetGameInstance()->GetSubsystem<UCoopRPGSessionEconomy>();
    const FUniqueNetIdRepl& OwnerId = Identity->GetUniqueId();
    const int32 BeforeGold = Economy->GetGold(OwnerId);
    if (!Economy->TryChangeGold(OwnerId, bBuy ? -Price : Price))
    {
        Reject(bBuy ? TEXT("InsufficientGold") : TEXT("GoldLimit"),
            bBuy ? TEXT("골드가 부족해요.") : TEXT("골드를 더 보유할 수 없어요."));
        return;
    }
    SessionGold = Economy->GetGold(OwnerId);
    AdjustStorageBag(Item, bBuy ? 1 : -1);
    ForceNetUpdate();
    ClientFinishShopTrade(Request, bBuy ? TEXT("한 개를 구매했어요.") : TEXT("한 개를 판매했어요."));
    UE_LOG(LogCoopRPGShop, Log, TEXT("ShopTrade Round=%s Request=%u Action=%s Item=%s Price=%d Gold=%d->%d Bag=%d->%d %s"),
        *RoundId.ToString(), Request, Action, *ItemName, Price, BeforeGold, SessionGold, BeforeBag, GetStorageBagCount(Item), *Context);
}

void ACoopRPGPlayerController::ClientFinishShopTrade_Implementation(uint32 Request, const FString& Message)
{
    if (Request == NextShopRequest)
    {
        bShopTradePending = false;
        ShopFeedback = Message;
    }
}

void ACoopRPGPlayerController::ServerUseRecovery_Implementation(FGuid RoundId, bool bStamina)
{
    auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId)
    {
        LogRecoveryRejection(TEXT("WrongRound"), RoundId, bStamina);
        return;
    }
    if (!State->IsRoundPlaying())
    {
        LogRecoveryRejection(TEXT("NotPlaying"), RoundId, bStamina);
        return;
    }

    auto* CurrentCharacter = Cast<ACoopRPGCharacter>(GetPawn());
    UAbilitySystemComponent* ASC = CurrentCharacter ? CurrentCharacter->GetAbilitySystemComponent() : nullptr;
    if (!ASC || CurrentCharacter->GetHealth() <= 0.0f)
    {
        LogRecoveryRejection(TEXT("NotAlive"), RoundId, bStamina);
        return;
    }
    const bool bExpedition = CoopRPG::Expedition::IsExpedition(GetWorld());
    if ((bExpedition && StageInventoryRound != RoundId) || (bStamina && !bExpedition))
    {
        LogRecoveryRejection(TEXT("SupplyUnavailable"), RoundId, bStamina);
        return;
    }

    const bool bUsesStageSupply = bExpedition && !IsRecoveryRegistered(bStamina);
    int32& StageStock = bStamina ? StageRecoverySupplies.Y : StageRecoverySupplies.X;
    int32& UsesRemaining = bStamina ? StageRecoveryUsesRemaining.Y : StageRecoveryUsesRemaining.X;
    const int32 CountBefore = bExpedition ? GetSelectedRecoveryStock(bStamina) : RecoveryCount;
    const int32 UsesBefore = UsesRemaining;
    if (CountBefore <= 0)
    {
        LogRecoveryRejection(bUsesStageSupply ? TEXT("StageSupplyDepleted") : TEXT("NoRecovery"), RoundId, bStamina);
        return;
    }
    if (bExpedition && UsesRemaining <= 0)
    {
        LogRecoveryRejection(TEXT("StageLimitReached"), RoundId, bStamina);
        return;
    }

    const FGameplayAttribute Attribute = bStamina ? UCoopRPGCombatAttributes::GetStaminaAttribute()
        : UCoopRPGCombatAttributes::GetHealthAttribute();
    const float ValueBefore = ASC->GetNumericAttribute(Attribute);
    const float Maximum = bStamina ? UCoopRPGCombatAttributes::MaxStamina : UCoopRPGCombatAttributes::MaxHealth;
    if (ValueBefore >= Maximum)
    {
        LogRecoveryRejection(bStamina ? TEXT("FullStamina") : TEXT("FullHealth"), RoundId, bStamina);
        return;
    }

    UClass* EffectClass = bStamina ? UCoopRPGStaminaRecoveryEffect::StaticClass() : UCoopRPGRecoveryEffect::StaticClass();
    const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.0f, ASC->MakeEffectContext());
    if (!Spec.IsValid())
    {
        LogRecoveryRejection(TEXT("InvalidEffect"), RoundId, bStamina);
        return;
    }
    const float RecoveryAmount = bExpedition && !bUsesStageSupply ? CoopRPG::Recovery::PersonalAmount
        : CoopRPG::Recovery::SupplyAmount;
    CoopRPG::Recovery::SetAmount(*Spec.Data.Get(), RecoveryAmount);
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    const float ValueAfter = ASC->GetNumericAttribute(Attribute);
    if (ValueAfter <= ValueBefore)
    {
        LogRecoveryRejection(TEXT("NoHealing"), RoundId, bStamina);
        return;
    }

    if (bExpedition)
    {
        --UsesRemaining;
    }
    if (bUsesStageSupply)
    {
        --StageStock;
        RefreshPartyRecoveryUses();
        ForceNetUpdate();
    }
    else
    {
        SetPersonalRecoveryCount(bStamina, CountBefore - 1);
    }
    ClientGameplayFeedback({ECoopRPGFeedbackKind::RecoveryUsed, CurrentCharacter});
    State->MulticastGameplayFeedback({bStamina ? ECoopRPGFeedbackKind::StaminaRecovery : ECoopRPGFeedbackKind::Recovery,
        CurrentCharacter, ValueAfter - ValueBefore});
    UE_LOG(LogCoopRPGChestRequest, Log,
        TEXT("RecoveryUsed Round=%s %s=%.2f->%.2f Recovery=%d->%d Source=%s PersonalHP=%d SupplyHP=%d SupplySP=%d PersonalSP=%d Uses=%d->%d %s"),
        *RoundId.ToString(), bStamina ? TEXT("SP") : TEXT("HP"), ValueBefore, ValueAfter, CountBefore, CountBefore - 1,
        bUsesStageSupply ? TEXT("Stage") : TEXT("Personal"), RecoveryCount, StageRecoverySupplies.X, StageRecoverySupplies.Y,
        StaminaRecoveryCount, UsesBefore, UsesRemaining,
        *CoopRPG::Debug::DescribePlayerRequestContext(CurrentCharacter, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::ServerSetRecoveryRegistration_Implementation(FGuid RoundId, bool bStamina, bool bRegistered)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const auto* Hero = Cast<ACoopRPGCharacter>(GetPawn());
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId || StageInventoryRound != RoundId
        || State->HasRoundResult() || !Hero || Hero->GetHealth() <= 0.0f)
    {
        return;
    }

    bool& Registered = bStamina ? RecoveryRegistration.bStamina : RecoveryRegistration.bHealth;
    const int32 Personal = bStamina ? StaminaRecoveryCount : RecoveryCount;
    if (Registered == bRegistered || (bRegistered && Personal <= 0))
    {
        return;
    }

    Registered = bRegistered;
    RefreshPartyRecoveryUses();
    ForceNetUpdate();
    UE_LOG(LogCoopRPGChestRequest, Log,
        TEXT("RecoveryRegistrationChanged Round=%s Resource=%s Registered=%d Personal=%d Supply=%d Uses=%d %s"),
        *RoundId.ToString(), bStamina ? TEXT("SP") : TEXT("HP"), Registered, Personal,
        bStamina ? StageRecoverySupplies.Y : StageRecoverySupplies.X,
        bStamina ? StageRecoveryUsesRemaining.Y : StageRecoveryUsesRemaining.X,
        *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::ServerUseStatScroll_Implementation(
    FGuid RoundId, ECoopRPGScrollStat Stat, int32 ExpectedScrollCount)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const auto* Hero = Cast<ACoopRPGCharacter>(GetPawn());
    auto* Progress = StatProgress.Find(Stat);
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId || StageInventoryRound != RoundId
        || !State->IsRoundPlaying() || !Hero || Hero->GetHealth() <= 0.0f || !Progress || Progress->ScrollCount <= 0
        || Progress->ScrollCount != ExpectedScrollCount)
    {
        return;
    }

    const bool bProtection = Stat == ECoopRPGScrollStat::Protection;
    const int32 Before = Progress->Level;
    --Progress->ScrollCount;
    Progress->Level += FCoopRPGStatProgress::PointsPerScroll;
    ForceNetUpdate();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("%s Round=%s %s=%d->%d Scrolls=%d->%d %s"),
        bProtection ? TEXT("ProtectionScrollUsed") : TEXT("MartialPowerScrollUsed"), *RoundId.ToString(),
        bProtection ? TEXT("Protection") : TEXT("MartialPower"), Before, Progress->Level, ExpectedScrollCount, Progress->ScrollCount,
        *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::ServerUseSlashCapsule_Implementation(FGuid RoundId)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const auto* Hero = Cast<ACoopRPGCharacter>(GetPawn());
    if (!State || !RoundId.IsValid() || State->GetRoundId() != RoundId || StageInventoryRound != RoundId
        || !State->IsRoundPlaying() || !Hero || Hero->GetHealth() <= 0.0f)
    {
        return;
    }
    if (!HasSlashCapsule() || HasSlashRegistered())
    {
        UE_LOG(LogCoopRPGChestRequest, Log, TEXT("SlashCapsuleRejected Round=%s Capsule=%d Registered=%d %s"),
            *RoundId.ToString(), HasSlashCapsule(), HasSlashRegistered(),
            *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }
    SlashProgress.bHasCapsule = false;
    SlashProgress.bRegistered = true;
    ForceNetUpdate();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("SlashCapsuleUsed Round=%s Capsule=1->0 Registered=0->1 Slot=2 %s"),
        *RoundId.ToString(), *CoopRPG::Debug::DescribePlayerRequestContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::ClientGameplayFeedback_Implementation(const FCoopRPGGameplayFeedback& Feedback)
{
    if (auto* HUD = Cast<ACoopRPGLootHUD>(GetHUD()))
    {
        HUD->ShowGameplayFeedback(Feedback);
    }
}

void ACoopRPGPlayerController::TakeEnemyLoot()
{
    RequestEnemyDrop(false);
}

void ACoopRPGPlayerController::TakeEnemyPotion()
{
    RequestEnemyDrop(true);
}

void ACoopRPGPlayerController::RequestEnemyDrop(bool bPotion)
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const ACoopRPGEnemyLoot* Drop = State ? (bPotion ? State->GetEnemyPotion() : State->GetEnemyLoot()) : nullptr;
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
    auto* Potion = State ? State->GetEnemyPotion() : nullptr;
    const bool bPotion = IsValid(Potion) && Potion->GetDropId() == DropId;
    auto* Drop = bPotion ? Potion : (State ? State->GetEnemyLoot() : nullptr);
    const TCHAR* Failure = GetEnemyLootRejection(RoundId, DropId, Drop);
    if (!Failure && bPotion && GetBagRoom(ECoopRPGStorageItem::HealthPotion) == 0)
    {
        Failure = TEXT("BagFull");
        ClientGameplayFeedback({ECoopRPGFeedbackKind::BagFull, GetPawn()});
    }
    if (!Failure && bPotion && Drop->TryClaim())
    {
        const int32 Previous = RecoveryCount;
        SetPersonalRecoveryCount(false, Previous + 1);
        ClientGameplayFeedback({ECoopRPGFeedbackKind::RecoveryAcquired, GetPawn()});
        UE_LOG(LogCoopRPGEnemyLootRequest, Log, TEXT("EnemyPotionGranted Round=%s DropId=%s Recovery=%d->%d %s"),
            *RoundId.ToString(), *DropId.ToString(), Previous, RecoveryCount, *Context);
        return;
    }
    if (!Failure && !bPotion && State->TryGrantEnemyLoot(Drop))
    {
        State->MulticastGameplayFeedback({ECoopRPGFeedbackKind::TeamLootAcquired, GetPawn()});
        UE_LOG(LogCoopRPGEnemyLootRequest, Log, TEXT("EnemyLootGranted Round=%s DropId=%s TeamLoot=%d->%d %s"),
            *RoundId.ToString(), *DropId.ToString(), Before, State->GetTeamLootCount(), *Context);
        return;
    }

    UE_LOG(LogCoopRPGEnemyLootRequest, Log, TEXT("EnemyLootRejected Round=%s DropId=%s Reason=%s TeamLoot=%d %s"),
        *RoundId.ToString(), *DropId.ToString(), Failure ? Failure : TEXT("AlreadyClaimed"), Before, *Context);
}

const TCHAR* ACoopRPGPlayerController::GetEnemyLootRejection(FGuid RoundId, FGuid DropId, const ACoopRPGEnemyLoot* Drop) const
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

void ACoopRPGPlayerController::SetPersonalRecoveryCount(bool bStamina, int32 Count)
{
    check(HasAuthority());
    int32& Stock = bStamina ? StaminaRecoveryCount : RecoveryCount;
    Stock = Count;
    RefreshPartyRecoveryUses();
    ForceNetUpdate();
}

void ACoopRPGPlayerController::RefreshPartyRecoveryUses()
{
    check(HasAuthority());
    auto* Identity = GetPlayerState<ACoopRPGPlayerState>();
    if (!Identity)
    {
        return;
    }

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const bool bHasStageInventory = State && StageInventoryRound.IsValid() && StageInventoryRound == State->GetRoundId();
    const FIntPoint Uses = bHasStageInventory ? GetAvailableRecoveryUses() : FIntPoint(INDEX_NONE, INDEX_NONE);
    Identity->SetPartyRecoveryUses(Uses);
}

void ACoopRPGPlayerController::LogRecoveryRejection(const TCHAR* Reason, FGuid RoundId, bool bStamina) const
{
    UE_LOG(LogCoopRPGChestRequest, Log,
        TEXT("RecoveryUseRejected Round=%s Reason=%s Recovery=%d Resource=%s Registered=%d PersonalHP=%d PersonalSP=%d SupplyHP=%d SupplySP=%d UsesHP=%d UsesSP=%d %s"),
        *RoundId.ToString(), Reason, RecoveryCount, bStamina ? TEXT("SP") : TEXT("HP"), IsRecoveryRegistered(bStamina),
        RecoveryCount, StaminaRecoveryCount, StageRecoverySupplies.X, StageRecoverySupplies.Y,
        StageRecoveryUsesRemaining.X, StageRecoveryUsesRemaining.Y,
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
    if (!IsWithinChestRange(*CurrentPawn, *Chest))
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
    const FString StockField = OpenedChest
        ? FString::Printf(TEXT(" Stock=%d WeaponStock=%d"), OpenedChest->GetRecoveryStock(), OpenedChest->GetWeaponStock())
        : FString();
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("ChestViewReplicated Chest=%s Open=%d%s %s"),
        *GetNameSafe(OpenedChest), OpenedChest ? 1 : 0, *StockField,
        *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::OnRep_RecoveryCount()
{
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("RecoveryCountReplicated Count=%d %s"),
        RecoveryCount, *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::OnRep_StageRecoverySupplies()
{
    UE_LOG(LogCoopRPGChestRequest, Log, TEXT("StageRecoverySuppliesReplicated SupplyHP=%d SupplySP=%d %s"),
        StageRecoverySupplies.X, StageRecoverySupplies.Y,
        *CoopRPG::Debug::DescribeLogContext(GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGPlayerController::OnRep_PuzzleReward()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    UE_LOG(LogCoopRPGRoundResultDisplay, Log, TEXT("PuzzleRewardDisplayed Round=%s Reward=%s"),
        State ? *State->GetRoundId().ToString() : TEXT("None"), *UEnum::GetValueAsString(PuzzleReward));
}
