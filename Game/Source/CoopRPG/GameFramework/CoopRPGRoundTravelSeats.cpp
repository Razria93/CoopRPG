#include "CoopRPGRoundTravelSeats.h"

#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundTravelSeats, Log, All);

void UCoopRPGRoundTravelSeats::EnterRound(FGuid RoundId, FName Map, bool bLobby)
{
    if (!RoundId.IsValid() || RoundId == CurrentRound)
    {
        return;
    }
    CurrentRound = RoundId;
    if (ReservedMap != Map)
    {
        SavedParticipants.Reset();
        CancelTravel();
        return;
    }

    for (auto& Pair : SavedParticipants)
    {
        auto& Participant = Pair.Value;
        if (bPendingRetry && Participant.StageEntry.IsSet())
        {
            Participant.Inventory = Participant.StageEntry->RestoreForRetry(Participant.Inventory);
        }
        Participant.StageEntry.Reset();
        if (bLobby)
        {
            Participant.Inventory.bExpeditionPowerReward = false;
        }
    }
    SavedParticipants.Append(MoveTemp(PendingParticipants));
    PendingParticipants.Reset();
    bPendingRetry = false;
}

void UCoopRPGRoundTravelSeats::Reserve(FName Destination, const TArray<FUniqueNetIdRepl>& SlotOwners,
    TMap<FUniqueNetIdRepl, FCoopRPGRoundTravelParticipant> Participants, bool bRetry)
{
    constexpr double ReservationSeconds = 30.0;
    ReservedMap = Destination;
    ReservedSlotOwners = SlotOwners;
    AwaitingPlayers = SlotOwners;
    PendingParticipants = MoveTemp(Participants);
    bPendingRetry = bRetry;
    ExpiresAt = FPlatformTime::Seconds() + ReservationSeconds;
    UE_LOG(LogCoopRPGRoundTravelSeats, Log, TEXT("RoundTravelSeatsReserved Map=%s Count=%d Seconds=%.0f"),
        *ReservedMap.ToString(), AwaitingPlayers.Num(), ReservationSeconds);
}

void UCoopRPGRoundTravelSeats::CancelTravel()
{
    ReservedMap = NAME_None;
    ReservedSlotOwners.Reset();
    AwaitingPlayers.Reset();
    PendingParticipants.Reset();
    bPendingRetry = false;
    ExpiresAt = 0.0;
}

FIntPoint UCoopRPGRoundTravelSeats::GetPotionCapacityBonus(const FUniqueNetIdRepl& ConnectionId) const
{
    const FIntPoint* Bonus = PotionCapacityBonuses.Find(ConnectionId);
    return Bonus ? *Bonus : FIntPoint::ZeroValue;
}

void UCoopRPGRoundTravelSeats::GrantPotionCapacityReward(const FUniqueNetIdRepl& ConnectionId, bool bStamina)
{
    FIntPoint& Bonus = PotionCapacityBonuses.FindOrAdd(ConnectionId, FIntPoint::ZeroValue);
    (bStamina ? Bonus.Y : Bonus.X) = 1;
}

bool UCoopRPGRoundTravelSeats::Allows(FName Map, const FUniqueNetIdRepl& ConnectionId) const
{
    return !IsActive(Map) || (ConnectionId.IsValid() && ReservedSlotOwners.Contains(ConnectionId));
}

int32 UCoopRPGRoundTravelSeats::Admit(FName Map, const FUniqueNetIdRepl& ConnectionId)
{
    if (!IsActive(Map))
    {
        return INDEX_NONE;
    }
    const int32 Slot = ReservedSlotOwners.IndexOfByKey(ConnectionId);
    if (Slot != INDEX_NONE)
    {
        AwaitingPlayers.RemoveSingle(ConnectionId);
        UE_LOG(LogCoopRPGRoundTravelSeats, Log, TEXT("RoundTravelSeatJoined Map=%s Remaining=%d"),
            *Map.ToString(), AwaitingPlayers.Num());
    }
    return Slot;
}

bool UCoopRPGRoundTravelSeats::TakeParticipant(FGuid RoundId, const FUniqueNetIdRepl& ConnectionId,
    FCoopRPGRoundTravelParticipant& Participant)
{
    return RoundId.IsValid() && RoundId == CurrentRound && ConnectionId.IsValid()
        && SavedParticipants.RemoveAndCopyValue(ConnectionId, Participant);
}

void UCoopRPGRoundTravelSeats::SaveParticipant(FGuid RoundId, const FUniqueNetIdRepl& ConnectionId,
    FCoopRPGRoundTravelParticipant Participant)
{
    if (RoundId.IsValid() && RoundId == CurrentRound && ConnectionId.IsValid())
    {
        SavedParticipants.Add(ConnectionId, MoveTemp(Participant));
    }
}

bool UCoopRPGRoundTravelSeats::IsActive(FName Map) const
{
    return Map == ReservedMap && !AwaitingPlayers.IsEmpty() && FPlatformTime::Seconds() < ExpiresAt;
}
