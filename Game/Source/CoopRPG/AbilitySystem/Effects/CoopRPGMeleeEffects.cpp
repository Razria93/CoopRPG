#include "CoopRPGMeleeEffects.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"

UCoopRPGMeleeDamage::UCoopRPGMeleeDamage()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo& Damage = Modifiers.AddDefaulted_GetRef();
    Damage.Attribute = UCoopRPGCombatAttributes::GetHealthAttribute();
    Damage.ModifierOp = EGameplayModOp::Additive;
    Damage.ModifierMagnitude = FScalableFloat(-10.0f);
}

UCoopRPGMeleeCost::UCoopRPGMeleeCost()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo& Cost = Modifiers.AddDefaulted_GetRef();
    Cost.Attribute = UCoopRPGCombatAttributes::GetStaminaAttribute();
    Cost.ModifierOp = EGameplayModOp::Additive;
    Cost.ModifierMagnitude = FScalableFloat(-10.0f);
}
