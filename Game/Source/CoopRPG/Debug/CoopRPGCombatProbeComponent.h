#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CoopRPGCombatProbeComponent.generated.h"

class AController;
class AGameModeBase;

// Development-only commands require -CoopCombatTest on both client and server.
UCLASS()
class COOPRPG_API UCoopRPGCombatProbeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCoopRPGCombatProbeComponent();

    void Prepare(float Stamina);
    void RequestActivation();
    void Snapshot(const FString& Label);
    void SetVitals(float TargetHealth, float AttackerHealth);
    void ConfirmTwice();
    void PrepareMeleeLayout(FName Layout);
    void PositionForEnemyLoot(bool bNear);
    void PositionForExtraction(bool bInside);
    void PrepareRoundEnd(FName Scenario);
    void SnapshotRound(FName Label);
    void RequestStalePickup(FGuid RoundId);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION(Server, Reliable)
    void ServerPrepare(float Stamina);

    UFUNCTION(Server, Reliable)
    void ServerSnapshot(const FString& Label);

    UFUNCTION(Server, Reliable)
    void ServerSetVitals(float TargetHealth, float AttackerHealth);

    UFUNCTION(Server, Reliable)
    void ServerConfirmTwice();

    UFUNCTION(Server, Reliable)
    void ServerPrepareMeleeLayout(FName Layout);

    UFUNCTION(Server, Reliable)
    void ServerPositionForEnemyLoot(bool bNear);

    UFUNCTION(Server, Reliable)
    void ServerPositionForExtraction(bool bInside);

    UFUNCTION(Server, Reliable)
    void ServerPrepareRoundEnd(FName Scenario);

    UFUNCTION(Server, Reliable)
    void ServerSnapshotRound(FName Label);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    void OnPeerLogout(AGameModeBase* Mode, AController* Exiting);
#endif

    TWeakObjectPtr<AActor> MeleeTestObstacle;

    void OnCooldownChanged(FGameplayTag Tag, int32 Count);
    void WriteSnapshot(const FString& Label) const;
    void WriteRoundSnapshot(FName Label) const;
};
