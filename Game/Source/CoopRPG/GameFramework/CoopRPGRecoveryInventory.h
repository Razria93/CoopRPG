#pragma once

#include "CoreMinimal.h"
#include "CoopRPGRecoveryInventory.generated.h"

USTRUCT()
struct FCoopRPGRecoveryRegistration
{
    GENERATED_BODY()

    UPROPERTY()
    bool bHealth = false;

    UPROPERTY()
    bool bStamina = false;
};

struct FCoopRPGRecoverySnapshot
{
    FIntPoint Personal = FIntPoint::ZeroValue;
    FCoopRPGRecoveryRegistration Registration;
};
