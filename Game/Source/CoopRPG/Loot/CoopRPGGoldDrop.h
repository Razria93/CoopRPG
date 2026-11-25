#pragma once

#include "GameFramework/Actor.h"
#include "CoopRPGGoldDrop.generated.h"

class UStaticMeshComponent;

UCLASS()
class ACoopRPGGoldDrop : public AActor
{
    GENERATED_BODY()

public:
    ACoopRPGGoldDrop();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void Tick(float DeltaSeconds) override;

    FGuid GetDropId() const { return DropId; }
    bool IsSettled() const;
    bool IsInPickupRange(const APawn& Pawn) const;

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnRep_DropId();

private:
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Rim;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Face;

    UPROPERTY(ReplicatedUsing = OnRep_DropId)
    FGuid DropId;

    double SpawnTime = 0.0;
};
