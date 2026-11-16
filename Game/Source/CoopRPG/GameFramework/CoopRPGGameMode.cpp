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
#include "Kismet/GameplayStatics.h"

ACoopRPGGameMode::ACoopRPGGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
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
    int32 Players = 0;
    int32 ReadyPlayers = 0;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const auto* Player = Cast<ACoopRPGPlayerController>(It->Get()); Player && Player != Leaving && Player->GetPawn())
        {
            ++Players;
            ReadyPlayers += Player->IsRoundReady() ? 1 : 0;
        }
    }
    if (auto* State = GetGameState<ACoopRPGGameState>())
    {
        State->SetRoundPlaying(Players == 2 && ReadyPlayers == 2);
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
    if (GetWorld()->GetMapName().EndsWith(TEXT("L_Movement")))
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
    const ACoopRPGExtractionZone* Zone = State ? State->GetExtractionZone() : nullptr;
    if (!IsValid(Zone) || !State->IsRoundPlaying() || State->GetTeamLootCount() < 1)
    {
        return;
    }

    int32 PlayersInside = 0;
    int32 Players = 0;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const auto* Player = Cast<ACoopRPGPlayerController>(It->Get());
        if (Player)
        {
            ++Players;
            const auto* Pawn = Cast<ACoopRPGCharacter>(Player->GetPawn());
            if (Pawn && Pawn->GetHealth() > 0.0f && Zone->ContainsPawn(*Pawn))
            {
                ++PlayersInside;
            }
        }
    }
    State->UpdateExtraction(Players == 2 && PlayersInside == 2);
}

void ACoopRPGGameMode::Logout(AController* Exiting)
{
    Super::Logout(Exiting);
    UpdateLootRoundPhase(Exiting);
}
