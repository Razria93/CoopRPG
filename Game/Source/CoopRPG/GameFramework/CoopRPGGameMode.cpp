#include "CoopRPGGameMode.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"

ACoopRPGGameMode::ACoopRPGGameMode()
{
    DefaultPawnClass = ACoopRPGCharacter::StaticClass();
}
