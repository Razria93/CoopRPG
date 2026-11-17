#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CoopRPGGameMode.generated.h"

class ACoopRPGAttackTarget;

UCLASS()
class COOPRPG_API ACoopRPGGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ACoopRPGGameMode();

    ACoopRPGAttackTarget* GetAttackTarget() const;
    void UpdateLootRoundPhase(const AController* Leaving = nullptr);

protected:
    virtual void BeginPlay() override;
    virtual void Logout(AController* Exiting) override;

private:
    UPROPERTY()
    TObjectPtr<ACoopRPGAttackTarget> AttackTarget;
};
