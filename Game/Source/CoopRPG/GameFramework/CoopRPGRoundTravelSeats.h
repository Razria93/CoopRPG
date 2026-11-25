#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopRPGExpeditionInventory.h"
#include "CoopRPGPlayerState.h"
#include "CoopRPGRoundTravelSeats.generated.h"

struct FCoopRPGRoundTravelParticipant
{
    FCoopRPGExpeditionInventory Inventory;
    ECoopRPGHero Hero = ECoopRPGHero::Unassigned;
    TOptional<FCoopRPGExpeditionInventory> StageEntry;
};

UCLASS()
class UCoopRPGRoundTravelSeats : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    void EnterRound(FGuid RoundId, FName Map, bool bLobby);
    void Reserve(FName Destination, const TArray<FUniqueNetIdRepl>& SlotOwners,
        TMap<FUniqueNetIdRepl, FCoopRPGRoundTravelParticipant> Participants, bool bRetry);
    void CancelTravel();
    FIntPoint GetPotionCapacityBonus(const FUniqueNetIdRepl& ConnectionId) const;
    void GrantPotionCapacityReward(const FUniqueNetIdRepl& ConnectionId, bool bStamina);
    bool Allows(FName Map, const FUniqueNetIdRepl& ConnectionId) const;
    int32 Admit(FName Map, const FUniqueNetIdRepl& ConnectionId);
    bool TakeParticipant(FGuid RoundId, const FUniqueNetIdRepl& ConnectionId,
        FCoopRPGRoundTravelParticipant& Participant);
    void SaveParticipant(FGuid RoundId, const FUniqueNetIdRepl& ConnectionId,
        FCoopRPGRoundTravelParticipant Participant);

private:
    bool IsActive(FName Map) const;

private:
    FGuid CurrentRound;
    FName ReservedMap;
    TArray<FUniqueNetIdRepl> ReservedSlotOwners;
    TArray<FUniqueNetIdRepl> AwaitingPlayers;
    TMap<FUniqueNetIdRepl, FCoopRPGRoundTravelParticipant> PendingParticipants;
    TMap<FUniqueNetIdRepl, FCoopRPGRoundTravelParticipant> SavedParticipants;
    bool bPendingRetry = false;
    double ExpiresAt = 0.0;
    TMap<FUniqueNetIdRepl, FIntPoint> PotionCapacityBonuses;
};
