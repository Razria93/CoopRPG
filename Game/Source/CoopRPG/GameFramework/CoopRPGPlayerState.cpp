#include "CoopRPGPlayerState.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "Net/UnrealNetwork.h"

void ACoopRPGPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGPlayerState, SelectedHero);
    DOREPLIFETIME(ACoopRPGPlayerState, PartyRecoveryCount);
}

void ACoopRPGPlayerState::SetSelectedHero(ECoopRPGHero Hero)
{
    if (HasAuthority() && SelectedHero != Hero)
    {
        SelectedHero = Hero;
        OnRep_SelectedHero();
        ForceNetUpdate();
    }
}

void ACoopRPGPlayerState::SetPartyRecoveryCount(int32 Count)
{
    if (HasAuthority() && PartyRecoveryCount != Count)
    {
        PartyRecoveryCount = Count;
        ForceNetUpdate();
    }
}

void ACoopRPGPlayerState::OnRep_SelectedHero()
{
    if (auto* Character = Cast<ACoopRPGCharacter>(GetPawn()))
    {
        Character->RefreshHeroAppearance();
    }
}
