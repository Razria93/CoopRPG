#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoopRPGSharedChest.generated.h"

enum class ECoopRPGScrollStat : uint8;

class USkeletalMeshComponent;

UCLASS()
class COOPRPG_API ACoopRPGSharedChest : public AActor
{
    GENERATED_BODY()

public:
    ACoopRPGSharedChest();

    virtual void BeginPlay() override;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    int32 GetRecoveryStock() const { return RecoveryStock; }
    int32 GetWeaponStock() const { return WeaponStock; }
    int32 GetStaminaRecoveryStock() const { return StaminaRecoveryStock; }
    bool TryTakeRecovery(bool bStamina);
    bool TryTakeWeapon();
    int32 GetStatScrollStock(ECoopRPGScrollStat Stat) const;
    bool TryTakeStatScroll(ECoopRPGScrollStat Stat);
    int32 GetSlashCapsuleStock() const { return SlashCapsuleStock; }
    bool TryTakeSlashCapsule();
    int32 GetTauntCapsuleStock() const { return TauntCapsuleStock; }
    bool TryTakeTauntCapsule();
    void RecordFirstOpen();
    int32 GetPersonalShieldStock() const { return PersonalShieldStock; }
    bool TryTakePersonalShield();
    void SetPersonalShieldStock(int32 Stock);
    void SetBossRewardStock();

private:
    bool TryTakeStock(int32& Stock);
    void InitializeForestPresentation();

    UFUNCTION()
    void OnRep_FirstOpenedAt();

    UFUNCTION()
    void OnRep_RecoveryStock();

    UFUNCTION()
    void OnRep_WeaponStock();

private:
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> ChestBody;

    UPROPERTY(ReplicatedUsing = OnRep_FirstOpenedAt)
    double FirstOpenedAt = 0.0;

    UPROPERTY(ReplicatedUsing = OnRep_RecoveryStock)
    int32 RecoveryStock = 1;

    UPROPERTY(Replicated)
    int32 StaminaRecoveryStock = 2;

    UPROPERTY(ReplicatedUsing = OnRep_WeaponStock)
    int32 WeaponStock = 2;

    UPROPERTY(Transient, Replicated)
    int32 MartialPowerScrollStock = 1;

    UPROPERTY(Transient, Replicated)
    int32 ProtectionScrollStock = 1;

    UPROPERTY(Transient, Replicated)
    int32 SlashCapsuleStock = 1;

    UPROPERTY(Transient, Replicated)
    int32 TauntCapsuleStock = 1;

    UPROPERTY(Transient, Replicated)
    int32 PersonalShieldStock = 0;
};
