#include "CoopRPGRoundTravelSeats.h"

#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundTravelSeats, Log, All);

void UCoopRPGRoundTravelSeats::Reserve(FName Destination, TArray<FUniqueNetIdRepl> Participants)
{
    constexpr double ReservationSeconds = 30.0;
    ReservedMap = Destination;
    ReservedPlayers = MoveTemp(Participants);
    AwaitingPlayers = ReservedPlayers;
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

bool UCoopRPGRoundTravelSeats::Allows(FName Map, const FUniqueNetIdRepl& ConnectionId) const
{
    return !IsActive(Map) || (ConnectionId.IsValid() && AwaitingPlayers.Contains(ConnectionId));
}

int32 UCoopRPGRoundTravelSeats::Admit(FName Map, const FUniqueNetIdRepl& ConnectionId)
{
    if (IsActive(Map) && AwaitingPlayers.RemoveSingle(ConnectionId) == 1)
    {
        UE_LOG(LogCoopRPGRoundTravelSeats, Log, TEXT("RoundTravelSeatJoined Map=%s Remaining=%d"),
            *Map.ToString(), AwaitingPlayers.Num());
        return ReservedPlayers.IndexOfByKey(ConnectionId);
    }
    return INDEX_NONE;
}

bool UCoopRPGRoundTravelSeats::IsActive(FName Map) const
{
    return Map == ReservedMap && !AwaitingPlayers.IsEmpty() && FPlatformTime::Seconds() < ExpiresAt;
}
