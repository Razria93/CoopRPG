#pragma once

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "CoopRPGCombatAttributes.generated.h"

UCLASS()
class COOPRPG_API UCoopRPGCombatAttributes : public UAttributeSet
{
    GENERATED_BODY()

public:
    UCoopRPGCombatAttributes();

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UCoopRPGCombatAttributes, Health)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Health)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UCoopRPGCombatAttributes, Stamina)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Stamina)

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

private:
    UFUNCTION()
    void OnRep_Health(const FGameplayAttributeData& PreviousHealth);

    UFUNCTION()
    void OnRep_Stamina(const FGameplayAttributeData& PreviousStamina);

private:
    UPROPERTY(ReplicatedUsing = OnRep_Health)
    FGameplayAttributeData Health;

    UPROPERTY(ReplicatedUsing = OnRep_Stamina)
    FGameplayAttributeData Stamina;
};
