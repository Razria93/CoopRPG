#pragma once

#include "GameFramework/Actor.h"
#include "CoopRPGEnemyLoot.generated.h"

UCLASS()
class COOPRPG_API ACoopRPGEnemyLoot : public AActor
{
    GENERATED_BODY()

public:
    ACoopRPGEnemyLoot();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    FGuid GetDropId() const { return DropId; }

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnRep_DropId();

private:
    UPROPERTY(ReplicatedUsing = OnRep_DropId)
    FGuid DropId;
};
