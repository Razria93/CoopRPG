#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CoopRPGGameplayFeedback.h"
#include "CoopRPGLobbyStorage.h"
#include "CoopRPGRecoveryInventory.h"
#include "CoopRPGPuzzleReward.h"
#include "CoopRPGStatProgress.h"
#include "CoopRPGSlashProgress.h"
#include "CoopRPGSwordInventory.h"
#include "CoopRPGPlayerController.generated.h"

class ACoopRPGEnemyLoot;

class ACoopRPGSharedChest;
class UCoopRPGCombatProbeComponent;

UENUM()
enum class ECoopRPGChestItem : uint8
{
    Recovery,
    MeleeWeapon,
    StaminaRecovery,
    MartialPowerScroll,
    ProtectionScroll,
    SlashCapsule
};

UCLASS()
class COOPRPG_API ACoopRPGPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    virtual void ReceivedPlayer() override;
    virtual bool ShouldFlushKeysWhenViewportFocusChanges() const override;
    virtual void FlushPressedKeys() override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void AcknowledgeRoundResult(FGuid RoundId);
    ECoopRPGPuzzleReward GetPuzzleReward() const { return PuzzleReward; }
    bool HasExpeditionPowerReward() const { return bExpeditionPowerReward; }
    bool FinalizePuzzleReward(ECoopRPGPuzzleReward Reward);

    UFUNCTION(Server, Reliable)
    void ServerSelectPuzzleReward(FGuid RoundId, ECoopRPGPuzzleReward Reward);

    bool IsRoundReady() const { return bRoundReady; }
    void ResetLobbyReady();
    void RestoreTravelInventory(const FCoopRPGRecoverySnapshot& Recovery, const FCoopRPGSwordSnapshot& Sword,
        const FCoopRPGStatProgress& Stats, const FCoopRPGSlashProgress& Slash, bool bPowerReward);
    void RestoreSword(const FCoopRPGSwordSnapshot& Sword);
    void ApplyExpeditionEquipment();
    void InitializeStageInventory(FGuid RoundId);
    void LoadSessionGold(FGuid RoundId);
    void AwardBossGold(FGuid RoundId);
    int32 GetSessionGold() const { return SessionGold; }
    void RequestShopTrade(ECoopRPGStorageItem Item, bool bBuy);
    const FString& GetShopFeedback() const { return ShopFeedback; }
    bool IsShopTradePending() const { return bShopTradePending; }
    void LoadPersonalStorage();
    const FCoopRPGStorageStock& GetPersonalStorage() const { return PersonalStorage; }
    const FString& GetStorageFeedback() const { return StorageFeedback; }
    void ClearStorageFeedback() { StorageFeedback.Reset(); }
    int32 GetStorageBagCount(ECoopRPGStorageItem Item) const;
    void RequestStorageTransfer(ECoopRPGStorageItem Item, bool bPersonal, bool bDeposit);
    FCoopRPGRecoverySnapshot GetTravelRecovery(bool bRetry) const;
    FIntPoint GetStageRecoverySupplies() const { return StageRecoverySupplies; }
    FIntPoint GetStageRecoveryUsesRemaining() const { return StageRecoveryUsesRemaining; }
    bool IsRecoveryRegistered(bool bStamina) const;
    int32 GetMartialPower() const { return StatProgress.MartialPower.Level + static_cast<int32>(bExpeditionPowerReward); }
    int32 GetProtection() const { return StatProgress.Protection.Level; }
    int32 GetStatScrollCount(ECoopRPGScrollStat Stat) const;
    const FCoopRPGStatProgress& GetTravelStats(bool bRetry) const;
    ECoopRPGSwordOwnership GetSwordOwnership() const { return SwordOwnership; }
    FCoopRPGSwordSnapshot GetTravelSword(bool bRetry) const;
    void RequestSwordEquipped(bool bEquipped);
    bool IsPersonalSwordEquipped() const;
    ACoopRPGSharedChest* GetOpenedChest() const { return OpenedChest.Get(); }
    int32 GetRecoveryCount() const { return RecoveryCount; }
    int32 GetStaminaRecoveryCount() const { return StaminaRecoveryCount; }
    void TakeRecoveryItem();
    void TakeStaminaRecoveryItem();
    void TakeMeleeWeapon();
    void CloseOpenedChest(FGuid RoundId, const TCHAR* Reason);
    void RequestRecoveryRegistration(bool bStamina, bool bRegistered);
    void TakeStatScroll(ECoopRPGScrollStat Stat);
    void TakeSlashCapsule();
    void RequestUseSlashCapsule();
    bool HasSlashCapsule() const { return SlashProgress.bHasCapsule; }
    bool HasSlashRegistered() const { return SlashProgress.bRegistered; }
    const FCoopRPGSlashProgress& GetTravelSlash(bool bRetry) const { return bRetry ? EntrySlash : SlashProgress; }
    void RequestUseStatScroll(ECoopRPGScrollStat Stat);

protected:
    virtual void SetupInputComponent() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    friend class UCoopRPGCombatProbeComponent;

    void ReadyForRound();

    UFUNCTION(Server, Reliable)
    void ServerAcknowledgeRoundResult(FGuid RoundId);
    void OpenSharedChest();
    void CloseSharedChest();
    void UseRecoveryItem();
    void UseStaminaRecoveryItem();
    void RequestRecoveryUse(bool bStamina);
    void RequestChestItem(ECoopRPGChestItem Item);

    UFUNCTION(Server, Reliable)
    void ServerReadyForRound(FGuid RoundId);

    UFUNCTION(Server, Reliable)
    void ServerSetChestOpen(ACoopRPGSharedChest* Chest, FGuid RoundId, bool bOpen);

    UFUNCTION(Server, Reliable)
    void ServerTakeChestItem(ACoopRPGSharedChest* Chest, FGuid RoundId, uint32 Request, ECoopRPGChestItem Item);

    UFUNCTION(Server, Reliable)
    void ServerSetSwordEquipped(FGuid RoundId, bool bEquipped);

    UFUNCTION(Server, Reliable)
    void ServerTransferStorage(FGuid RoundId, ECoopRPGStorageItem Item, bool bPersonal, bool bDeposit);

    UFUNCTION(Client, Reliable)
    void ClientStorageFeedback(const FString& Message);

    void AdjustStorageBag(ECoopRPGStorageItem Item, int32 Delta);

    UFUNCTION(Server, Reliable)
    void ServerShopTrade(FGuid RoundId, uint32 Request, ECoopRPGStorageItem Item, bool bBuy);

    UFUNCTION(Client, Reliable)
    void ClientFinishShopTrade(uint32 Request, const FString& Message);

    UFUNCTION(Server, Reliable)
    void ServerUseRecovery(FGuid RoundId, bool bStamina);

    UFUNCTION(Server, Reliable)
    void ServerSetRecoveryRegistration(FGuid RoundId, bool bStamina, bool bRegistered);

    UFUNCTION(Server, Reliable)
    void ServerUseStatScroll(FGuid RoundId, ECoopRPGScrollStat Stat, int32 ExpectedScrollCount);

    UFUNCTION(Server, Reliable)
    void ServerUseSlashCapsule(FGuid RoundId);

    UFUNCTION(Client, Unreliable)
    void ClientGameplayFeedback(const FCoopRPGGameplayFeedback& Feedback);

    void TakeEnemyLoot();
    void TakeEnemyPotion();
    void RequestEnemyDrop(bool bPotion);

    UFUNCTION(Server, Reliable)
    void ServerTakeEnemyLoot(FGuid RoundId, FGuid DropId);

    const TCHAR* GetEnemyLootRejection(FGuid RoundId, FGuid DropId, const ACoopRPGEnemyLoot* Drop) const;

    void SetRecoveryCount(int32 Count);

    void LogRecoveryRejection(const TCHAR* Reason, FGuid RoundId, bool bStamina) const;

    const TCHAR* GetChestRejection(const ACoopRPGSharedChest* Chest, FGuid RoundId) const;
    void LogChestRejection(const TCHAR* Action, const TCHAR* Reason,
        const ACoopRPGSharedChest* Chest, FGuid RoundId, uint32 Request = 0) const;

    UFUNCTION()
    void OnRep_OpenedChest();

    UFUNCTION()
    void OnRep_RecoveryCount();

    UFUNCTION()
    void OnRep_StageRecoverySupplies();

    UFUNCTION()
    void OnRep_PuzzleReward();

private:
    UPROPERTY(ReplicatedUsing = OnRep_OpenedChest)
    TObjectPtr<ACoopRPGSharedChest> OpenedChest;

    UPROPERTY(ReplicatedUsing = OnRep_RecoveryCount)
    int32 RecoveryCount = 0;

    UPROPERTY(Replicated)
    int32 StaminaRecoveryCount = 0;

    UPROPERTY(Replicated)
    FCoopRPGRecoveryRegistration RecoveryRegistration;

    UPROPERTY(Replicated)
    FIntPoint StageRecoveryUsesRemaining = FIntPoint::ZeroValue;

    // Personal potions travel separately from the HP/SP supplies for this stage.
    UPROPERTY(ReplicatedUsing = OnRep_StageRecoverySupplies)
    FIntPoint StageRecoverySupplies = FIntPoint::ZeroValue;

    UPROPERTY(Replicated)
    FCoopRPGStorageStock PersonalStorage;

    FString StorageFeedback;

    UPROPERTY(Replicated)
    int32 SessionGold = 0;

    uint32 NextShopRequest = 0;
    uint32 LastShopRequest = 0;
    bool bShopTradePending = false;
    FString ShopFeedback;
    FGuid StageInventoryRound;
    FCoopRPGRecoverySnapshot EntryRecovery;
    FCoopRPGSwordSnapshot EntrySword;

    UPROPERTY(Replicated)
    ECoopRPGSwordOwnership SwordOwnership = ECoopRPGSwordOwnership::None;

    UPROPERTY(Replicated)
    bool bPersonalSwordEquipped = false;

    UPROPERTY(Replicated)
    FCoopRPGStatProgress StatProgress;

    FCoopRPGStatProgress EntryStats;

    UPROPERTY(ReplicatedUsing = OnRep_PuzzleReward)
    ECoopRPGPuzzleReward PuzzleReward = ECoopRPGPuzzleReward::None;

    UPROPERTY(Replicated)
    bool bExpeditionPowerReward = false;

    UPROPERTY(Replicated)
    FCoopRPGSlashProgress SlashProgress;

    FCoopRPGSlashProgress EntrySlash;

    UPROPERTY(Replicated)
    bool bRoundReady = false;

    FGuid LastDisplayedResultRound;
    uint32 NextPickupRequest = 0;
    FGuid LastPickupRound;
    uint32 LastPickupRequest = 0;

    // Local validation only; never replicated and disabled in Shipping/Test.
    bool bManualInput = false;
    bool bAwaitFocusClick = false;
    bool bConsumeFocusClickRelease = false;
};
