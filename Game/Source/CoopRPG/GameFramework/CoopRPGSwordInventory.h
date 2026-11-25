#pragma once

#include "CoreMinimal.h"
#include "CoopRPGSwordInventory.generated.h"

UENUM()
enum class ECoopRPGEquipmentOwnership : uint8
{
    None,
    Unbound,
    Bound
};

USTRUCT()
struct FCoopRPGPersonalEquipment
{
    GENERATED_BODY()

    UPROPERTY()
    ECoopRPGEquipmentOwnership Ownership = ECoopRPGEquipmentOwnership::None;

    UPROPERTY()
    bool bEquipped = false;

    FCoopRPGPersonalEquipment RestoreForRetry(const FCoopRPGPersonalEquipment& Current) const
    {
        FCoopRPGPersonalEquipment Restored = *this;
        if (Ownership != ECoopRPGEquipmentOwnership::None && Current.Ownership == ECoopRPGEquipmentOwnership::Bound)
        {
            Restored.Ownership = Current.Ownership;
        }
        return Restored;
    }
};

struct FCoopRPGEquipmentSnapshot
{
    FCoopRPGPersonalEquipment Sword;
    FCoopRPGPersonalEquipment Shield;
};
