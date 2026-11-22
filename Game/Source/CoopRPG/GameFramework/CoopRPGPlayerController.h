#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CoopRPGGameplayFeedback.h"
#include "CoopRPGRecoveryInventory.h"
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

    bool IsRoundReady() const { return bRoundReady; }
    void ResetLobbyReady();
    void RestoreTravelInventory(const FCoopRPGRecoverySnapshot& Recovery, const FCoopRPGSwordSnapshot& Sword,
        const FCoopRPGStatProgress& Stats, bool bShieldEquipped, const FCoopRPGSlashProgress& Slash);
    void InitializeStageInventory(FGuid RoundId);
    ECoopRPGSwordOwnership GetSwordOwnership() const { return SwordOwnership; }
    FCoopRPGSwordSnapshot GetTravelSword(bool bRetry) const;
    void RequestSwordEquipped(bool bEquipped);
    bool CanChangeShield() const;
    bool GetTravelShieldEquipped(bool bRetry) const;
    void RequestShieldEquipped(bool bEquipped);
    FCoopRPGRecoverySnapshot GetTravelRecovery(bool bRetry) const;
    FIntPoint GetStageRecoverySupplies() const { return StageRecoverySupplies; }
    FIntPoint GetStageRecoveryUsesRemaining() const { return StageRecoveryUsesRemaining; }
    bool IsRecoveryRegistered(bool bStamina) const;
    int32 GetMartialPower() const { return StatProgress.MartialPower.Level; }
    int32 GetProtection() const { return StatProgress.Protection.Level; }
    int32 GetStatScrollCount(ECoopRPGScrollStat Stat) const;
    const FCoopRPGStatProgress& GetTravelStats(bool bRetry) const;
    void RequestRecoveryRegistration(bool bStamina, bool bRegistered);
    void TakeStatScroll(ECoopRPGScrollStat Stat);
    void TakeSlashCapsule();
    void RequestUseSlashCapsule();
    bool HasSlashCapsule() const { return SlashProgress.bHasCapsule; }
    bool HasSlashRegistered() const { return SlashProgress.bRegistered; }
    const FCoopRPGSlashProgress& GetTravelSlash(bool bRetry) const { return bRetry ? EntrySlash : SlashProgress; }
    void RequestUseStatScroll(ECoopRPGScrollStat Stat);
    ACoopRPGSharedChest* GetOpenedChest() const { return OpenedChest.Get(); }
    int32 GetRecoveryCount() const { return RecoveryCount; }
    int32 GetStaminaRecoveryCount() const { return StaminaRecoveryCount; }
    void TakeRecoveryItem();
    void TakeStaminaRecoveryItem();
    void TakeMeleeWeapon();

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
    void CloseOpenedChest(FGuid RoundId, const TCHAR* Reason);
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
    void ServerSetShieldEquipped(FGuid RoundId, bool bEquipped);

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

    FGuid StageInventoryRound;
    FCoopRPGRecoverySnapshot EntryRecovery;
    FCoopRPGSwordSnapshot EntrySword;
    bool bEntryShieldEquipped = false;

    UPROPERTY(Replicated)
    ECoopRPGSwordOwnership SwordOwnership = ECoopRPGSwordOwnership::None;

    UPROPERTY(Replicated)
    FCoopRPGStatProgress StatProgress;

    FCoopRPGStatProgress EntryStats;

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
