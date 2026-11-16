#include "CoopRPGRecoveryEffect.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"

UCoopRPGRecoveryEffect::UCoopRPGRecoveryEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;

    FGameplayModifierInfo& Healing = Modifiers.AddDefaulted_GetRef();
    Healing.Attribute = UCoopRPGCombatAttributes::GetHealthAttribute();
    Healing.ModifierOp = EGameplayModOp::Additive;
    Healing.ModifierMagnitude = FScalableFloat(30.0f);
}
