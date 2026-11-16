#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CoopRPGGameMode.generated.h"

class ACoopRPGAttackTarget;
class ACoopRPGEnemy;

UCLASS()
class COOPRPG_API ACoopRPGGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ACoopRPGGameMode();

    ACoopRPGAttackTarget* GetAttackTarget() const;
    AActor* GetMeleeTarget() const;
    void UpdateLootRoundPhase(const AController* Leaving = nullptr);

protected:
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void Logout(AController* Exiting) override;

private:
    UPROPERTY()
    TObjectPtr<ACoopRPGAttackTarget> AttackTarget;

    UPROPERTY()
    TObjectPtr<ACoopRPGEnemy> Enemy;

    bool bEnemyCombat = false;
};
