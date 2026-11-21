#include "CoopRPGGameState.h"

#include "CoopRPGExtractionZone.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPGLootHUD.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Components/DynamicMeshComponent.h"
#include "CoreGlobals.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRound, Log, All);

namespace
{
    constexpr double RoundDurationSeconds = 300.0;
    constexpr double ExtractionHoldSeconds = 3.0;
    constexpr TCHAR FirstValidationMap[] = TEXT("/Game/Maps/L_Movement");
    constexpr TCHAR SecondValidationMap[] = TEXT("/Game/Maps/L_Movement_Round2");

    bool IsSuccessful(ECoopRPGRoundEndReason Reason)
    {
        return Reason == ECoopRPGRoundEndReason::Extracted || Reason == ECoopRPGRoundEndReason::PuzzleSolved
            || Reason == ECoopRPGRoundEndReason::BossDefeated;
    }

    const TCHAR* GetResultEvent(ECoopRPGRoundEndReason Reason, bool bReplicated)
    {
        switch (Reason)
        {
        case ECoopRPGRoundEndReason::Extracted:
            return bReplicated ? TEXT("ExtractionResultReplicated") : TEXT("ExtractionResult");
        case ECoopRPGRoundEndReason::PuzzleSolved:
            return bReplicated ? TEXT("PuzzleResultReplicated") : TEXT("PuzzleResult");
        case ECoopRPGRoundEndReason::BossDefeated:
            return bReplicated ? TEXT("ExpeditionResultReplicated") : TEXT("ExpeditionResult");
        default:
            return bReplicated ? TEXT("RoundResultReplicated") : TEXT("RoundResult");
        }
    }

    const TCHAR* GetRoundOutcome(ECoopRPGRoundEndReason Reason)
    {
        if (IsSuccessful(Reason))
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
    DOREPLIFETIME(ACoopRPGGameState, PuzzleStatus);
    DOREPLIFETIME(ACoopRPGGameState, ExtractionZone);
    DOREPLIFETIME(ACoopRPGGameState, EnemyPotion);
    DOREPLIFETIME(ACoopRPGGameState, EnemyLoot);
    DOREPLIFETIME(ACoopRPGGameState, TeamLootCount);
}

void ACoopRPGGameState::InitializeLobby()
{
    if (HasAuthority() && !RoundId.IsValid())
    {
        RoundId = FGuid::NewGuid();
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExpeditionLobbyCreated Round=%s"), *RoundId.ToString());
        ForceNetUpdate();
    }
}

void ACoopRPGGameState::InitializePuzzle(const FVector& PlateA, const FVector& PlateB)
{
    if (HasAuthority() && !RoundId.IsValid())
    {
        PuzzleStatus.PlateA = PlateA;
        PuzzleStatus.PlateB = PlateB;
        RoundId = FGuid::NewGuid();
        UE_LOG(LogCoopRPGRound, Log,
            TEXT("ExpeditionPuzzleCreated Round=%s PlateA=%s PlateB=%s Radius=%.0f TeamLoot=%d Score=%d"),
            *RoundId.ToString(), *PlateA.ToCompactString(), *PlateB.ToCompactString(),
            CoopRPG::Expedition::PuzzlePlateRadius, GetTeamLootCount(), GetTeamScore());
        ForceNetUpdate();
    }
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
            RoundStatus.ExtractionPlayersInside = 0;
            UpdateExtraction(false, Now);
        }
        const bool bTimedRound = IsValid(ExtractionZone) || CoopRPG::Expedition::IsBoss(GetWorld());
        RoundStatus.RoundDeadline = bPlaying && bTimedRound ? Now + RoundDurationSeconds : 0.0;
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
    if (!HasAuthority() || !RoundId.IsValid() || !IsRoundPlaying() || !IsValid(Drop) || !Drop->GetDropId().IsValid())
    {
        return false;
    }

    auto& Slot = Drop->IsPotion() ? EnemyPotion : EnemyLoot;
    if (Slot)
    {
        return false;
    }
    Slot = Drop;
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
    OnRep_ExtractionZone();
    ForceNetUpdate();
    return true;
}

void ACoopRPGGameState::EvaluateRound(bool bDisconnected, bool bDeath, int32 PlayersInside, bool bBossDefeated)
{
    if (!HasAuthority() || !IsRoundPlaying())
    {
        return;
    }

    const double Now = GetServerWorldTimeSeconds();
    const bool bTimeout = RoundStatus.RoundDeadline > 0.0 && Now >= RoundStatus.RoundDeadline;
    const bool bBothInside = PlayersInside == RoundCapacity;
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
    else if (bBossDefeated)
    {
        Reason = ECoopRPGRoundEndReason::BossDefeated;
    }
    else if (bExtractionReady)
    {
        Reason = ECoopRPGRoundEndReason::Extracted;
    }
    else
    {
        const int32 PreviousPlayersInside = RoundStatus.ExtractionPlayersInside;
        RoundStatus.ExtractionPlayersInside = PlayersInside;
        if (PreviousPlayersInside != PlayersInside)
        {
            UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionOccupancy Round=%s Before=%d Inside=%d"),
                *RoundId.ToString(), PreviousPlayersInside, PlayersInside);
            ForceNetUpdate();
        }
        UpdateExtraction(bBothInside, Now);
        return;
    }

    FinishRound(Reason, Now, bDisconnected, bDeath, bTimeout, bExtractionReady);
}

void ACoopRPGGameState::EvaluatePuzzle(bool bDisconnected, bool bDeath, uint8 OccupantsA, uint8 OccupantsB)
{
    if (!HasAuthority() || !IsRoundPlaying())
    {
        return;
    }

    if (PuzzleStatus.OccupantsA != OccupantsA || PuzzleStatus.OccupantsB != OccupantsB)
    {
        PuzzleStatus.OccupantsA = OccupantsA;
        PuzzleStatus.OccupantsB = OccupantsB;
        UpdatePuzzlePlatePresentation();
        UE_LOG(LogCoopRPGRound, Log, TEXT("PuzzleOccupancy Round=%s A=%u B=%u"),
            *RoundId.ToString(), OccupantsA, OccupantsB);
        ForceNetUpdate();
    }

    const bool bDifferentPlayers = (OccupantsA == 1 && OccupantsB == 2)
        || (OccupantsA == 2 && OccupantsB == 1);
    if (bDisconnected || bDeath)
    {
        EvaluateRound(bDisconnected, bDeath, 0);
    }
    else if (bDifferentPlayers)
    {
        FinishRound(ECoopRPGRoundEndReason::PuzzleSolved, GetServerWorldTimeSeconds(), false, false, false, false);
    }
}

double ACoopRPGGameState::GetRoundRemainingSeconds() const
{
    return FMath::Max(0.0, RoundStatus.RoundDeadline - GetServerWorldTimeSeconds());
}

double ACoopRPGGameState::GetExtractionRemainingSeconds() const
{
    return FMath::Max(0.0, RoundStatus.ExtractionDeadline - GetServerWorldTimeSeconds());
}

FString ACoopRPGGameState::GetRoundTravelDestination() const
{
    const FString CurrentMap = UWorld::RemovePIEPrefix(GetWorld()->GetPackage()->GetName());
    switch (GetRoundEndReason())
    {
    case ECoopRPGRoundEndReason::Death:
    case ECoopRPGRoundEndReason::Timeout:
        return CurrentMap;
    case ECoopRPGRoundEndReason::PuzzleSolved:
        return CurrentMap == CoopRPG::Expedition::PuzzlePackage ? FString(CoopRPG::Expedition::BossPackage) : FString();
    case ECoopRPGRoundEndReason::Extracted:
        if (CurrentMap == CoopRPG::Expedition::SkirmishPackage)
        {
            return CoopRPG::Expedition::PuzzlePackage;
        }
        if (CurrentMap == FirstValidationMap)
        {
            return SecondValidationMap;
        }
        if (CurrentMap == SecondValidationMap)
        {
            return FirstValidationMap;
        }
        return {};
    default:
        return {};
    }
}

void ACoopRPGGameState::FinishRound(ECoopRPGRoundEndReason Reason, double Now, bool bDisconnected,
    bool bDeath, bool bTimeout, bool bExtractionReady)
{
    const double HeldSeconds = IsExtractionInProgress()
        ? Now - (RoundStatus.ExtractionDeadline - ExtractionHoldSeconds) : 0.0;
    RoundStatus.Phase = ECoopRPGRoundPhase::Result;
    RoundStatus.EndReason = Reason;
    RoundStatus.TeamScore = IsSuccessful(Reason) ? 1 : 0;
    RoundStatus.ExtractionDeadline = 0.0;
    RoundStatus.ExtractionPlayersInside = 0;
    RoundStatus.RoundDeadline = 0.0;
    const FString ReasonName = StaticEnum<ECoopRPGRoundEndReason>()->GetNameStringByValue(static_cast<int64>(Reason));
    UE_LOG(LogCoopRPGRound, Log,
        TEXT("%s Round=%s Outcome=%s Score=%d Time=%.3f Held=%.3f Reason=%s Frame=%llu Disconnect=%d Death=%d Timeout=%d Extraction=%d"),
        GetResultEvent(Reason, false), *RoundId.ToString(), GetRoundOutcome(Reason),
        RoundStatus.TeamScore, Now, HeldSeconds, *ReasonName, static_cast<unsigned long long>(GFrameCounter),
        bDisconnected, bDeath, bTimeout, bExtractionReady);
    ForceNetUpdate();
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

void ACoopRPGGameState::OnRep_ExtractionZone()
{
    if (IsValid(ExtractionZone))
    {
        const bool bActive = TeamLootCount > 0;
        if (bActive)
        {
            ExtractionZone->OpenPortal();
        }
        ExtractionZone->PresentSkirmishPortal(bActive, bActive && IsRoundPlaying() && IsExtractionInProgress());
    }
}

void ACoopRPGGameState::OnRep_TeamLootCount(int32 PreviousCount)
{
    OnRep_ExtractionZone();
    UE_LOG(LogCoopRPGRound, Log, TEXT("TeamLootCountReplicated Before=%d After=%d %s"),
        PreviousCount, TeamLootCount, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGGameState::OnRep_RoundStatus(const FCoopRPGRoundStatus& PreviousStatus)
{
    OnRep_ExtractionZone();
    const FString Context = CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__));
    if (HasRoundResult() && PreviousStatus.Phase != ECoopRPGRoundPhase::Result)
    {
        const ECoopRPGRoundEndReason Reason = GetRoundEndReason();
        const FString ReasonName = StaticEnum<ECoopRPGRoundEndReason>()->GetNameStringByValue(static_cast<int64>(Reason));
        UE_LOG(LogCoopRPGRound, Log, TEXT("%s Round=%s Outcome=%s Score=%d Reason=%s %s"),
            GetResultEvent(Reason, true), *RoundId.ToString(), GetRoundOutcome(Reason), GetTeamScore(), *ReasonName, *Context);
    }
    else if (PreviousStatus.ExtractionPlayersInside != RoundStatus.ExtractionPlayersInside)
    {
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionOccupancyReplicated Round=%s Before=%d Inside=%d %s"),
            *RoundId.ToString(), PreviousStatus.ExtractionPlayersInside, RoundStatus.ExtractionPlayersInside, *Context);
    }
    if (!HasRoundResult() && PreviousStatus.ExtractionDeadline != RoundStatus.ExtractionDeadline)
    {
        UE_LOG(LogCoopRPGRound, Log, TEXT("ExtractionProgressReplicated Round=%s Phase=%s Deadline=%.3f %s"),
            *RoundId.ToString(), IsExtractionInProgress() ? TEXT("Started") : TEXT("Reset"),
            RoundStatus.ExtractionDeadline, *Context);
    }
}

void ACoopRPGGameState::OnRep_PuzzleStatus()
{
    UE_LOG(LogCoopRPGRound, Log, TEXT("PuzzleOccupancyReplicated Round=%s A=%u B=%u"),
        *RoundId.ToString(), PuzzleStatus.OccupantsA, PuzzleStatus.OccupantsB);
    UpdatePuzzlePlatePresentation();
}

void ACoopRPGGameState::UpdatePuzzlePlatePresentation()
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    if (!PuzzlePlateA && PuzzleStatus.OccupantsA)
    {
        PuzzlePlateA = CreatePuzzlePlate(TEXT("PuzzlePlateA"));
    }
    if (!PuzzlePlateB && PuzzleStatus.OccupantsB)
    {
        PuzzlePlateB = CreatePuzzlePlate(TEXT("PuzzlePlateB"));
    }
    UpdatePuzzlePlate(PuzzlePlateA, PuzzleStatus.OccupantsA);
    UpdatePuzzlePlate(PuzzlePlateB, PuzzleStatus.OccupantsB);
}

UDynamicMeshComponent* ACoopRPGGameState::CreatePuzzlePlate(FName PlateTag)
{
    TArray<AActor*> Plates;
    UGameplayStatics::GetAllActorsWithTag(this, PlateTag, Plates);
    if (Plates.Num() != 1 || !Plates[0]->GetRootComponent())
    {
        return nullptr;
    }
    AActor* PlateActor = Plates[0];
    FVector PlateOrigin, Extent;
    PlateActor->GetActorBounds(false, PlateOrigin, Extent);

    // Reuse the unlit, tintable floor marker material on top of the existing gray plate.
    auto* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Presentation/Materials/M_EnemyGroundRing.M_EnemyGroundRing"));
    if (!Material)
    {
        UE_LOG(LogCoopRPGRound, Warning, TEXT("Puzzle plate could not load the tint material"));
        return nullptr;
    }

    using namespace UE::Geometry;
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnableMaterialID();
    Mesh.EnableVertexNormals(FVector3f(0.0f, 0.0f, 1.0f));

    constexpr int32 SegmentsPerHalf = 24;
    constexpr double Radius = CoopRPG::Expedition::PuzzlePlateRadius - 3.0;
    const int32 Origin = Mesh.AppendVertex(FVector::ZeroVector);
    int32 Previous = Mesh.AppendVertex(FVector(Radius, 0.0, 0.0));
    const int32 Start = Previous;
    for (int32 Index = 1; Index <= 2 * SegmentsPerHalf; ++Index)
    {
        const double Angle = UE_DOUBLE_PI * Index / SegmentsPerHalf;
        const int32 Next = Index == 2 * SegmentsPerHalf ? Start
            : Mesh.AppendVertex(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0));
        const int32 Triangle = Mesh.AppendTriangle(Origin, Next, Previous);
        Mesh.Attributes()->GetMaterialID()->SetValue(Triangle, Index <= SegmentsPerHalf ? 0 : 1);
        Previous = Next;
    }
    FMeshNormals::InitializeOverlayToPerVertexNormals(Mesh.Attributes()->PrimaryNormals());

    auto* Plate = NewObject<UDynamicMeshComponent>(PlateActor);
    Plate->SetupAttachment(PlateActor->GetRootComponent());
    Plate->SetAbsolute(true, true, true);
    Plate->SetWorldLocation(PlateOrigin + FVector(0.0, 0.0, Extent.Z + 0.75));
    Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Plate->SetGenerateOverlapEvents(false);
    Plate->SetCanEverAffectNavigation(false);
    Plate->SetCastShadow(false);
    Plate->SetReceivesDecals(false);
    Plate->SetVisibility(false);
    Plate->ConfigureMaterialSet({
        UMaterialInstanceDynamic::Create(Material, Plate),
        UMaterialInstanceDynamic::Create(Material, Plate)});
    Plate->SetMesh(MoveTemp(Mesh));
    Plate->RegisterComponent();
    return Plate;
}

void ACoopRPGGameState::UpdatePuzzlePlate(UDynamicMeshComponent* Plate, uint8 Occupants) const
{
    if (!Plate)
    {
        return;
    }

    Plate->SetVisibility(Occupants != 0);
    if (Occupants)
    {
        const bool bP1 = (Occupants & 1) != 0;
        const bool bP2 = (Occupants & 2) != 0;
        CastChecked<UMaterialInstanceDynamic>(Plate->GetMaterial(0))->SetVectorParameterValue(
            TEXT("Color"), CoopRPG::Presentation::PlayerColor(bP1 ? 1 : 2));
        CastChecked<UMaterialInstanceDynamic>(Plate->GetMaterial(1))->SetVectorParameterValue(
            TEXT("Color"), CoopRPG::Presentation::PlayerColor(bP2 ? 2 : 1));
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
