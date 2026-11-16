#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CoopRPGGameState.generated.h"

class ACoopRPGEnemyLoot;
class ACoopRPGSharedChest;

UCLASS()
class COOPRPG_API ACoopRPGGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void InitializeLootRound(ACoopRPGSharedChest* Chest);
    void SetRoundPlaying(bool bPlaying);
    bool RegisterEnemyLoot(ACoopRPGEnemyLoot* Drop);
    bool TryGrantEnemyLoot(ACoopRPGEnemyLoot* Drop);

    ACoopRPGSharedChest* GetSharedChest() const { return SharedChest.Get(); }
    FGuid GetRoundId() const { return RoundId; }
    bool IsRoundPlaying() const { return bRoundPlaying; }
    ACoopRPGEnemyLoot* GetEnemyLoot() const { return EnemyLoot.Get(); }
    int32 GetTeamLootCount() const { return TeamLootCount; }

private:
    UFUNCTION()
    void OnRep_TeamLootCount(int32 PreviousCount);

private:
    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGSharedChest> SharedChest;

    UPROPERTY(Replicated)
    FGuid RoundId;

    UPROPERTY(Replicated)
    bool bRoundPlaying = false;

    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGEnemyLoot> EnemyLoot;

    UPROPERTY(ReplicatedUsing = OnRep_TeamLootCount)
    int32 TeamLootCount = 0;
};
