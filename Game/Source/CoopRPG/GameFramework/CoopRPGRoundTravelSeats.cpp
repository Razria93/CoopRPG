#include "CoopRPGRoundTravelSeats.h"

#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundTravelSeats, Log, All);

void UCoopRPGRoundTravelSeats::Reserve(FName Destination, TArray<FCoopRPGRoundTravelParticipant> Participants)
{
    constexpr double ReservationSeconds = 30.0;
    ReservedMap = Destination;
    ReservedPlayers = MoveTemp(Participants);
    AwaitingPlayers.Reset(ReservedPlayers.Num());
    for (const auto& Participant : ReservedPlayers)
    {
        AwaitingPlayers.Add(Participant.ConnectionId);
    }
    ExpiresAt = FPlatformTime::Seconds() + ReservationSeconds;
    UE_LOG(LogCoopRPGRoundTravelSeats, Log, TEXT("RoundTravelSeatsReserved Map=%s Count=%d Seconds=%.0f"),
        *ReservedMap.ToString(), AwaitingPlayers.Num(), ReservationSeconds);
}

void UCoopRPGRoundTravelSeats::Clear()
{
    ReservedMap = NAME_None;
    AwaitingPlayers.Reset();
    ReservedPlayers.Reset();
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
    return !IsActive(Map) || (ConnectionId.IsValid() && AwaitingPlayers.Contains(ConnectionId));
}

int32 UCoopRPGRoundTravelSeats::Admit(FName Map, const FUniqueNetIdRepl& ConnectionId, FCoopRPGRoundTravelParticipant& OutParticipant)
{
    if (IsActive(Map) && AwaitingPlayers.RemoveSingle(ConnectionId) == 1)
    {
        UE_LOG(LogCoopRPGRoundTravelSeats, Log, TEXT("RoundTravelSeatJoined Map=%s Remaining=%d"),
            *Map.ToString(), AwaitingPlayers.Num());
        const int32 Slot = ReservedPlayers.IndexOfByPredicate(
            [&ConnectionId](const auto& Participant) { return Participant.ConnectionId == ConnectionId; });
        if (ReservedPlayers.IsValidIndex(Slot))
        {
            OutParticipant = ReservedPlayers[Slot];
        }
        return Slot;
    }
    return INDEX_NONE;
}

bool UCoopRPGRoundTravelSeats::IsActive(FName Map) const
{
    return Map == ReservedMap && !AwaitingPlayers.IsEmpty() && FPlatformTime::Seconds() < ExpiresAt;
}
