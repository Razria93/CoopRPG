#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopRPGRoundTravelSeats.generated.h"

UCLASS()
class UCoopRPGRoundTravelSeats : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    void Reserve(FName Destination, TArray<FUniqueNetIdRepl> Participants);
    void Clear();
    bool Allows(FName Map, const FUniqueNetIdRepl& ConnectionId) const;
    void Admit(FName Map, const FUniqueNetIdRepl& ConnectionId);

private:
    bool IsActive(FName Map) const;

private:
    FName ReservedMap;
    TArray<FUniqueNetIdRepl> AwaitingPlayers;
    double ExpiresAt = 0.0;
};
