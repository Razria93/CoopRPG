#include "CoopRPGGameMode.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Combat/CoopRPGAttackTarget.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"
#include "CoopRPGGameState.h"
#include "CoopRPGLootHUD.h"
#include "CoopRPGPlayerController.h"

#include "Engine/World.h"

ACoopRPGGameMode::ACoopRPGGameMode()
{
    DefaultPawnClass = ACoopRPGCharacter::StaticClass();
    PlayerControllerClass = ACoopRPGPlayerController::StaticClass();
    GameStateClass = ACoopRPGGameState::StaticClass();
    HUDClass = ACoopRPGLootHUD::StaticClass();
}

ACoopRPGAttackTarget* ACoopRPGGameMode::GetAttackTarget() const
{
    return AttackTarget;
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

void ACoopRPGGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (GetWorld()->GetMapName().EndsWith(TEXT("L_Movement")))
    {
        // Fixed position on the movement map, outside the starting attack range.
        AttackTarget = GetWorld()->SpawnActor<ACoopRPGAttackTarget>(FVector(350.0f, 35.0f, 60.0f), FRotator::ZeroRotator);
        if (auto* State = GetGameState<ACoopRPGGameState>())
        {
            auto* Chest = GetWorld()->SpawnActor<ACoopRPGSharedChest>(FVector(300.0f, -95.0f, 28.0f), FRotator::ZeroRotator);
            State->InitializeLootRound(Chest);
        }
    }
}

void ACoopRPGGameMode::Logout(AController* Exiting)
{
    Super::Logout(Exiting);
    UpdateLootRoundPhase(Exiting);
}
