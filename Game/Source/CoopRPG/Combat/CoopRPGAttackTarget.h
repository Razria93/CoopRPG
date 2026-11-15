#pragma once

#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoopRPGAttackTarget.generated.h"

class UAbilitySystemComponent;
class UCoopRPGCombatAttributes;

UCLASS()
class COOPRPG_API ACoopRPGAttackTarget : public AActor, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ACoopRPGAttackTarget();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    float GetHealth() const;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;

    UPROPERTY()
    TObjectPtr<UCoopRPGCombatAttributes> CombatAttributes;
};
