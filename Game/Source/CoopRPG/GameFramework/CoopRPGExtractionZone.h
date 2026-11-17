#pragma once

#include "GameFramework/Actor.h"
#include "CoopRPGExtractionZone.generated.h"

class APawn;

UCLASS()
class COOPRPG_API ACoopRPGExtractionZone : public AActor
{
    GENERATED_BODY()

public:
    ACoopRPGExtractionZone();

    bool ContainsPawn(const APawn& Pawn) const;
};
