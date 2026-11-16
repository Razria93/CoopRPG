#include "CoopRPGGameState.h"

#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRound, Log, All);

void ACoopRPGGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGGameState, SharedChest);
    DOREPLIFETIME(ACoopRPGGameState, RoundId);
    DOREPLIFETIME(ACoopRPGGameState, bRoundPlaying);
    DOREPLIFETIME(ACoopRPGGameState, EnemyLoot);
    DOREPLIFETIME(ACoopRPGGameState, TeamLootCount);
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

bool ACoopRPGGameState::RegisterEnemyLoot(ACoopRPGEnemyLoot* Drop)
{
    if (!HasAuthority() || !RoundId.IsValid() || EnemyLoot || !IsValid(Drop) || !Drop->GetDropId().IsValid())
    {
        return false;
    }

    EnemyLoot = Drop;
    ForceNetUpdate();
    return true;
}

bool ACoopRPGGameState::TryGrantEnemyLoot(ACoopRPGEnemyLoot* Drop)
{
    if (!HasAuthority() || !bRoundPlaying || !IsValid(Drop) || EnemyLoot != Drop || !Drop->TryClaim())
    {
        return false;
    }

    ++TeamLootCount;
    ForceNetUpdate();
    return true;
}

void ACoopRPGGameState::OnRep_TeamLootCount(int32 PreviousCount)
{
    UE_LOG(LogCoopRPGRound, Log, TEXT("TeamLootCountReplicated Before=%d After=%d %s"),
        PreviousCount, TeamLootCount, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}
