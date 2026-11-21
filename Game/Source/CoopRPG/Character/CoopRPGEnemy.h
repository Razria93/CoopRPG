#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "CoopRPGEnemy.generated.h"

class ACoopRPGCharacter;
class UAnimMontage;
class UAnimSequence;
class UAnimSequenceBase;
class UBlendSpace;
class UCoopRPGLocomotionAnimInstance;
class UAbilitySystemComponent;
class UCoopRPGCombatAttributes;
class UCoopRPGCombatProbeComponent;
class UDynamicMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FOnAttributeChangeData;

UENUM(BlueprintType)
enum class ECoopRPGEnemyKind : uint8
{
    Training,
    Mushroom,
    BlackKnight
};

USTRUCT()
struct FCoopRPGShieldWarning
{
    GENERATED_BODY()

    UPROPERTY()
    double EndsAt = 0.0;

    UPROPERTY()
    float FacingYaw = 0.0f;

    UPROPERTY()
    int32 TargetSlot = 0;

    UPROPERTY()
    uint32 Sequence = 0;
};

USTRUCT()
struct FCoopRPGMushroomStrike
{
    GENERATED_BODY()

    UPROPERTY()
    double At = 0.0;

    UPROPERTY()
    float FacingYaw = 0.0f;
};

UCLASS()
class COOPRPG_API ACoopRPGEnemy : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ACoopRPGEnemy();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    float GetHealth() const;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnRep_ReplicatedMovement() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    bool IsDead() const { return bDead; }
    void FlashPresentationHit();
    void EnableShieldEncounter();
    bool IsShieldEncounter() const { return bShieldEncounter; }
    bool IsBlackKnight() const { return Kind == ECoopRPGEnemyKind::BlackKnight; }
    float GetMeleeRangeBonus() const;
    bool IsShieldRaised() const;
    double GetShieldAngle(const FVector& Position) const;
    const FCoopRPGShieldWarning& GetShieldWarning() const { return ShieldWarning; }

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class UCoopRPGCombatProbeComponent;

    ACoopRPGCharacter* FindTarget(float Range, bool bMeasureAttackDistance = false) const;
    double GetAttackDistance(const ACoopRPGCharacter& Player) const;
    float GetAttackRange() const;
    void SetShieldWarning(int32 TargetSlot, float FacingYaw, double EndsAt);
    void StopChasing();
    void StopCombat();
    void ResolveAttack(ACoopRPGCharacter* Player);
    void HandleHealthChanged(const FOnAttributeChangeData& Change);
    void SpawnDeathLoot(const FGuid& RoundId);
    void SpawnSkirmishLoot(const FGuid& RoundId);
    bool ApplyModel();
    void StartAnimation();
    void UpdateAttackPose();
    void HideCorpse();
    void ApplyDeathPresentation();
    void SetPresentationTint(bool bDarkened);
    void RestorePresentationTint();

    UFUNCTION()
    void OnRep_Dead();

    void CreateGroundRing();
    void UpdateShieldPresentation(bool bRaised);

    UFUNCTION()
    void OnRep_ShieldEncounter();

    UFUNCTION()
    void OnRep_ShieldWarning();

public:
    static constexpr float AttackRangeCm = 135.0f;
    static constexpr double ShieldHalfAngleDegrees = 45.0;

private:
    bool bLargeExpeditionBoss = false;

    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;

    UPROPERTY()
    TObjectPtr<UCoopRPGCombatAttributes> CombatAttributes;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> EnemyVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> FacingMarker;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ShieldVisual;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> GroundRingMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UDynamicMeshComponent> GroundRing;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> GroundRingFrontMaterial;

    UPROPERTY(EditInstanceOnly, Category = "Presentation")
    bool bShowFacingMarker = false;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> PresentationMaterial;

    FTimerHandle HitFlashTimer;

    UPROPERTY(ReplicatedUsing = OnRep_Dead)
    bool bDead = false;

    TWeakObjectPtr<ACoopRPGCharacter> Target;
    double NextActionTime = 0.0;
    bool bChasing = false;
    bool bAttackPending = false;
    UPROPERTY(ReplicatedUsing = OnRep_ShieldEncounter, Transient)
    bool bShieldEncounter = false;

    UPROPERTY(ReplicatedUsing = OnRep_ShieldWarning)
    FCoopRPGShieldWarning ShieldWarning;

    FTimerHandle ShieldPresentationTimer;

    UPROPERTY(EditDefaultsOnly, Category = "Enemy")
    ECoopRPGEnemyKind Kind = ECoopRPGEnemyKind::Training;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation")
    TSoftObjectPtr<USkeletalMesh> BodyMesh;

    UPROPERTY(EditAnywhere, Category = "Presentation", meta = (ClampMin = "1", Units = "cm"))
    float ModelHeight = 80.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation", meta = (Units = "deg"))
    float ModelYaw = -90.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Equipment")
    TSoftObjectPtr<UStaticMesh> AxeMesh;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Equipment")
    TSoftObjectPtr<UStaticMesh> ShieldMesh;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Equipment")
    FName AxeSocket = TEXT("AxeSocket");

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Equipment")
    FName ShieldSocket = TEXT("ShieldSocket");

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Animation")
    TObjectPtr<UBlendSpace> Locomotion;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Animation")
    TObjectPtr<UAnimSequence> GuardPose;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Animation")
    TObjectPtr<UAnimMontage> AttackMontage;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Animation")
    TObjectPtr<UAnimMontage> HitMontage;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation|Animation")
    TObjectPtr<UAnimMontage> DeathMontage;

    UPROPERTY(VisibleAnywhere, Category = "Presentation|Equipment")
    TObjectPtr<UStaticMeshComponent> Axe;

    UPROPERTY(VisibleAnywhere, Category = "Presentation|Equipment")
    TObjectPtr<UStaticMeshComponent> Shield;

    UPROPERTY(Transient)
    TObjectPtr<UCoopRPGLocomotionAnimInstance> Animation;

    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> GuardLoop;

    UPROPERTY(Replicated)
    FCoopRPGMushroomStrike MushroomStrike;

    FTimerHandle CorpseTimer;
    double KnightStrikeAt = 0.0;
    float KnightFacingYaw = 0.0f;
    uint32 KnightSequence = 0;
    uint32 LastKnightImpact = 0;
    const UAnimSequenceBase* LastObservedClip = nullptr;
    double PendingHitUntil = 0.0;
    double PresentedStrike = 0.0;
    bool bMushroomEncounter = false;

    FVector ObservationOrigin = FVector::ZeroVector;
    bool bObservedMovement = false;
};
