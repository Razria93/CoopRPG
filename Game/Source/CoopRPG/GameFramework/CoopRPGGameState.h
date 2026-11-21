#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CoopRPGGameplayFeedback.h"
#include "CoopRPGGameState.generated.h"

class ACoopRPGEnemy;
class ACoopRPGEnemyLoot;
class ACoopRPGExtractionZone;
class ACoopRPGSharedChest;
class UCoopRPGCombatProbeComponent;
class UDynamicMeshComponent;

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
    Extracted,
    PuzzleSolved,
    BossDefeated
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
    int32 ExtractionPlayersInside = 0;

    UPROPERTY()
    ECoopRPGRoundEndReason EndReason = ECoopRPGRoundEndReason::None;

    UPROPERTY()
    int32 TeamScore = 0;
};

USTRUCT()
struct FCoopRPGPuzzleStatus
{
    GENERATED_BODY()

    UPROPERTY()
    FVector PlateA = FVector::ZeroVector;

    UPROPERTY()
    FVector PlateB = FVector::ZeroVector;

    // Bit 0 represents P1 and bit 1 represents P2.
    UPROPERTY()
    uint8 OccupantsA = 0;

    UPROPERTY()
    uint8 OccupantsB = 0;
};

UENUM()
enum class ECoopRPGReadyStatus : uint8
{
    Disconnected,
    Waiting,
    Ready
};

USTRUCT()
struct FCoopRPGReadySlots
{
    GENERATED_BODY()

    UPROPERTY()
    ECoopRPGReadyStatus PlayerOne = ECoopRPGReadyStatus::Disconnected;

    UPROPERTY()
    ECoopRPGReadyStatus PlayerTwo = ECoopRPGReadyStatus::Disconnected;
};

UENUM()
enum class ECoopRPGMeleeSweepResult : uint8
{
    Missed,
    Hit,
    Obstructed
};

USTRUCT()
struct FCoopRPGMeleeSweepTrace
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Start = FVector::ZeroVector;

    UPROPERTY()
    FVector End = FVector::ZeroVector;

    UPROPERTY()
    float Radius = 0.0f;

    UPROPERTY()
    float ContactTime = 0.0f;

    UPROPERTY()
    int32 Slot = 0;

    UPROPERTY()
    uint32 Swing = 0;

    UPROPERTY()
    ECoopRPGMeleeSweepResult Result = ECoopRPGMeleeSweepResult::Missed;
};

UCLASS()
class COOPRPG_API ACoopRPGGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void InitializeLobby();
    void InitializePuzzle(const FVector& PlateA, const FVector& PlateB);
    void InitializeLootRound(ACoopRPGSharedChest* Chest, ACoopRPGExtractionZone* Zone);
    void SetRoundPlaying(bool bPlaying);

    UFUNCTION(NetMulticast, Unreliable)
    void MulticastGameplayFeedback(const FCoopRPGGameplayFeedback& Feedback);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastMeleeSweepTrace(const FCoopRPGMeleeSweepTrace& Trace);

    void SetReadySlots(const FCoopRPGReadySlots& Slots);
    int32 RegisterSkirmishDefeat(ACoopRPGEnemy* Defeated);
    bool RegisterEnemyLoot(ACoopRPGEnemyLoot* Drop);
    bool TryGrantEnemyLoot(ACoopRPGEnemyLoot* Drop);
    static constexpr int32 RoundCapacity = 2;

    void EvaluateRound(bool bDisconnected, bool bDeath, int32 PlayersInside, bool bBossDefeated = false);
    void EvaluatePuzzle(bool bDisconnected, bool bDeath, uint8 OccupantsA, uint8 OccupantsB);
    double GetRoundRemainingSeconds() const;
    double GetExtractionRemainingSeconds() const;
    FString GetRoundTravelDestination() const;

    ACoopRPGSharedChest* GetSharedChest() const { return SharedChest.Get(); }
    FGuid GetRoundId() const { return RoundId; }
    bool IsRoundPlaying() const { return RoundStatus.Phase == ECoopRPGRoundPhase::Playing; }
    bool HasRoundResult() const { return RoundStatus.Phase == ECoopRPGRoundPhase::Result; }
    bool IsExtractionInProgress() const { return RoundStatus.ExtractionDeadline > 0.0; }
    int32 GetExtractionPlayersInside() const { return RoundStatus.ExtractionPlayersInside; }
    ACoopRPGExtractionZone* GetExtractionZone() const { return ExtractionZone.Get(); }
    ECoopRPGRoundEndReason GetRoundEndReason() const { return RoundStatus.EndReason; }
    int32 GetTeamScore() const { return RoundStatus.TeamScore; }
    ACoopRPGEnemyLoot* GetEnemyPotion() const { return EnemyPotion.Get(); }
    ACoopRPGEnemyLoot* GetEnemyLoot() const { return EnemyLoot.Get(); }
    int32 GetTeamLootCount() const { return TeamLootCount; }
    int32 GetSkirmishDefeats() const { return SkirmishDefeats; }
    const FCoopRPGReadySlots& GetReadySlots() const { return ReadySlots; }
    const FCoopRPGPuzzleStatus& GetPuzzleStatus() const { return PuzzleStatus; }

private:
    friend class UCoopRPGCombatProbeComponent;

    void FinishRound(ECoopRPGRoundEndReason Reason, double Now, bool bDisconnected, bool bDeath,
        bool bTimeout, bool bExtractionReady);
    void UpdateExtraction(bool bEligible, double Now);

    UFUNCTION()
    void OnRep_ExtractionZone();

    UFUNCTION()
    void OnRep_TeamLootCount(int32 PreviousCount);

    UFUNCTION()
    void OnRep_SkirmishDefeats();

    UFUNCTION()
    void OnRep_RoundStatus(const FCoopRPGRoundStatus& PreviousStatus);

    UFUNCTION()
    void OnRep_PuzzleStatus();
    void UpdatePuzzlePlatePresentation();
    void UpdatePuzzlePlate(UDynamicMeshComponent* Plate, uint8 Occupants) const;
    UDynamicMeshComponent* CreatePuzzlePlate(FName PlateTag);

    UFUNCTION()
    void OnRep_ReadySlots();

private:
    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGSharedChest> SharedChest;

    UPROPERTY(Replicated)
    FGuid RoundId;

    UPROPERTY(ReplicatedUsing = OnRep_RoundStatus)
    FCoopRPGRoundStatus RoundStatus;

    UPROPERTY(ReplicatedUsing = OnRep_ReadySlots)
    FCoopRPGReadySlots ReadySlots;

    UPROPERTY(ReplicatedUsing = OnRep_PuzzleStatus)
    FCoopRPGPuzzleStatus PuzzleStatus;

    UPROPERTY(Transient)
    TObjectPtr<UDynamicMeshComponent> PuzzlePlateA;

    UPROPERTY(Transient)
    TObjectPtr<UDynamicMeshComponent> PuzzlePlateB;

    UPROPERTY(ReplicatedUsing = OnRep_ExtractionZone)
    TObjectPtr<ACoopRPGExtractionZone> ExtractionZone;

    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGEnemyLoot> EnemyLoot;

    UPROPERTY(Replicated)
    TObjectPtr<ACoopRPGEnemyLoot> EnemyPotion;

    UPROPERTY(ReplicatedUsing = OnRep_TeamLootCount)
    int32 TeamLootCount = 0;

    UPROPERTY(ReplicatedUsing = OnRep_SkirmishDefeats)
    int32 SkirmishDefeats = 0;

    TSet<TWeakObjectPtr<ACoopRPGEnemy>> DefeatedSkirmishEnemies;
};
