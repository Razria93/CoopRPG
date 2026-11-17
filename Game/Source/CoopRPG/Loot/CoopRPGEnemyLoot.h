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

    bool TryClaim();

    bool IsClaimed() const { return bClaimed; }
    FGuid GetDropId() const { return DropId; }

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnRep_DropId();

    UFUNCTION()
    void OnRep_Claimed();

    void ApplyClaimPresentation();

public:
    static constexpr float PickupRangeCm = 250.0f;

private:
    UPROPERTY(ReplicatedUsing = OnRep_DropId)
    FGuid DropId;

    UPROPERTY(ReplicatedUsing = OnRep_Claimed)
    bool bClaimed = false;
};
