#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CoopRPGGameState.generated.h"

class ACoopRPGSharedChest;

UCLASS()
class COOPRPG_API ACoopRPGGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void InitializeLootRound(ACoopRPGSharedChest* Chest);
    void SetRoundPlaying(bool bPlaying);

    ACoopRPGSharedChest* GetSharedChest() const { return SharedChest.Get(); }
    FGuid GetRoundId() const { return RoundId; }
    bool IsRoundPlaying() const { return bRoundPlaying; }

private:
    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGSharedChest> SharedChest;

    UPROPERTY(Replicated)
    FGuid RoundId;

    UPROPERTY(Replicated)
    bool bRoundPlaying = false;
};
