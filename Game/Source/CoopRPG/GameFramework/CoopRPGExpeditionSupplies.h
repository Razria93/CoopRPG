#pragma once

#include "CoreMinimal.h"
#include "CoopRPGExpeditionSupplies.generated.h"

USTRUCT()
struct FCoopRPGExpeditionSupplies
{
    GENERATED_BODY()

    bool HasSword() const { return OriginRound.IsValid(); }

    UPROPERTY()
    FGuid OriginRound;

    UPROPERTY()
    bool bSwordEquipped = false;

    UPROPERTY()
    bool bHasShield = false;

    UPROPERTY()
    bool bShieldEquipped = false;
};
