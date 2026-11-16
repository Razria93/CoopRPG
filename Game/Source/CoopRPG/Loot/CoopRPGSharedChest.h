#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoopRPGSharedChest.generated.h"

UCLASS()
class COOPRPG_API ACoopRPGSharedChest : public AActor
{
    GENERATED_BODY()

public:
    ACoopRPGSharedChest();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    int32 GetRecoveryStock() const { return RecoveryStock; }
    int32 GetWeaponStock() const { return WeaponStock; }
    bool TryTakeRecovery();
    bool TryTakeWeapon();

private:
    bool TryTakeStock(int32& Stock);

    UFUNCTION()
    void OnRep_RecoveryStock();

    UFUNCTION()
    void OnRep_WeaponStock();

private:
    UPROPERTY(ReplicatedUsing = OnRep_RecoveryStock)
    int32 RecoveryStock = 1;

    UPROPERTY(ReplicatedUsing = OnRep_WeaponStock)
    int32 WeaponStock = 2;
};
