#include "CoopRPGGameState.h"

#include "CoopRPGExtractionZone.h"
#include "CoopRPGLootHUD.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "CoreGlobals.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRound, Log, All);

namespace
{
    constexpr double RoundDurationSeconds = 300.0;
    constexpr double ExtractionHoldSeconds = 3.0;

    const TCHAR* GetRoundOutcome(ECoopRPGRoundEndReason Reason)
    {
        if (Reason == ECoopRPGRoundEndReason::Extracted)
        {
            return TEXT("Success");
        }
        return Reason == ECoopRPGRoundEndReason::Disconnected ? TEXT("Aborted") : TEXT("Failed");
    }
}

void ACoopRPGGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGGameState, SharedChest);
    DOREPLIFETIME(ACoopRPGGameState, RoundId);
    DOREPLIFETIME(ACoopRPGGameState, RoundStatus);
    DOREPLIFETIME(ACoopRPGGameState, ReadySlots);
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
        UE_LOG(LogCoopRPGRound, Log, TEXT("LootRoundCreated Round=%s Chest=%s Stock=%d Map=%s"),
            *RoundId.ToString(), *GetNameSafe(SharedChest), Chest->GetRecoveryStock(),
            *UGameplayStatics::GetCurrentLevelName(this));
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::SetRoundPlaying(bool bPlaying)
{
    if (HasAuthority() && RoundId.IsValid() && !HasRoundResult() && IsRoundPlaying() != bPlaying)
    {
        const double Now = GetServerWorldTimeSeconds();
        if (!bPlaying)
        {
            UpdateExtraction(false, Now);
        }
        RoundStatus.RoundDeadline = bPlaying && IsValid(ExtractionZone) ? Now + RoundDurationSeconds : 0.0;
        if (RoundStatus.RoundDeadline > 0.0)
        {
            UE_LOG(LogCoopRPGRound, Log, TEXT("RoundClockStarted Round=%s Time=%.3f Deadline=%.3f Duration=%.0f"),
                *RoundId.ToString(), Now, RoundStatus.RoundDeadline, RoundDurationSeconds);
        }
        RoundStatus.Phase = bPlaying ? ECoopRPGRoundPhase::Playing : ECoopRPGRoundPhase::Waiting;
        UE_LOG(LogCoopRPGRound, Log, TEXT("LootRoundPhase Round=%s Phase=%s"),
            *RoundId.ToString(), bPlaying ? TEXT("Playing") : TEXT("Waiting"));
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::MulticastGameplayFeedback_Implementation(const FCoopRPGGameplayFeedback& Feedback)
{
    if (GetNetMode() != NM_DedicatedServer)
    {
        const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
        if (auto* HUD = Controller ? Cast<ACoopRPGLootHUD>(Controller->GetHUD()) : nullptr)
        {
            HUD->ShowGameplayFeedback(Feedback);
        }
    }
}

void ACoopRPGGameState::MulticastMeleeSweepTrace_Implementation(const FCoopRPGMeleeSweepTrace& Trace)
{
#if ENABLE_DRAW_DEBUG && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (GetNetMode() == NM_DedicatedServer || !FParse::Param(FCommandLine::Get(), TEXT("CoopMeleeSweepDebug")))
    {
        return;
    }

    const FVector Delta = Trace.End - Trace.Start;
    const double HalfLength = Delta.Size() * 0.5;
    const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal());
    const bool bHit = Trace.Result == ECoopRPGMeleeSweepResult::Hit;
    const bool bObstructed = Trace.Result == ECoopRPGMeleeSweepResult::Obstructed;
    const FColor Color = bHit ? FColor::Cyan : bObstructed ? FColor::Orange : FColor::Red;
    constexpr float Lifetime = 1.25f;
    DrawDebugCapsule(GetWorld(), Trace.Start + Delta * 0.5, HalfLength + Trace.Radius, Trace.Radius,
        Rotation, Color, false, Lifetime, SDPG_Foreground, 1.4f);
    DrawDebugDirectionalArrow(GetWorld(), Trace.Start, Trace.End, 18.0f, Color, false, Lifetime, SDPG_Foreground, 2.0f);
    if (bHit || bObstructed)
    {
        // Time is the sweep fraction at the winning contact. The marker is the sphere center at that fraction.
        DrawDebugPoint(GetWorld(), FMath::Lerp(Trace.Start, Trace.End, Trace.ContactTime), 10.0f,
            Color, false, Lifetime, SDPG_Foreground);
    }
    UE_LOG(LogCoopRPGRound, Log, TEXT("MeleeSweepDrawn Slot=%d Swing=%u Start=%s End=%s Radius=%.2f Result=%s Contact=%.4f"),
        Trace.Slot, Trace.Swing, *Trace.Start.ToCompactString(), *Trace.End.ToCompactString(), Trace.Radius,
        *StaticEnum<ECoopRPGMeleeSweepResult>()->GetNameStringByValue(static_cast<int64>(Trace.Result)), Trace.ContactTime);
#endif
}

void ACoopRPGGameState::SetReadySlots(const FCoopRPGReadySlots& Slots)
{
    if (HasAuthority() && (Slots.PlayerOne != ReadySlots.PlayerOne || Slots.PlayerTwo != ReadySlots.PlayerTwo))
    {
        ReadySlots = Slots;
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

void ACoopRPGGameState::EvaluateRound(bool bDisconnected, bool bDeath, bool bBothInside)
{
    if (!HasAuthority() || !IsRoundPlaying())
    {
        return;
    }

    const double Now = GetServerWorldTimeSeconds();
    const bool bTimeout = RoundStatus.RoundDeadline > 0.0 && Now >= RoundStatus.RoundDeadline;
    const bool bExtractionReady = bBothInside && IsExtractionInProgress() && Now >= RoundStatus.ExtractionDeadline;
    ECoopRPGRoundEndReason Reason = ECoopRPGRoundEndReason::None;
    if (bDisconnected)
    {
        Reason = ECoopRPGRoundEndReason::Disconnected;
    }
    else if (bDeath)
    {
        Reason = ECoopRPGRoundEndReason::Death;
    }
    else if (bTimeout)
    {
        Reason = ECoopRPGRoundEndReason::Timeout;
    }
    else if (bExtractionReady)
    {
        Reason = ECoopRPGRoundEndReason::Extracted;
    }
    else
    {
        UpdateExtraction(bBothInside, Now);
        return;
    }

    const bool bSuccess = Reason == ECoopRPGRoundEndReason::Extracted;
    const double HeldSeconds = IsExtractionInProgress()
        ? Now - (RoundStatus.ExtractionDeadline - ExtractionHoldSeconds) : 0.0;
    RoundStatus.Phase = ECoopRPGRoundPhase::Result;
    RoundStatus.EndReason = Reason;
    RoundStatus.TeamScore = bSuccess ? 1 : 0;
    RoundStatus.ExtractionDeadline = 0.0;
    RoundStatus.RoundDeadline = 0.0;
    const FString ReasonName = StaticEnum<ECoopRPGRoundEndReason>()->GetNameStringByValue(static_cast<int64>(Reason));
    UE_LOG(LogCoopRPGRound, Log,
        TEXT("%s Round=%s Outcome=%s Score=%d Time=%.3f Held=%.3f Reason=%s Frame=%llu Disconnect=%d Death=%d Timeout=%d Extraction=%d"),
        bSuccess ? TEXT("ExtractionResult") : TEXT("RoundResult"), *RoundId.ToString(), GetRoundOutcome(Reason),
        RoundStatus.TeamScore, Now, HeldSeconds, *ReasonName, static_cast<unsigned long long>(GFrameCounter),
        bDisconnected, bDeath, bTimeout, bExtractionReady);
    ForceNetUpdate();
}

double ACoopRPGGameState::GetRoundRemainingSeconds() const
{
    return FMath::Max(0.0, RoundStatus.RoundDeadline - GetServerWorldTimeSeconds());
}

double ACoopRPGGameState::GetExtractionRemainingSeconds() const
{
    return FMath::Max(0.0, RoundStatus.ExtractionDeadline - GetServerWorldTimeSeconds());
}

void ACoopRPGGameState::UpdateExtraction(bool bEligible, double Now)
{
    double& Deadline = RoundStatus.ExtractionDeadline;
    if (!bEligible)
    {
        if (IsExtractionInProgress())
        {
            UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionReset Round=%s Time=%.3f Elapsed=%.3f"),
                *RoundId.ToString(), Now, Now - (Deadline - ExtractionHoldSeconds));
            Deadline = 0.0;
            ForceNetUpdate();
        }
        return;
    }

    if (!IsExtractionInProgress())
    {
        Deadline = Now + ExtractionHoldSeconds;
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionStarted Round=%s Time=%.3f Deadline=%.3f"),
            *RoundId.ToString(), Now, Deadline);
        ForceNetUpdate();
    }
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
        const ECoopRPGRoundEndReason Reason = GetRoundEndReason();
        const bool bSuccess = Reason == ECoopRPGRoundEndReason::Extracted;
        const FString ReasonName = StaticEnum<ECoopRPGRoundEndReason>()->GetNameStringByValue(static_cast<int64>(Reason));
        UE_LOG(LogCoopRPGRound, Log, TEXT("%s Round=%s Outcome=%s Score=%d Reason=%s %s"),
            bSuccess ? TEXT("ExtractionResultReplicated") : TEXT("RoundResultReplicated"),
            *RoundId.ToString(), GetRoundOutcome(Reason), GetTeamScore(), *ReasonName, *Context);
    }
    else if (PreviousStatus.ExtractionDeadline != RoundStatus.ExtractionDeadline)
    {
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionProgressReplicated Round=%s Phase=%s Deadline=%.3f %s"),
            *RoundId.ToString(), IsExtractionInProgress() ? TEXT("Started") : TEXT("Reset"),
            RoundStatus.ExtractionDeadline, *Context);
    }
}

void ACoopRPGGameState::OnRep_ReadySlots()
{
    const auto* StatusEnum = StaticEnum<ECoopRPGReadyStatus>();
    UE_LOG(LogCoopRPGRound, Log, TEXT("RoundReadySlotsReplicated Round=%s P1=%s P2=%s %s"),
        *RoundId.ToString(), *StatusEnum->GetNameStringByValue(static_cast<int64>(ReadySlots.PlayerOne)),
        *StatusEnum->GetNameStringByValue(static_cast<int64>(ReadySlots.PlayerTwo)),
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}
