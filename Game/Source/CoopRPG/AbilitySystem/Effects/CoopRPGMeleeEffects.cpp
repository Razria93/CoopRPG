#include "CoopRPGMeleeEffects.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_Melee, "Cooldown.CoopRPG.Melee");

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

UCoopRPGMeleeCooldown::UCoopRPGMeleeCooldown()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(2.0f);

    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTags"));
    FInheritedTagContainer Changes;
    Changes.AddTag(GetCooldownTag());
    Tags->SetAndApplyTargetTagChanges(Changes);
    GEComponents.Add(Tags);
}

FGameplayTag UCoopRPGMeleeCooldown::GetCooldownTag()
{
    return TAG_Cooldown_Melee.GetTag();
}
