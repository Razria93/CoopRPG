#include "CoopRPGGameState.h"

#include "CoopRPGExtractionZone.h"

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
    DOREPLIFETIME(ACoopRPGGameState, RoundStatus);
    DOREPLIFETIME(ACoopRPGGameState, ExtractionZone);
    DOREPLIFETIME(ACoopRPGGameState, EnemyLoot);
    DOREPLIFETIME(ACoopRPGGameState, TeamLootCount);
}

void ACoopRPGGameState::InitializeLootRound(ACoopRPGSharedChest* Chest, ACoopRPGExtractionZone* Zone)
{
    if (HasAuthority() && IsValid(Chest) && !RoundId.IsValid())
    {
        SharedChest = Chest;
        ExtractionZone = Zone;
        RoundId = FGuid::NewGuid();
        UE_LOG(LogCoopRPGRound, Log, TEXT("LootRoundCreated Round=%s Chest=%s Stock=%d"),
            *RoundId.ToString(), *GetNameSafe(SharedChest), Chest->GetRecoveryStock());
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::SetRoundPlaying(bool bPlaying)
{
    if (HasAuthority() && RoundId.IsValid() && !HasRoundResult() && IsRoundPlaying() != bPlaying)
    {
        if (!bPlaying)
        {
            UpdateExtraction(false);
        }
        RoundStatus.Phase = bPlaying ? ECoopRPGRoundPhase::Playing : ECoopRPGRoundPhase::Waiting;
        UE_LOG(LogCoopRPGRound, Log, TEXT("LootRoundPhase Round=%s Phase=%s"),
            *RoundId.ToString(), bPlaying ? TEXT("Playing") : TEXT("Waiting"));
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
    if (!HasAuthority() || !IsRoundPlaying() || !IsValid(Drop) || EnemyLoot != Drop || !Drop->TryClaim())
    {
        return false;
    }

    ++TeamLootCount;
    ForceNetUpdate();
    return true;
}

void ACoopRPGGameState::UpdateExtraction(bool bEligible)
{
    if (!HasAuthority() || !IsRoundPlaying())
    {
        return;
    }

    constexpr double HoldSeconds = 3.0;
    const double Now = GetServerWorldTimeSeconds();
    double& Deadline = RoundStatus.ExtractionDeadline;
    if (!bEligible)
    {
        if (IsExtractionInProgress())
        {
            UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionReset Round=%s Time=%.3f Elapsed=%.3f"),
                *RoundId.ToString(), Now, Now - (Deadline - HoldSeconds));
            Deadline = 0.0;
            ForceNetUpdate();
        }
        return;
    }

    if (!IsExtractionInProgress())
    {
        Deadline = Now + HoldSeconds;
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionStarted Round=%s Time=%.3f Deadline=%.3f"),
            *RoundId.ToString(), Now, Deadline);
        ForceNetUpdate();
        return;
    }
    if (Now < Deadline)
    {
        return;
    }

    const double HeldSeconds = Now - (Deadline - HoldSeconds);
    RoundStatus.Phase = ECoopRPGRoundPhase::Result;
    RoundStatus.TeamScore = 1;
    Deadline = 0.0;
    UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionResult Round=%s Outcome=Success Score=%d Time=%.3f Held=%.3f"),
        *RoundId.ToString(), RoundStatus.TeamScore, Now, HeldSeconds);
    ForceNetUpdate();
}

double ACoopRPGGameState::GetExtractionRemainingSeconds() const
{
    return FMath::Max(0.0, RoundStatus.ExtractionDeadline - GetServerWorldTimeSeconds());
}

void ACoopRPGGameState::OnRep_TeamLootCount(int32 PreviousCount)
{
    UE_LOG(LogCoopRPGRound, Log, TEXT("TeamLootCountReplicated Before=%d After=%d %s"),
        PreviousCount, TeamLootCount, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGGameState::OnRep_RoundStatus(const FCoopRPGRoundStatus& PreviousStatus)
{
    const FString Context = CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__));
    if (HasRoundResult() && PreviousStatus.Phase != ECoopRPGRoundPhase::Result)
    {
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionResultReplicated Round=%s Outcome=Success Score=%d %s"),
            *RoundId.ToString(), GetTeamScore(), *Context);
    }
    else if (PreviousStatus.ExtractionDeadline != RoundStatus.ExtractionDeadline)
    {
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionProgressReplicated Round=%s Phase=%s Deadline=%.3f %s"),
            *RoundId.ToString(), IsExtractionInProgress() ? TEXT("Started") : TEXT("Reset"),
            RoundStatus.ExtractionDeadline, *Context);
    }
}
