#include "CoopRPGCombatAttributes.h"

#include "CoopRPG/Debug/CoopRPGLogContext.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGCombatAttributes, Log, All);

UCoopRPGCombatAttributes::UCoopRPGCombatAttributes()
    : Health(MaxHealth), Stamina(100.0f)
{
}

void UCoopRPGCombatAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UCoopRPGCombatAttributes, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UCoopRPGCombatAttributes, Stamina, COND_OwnerOnly, REPNOTIFY_Always);
}

void UCoopRPGCombatAttributes::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
    Super::PreAttributeBaseChange(Attribute, NewValue);
    if (Attribute == GetHealthAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.0f, MaxHealth);
    }
    else if (Attribute == GetStaminaAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.0f, 100.0f);
    }
}

void UCoopRPGCombatAttributes::OnRep_Health(const FGameplayAttributeData& PreviousHealth)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UCoopRPGCombatAttributes, Health, PreviousHealth);
    const AActor* Owner = GetOwningActor();
    UE_LOG(LogCoopRPGCombatAttributes, Log, TEXT("HealthReplicated Actor=%s Before=%.2f After=%.2f %s"),
        *GetNameSafe(Owner), PreviousHealth.GetCurrentValue(), GetHealth(),
        *CoopRPG::Debug::DescribeLogContext(Owner, ANSI_TO_TCHAR(__FUNCTION__)));
}

void UCoopRPGCombatAttributes::OnRep_Stamina(const FGameplayAttributeData& PreviousStamina)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UCoopRPGCombatAttributes, Stamina, PreviousStamina);
    const AActor* Owner = GetOwningActor();
    UE_LOG(LogCoopRPGCombatAttributes, Log, TEXT("StaminaReplicated Actor=%s Before=%.2f After=%.2f %s"),
        *GetNameSafe(Owner), PreviousStamina.GetCurrentValue(), GetStamina(),
        *CoopRPG::Debug::DescribeLogContext(Owner, ANSI_TO_TCHAR(__FUNCTION__)));
}
