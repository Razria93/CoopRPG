#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CoopRPGGameState.generated.h"

class ACoopRPGEnemyLoot;
class ACoopRPGExtractionZone;
class ACoopRPGSharedChest;
class UCoopRPGCombatProbeComponent;

UENUM()
enum class ECoopRPGRoundPhase : uint8
{
    Waiting,
    Playing,
    Result
};

UENUM()
enum class ECoopRPGRoundEndReason : uint8
{
    None,
    Disconnected,
    Death,
    Timeout,
    Extracted
};

USTRUCT()
struct FCoopRPGRoundStatus
{
    GENERATED_BODY()

    UPROPERTY()
    ECoopRPGRoundPhase Phase = ECoopRPGRoundPhase::Waiting;

    UPROPERTY()
    double ExtractionDeadline = 0.0;

    UPROPERTY()
    double RoundDeadline = 0.0;

    UPROPERTY()
    ECoopRPGRoundEndReason EndReason = ECoopRPGRoundEndReason::None;

    UPROPERTY()
    int32 TeamScore = 0;
};

UCLASS()
class COOPRPG_API ACoopRPGGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void InitializeLootRound(ACoopRPGSharedChest* Chest, ACoopRPGExtractionZone* Zone);
    void SetRoundPlaying(bool bPlaying);
    bool RegisterEnemyLoot(ACoopRPGEnemyLoot* Drop);
    bool TryGrantEnemyLoot(ACoopRPGEnemyLoot* Drop);
    void EvaluateRound(bool bDisconnected, bool bDeath, bool bBothInside);
    double GetRoundRemainingSeconds() const;
    double GetExtractionRemainingSeconds() const;

    ACoopRPGSharedChest* GetSharedChest() const { return SharedChest.Get(); }
    FGuid GetRoundId() const { return RoundId; }
    bool IsRoundPlaying() const { return RoundStatus.Phase == ECoopRPGRoundPhase::Playing; }
    bool HasRoundResult() const { return RoundStatus.Phase == ECoopRPGRoundPhase::Result; }
    bool IsExtractionInProgress() const { return RoundStatus.ExtractionDeadline > 0.0; }
    ACoopRPGExtractionZone* GetExtractionZone() const { return ExtractionZone.Get(); }
    ECoopRPGRoundEndReason GetRoundEndReason() const { return RoundStatus.EndReason; }
    int32 GetTeamScore() const { return RoundStatus.TeamScore; }
    ACoopRPGEnemyLoot* GetEnemyLoot() const { return EnemyLoot.Get(); }
    int32 GetTeamLootCount() const { return TeamLootCount; }

private:
    friend class UCoopRPGCombatProbeComponent;

    void UpdateExtraction(bool bEligible, double Now);

    UFUNCTION()
    void OnRep_TeamLootCount(int32 PreviousCount);

    UFUNCTION()
    void OnRep_RoundStatus(const FCoopRPGRoundStatus& PreviousStatus);

private:
    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGSharedChest> SharedChest;

    UPROPERTY(Replicated)
    FGuid RoundId;

    UPROPERTY(ReplicatedUsing = OnRep_RoundStatus)
    FCoopRPGRoundStatus RoundStatus;

    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGExtractionZone> ExtractionZone;

    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGEnemyLoot> EnemyLoot;

    UPROPERTY(ReplicatedUsing = OnRep_TeamLootCount)
    int32 TeamLootCount = 0;
};
