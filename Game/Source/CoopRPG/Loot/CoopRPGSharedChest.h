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
    bool TryTakeRecovery();

private:
    UFUNCTION()
    void OnRep_RecoveryStock();

private:
    UPROPERTY(ReplicatedUsing = OnRep_RecoveryStock)
    int32 RecoveryStock = 1;
};
