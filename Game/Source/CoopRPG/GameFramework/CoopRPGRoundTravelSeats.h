#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopRPGRoundTravelSeats.generated.h"

struct FCoopRPGRoundTravelParticipant
{
    FUniqueNetIdRepl ConnectionId;
    int32 RecoveryCount = 0;
};

UCLASS()
class UCoopRPGRoundTravelSeats : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    void Reserve(FName Destination, TArray<FCoopRPGRoundTravelParticipant> Participants);
    void Clear();
    bool Allows(FName Map, const FUniqueNetIdRepl& ConnectionId) const;
    int32 Admit(FName Map, const FUniqueNetIdRepl& ConnectionId, int32& OutRecoveryCount);

private:
    bool IsActive(FName Map) const;

private:
    FName ReservedMap;
    TArray<FUniqueNetIdRepl> AwaitingPlayers;
    TArray<FCoopRPGRoundTravelParticipant> ReservedPlayers;
    double ExpiresAt = 0.0;
};
