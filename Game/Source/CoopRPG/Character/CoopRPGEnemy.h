#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "CoopRPGEnemy.generated.h"

class ACoopRPGCharacter;
class UAbilitySystemComponent;
class UCoopRPGCombatAttributes;
class UStaticMeshComponent;
struct FOnAttributeChangeData;

UCLASS()
class COOPRPG_API ACoopRPGEnemy : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ACoopRPGEnemy();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnRep_ReplicatedMovement() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    bool IsDead() const { return bDead; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    ACoopRPGCharacter* FindTarget() const;
    void StopChasing();
    void StopCombat();
    void ResolveAttack(ACoopRPGCharacter* Player);
    void HandleHealthChanged(const FOnAttributeChangeData& Change);
    void SpawnDeathLoot(const FGuid& RoundId);
    void ApplyDeathPresentation();

    UFUNCTION()
    void OnRep_Dead();

private:
    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;

    UPROPERTY()
    TObjectPtr<UCoopRPGCombatAttributes> CombatAttributes;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> EnemyVisual;

    UPROPERTY(ReplicatedUsing = OnRep_Dead)
    bool bDead = false;

    TWeakObjectPtr<ACoopRPGCharacter> Target;
    double NextActionTime = 0.0;
    bool bChasing = false;
    bool bAttackPending = false;

    FVector ObservationOrigin = FVector::ZeroVector;
    bool bObservedMovement = false;
};
