#pragma once

#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CoopRPGCharacter.generated.h"

class UAbilitySystemComponent;
class UCoopRPGCombatAttributes;
class UInputAction;
class UStaticMeshComponent;
struct FInputActionValue;

UCLASS()
class COOPRPG_API ACoopRPGCharacter : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ACoopRPGCharacter();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    float GetHealth() const;
    bool HasMeleeWeapon() const { return bHasMeleeWeapon; }
    void EquipMeleeWeapon();

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

    UFUNCTION()
    void OnRep_MeleeWeapon();

private:
    UPROPERTY()
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;

    UPROPERTY()
    TObjectPtr<UCoopRPGCombatAttributes> CombatAttributes;

    UPROPERTY()
    TObjectPtr<UInputAction> AttackAction;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WeaponMesh;

    UPROPERTY(ReplicatedUsing = OnRep_MeleeWeapon)
    bool bHasMeleeWeapon = false;
};
