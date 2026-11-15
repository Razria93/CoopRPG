#include "CoopRPGGameMode.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Combat/CoopRPGAttackTarget.h"

#include "Engine/World.h"

ACoopRPGGameMode::ACoopRPGGameMode()
{
    DefaultPawnClass = ACoopRPGCharacter::StaticClass();
}

ACoopRPGAttackTarget* ACoopRPGGameMode::GetAttackTarget() const
{
    return AttackTarget;
}

void ACoopRPGGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (GetWorld()->GetMapName().EndsWith(TEXT("L_Movement")))
    {
        // Fixed position on the movement map, outside the starting attack range.
        AttackTarget = GetWorld()->SpawnActor<ACoopRPGAttackTarget>(FVector(350.0f, 35.0f, 60.0f), FRotator::ZeroRotator);
    }
}
