#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoopRPGSharedChest.generated.h"

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
    bool TryTakeRecovery();
    bool TryTakeWeapon();
    void RecordFirstOpen();

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

    UPROPERTY(ReplicatedUsing = OnRep_WeaponStock)
    int32 WeaponStock = 2;
};
