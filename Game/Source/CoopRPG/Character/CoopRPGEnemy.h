#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "CoopRPGEnemy.generated.h"

class ACoopRPGCharacter;
class UAbilitySystemComponent;
class UCoopRPGCombatAttributes;

UCLASS()
class COOPRPG_API ACoopRPGEnemy : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ACoopRPGEnemy();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnRep_ReplicatedMovement() override;

protected:
    virtual void BeginPlay() override;

private:
    ACoopRPGCharacter* FindTarget() const;
    void StopChasing();
    void ResolveAttack(ACoopRPGCharacter* Player);

private:
    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;

    UPROPERTY()
    TObjectPtr<UCoopRPGCombatAttributes> CombatAttributes;

    TWeakObjectPtr<ACoopRPGCharacter> Target;
    double NextActionTime = 0.0;
    bool bChasing = false;
    bool bAttackPending = false;

    FVector ObservationOrigin = FVector::ZeroVector;
    bool bObservedMovement = false;
};
