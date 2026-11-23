#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoopRPGGameplayFeedback.generated.h"

UENUM()
enum class ECoopRPGFeedbackKind : uint8
{
    Damage,
    Recovery,
    WeaponAcquired,
    RecoveryAcquired,
    RecoveryUsed,
    TeamLootAcquired,
    ShieldBlocked,
    StaminaRecovery,
    StaminaRecoveryAcquired
};

UENUM()
enum class ECoopRPGMeleeEffect : uint8
{
    None,
    SwordSwing,
    MushroomHit,
    ArmorHit,
    ShieldBlock
};

USTRUCT()
struct FCoopRPGGameplayFeedback
{
    GENERATED_BODY()

    FCoopRPGGameplayFeedback() = default;
    FCoopRPGGameplayFeedback(ECoopRPGFeedbackKind InKind, AActor* InSubject, float InAmount = 0.0f,
        int32 InAttackerSlot = INDEX_NONE)
        : Kind(InKind), Subject(InSubject), Location(InSubject->GetActorLocation()),
          Amount(InAmount), AttackerSlot(InAttackerSlot)
    {
    }

public:
    UPROPERTY()
    ECoopRPGFeedbackKind Kind = ECoopRPGFeedbackKind::Damage;

    UPROPERTY()
    TObjectPtr<AActor> Subject;

    UPROPERTY()
    FVector Location = FVector::ZeroVector;

    UPROPERTY()
    float Amount = 0.0f;

    // Zero identifies an enemy attacker. Player slots start at one.
    UPROPERTY()
    int32 AttackerSlot = INDEX_NONE;

    UPROPERTY()
    ECoopRPGMeleeEffect MeleeEffect = ECoopRPGMeleeEffect::None;

    UPROPERTY()
    FVector MeleeEffectLocation = FVector::ZeroVector;

    UPROPERTY()
    float MeleeEffectYaw = 0.0f;

    UPROPERTY()
    uint32 MeleeSwing = 0;

    UPROPERTY()
    bool bPreserveEnemyAttackPose = false;

    UPROPERTY()
    bool bTriggerLocalMeleeImpact = true;
};

namespace CoopRPG::Presentation
{
    inline FLinearColor PlayerColor(int32 Slot)
    {
        return Slot == 1 ? FLinearColor(1.0f, 0.72f, 0.04f) : FLinearColor(0.08f, 0.44f, 1.0f);
    }

    inline FLinearColor EnemyColor()
    {
        return FLinearColor(1.0f, 0.10f, 0.10f);
    }
}
