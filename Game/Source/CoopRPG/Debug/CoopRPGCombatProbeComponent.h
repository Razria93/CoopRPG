#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CoopRPGCombatProbeComponent.generated.h"

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
    void PositionForEnemyLoot(bool bNear);
    void PositionForExtraction(bool bInside);

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
    void ServerPositionForEnemyLoot(bool bNear);

    UFUNCTION(Server, Reliable)
    void ServerPositionForExtraction(bool bInside);

    void OnCooldownChanged(FGameplayTag Tag, int32 Count);
    void WriteSnapshot(const FString& Label) const;
};
