#pragma once

#include "CoreMinimal.h"
#include "CoopRPGSwordInventory.generated.h"

UENUM()
enum class ECoopRPGSwordOwnership : uint8
{
    None,
    Unbound,
    Bound
};

struct FCoopRPGSwordSnapshot
{
    ECoopRPGSwordOwnership Ownership = ECoopRPGSwordOwnership::None;
    bool bEquipped = false;
};
