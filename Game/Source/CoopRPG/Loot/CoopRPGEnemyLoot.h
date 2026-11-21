#pragma once

#include "GameFramework/Actor.h"
#include "CoopRPGEnemyLoot.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UNiagaraComponent;

UCLASS()
class COOPRPG_API ACoopRPGEnemyLoot : public AActor
{
    GENERATED_BODY()

public:
    ACoopRPGEnemyLoot();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;


    void ConfigureSkirmishDrop(bool bIsPotion, const FVector& Origin);
    bool IsPotion() const { return bPotion; }

    bool TryClaim();

    bool IsClaimed() const { return bClaimed; }
    FGuid GetDropId() const { return DropId; }

protected:
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnRep_DropId();

    UFUNCTION()
    void OnRep_Claimed();


    UFUNCTION()
    void OnRep_SkirmishAppearance();

    void ApplyClaimPresentation();

public:
    static constexpr float PickupRangeCm = 250.0f;
    static constexpr double GroundToAnchorCm = 38.0;

private:

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> LootVisual;

    UPROPERTY()
    TObjectPtr<UNiagaraComponent> RarityEffect;

    UPROPERTY(ReplicatedUsing = OnRep_SkirmishAppearance)
    bool bSkirmishDrop = false;

    UPROPERTY(ReplicatedUsing = OnRep_SkirmishAppearance)
    bool bPotion = false;

    UPROPERTY(Replicated)
    FVector_NetQuantize DropOrigin = FVector::ZeroVector;

    bool bAppearanceReady = false;
    double AnimationStart = 0.0;
    double SettledVisualOffset = 0.0;
    bool bSettled = false;

    UPROPERTY(ReplicatedUsing = OnRep_DropId)
    FGuid DropId;

    UPROPERTY(ReplicatedUsing = OnRep_Claimed)
    bool bClaimed = false;
};
