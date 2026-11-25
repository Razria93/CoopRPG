#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/OnlineReplStructs.h"
#include "CoopRPGPuzzleReward.h"
#include "CoopRPGSwordInventory.h"
#include "CoopRPGGameMode.generated.h"

class ACoopRPGAttackTarget;
class ACoopRPGEnemy;
class ACoopRPGGameState;
class ACoopRPGPlayerController;

UCLASS()
class COOPRPG_API ACoopRPGGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ACoopRPGGameMode();

    ACoopRPGAttackTarget* GetAttackTarget() const;
    AActor* GetMeleeTarget() const;
    bool IsSkirmishEncounter() const { return bExpeditionSkirmish; }
    bool IsSkirmishEnemy(const ACoopRPGEnemy* Candidate) const;
    ACoopRPGEnemy* FindClosestSkirmishEnemy(const FVector& Origin) const;
    const TArray<TObjectPtr<ACoopRPGEnemy>>& GetSkirmishEnemies() const { return SkirmishEnemies; }
    void UpdateLootRoundPhase(const AController* Leaving = nullptr);
    void AcknowledgeRoundResult(ACoopRPGPlayerController* Player, FGuid RoundId);
    void SelectPuzzleReward(ACoopRPGPlayerController& Player, FGuid RoundId, ECoopRPGPuzzleReward Reward);
    bool HasHealthPotionCapacityReward(const FUniqueNetIdRepl& ConnectionId) const;

protected:
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
        FString& ErrorMessage) override;
    virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
        const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void Logout(AController* Exiting) override;
    virtual void RestartPlayer(AController* NewPlayer) override;

private:
    void PublishReadySlots(ACoopRPGGameState& State);
    FString GetRoundJoinRejection(const FString& Options, const FUniqueNetIdRepl& UniqueId);
    bool ReserveRoundTravelSeats(const ACoopRPGGameState& State, const FString& Destination) const;
    bool IsRoundParticipant(const AController* Controller) const;
    void TickPuzzleRewards(ACoopRPGGameState& State);
    void FinalizePuzzleReward(ACoopRPGGameState& State, ACoopRPGPlayerController& Player,
        ECoopRPGPuzzleReward Reward, bool bTimedOut);
    void OpenBossRewards(ACoopRPGGameState& State);
    void AdvanceRoundIfReady(ACoopRPGGameState& State, const FString& Destination);

private:
    UPROPERTY()
    TObjectPtr<ACoopRPGAttackTarget> AttackTarget;

    UPROPERTY()
    TObjectPtr<ACoopRPGEnemy> Enemy;

    UPROPERTY()
    TArray<TObjectPtr<ACoopRPGEnemy>> SkirmishEnemies;

    // A player may reconnect while the stage is still waiting for readiness.
    TMap<FUniqueNetIdRepl, FCoopRPGSwordSnapshot> WaitingPlayerSwords;
    TWeakObjectPtr<APlayerController> PlayerSlots[2];
    FString ValidationClientLabels[2];
    FString LastValidationParticipants;
    TArray<TWeakObjectPtr<ACoopRPGPlayerController>> RoundPlayers;
    TArray<TWeakObjectPtr<ACoopRPGPlayerController>> ResultAcknowledgements;
    double NextTravelTime = 0.0;
    bool bTravelAttempted = false;
    bool bParticipantDisconnected = false;
    bool bEnemyCombat = false;
    bool bExpeditionLobby = false;
    bool bExpeditionPuzzle = false;
    bool bExpeditionBoss = false;
    bool bExpeditionSkirmish = false;
};
