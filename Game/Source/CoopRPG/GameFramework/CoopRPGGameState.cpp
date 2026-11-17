#include "CoopRPGGameState.h"

#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRound, Log, All);

void ACoopRPGGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGGameState, SharedChest);
    DOREPLIFETIME(ACoopRPGGameState, RoundId);
    DOREPLIFETIME(ACoopRPGGameState, bRoundPlaying);
}

void ACoopRPGGameState::InitializeLootRound(ACoopRPGSharedChest* Chest)
{
    if (HasAuthority() && IsValid(Chest) && !RoundId.IsValid())
    {
        SharedChest = Chest;
        RoundId = FGuid::NewGuid();
        UE_LOG(LogCoopRPGRound, Log, TEXT("LootRoundCreated Round=%s Chest=%s Stock=%d"),
            *RoundId.ToString(), *GetNameSafe(SharedChest), Chest->GetRecoveryStock());
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::SetRoundPlaying(bool bPlaying)
{
    if (HasAuthority() && RoundId.IsValid() && bRoundPlaying != bPlaying)
    {
        bRoundPlaying = bPlaying;
        UE_LOG(LogCoopRPGRound, Log, TEXT("LootRoundPhase Round=%s Phase=%s"),
            *RoundId.ToString(), bRoundPlaying ? TEXT("Playing") : TEXT("Waiting"));
        ForceNetUpdate();
    }
}
