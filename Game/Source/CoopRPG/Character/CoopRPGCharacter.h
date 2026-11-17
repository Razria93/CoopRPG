#pragma once

#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CoopRPGCharacter.generated.h"

class UAbilitySystemComponent;
class UCoopRPGCombatAttributes;
class UInputAction;
struct FInputActionValue;

UCLASS()
class COOPRPG_API ACoopRPGCharacter : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ACoopRPGCharacter();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
    virtual void BeginPlay() override;
    virtual void NotifyControllerChanged() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
    void Move(const FInputActionValue& Value);
    void Attack();
    void CancelAttack();

    UFUNCTION(Server, Reliable)
    void ServerCancelAttack();

private:
    UPROPERTY()
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;

    UPROPERTY()
    TObjectPtr<UCoopRPGCombatAttributes> CombatAttributes;

    UPROPERTY()
    TObjectPtr<UInputAction> AttackAction;
};
