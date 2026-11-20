#pragma once

#include "AbilitySystemInterface.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/Character.h"
#include "UObject/StrongObjectPtr.h"
#include "CoopRPGEnemy.generated.h"

class ACoopRPGCharacter;
class UAbilitySystemComponent;
class UCoopRPGCombatAttributes;
class UCoopRPGCombatProbeComponent;
class UDynamicMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FOnAttributeChangeData;

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
    bool IsShieldRaised() const;
    double GetShieldAngle(const FVector& Position) const;
    const FCoopRPGShieldWarning& GetShieldWarning() const { return ShieldWarning; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class UCoopRPGCombatProbeComponent;

    ACoopRPGCharacter* FindTarget(float Range) const;
    void SetShieldWarning(int32 TargetSlot, float FacingYaw, double EndsAt);
    void StopChasing();
    void StopCombat();
    void ResolveAttack(ACoopRPGCharacter* Player);
    void HandleHealthChanged(const FOnAttributeChangeData& Change);
    void SpawnDeathLoot(const FGuid& RoundId);
    void ApplyDeathPresentation();
    void SetPresentationTint(bool bDarkened);
    void RestorePresentationTint();

    UFUNCTION()
    void OnRep_Dead();

    void ConfigureBlackKnightPresentation();
    void UpdateBlackKnightAnimation();
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

    TStrongObjectPtr<UAnimSequence> BlackKnightIdle;
    TStrongObjectPtr<UAnimSequence> BlackKnightWalk;
    TStrongObjectPtr<UAnimSequence> BlackKnightGuard;
    TStrongObjectPtr<UAnimSequence> BlackKnightAttack;
    UAnimSequence* BlackKnightCurrentClip = nullptr;

    double BlackKnightStrikeAt = 0.0;
    float BlackKnightFacingYaw = 0.0f;
    uint32 BlackKnightAttackSequence = 0;
    uint32 LastBlackKnightStrikeFrame = 0;
    bool bBlackKnightPresentationAttempted = false;

    FVector ObservationOrigin = FVector::ZeroVector;
    bool bObservedMovement = false;
};
