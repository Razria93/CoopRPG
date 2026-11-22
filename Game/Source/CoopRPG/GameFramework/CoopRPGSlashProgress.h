#pragma once

#include "CoreMinimal.h"
#include "CoopRPGSlashProgress.generated.h"

USTRUCT()
struct FCoopRPGSlashProgress
{
    GENERATED_BODY()

    UPROPERTY()
    bool bHasCapsule = false;

    UPROPERTY()
    bool bRegistered = false;
};
