#pragma once

#include "CoopRPGRecoveryInventory.h"
#include "CoopRPGSlashProgress.h"
#include "CoopRPGStatProgress.h"
#include "CoopRPGSwordInventory.h"

struct FCoopRPGExpeditionInventory
{
    FCoopRPGRecoverySnapshot Recovery;
    FCoopRPGEquipmentSnapshot Equipment;
    FCoopRPGStatProgress Stats;
    FCoopRPGSkillCapsuleProgress SkillProgress;
    FCoopRPGActiveSkillSlots SkillSlots;
    bool bExpeditionPowerReward = false;

    FCoopRPGExpeditionInventory RestoreForRetry(const FCoopRPGExpeditionInventory& Current) const
    {
        FCoopRPGExpeditionInventory Restored = *this;
        Restored.Equipment.Sword = Equipment.Sword.RestoreForRetry(Current.Equipment.Sword);
        Restored.Equipment.Shield = Equipment.Shield.RestoreForRetry(Current.Equipment.Shield);
        return Restored;
    }
};
