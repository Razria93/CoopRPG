#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "CoopRPGPlayerState.generated.h"

UENUM()
enum class ECoopRPGHero : uint8
{
    Unassigned,
    Adventurer,
    AnimalHero
};

UCLASS()
class COOPRPG_API ACoopRPGPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    ECoopRPGHero GetSelectedHero() const { return SelectedHero; }
    void SetSelectedHero(ECoopRPGHero Hero);

private:
    UFUNCTION()
    void OnRep_SelectedHero();

private:
    UPROPERTY(ReplicatedUsing = OnRep_SelectedHero)
    ECoopRPGHero SelectedHero = ECoopRPGHero::Unassigned;
};
