#include "CoopRPGEnemy.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"

#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEnemy, Log, All);

namespace
{
    constexpr float DetectionRangeCm = 360.0f;
    constexpr float AttackRangeCm = 135.0f;
    constexpr float WindupSeconds = 0.9f;
    constexpr float RecoverySeconds = 2.5f;
    constexpr float MovementObservationCm = 40.0f;

    bool IsLivePlayer(const ACoopRPGCharacter* Player)
    {
        return IsValid(Player) && Player->IsPlayerControlled() && Player->GetHealth() > 0.0f;
    }

    int32 GetTargetPlayerId(const ACoopRPGCharacter* Player)
    {
        const APlayerState* State = IsValid(Player) ? Player->GetPlayerState() : nullptr;
        return State ? State->GetPlayerId() : INDEX_NONE;
    }
}

ACoopRPGEnemy::ACoopRPGEnemy()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    AutoPossessAI = EAutoPossessAI::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(34.0f, 88.0f);

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->bRunPhysicsWithNoController = true;
    Movement->bOrientRotationToMovement = true;
    Movement->MaxWalkSpeed = 220.0f;

    EnemyVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EnemyVisual"));
    EnemyVisual->SetupAttachment(GetRootComponent());
    EnemyVisual->SetRelativeScale3D(FVector(0.62f, 0.62f, 1.65f));
    EnemyVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    EnemyVisual->SetStaticMesh(Cube.Object);

    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
    AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
    CombatAttributes = CreateDefaultSubobject<UCoopRPGCombatAttributes>(TEXT("CombatAttributes"));
}

UAbilitySystemComponent* ACoopRPGEnemy::GetAbilitySystemComponent() const
{
    return AbilitySystem;
}

void ACoopRPGEnemy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!HasAuthority() || bDead)
    {
        return;
    }
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || !State->IsRoundPlaying())
    {
        StopCombat();
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();
    ACoopRPGCharacter* Player = Target.Get();
    if (bAttackPending)
    {
        if (Now >= NextActionTime)
        {
            bAttackPending = false;
            NextActionTime = Now + RecoverySeconds;
            ResolveAttack(Player);
        }
        return;
    }

    if (!IsLivePlayer(Player) ||
        FVector::DistSquared2D(GetActorLocation(), Player->GetActorLocation()) > FMath::Square(DetectionRangeCm))
    {
        StopChasing();
        Player = FindTarget();
        Target = Player;
        if (Player)
        {
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyDetected Actor=%s Target=%s TargetPlayerId=%d Distance=%.2f %s"),
                *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player),
                FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()),
                *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        }
    }
    if (!Player)
    {
        return;
    }

    const FVector Offset = Player->GetActorLocation() - GetActorLocation();
    const float Distance = Offset.Size2D();
    if (Distance > AttackRangeCm)
    {
        if (!bChasing)
        {
            bChasing = true;
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyChaseStarted Actor=%s Target=%s TargetPlayerId=%d Distance=%.2f %s"),
                *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player), Distance,
                *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        }
        AddMovementInput(Offset.GetSafeNormal2D());
        return;
    }

    StopChasing();
    if (Now >= NextActionTime)
    {
        bAttackPending = true;
        NextActionTime = Now + WindupSeconds;
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyAttackStarted Actor=%s Target=%s TargetPlayerId=%d Distance=%.2f Range=%.2f Windup=%.2f %s"),
            *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player), Distance, AttackRangeCm, WindupSeconds,
            *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void ACoopRPGEnemy::OnRep_ReplicatedMovement()
{
    Super::OnRep_ReplicatedMovement();

    // One observation per client confirms significant replicated movement without logging each update.
    if (HasActorBegunPlay() && !bDead && !bObservedMovement)
    {
        const double Distance = FVector::Dist2D(ObservationOrigin, GetReplicatedMovement().Location);
        if (Distance >= MovementObservationCm)
        {
            bObservedMovement = true;
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyMovementObserved Actor=%s Distance=%.2f %s"),
                *GetName(), Distance, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        }
    }
}

void ACoopRPGEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGEnemy, bDead);
}

void ACoopRPGEnemy::BeginPlay()
{
    Super::BeginPlay();
    AbilitySystem->InitAbilityActorInfo(this, this);
    ObservationOrigin = GetActorLocation();
    SetActorTickEnabled(HasAuthority());
    if (HasAuthority())
    {
        AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCoopRPGCombatAttributes::GetHealthAttribute())
            .AddUObject(this, &ACoopRPGEnemy::HandleHealthChanged);
    }
}

void ACoopRPGEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority())
    {
        AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCoopRPGCombatAttributes::GetHealthAttribute()).RemoveAll(this);
    }
    Super::EndPlay(EndPlayReason);
}

ACoopRPGCharacter* ACoopRPGEnemy::FindTarget() const
{
    ACoopRPGCharacter* Nearest = nullptr;
    double NearestDistanceSquared = FMath::Square(DetectionRangeCm);
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PlayerController = It->Get();
        auto* Player = PlayerController ? Cast<ACoopRPGCharacter>(PlayerController->GetPawn()) : nullptr;
        if (IsLivePlayer(Player))
        {
            const double DistanceSquared = FVector::DistSquared2D(GetActorLocation(), Player->GetActorLocation());
            if (DistanceSquared <= NearestDistanceSquared)
            {
                Nearest = Player;
                NearestDistanceSquared = DistanceSquared;
            }
        }
    }
    return Nearest;
}

void ACoopRPGEnemy::StopChasing()
{
    if (bChasing)
    {
        bChasing = false;
        GetCharacterMovement()->StopMovementImmediately();
    }
}

void ACoopRPGEnemy::StopCombat()
{
    StopChasing();
    Target.Reset();
    NextActionTime = 0.0;
    bAttackPending = false;
}

void ACoopRPGEnemy::ResolveAttack(ACoopRPGCharacter* Player)
{
    const bool bLivePlayer = IsLivePlayer(Player);
    const double Distance = bLivePlayer ? FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()) : 0.0;
    if (!bLivePlayer || Distance > AttackRangeCm)
    {
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyAttackMissed Actor=%s Target=%s TargetPlayerId=%d Reason=%s Distance=%.2f Range=%.2f %s"),
            *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player), bLivePlayer ? TEXT("OutOfRange") : TEXT("InvalidTarget"),
            Distance, AttackRangeCm, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    const FGameplayEffectSpecHandle Damage = AbilitySystem->MakeOutgoingSpec(UCoopRPGMeleeDamage::StaticClass(), 1.0f,
        AbilitySystem->MakeEffectContext());
    check(Damage.IsValid());
    const float HealthBefore = Player->GetHealth();
    AbilitySystem->ApplyGameplayEffectSpecToTarget(*Damage.Data.Get(), Player->GetAbilitySystemComponent());
    UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyAttackHit Actor=%s Target=%s TargetPlayerId=%d Distance=%.2f Range=%.2f HP=%.2f->%.2f %s"),
        *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player), Distance, AttackRangeCm,
        HealthBefore, Player->GetHealth(), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGEnemy::HandleHealthChanged(const FOnAttributeChangeData& Change)
{
    if (!HasAuthority() || bDead || Change.OldValue <= 0.0f || Change.NewValue > 0.0f)
    {
        return;
    }

    const bool bDiscardedAttack = bAttackPending;
    bDead = true;
    StopCombat();
    GetCharacterMovement()->DisableMovement();
    SetActorTickEnabled(false);
    ApplyDeathPresentation();
    ForceNetUpdate();

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const FGuid RoundId = State ? State->GetRoundId() : FGuid();
    UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyDeathConfirmed Actor=%s Round=%s HP=%.2f->%.2f PendingAttackDiscarded=%d %s"),
        *GetName(), *RoundId.ToString(), Change.OldValue, Change.NewValue, bDiscardedAttack ? 1 : 0,
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    SpawnDeathLoot(RoundId);
}

void ACoopRPGEnemy::SpawnDeathLoot(const FGuid& RoundId)
{
    const FVector Location = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 20.0f);
    const FTransform Transform(FRotator(0.0f, 45.0f, 0.0f), Location, FVector(0.35f));
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Drop = GetWorld()->SpawnActor<ACoopRPGEnemyLoot>(ACoopRPGEnemyLoot::StaticClass(), Transform, SpawnParameters);
    auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || !State->RegisterEnemyLoot(Drop))
    {
        if (IsValid(Drop))
        {
            Drop->Destroy();
        }
        UE_LOG(LogCoopRPGEnemy, Error, TEXT("EnemyLootSpawnFailed Actor=%s Round=%s %s"),
            *GetName(), *RoundId.ToString(), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyLootSpawned Actor=%s Enemy=%s Round=%s DropId=%s %s"),
        *GetNameSafe(Drop), *GetName(), *RoundId.ToString(), *Drop->GetDropId().ToString(),
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGEnemy::ApplyDeathPresentation()
{
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EnemyVisual->SetVisibility(false);
}

void ACoopRPGEnemy::OnRep_Dead()
{
    if (bDead)
    {
        ApplyDeathPresentation();
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyDeathReplicated Actor=%s %s"),
            *GetName(), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}
