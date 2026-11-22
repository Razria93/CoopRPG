#include "CoopRPGMeleeEffects.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_Melee, "Cooldown.CoopRPG.Melee");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_Slash, "Cooldown.CoopRPG.Slash");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Data_MeleeDamage, "Data.CoopRPG.MeleeDamage");

UCoopRPGMeleeDamage::UCoopRPGMeleeDamage()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo& Damage = Modifiers.AddDefaulted_GetRef();
    Damage.Attribute = UCoopRPGCombatAttributes::GetHealthAttribute();
    Damage.ModifierOp = EGameplayModOp::Additive;
    FSetByCallerFloat Magnitude;
    Magnitude.DataTag = TAG_Data_MeleeDamage.GetTag();
    Damage.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
}

void UCoopRPGMeleeDamage::SetDamage(FGameplayEffectSpec& Spec, float Amount)
{
    Spec.SetSetByCallerMagnitude(TAG_Data_MeleeDamage.GetTag(), -Amount);
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

UCoopRPGSlashCost::UCoopRPGSlashCost()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo& Cost = Modifiers.AddDefaulted_GetRef();
    Cost.Attribute = UCoopRPGCombatAttributes::GetStaminaAttribute();
    Cost.ModifierOp = EGameplayModOp::Additive;
    Cost.ModifierMagnitude = FScalableFloat(-Stamina);
}

FGameplayTag UCoopRPGSlashCooldown::GetCooldownTag()
{
    return TAG_Cooldown_Slash.GetTag();
}

UCoopRPGSlashCooldown::UCoopRPGSlashCooldown()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(Seconds);

    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTags"));
    FInheritedTagContainer Changes;
    Changes.AddTag(GetCooldownTag());
    Tags->SetAndApplyTargetTagChanges(Changes);
    GEComponents.Add(Tags);
}
