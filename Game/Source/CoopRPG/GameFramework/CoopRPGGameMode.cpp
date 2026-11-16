#include "CoopRPGGameMode.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"
#include "CoopRPGExtractionZone.h"
#include "CoopRPGGameState.h"
#include "CoopRPGLootHUD.h"
#include "CoopRPGPlayerController.h"

#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGRoundTravel, Log, All);

namespace
{
    constexpr TCHAR FirstMapPackage[] = TEXT("/Game/Maps/L_Movement");
    constexpr TCHAR SecondMapPackage[] = TEXT("/Game/Maps/L_Movement_Round2");
    constexpr double ResultDisplaySeconds = 3.0;

    FString GetNextRoundMap(const FString& CurrentMap)
    {
        return CurrentMap.EndsWith(TEXT("L_Movement_Round2")) ? FirstMapPackage : SecondMapPackage;
    }
}

ACoopRPGGameMode::ACoopRPGGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    // Evaluate after combat timers and movement have updated the authoritative facts.
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    DefaultPawnClass = ACoopRPGCharacter::StaticClass();
    PlayerControllerClass = ACoopRPGPlayerController::StaticClass();
    GameStateClass = ACoopRPGGameState::StaticClass();
    HUDClass = ACoopRPGLootHUD::StaticClass();
}

ACoopRPGAttackTarget* ACoopRPGGameMode::GetAttackTarget() const
{
    return AttackTarget;
}

AActor* ACoopRPGGameMode::GetMeleeTarget() const
{
    if (!bEnemyCombat)
    {
        return AttackTarget.Get();
    }
    const auto* State = GetGameState<ACoopRPGGameState>();
    return State && State->IsRoundPlaying() && IsValid(Enemy.Get()) && !Enemy->IsDead() ? Enemy.Get() : nullptr;
}

void ACoopRPGGameMode::UpdateLootRoundPhase(const AController* Leaving)
{
    auto* State = GetGameState<ACoopRPGGameState>();
    if (!State || State->HasRoundResult() || (bEnemyCombat && State->IsRoundPlaying()))
    {
        return;
    }

    int32 Players = 0;
    TArray<TWeakObjectPtr<ACoopRPGPlayerController>> ReadyPlayers;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (auto* Player = Cast<ACoopRPGPlayerController>(It->Get()); Player && Player != Leaving && Player->GetPawn())
        {
            ++Players;
            if (Player->IsRoundReady())
            {
                ReadyPlayers.Add(Player);
            }
        }
    }
    const bool bAllReady = Players == 2 && ReadyPlayers.Num() == 2;
    if (bEnemyCombat && bAllReady)
    {
        RoundPlayers = MoveTemp(ReadyPlayers);
        bParticipantDisconnected = false;
    }
    State->SetRoundPlaying(bAllReady);
}

void ACoopRPGGameMode::AcknowledgeRoundResult(ACoopRPGPlayerController* Player, FGuid RoundId)
{
    const auto* State = GetGameState<ACoopRPGGameState>();
    if (!bEnemyCombat || !State || !State->HasRoundResult() || State->GetRoundId() != RoundId
        || bParticipantDisconnected || bTravelAttempted || !IsRoundParticipant(Player))
    {
        return;
    }

    const int32 PreviousCount = ResultAcknowledgements.Num();
    ResultAcknowledgements.AddUnique(Player);
    const int32 Count = ResultAcknowledgements.Num();
    if (Count == PreviousCount)
    {
        return;
    }

    const APlayerState* PlayerState = Player->GetPlayerState<APlayerState>();
    UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("RoundResultAcknowledged Round=%s PlayerId=%d Count=%d"),
        *RoundId.ToString(), PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE, Count);
    if (Count == 2)
    {
        NextTravelTime = State->GetServerWorldTimeSeconds() + ResultDisplaySeconds;
    }
}

void ACoopRPGGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    bEnemyCombat = UGameplayStatics::HasOption(Options, TEXT("EnemyCombat"));
}

void ACoopRPGGameMode::BeginPlay()
{
    Super::BeginPlay();
    const FString MapName = UGameplayStatics::GetCurrentLevelName(this);
    if (MapName == TEXT("L_Movement") || MapName == TEXT("L_Movement_Round2"))
    {
        ACoopRPGExtractionZone* Zone = nullptr;
        if (bEnemyCombat)
        {
            Zone = GetWorld()->SpawnActor<ACoopRPGExtractionZone>(FVector(620.0f, 350.0f, 6.0f), FRotator::ZeroRotator);
            Enemy = GetWorld()->SpawnActor<ACoopRPGEnemy>(FVector(750.0f, -95.0f, 90.0f), FRotator::ZeroRotator);
        }
        else
        {
            AttackTarget = GetWorld()->SpawnActor<ACoopRPGAttackTarget>(FVector(350.0f, 35.0f, 60.0f), FRotator::ZeroRotator);
        }
        if (auto* State = GetGameState<ACoopRPGGameState>())
        {
            auto* Chest = GetWorld()->SpawnActor<ACoopRPGSharedChest>(FVector(300.0f, -95.0f, 28.0f), FRotator::ZeroRotator);
            State->InitializeLootRound(Chest, Zone);
        }
    }
}

void ACoopRPGGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    auto* State = GetGameState<ACoopRPGGameState>();
    if (!bEnemyCombat || !State)
    {
        return;
    }
    if (State->HasRoundResult())
    {
        AdvanceRoundIfReady(*State);
        return;
    }
    if (!State->IsRoundPlaying())
    {
        return;
    }

    const ACoopRPGExtractionZone* Zone = State->GetExtractionZone();
    const bool bCanUseZone = IsValid(Zone) && State->GetTeamLootCount() >= 1;
    bool bDeath = false;
    int32 PlayersInside = 0;
    for (const TWeakObjectPtr<ACoopRPGPlayerController>& Participant : RoundPlayers)
    {
        const ACoopRPGPlayerController* Player = Participant.Get();
        if (!Player)
        {
            bParticipantDisconnected = true;
            continue;
        }

        const auto* Pawn = Cast<ACoopRPGCharacter>(Player->GetPawn());
        bDeath |= !Pawn || Pawn->GetHealth() <= 0.0f;
        if (bCanUseZone && Pawn && Zone->ContainsPawn(*Pawn))
        {
            ++PlayersInside;
        }
    }
    State->EvaluateRound(bParticipantDisconnected, bDeath, PlayersInside == 2);
}

void ACoopRPGGameMode::Logout(AController* Exiting)
{
    const auto* State = GetGameState<ACoopRPGGameState>();
    if (bEnemyCombat && State && (State->IsRoundPlaying() || State->HasRoundResult())
        && IsRoundParticipant(Exiting))
    {
        bParticipantDisconnected = true;
    }
    Super::Logout(Exiting);
    UpdateLootRoundPhase(Exiting);
}

bool ACoopRPGGameMode::IsRoundParticipant(const AController* Controller) const
{
    return Controller && RoundPlayers.ContainsByPredicate(
        [Controller](const auto& Player) { return Player.Get() == Controller; });
}

void ACoopRPGGameMode::AdvanceRoundIfReady(const ACoopRPGGameState& State)
{
    if (bTravelAttempted || bParticipantDisconnected || NextTravelTime <= 0.0
        || State.GetRoundEndReason() == ECoopRPGRoundEndReason::Disconnected
        || State.GetServerWorldTimeSeconds() < NextTravelTime
        || RoundPlayers.ContainsByPredicate([](const auto& Player) { return !Player.IsValid(); }))
    {
        return;
    }

    bTravelAttempted = true;
    const FString Destination = GetNextRoundMap(UGameplayStatics::GetCurrentLevelName(this));
    const FString URL = Destination + TEXT("?EnemyCombat?NoSeamlessTravel");
    UE_LOG(LogCoopRPGRoundTravel, Log, TEXT("RoundTravelStarted Round=%s TargetMap=%s"),
        *State.GetRoundId().ToString(), *Destination);
    if (!GetWorld()->ServerTravel(URL, true))
    {
        UE_LOG(LogCoopRPGRoundTravel, Error, TEXT("RoundTravelFailed Round=%s TargetMap=%s"),
            *State.GetRoundId().ToString(), *Destination);
    }
}
