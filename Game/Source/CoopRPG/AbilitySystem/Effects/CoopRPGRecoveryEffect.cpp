#include "CoopRPGRecoveryEffect.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"

UCoopRPGRecoveryEffect::UCoopRPGRecoveryEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;

    FGameplayModifierInfo& Healing = Modifiers.AddDefaulted_GetRef();
    Healing.Attribute = UCoopRPGCombatAttributes::GetHealthAttribute();
    Healing.ModifierOp = EGameplayModOp::Additive;
    Healing.ModifierMagnitude = FScalableFloat(HealthRestored);
}

UCoopRPGStaminaRecoveryEffect::UCoopRPGStaminaRecoveryEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;

    FGameplayModifierInfo& Recovery = Modifiers.AddDefaulted_GetRef();
    Recovery.Attribute = UCoopRPGCombatAttributes::GetStaminaAttribute();
    Recovery.ModifierOp = EGameplayModOp::Additive;
    Recovery.ModifierMagnitude = FScalableFloat(StaminaRestored);
}
