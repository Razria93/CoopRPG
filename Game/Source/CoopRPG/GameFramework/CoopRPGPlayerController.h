#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CoopRPGPlayerController.generated.h"

class ACoopRPGSharedChest;

UENUM()
enum class ECoopRPGChestItem : uint8
{
    Recovery,
    MeleeWeapon
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

    bool IsRoundReady() const { return bRoundReady; }
    ACoopRPGSharedChest* GetOpenedChest() const { return OpenedChest.Get(); }
    int32 GetRecoveryCount() const { return RecoveryCount; }

protected:
    virtual void SetupInputComponent() override;

private:
    void ReadyForRound();
    void OpenSharedChest();
    void CloseSharedChest();
    void TakeRecoveryItem();
    void TakeMeleeWeapon();
    void UseRecoveryItem();
    void RequestChestItem(ECoopRPGChestItem Item);

    UFUNCTION(Server, Reliable)
    void ServerReadyForRound(FGuid RoundId);

    UFUNCTION(Server, Reliable)
    void ServerSetChestOpen(ACoopRPGSharedChest* Chest, FGuid RoundId, bool bOpen);

    UFUNCTION(Server, Reliable)
    void ServerTakeChestItem(ACoopRPGSharedChest* Chest, FGuid RoundId, uint32 Request, ECoopRPGChestItem Item);

    UFUNCTION(Server, Reliable)
    void ServerUseRecovery(FGuid RoundId);

    void TakeEnemyLoot();

    UFUNCTION(Server, Reliable)
    void ServerTakeEnemyLoot(FGuid RoundId, FGuid DropId);

    const TCHAR* GetEnemyLootRejection(FGuid RoundId, FGuid DropId) const;

    void LogRecoveryRejection(const TCHAR* Reason, FGuid RoundId) const;

    const TCHAR* GetChestRejection(const ACoopRPGSharedChest* Chest, FGuid RoundId) const;
    void LogChestRejection(const TCHAR* Action, const TCHAR* Reason,
        const ACoopRPGSharedChest* Chest, FGuid RoundId, uint32 Request = 0) const;

    UFUNCTION()
    void OnRep_OpenedChest();

    UFUNCTION()
    void OnRep_RecoveryCount();

private:
    UPROPERTY(ReplicatedUsing = OnRep_OpenedChest)
    TObjectPtr<ACoopRPGSharedChest> OpenedChest;

    UPROPERTY(ReplicatedUsing = OnRep_RecoveryCount)
    int32 RecoveryCount = 0;

    UPROPERTY(Replicated)
    bool bRoundReady = false;

    uint32 NextPickupRequest = 0;
    FGuid LastPickupRound;
    uint32 LastPickupRequest = 0;

    // Local validation only; never replicated and disabled in Shipping/Test.
    bool bManualInput = false;
    bool bAwaitFocusClick = false;
    bool bConsumeFocusClickRelease = false;
};
