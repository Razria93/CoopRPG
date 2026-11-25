#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopRPGRecoveryInventory.h"
#include "CoopRPGStatProgress.h"
#include "CoopRPGSlashProgress.h"
#include "CoopRPGSwordInventory.h"
#include "CoopRPGRoundTravelSeats.generated.h"

struct FCoopRPGRoundTravelParticipant
{
    FUniqueNetIdRepl ConnectionId;
    FCoopRPGRecoverySnapshot Recovery;
    FCoopRPGSwordSnapshot Sword;
    FCoopRPGStatProgress Stats;
    FCoopRPGSlashProgress Slash;
    bool bExpeditionPowerReward = false;
};

UCLASS()
class UCoopRPGRoundTravelSeats : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    void Reserve(FName Destination, TArray<FCoopRPGRoundTravelParticipant> Participants);
    void Clear();
    FIntPoint GetPotionCapacityBonus(const FUniqueNetIdRepl& ConnectionId) const;
    void GrantPotionCapacityReward(const FUniqueNetIdRepl& ConnectionId, bool bStamina);
    bool Allows(FName Map, const FUniqueNetIdRepl& ConnectionId) const;
    int32 Admit(FName Map, const FUniqueNetIdRepl& ConnectionId, FCoopRPGRoundTravelParticipant& OutParticipant);

private:
    bool IsActive(FName Map) const;

private:
    FName ReservedMap;
    TArray<FUniqueNetIdRepl> AwaitingPlayers;
    TArray<FCoopRPGRoundTravelParticipant> ReservedPlayers;
    double ExpiresAt = 0.0;
    TMap<FUniqueNetIdRepl, FIntPoint> PotionCapacityBonuses;
};
