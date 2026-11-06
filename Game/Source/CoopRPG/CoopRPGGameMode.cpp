#include "CoopRPGGameMode.h"

#include "CoopRPGCharacter.h"

ACoopRPGGameMode::ACoopRPGGameMode()
{
    DefaultPawnClass = ACoopRPGCharacter::StaticClass();
}
