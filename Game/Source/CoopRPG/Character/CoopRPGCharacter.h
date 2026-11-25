#pragma once

#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Character.h"
#include "CoopRPGCharacter.generated.h"

class UAbilitySystemComponent;
class UAnimMontage;
class UAnimSequence;
class UBlendSpace;
class UCoopRPGCombatAttributes;
class UInputAction;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
enum class ECoopRPGHero : uint8;
struct FInputActionValue;
struct FKey;
struct FOnAttributeChangeData;

USTRUCT()
struct FCoopRPGMeleePresentation
{
    GENERATED_BODY()

    UPROPERTY()
    uint32 Sequence = 0;

    UPROPERTY()
    double StartedAt = 0.0;

    UPROPERTY()
    float FacingYaw = 0.0f;

    UPROPERTY()
    float Windup = 0.0f;

    UPROPERTY()
    bool bCancelled = false;

    UPROPERTY()
    bool bSlash = false;
};

UCLASS()
class COOPRPG_API ACoopRPGCharacter : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ACoopRPGCharacter();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    float GetHealth() const;
    float GetStamina() const;
    int32 GetPartyStamina() const { return PartyStamina; }
    bool HasMeleeWeapon() const { return bHasMeleeWeapon; }
    void SetMeleeWeaponEquipped(bool bEquipped);
    bool HasShieldEquipped() const { return bShieldEquipped; }
    bool IsGuardian() const;
    FVector GetTauntPulseOrigin() const { return TauntPulseOrigin; }
    double GetTauntPulseAge() const;

    UFUNCTION(NetMulticast, Reliable)
    void MulticastTauntPulse(FVector_NetQuantize Origin);
    bool IsGuarding() const;
    bool EvaluateGuardForHit();
    void RestoreGuardPointsOnBlock();
    double GetGuardPoints() const { return GuardPoints; }
    bool IsGuardExhausted() const { return bGuardExhausted; }
    void SetShieldEquipped(bool bEquipped);
    void ReleaseGuardInput();
    void ClearGuard();
    double GetGuardAngle(const FVector& SourcePosition) const;
    FVector GetShieldImpactLocation() const;
    int32 GetDisplaySlot() const { return DisplaySlot; }
    void SetDisplaySlot(int32 Slot);
    void FlashPresentationHit();
    void RefreshHeroAppearance();
    float GetMeleeWindup() const;
    bool IsMeleeWindupActive() const { return bMeleeFacingLocked; }
    uint32 StartMeleePresentation(float FacingYaw, float Windup, bool bSlash = false);
    void FinishMeleeWindup(bool bInterrupted);
    void RequestMeleeHitStop(uint32 Swing);

protected:
    virtual void BeginPlay() override;
    virtual void OnPlayerStateChanged(APlayerState* NewPlayerState, APlayerState* OldPlayerState) override;
    virtual void NotifyControllerChanged() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
    void HandleGuardHealthChanged(const FOnAttributeChangeData& Change);
    void UpdatePartyStamina(const FOnAttributeChangeData& Change);
    void Move(const FInputActionValue& Value);
    void Attack();
    void CancelAttack();
    void UseSlash();
    void UseTaunt();
    void PressGuardInput(FKey Key);
    void ReleaseGuardInputKey(FKey Key);
    void SetGuarding(bool bGuarding);
    bool CanUpdateGuardPoints() const;
    void UpdateGuardPoints();
    void ScheduleGuardPointsUpdate();
    void LogGuardPoints(const TCHAR* Event) const;
    void UpdateGuardPose();

    UFUNCTION(Server, Reliable)
    void ServerSetGuarding(FGuid RoundId, bool bRequested);

    UFUNCTION()
    void OnRep_ShieldState();

    UFUNCTION(Server, Reliable)
    void ServerCancelAttack();

    UFUNCTION()
    void OnRep_MeleeWeapon();

    UFUNCTION()
    void UpdateLocomotion(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);

    UFUNCTION()
    void OnRep_DisplaySlot();

    UFUNCTION()
    void OnRep_MeleePresentation();

    void PlayMeleeSwingEffects();
    void ApplyMeleeHitStop(uint32 Swing);
    void ReleaseMeleeFacing();
    void ConfigureHeroWeapon(ECoopRPGHero Hero);
    void RefreshWeaponVisibility();
    void ConfigureHeroShield(ECoopRPGHero Hero);
    void SetPresentationTint(bool bDarkened);
    void RestorePresentationTint();

private:
    FVector TauntPulseOrigin = FVector::ZeroVector;
    double TauntPulseStartedAt = -100.0;

public:
    static constexpr double MaxGuardPoints = 100.0;
    static constexpr double GuardRestartPoints = 20.0;
    static constexpr double GuardHalfAngleDegrees = 45.0;
    static constexpr float GuardDamageMultiplier = 0.5f;

private:
    UPROPERTY()
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;

    UPROPERTY()
    TObjectPtr<UCoopRPGCombatAttributes> CombatAttributes;

    UPROPERTY()
    TObjectPtr<UInputAction> AttackAction;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WeaponMesh;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> HeroWeaponMesh;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> HeroShieldMesh;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> HeadMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> PresentationMaterial;

    UPROPERTY(ReplicatedUsing = OnRep_DisplaySlot)
    int32 DisplaySlot = 0;

    // Expedition HUD snapshot for observers. The GAS attribute is replicated to its owner.
    UPROPERTY(Replicated)
    int32 PartyStamina = INDEX_NONE;

    // The constructor asset finder keeps the material referenced.
    TObjectPtr<UMaterialInterface> HeroHitDarkenMaterial = nullptr;
    FTimerHandle HitFlashTimer;
    FTimerHandle MeleeFacingTimer;
    FTimerHandle MeleeSwingEffectsTimer;

    UPROPERTY(Transient)
    TObjectPtr<UBlendSpace> HeroLocomotion;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> HeroAttackClip;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> HeroGuardClip;

    UPROPERTY(ReplicatedUsing = OnRep_MeleePresentation)
    FCoopRPGMeleePresentation MeleePresentation;

    TWeakObjectPtr<UAnimMontage> LocalMeleeMontage;
    TWeakObjectPtr<UAnimMontage> LocalGuardMontage;
    uint32 PresentedMeleeSequence = 0;
    uint32 LastHitStoppedMeleeSequence = 0;
    uint32 PendingHitStopSwing = 0;
    double PendingHitStopDeadline = 0.0;
    bool bMeleeFacingLocked = false;

    UPROPERTY(ReplicatedUsing = OnRep_MeleeWeapon)
    bool bHasMeleeWeapon = false;

    UPROPERTY(ReplicatedUsing = OnRep_ShieldState)
    bool bShieldEquipped = false;

    UPROPERTY(ReplicatedUsing = OnRep_ShieldState)
    bool bGuarding = false;

    UPROPERTY(Replicated)
    double GuardPoints = MaxGuardPoints;

    UPROPERTY(Replicated)
    bool bGuardExhausted = false;

    FTimerHandle GuardPointsTimer;
    double GuardPointsUpdatedAt = 0.0;
    double GuardRecoveryStartsAt = 0.0;
    bool bGuardInputNeedsRelease = false;
    FGuid PendingGuardRound;
    bool bGuardKeyHeld = false;
    bool bGuardMouseHeld = false;
};
