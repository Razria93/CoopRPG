#include "CoopRPGRecoveryEffect.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Data_Recovery, "Data.CoopRPG.Recovery");

namespace
{
    void ConfigureRecovery(FGameplayModifierInfo& Modifier, const FGameplayAttribute& Attribute)
    {
        Modifier.Attribute = Attribute;
        Modifier.ModifierOp = EGameplayModOp::Additive;
        FSetByCallerFloat Magnitude;
        Magnitude.DataTag = TAG_Data_Recovery.GetTag();
        Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
    }
}

void CoopRPG::Recovery::SetAmount(FGameplayEffectSpec& Spec, float Amount)
{
    Spec.SetSetByCallerMagnitude(TAG_Data_Recovery.GetTag(), Amount);
}

UCoopRPGRecoveryEffect::UCoopRPGRecoveryEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;

    ConfigureRecovery(Modifiers.AddDefaulted_GetRef(), UCoopRPGCombatAttributes::GetHealthAttribute());
}

UCoopRPGStaminaRecoveryEffect::UCoopRPGStaminaRecoveryEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;

    ConfigureRecovery(Modifiers.AddDefaulted_GetRef(), UCoopRPGCombatAttributes::GetStaminaAttribute());
}
