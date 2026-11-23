#include "CoopRPGEnemy.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Character/CoopRPGLocomotionAnimInstance.h"
#include "CoopRPG/GameFramework/CoopRPGExpeditionMaps.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"
#include "CoopRPG/GameFramework/CoopRPGPlayerController.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Components/CapsuleComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEnemy, Log, All);

namespace
{
    constexpr float DetectionRangeCm = 360.0f;
    constexpr float EnemyWindupSeconds = 0.9f;
    constexpr float RecoverySeconds = 2.5f;
    constexpr float ShieldWindupSeconds = 1.5f;
    constexpr float ShieldRecoverySeconds = 0.8f;
    constexpr float MovementObservationCm = 40.0f;
    constexpr float BlackKnightStrikeClipTime = 0.4f;
    constexpr float DefaultCapsuleRadius = 34.0f;
    constexpr float ExpeditionBossCapsuleRadius = 96.0f;
    constexpr float ExpeditionBossCapsuleHalfHeight = 152.0f;
    constexpr float ExpeditionBossModelScale = 2.0f;
    constexpr float PlayerCapsuleRadiusForLegacyReach = 34.0f;
    constexpr float MushroomCapsuleRadius = 25.0f;
    constexpr float MushroomCapsuleHalfHeight = 44.0f;
    constexpr double GroundRingEndAngle = 135.0;
    constexpr double GroundRingOutline = 1.25;
    constexpr double GroundRingRadius = 82.0;
    constexpr double ExpeditionBossRingRadius = 114.0;
    constexpr double OuterRingWidth = 7.0;
    constexpr double InnerRingWidth = 2.5;
    constexpr double RingPadding = 5.0;
    constexpr int32 RingOutlineMaterial = 0;
    constexpr int32 RingSideMaterial = 1;
    constexpr int32 RingFrontMaterial = 2;
    const FLinearColor RingOutlineColor(FColor(13, 17, 20));
    const FLinearColor RingBaseColor(FColor(36, 42, 47));
    const FLinearColor RingGuardColor(FColor(255, 215, 37));

    void AddRingArc(UE::Geometry::FDynamicMesh3& Mesh, double Inner, double Outer,
        double StartAngle, double EndAngle, double Height, int32 Material)
    {
        const int32 Segments = FMath::CeilToInt((EndAngle - StartAngle) / 5.0);
        int32 LastInner = INDEX_NONE;
        int32 LastOuter = INDEX_NONE;
        for (int32 Index = 0; Index <= Segments; ++Index)
        {
            const double Angle = FMath::Lerp(StartAngle, EndAngle, static_cast<double>(Index) / Segments);
            const FVector Direction = FRotator(0.0, Angle, 0.0).Vector();
            const int32 ThisInner = Mesh.AppendVertex(Direction * Inner + FVector(0.0, 0.0, Height));
            const int32 ThisOuter = Mesh.AppendVertex(Direction * Outer + FVector(0.0, 0.0, Height));
            if (Index > 0)
            {
                auto* MaterialIds = Mesh.Attributes()->GetMaterialID();
                MaterialIds->SetValue(Mesh.AppendTriangle(LastInner, ThisOuter, LastOuter), Material);
                MaterialIds->SetValue(Mesh.AppendTriangle(LastInner, ThisInner, ThisOuter), Material);
            }
            LastInner = ThisInner;
            LastOuter = ThisOuter;
        }
    }

    void AddRingArrow(UE::Geometry::FDynamicMesh3& Mesh, double BaseX, double HalfWidth,
        double TipX, double Height, int32 Material)
    {
        const int32 Left = Mesh.AppendVertex(FVector(BaseX, -HalfWidth, Height));
        const int32 Right = Mesh.AppendVertex(FVector(BaseX, HalfWidth, Height));
        const int32 Tip = Mesh.AppendVertex(FVector(TipX, 0.0, Height));
        Mesh.Attributes()->GetMaterialID()->SetValue(Mesh.AppendTriangle(Left, Right, Tip), Material);
    }

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
    GetCapsuleComponent()->InitCapsuleSize(DefaultCapsuleRadius, 88.0f);

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
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> TintableMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    EnemyVisual->SetMaterial(0, TintableMaterial.Object);

    FacingMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FacingMarker"));
    FacingMarker->SetupAttachment(GetRootComponent());
    FacingMarker->SetStaticMesh(Cube.Object);
    FacingMarker->SetMaterial(0, TintableMaterial.Object);
    FacingMarker->SetRelativeLocation(FVector(35.0f, 0.0f, 70.0f));
    FacingMarker->SetRelativeScale3D(FVector(0.20f, 0.30f, 0.18f));
    FacingMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FacingMarker->SetVisibility(false);

    ShieldVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldVisual"));
    ShieldVisual->SetupAttachment(GetRootComponent());
    ShieldVisual->SetStaticMesh(Cube.Object);
    ShieldVisual->SetMaterial(0, TintableMaterial.Object);
    ShieldVisual->SetRelativeLocation(FVector(41.0, 0.0, 12.0));
    ShieldVisual->SetRelativeScale3D(FVector(0.12, 0.72, 0.93));
    ShieldVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ShieldVisual->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> RingMaterial(
        TEXT("/Game/Presentation/Materials/M_EnemyGroundRing.M_EnemyGroundRing"));
    GroundRingMaterial = RingMaterial.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> OcclusionMaterial(
        TEXT("/Game/Presentation/Materials/M_BlackKnightCounterOccluded.M_BlackKnightCounterOccluded"));
    CounterOcclusionMaterial = OcclusionMaterial.Object;

    Axe = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Axe"));
    Shield = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shield"));
    for (UStaticMeshComponent* Part : {Axe.Get(), Shield.Get()})
    {
        Part->SetupAttachment(GetMesh());
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
    }

    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
    AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
    CombatAttributes = CreateDefaultSubobject<UCoopRPGCombatAttributes>(TEXT("CombatAttributes"));
}

UAbilitySystemComponent* ACoopRPGEnemy::GetAbilitySystemComponent() const
{
    return AbilitySystem;
}

float ACoopRPGEnemy::GetHealth() const
{
    return CombatAttributes->GetHealth();
}

float ACoopRPGEnemy::GetMaxHealth() const
{
    return IsMushroom() && CoopRPG::Expedition::IsSkirmish(GetWorld())
        ? CoopRPG::Expedition::SkirmishEnemyHealth : UCoopRPGCombatAttributes::MaxHealth;
}

void ACoopRPGEnemy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateAttackPose();
    UpdateCounterTelegraph();

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
            NextActionTime = Now + (bShieldEncounter ? ShieldRecoverySeconds : RecoverySeconds);
            if (bShieldEncounter)
            {
                ResolvedKnightStrikeAt = ShieldWarning.EndsAt;
                SetShieldWarning(0, ShieldWarning.FacingYaw, 0.0);
            }
            ResolveAttack(Player);
        }
        return;
    }

    if (bShieldEncounter && Now < NextActionTime)
    {
        return;
    }
    if (bShieldEncounter)
    {
        if (ACoopRPGCharacter* InRange = FindTarget(GetAttackRange(), true))
        {
            Player = InRange;
            Target = InRange;
        }
    }

    if (!IsLivePlayer(Player) ||
        FVector::DistSquared2D(GetActorLocation(), Player->GetActorLocation()) > FMath::Square(DetectionRangeCm))
    {
        StopChasing();
        Player = FindTarget(DetectionRangeCm);
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
    const double Distance = GetAttackDistance(*Player);
    if (Distance > GetAttackRange())
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
        const float WindupSeconds = bShieldEncounter ? ShieldWindupSeconds : EnemyWindupSeconds;
        bAttackPending = true;
        NextActionTime = Now + WindupSeconds;
        if (bShieldEncounter)
        {
            const float FacingYaw = Offset.Rotation().Yaw;
            SetActorRotation(FRotator(0.0, FacingYaw, 0.0));
            SetShieldWarning(Player->GetDisplaySlot(), FacingYaw, NextActionTime);
        }
        if (bMushroomEncounter)
        {
            const float FacingYaw = Offset.Rotation().Yaw;
            SetActorRotation(FRotator(0.0, FacingYaw, 0.0));
            MushroomStrike = {NextActionTime, FacingYaw};
            ForceNetUpdate();
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("MushroomStrikeAuthoritative Target=%d FacingYaw=%.2f StrikeAt=%.3f"),
                GetTargetPlayerId(Player), FacingYaw, NextActionTime);
        }
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyAttackStarted Actor=%s Target=%s TargetPlayerId=%d Distance=%.2f Range=%.2f Windup=%.2f %s"),
            *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player), Distance, GetAttackRange(), WindupSeconds,
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
    DOREPLIFETIME(ACoopRPGEnemy, bShieldEncounter);
    DOREPLIFETIME(ACoopRPGEnemy, MushroomStrike);
    DOREPLIFETIME(ACoopRPGEnemy, ShieldWarning);
}

bool ACoopRPGEnemy::ShouldPreserveAttackPose() const
{
    if (!IsBlackKnight() || !bShieldEncounter || bDead)
    {
        return false;
    }

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || !State->IsRoundPlaying())
    {
        return false;
    }

    if (ShieldWarning.EndsAt > 0.0)
    {
        return true;
    }

    const double StrikeAt = HasAuthority() ? ResolvedKnightStrikeAt : KnightStrikeAt;
    return StrikeAt > 0.0 && AttackMontage
        && State->GetServerWorldTimeSeconds() < StrikeAt + AttackMontage->GetPlayLength() - BlackKnightStrikeClipTime;
}

void ACoopRPGEnemy::FlashPresentationHit(bool bPreserveAttackPose)
{
    if (!bDead && Animation && HitMontage && !bPreserveAttackPose && !ShouldPreserveAttackPose())
    {
        PendingHitUntil = GetWorld()->GetTimeSeconds() + HitMontage->GetPlayLength();
    }
    SetPresentationTint(true);
    GetWorldTimerManager().SetTimer(HitFlashTimer, this, &ThisClass::RestorePresentationTint, 0.22f);
}

void ACoopRPGEnemy::EnableShieldEncounter()
{
    check(HasAuthority());
    bShieldEncounter = true;
    ForceNetUpdate();
    OnRep_ShieldEncounter();
}

float ACoopRPGEnemy::GetMeleeRangeBonus() const
{
    return bLargeExpeditionBoss ? GetCapsuleComponent()->GetScaledCapsuleRadius() - DefaultCapsuleRadius : 0.0f;
}

bool ACoopRPGEnemy::IsShieldRaised() const
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    return !bDead && State && State->IsRoundPlaying() && ShieldWarning.EndsAt > 0.0
        && State->GetServerWorldTimeSeconds() < ShieldWarning.EndsAt;
}

double ACoopRPGEnemy::GetShieldAngle(const FVector& Position) const
{
    const FVector Facing = FRotator(0.0, ShieldWarning.FacingYaw, 0.0).Vector();
    const FVector Offset = (Position - GetActorLocation()).GetSafeNormal2D();
    const double Dot = FMath::Clamp(FVector::DotProduct(Facing, Offset), -1.0, 1.0);
    return FMath::RadiansToDegrees(FMath::Acos(Dot));
}

void ACoopRPGEnemy::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (!GetWorld()->IsGameWorld())
    {
        ApplyModel();
    }
}

void ACoopRPGEnemy::BeginPlay()
{
    Super::BeginPlay();
    bMushroomEncounter = Kind == ECoopRPGEnemyKind::Mushroom;
    bLargeExpeditionBoss = IsBlackKnight() && CoopRPG::Expedition::IsBoss(GetWorld());
    FacingMarker->SetVisibility(bShowFacingMarker && !bMushroomEncounter);
    if (bMushroomEncounter || bLargeExpeditionBoss)
    {
        const float PreviousHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        GetCapsuleComponent()->SetCapsuleSize(bLargeExpeditionBoss ? ExpeditionBossCapsuleRadius : MushroomCapsuleRadius,
            bLargeExpeditionBoss ? ExpeditionBossCapsuleHalfHeight : MushroomCapsuleHalfHeight);
        if (HasAuthority())
        {
            const float NewHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            AddActorWorldOffset(FVector(0.0, 0.0, NewHalfHeight - PreviousHalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
        }
    }
    if (bLargeExpeditionBoss)
    {
        UE_LOG(LogCoopRPGEnemy, Log,
            TEXT("ExpeditionBossGeometry Actor=%s Height=%.1f CapsuleRadius=%.1f CapsuleHalfHeight=%.1f RingOuter=%.1f CounterSurfaceRange=%.1f"),
            *GetName(), ModelHeight * ExpeditionBossModelScale, GetCapsuleComponent()->GetScaledCapsuleRadius(),
            GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), ExpeditionBossRingRadius, GetAttackRange());
    }
    if (HasAuthority() && IsBlackKnight())
    {
        EnableShieldEncounter();
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        if (auto* Material = ShieldVisual->CreateDynamicMaterialInstance(0))
        {
            Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.56f, 0.12f));
        }
        CreateGroundRing();
        OnRep_ShieldWarning();
    }
    RestorePresentationTint();
    AbilitySystem->InitAbilityActorInfo(this, this);
    ObservationOrigin = GetActorLocation();
    if (GetNetMode() != NM_DedicatedServer && ApplyModel())
    {
        StartAnimation();
    }
    if (bDead)
    {
        HideCorpse();
    }
    SetActorTickEnabled(!bDead && (HasAuthority() || Animation));
    if (HasAuthority())
    {
        if (IsMushroom() && CoopRPG::Expedition::IsSkirmish(GetWorld()))
        {
            AbilitySystem->SetNumericAttributeBase(UCoopRPGCombatAttributes::GetHealthAttribute(), GetMaxHealth());
        }
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

ACoopRPGCharacter* ACoopRPGEnemy::FindTarget(float Range, bool bMeasureAttackDistance) const
{
    ACoopRPGCharacter* Nearest = nullptr;
    double NearestDistanceSquared = FMath::Square(static_cast<double>(Range));
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PlayerController = It->Get();
        auto* Player = PlayerController ? Cast<ACoopRPGCharacter>(PlayerController->GetPawn()) : nullptr;
        if (IsLivePlayer(Player))
        {
            const double DistanceSquared = bMeasureAttackDistance ? FMath::Square(GetAttackDistance(*Player))
                : FVector::DistSquared2D(GetActorLocation(), Player->GetActorLocation());
            if (DistanceSquared > FMath::Square(static_cast<double>(Range)))
            {
                continue;
            }
            const bool bTied = bShieldEncounter && FMath::IsNearlyEqual(DistanceSquared, NearestDistanceSquared);
            if (!Nearest || (bTied ? Player->GetDisplaySlot() < Nearest->GetDisplaySlot()
                                  : DistanceSquared <= NearestDistanceSquared))
            {
                Nearest = Player;
                NearestDistanceSquared = DistanceSquared;
            }
        }
    }
    return Nearest;
}

double ACoopRPGEnemy::GetAttackDistance(const ACoopRPGCharacter& Player) const
{
    const double CenterDistance = FVector::Dist2D(GetActorLocation(), Player.GetActorLocation());
    return FMath::Max(0.0, CenterDistance - GetAttackDistanceOffset(Player));
}

double ACoopRPGEnemy::GetAttackDistanceOffset(const ACoopRPGCharacter& Player) const
{
    return bLargeExpeditionBoss
        ? static_cast<double>(GetCapsuleComponent()->GetScaledCapsuleRadius()) + Player.GetCapsuleComponent()->GetScaledCapsuleRadius()
        : 0.0;
}

float ACoopRPGEnemy::GetAttackRange() const
{
    // Preserve the old counter's reach outside both bodies as the boss capsule grows.
    return bLargeExpeditionBoss ? AttackRangeCm - DefaultCapsuleRadius - PlayerCapsuleRadiusForLegacyReach
        : AttackRangeCm;
}

void ACoopRPGEnemy::SetShieldWarning(int32 TargetSlot, float FacingYaw, double EndsAt)
{
    if (EndsAt > 0.0)
    {
        ++ShieldWarning.Sequence;
        if (bLargeExpeditionBoss)
        {
            ShieldWarning.Origin = GetActorLocation();
        }
    }
    else if (ShieldWarning.EndsAt == 0.0)
    {
        return;
    }

    ShieldWarning.FacingYaw = FacingYaw;
    ShieldWarning.TargetSlot = TargetSlot;
    ShieldWarning.StartedAt = EndsAt > 0.0 ? GetWorld()->GetTimeSeconds() : 0.0;
    ShieldWarning.EndsAt = EndsAt;
    UE_LOG(LogCoopRPGEnemy, Log, TEXT("ShieldWarning Active=%d Pattern=%u TargetSlot=%d Yaw=%.2f EndsAt=%.3f X=%.2f Y=%.2f"),
        EndsAt > 0.0, ShieldWarning.Sequence, TargetSlot, FacingYaw, EndsAt, GetActorLocation().X, GetActorLocation().Y);
    ForceNetUpdate();
    if (GetNetMode() != NM_DedicatedServer)
    {
        OnRep_ShieldWarning();
    }
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
    SetShieldWarning(0, ShieldWarning.FacingYaw, 0.0);
    ResolvedKnightStrikeAt = 0.0;
    MushroomStrike = {};
    NextActionTime = 0.0;
    bAttackPending = false;
}

void ACoopRPGEnemy::ResolveAttack(ACoopRPGCharacter* Player)
{
    const bool bLivePlayer = IsLivePlayer(Player);
    const double Distance = bLivePlayer ? GetAttackDistance(*Player) : 0.0;
    const bool bOutsideFacing = bShieldEncounter && bLivePlayer
        && GetShieldAngle(Player->GetActorLocation()) > ShieldHalfAngleDegrees + UE_DOUBLE_KINDA_SMALL_NUMBER;
    if (!bLivePlayer || Distance > GetAttackRange() || bOutsideFacing)
    {
        const TCHAR* Reason = !bLivePlayer ? TEXT("InvalidTarget")
            : Distance > GetAttackRange() ? TEXT("OutOfRange") : TEXT("OutsideFacing");
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyAttackMissed Actor=%s Target=%s TargetPlayerId=%d Reason=%s Distance=%.2f Range=%.2f %s"),
            *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player), Reason,
            Distance, GetAttackRange(), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    const FGameplayEffectSpecHandle Damage = AbilitySystem->MakeOutgoingSpec(UCoopRPGMeleeDamage::StaticClass(), 1.0f,
        AbilitySystem->MakeEffectContext());
    check(Damage.IsValid());
    const bool bGuarding = Player->IsGuarding();
    const double GuardAngle = Player->GetGuardAngle(GetActorLocation());
    const bool bGuarded = bGuarding
        && GuardAngle <= ACoopRPGCharacter::GuardHalfAngleDegrees + UE_DOUBLE_KINDA_SMALL_NUMBER;
    const auto* DefendingController = Player->GetController<ACoopRPGPlayerController>();
    const int32 Protection = DefendingController ? DefendingController->GetProtection() : 0;
    constexpr float ProtectionReductionPerPoint = 0.10f;
    constexpr float MinGuardDamageScale = 0.20f;
    const float DamageScale = bGuarded
        ? FMath::Max(MinGuardDamageScale, ACoopRPGCharacter::GuardDamageMultiplier - ProtectionReductionPerPoint * Protection)
        : 1.0f;
    UCoopRPGMeleeDamage::SetDamage(*Damage.Data.Get(), UCoopRPGMeleeDamage::BaseDamage * DamageScale);
    const float HealthBefore = Player->GetHealth();
    AbilitySystem->ApplyGameplayEffectSpecToTarget(*Damage.Data.Get(), Player->GetAbilitySystemComponent());
    if (const float DamageAmount = HealthBefore - Player->GetHealth(); DamageAmount > 0.0f)
    {
        if (auto* State = GetWorld()->GetGameState<ACoopRPGGameState>())
        {
            State->MulticastGameplayFeedback({ECoopRPGFeedbackKind::Damage, Player, DamageAmount, 0});
        }
    }
    UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyAttackHit Actor=%s Target=%s TargetPlayerId=%d Distance=%.2f Range=%.2f HP=%.2f->%.2f Guard=%d GuardAngle=%.2f Guarded=%d DamageScale=%.2f Protection=%d %s"),
        *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player), Distance, GetAttackRange(),
        HealthBefore, Player->GetHealth(), bGuarding, GuardAngle, bGuarded, DamageScale, Protection, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
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

    auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const FGuid RoundId = State ? State->GetRoundId() : FGuid();
    UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyDeathConfirmed Actor=%s Round=%s HP=%.2f->%.2f PendingAttackDiscarded=%d %s"),
        *GetName(), *RoundId.ToString(), Change.OldValue, Change.NewValue, bDiscardedAttack ? 1 : 0,
        *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
    if (CoopRPG::Expedition::IsSkirmish(GetWorld()))
    {
        const int32 Defeats = State ? State->RegisterSkirmishDefeat(this) : 0;
        if (Defeats == CoopRPG::Expedition::SkirmishPotionDefeats
            || Defeats == CoopRPG::Expedition::SkirmishEnemyCount)
        {
            SpawnSkirmishLoot(RoundId, Defeats == CoopRPG::Expedition::SkirmishPotionDefeats);
        }
    }
    else if (!CoopRPG::Expedition::IsBoss(GetWorld()))
    {
        SpawnDeathLoot(RoundId);
    }
}

void ACoopRPGEnemy::SpawnDeathLoot(const FGuid& RoundId)
{
    if (bMushroomEncounter)
    {
        SpawnSkirmishLoot(RoundId, false, true);
        return;
    }
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

void ACoopRPGEnemy::SpawnSkirmishLoot(const FGuid& RoundId, bool bPotion, bool bBoth)
{
    auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || !State->IsRoundPlaying())
    {
        return;
    }

    const FVector Feet = GetActorLocation() - GetActorUpVector() * GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Origin = Feet + FVector(0.0, 0.0, 35.0);
    const FCollisionObjectQueryParams WorldGeometry(ECC_WorldStatic);
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(SkirmishLootLanding), false, this);
    const int32 DropCount = bBoth ? 2 : 1;
    TArray<FVector, TInlineAllocator<2>> Locations;
    constexpr double SearchAngles[] = {-90.0, 90.0, 0.0, 180.0, -45.0, 45.0, -135.0, 135.0};
    for (const double SearchRadius : {85.0, 135.0, 185.0})
    {
        for (const double Angle : SearchAngles)
        {
            const FVector Point = Feet + FRotator(0.0, GetActorRotation().Yaw + Angle, 0.0).Vector() * SearchRadius;
            FHitResult Ground;
            if (!GetWorld()->LineTraceSingleByObjectType(Ground, Point + FVector(0.0, 0.0, 55.0),
                Point - FVector(0.0, 0.0, 70.0), WorldGeometry, Query) || Ground.ImpactNormal.Z < 0.7)
            {
                continue;
            }
            const FVector Landing = Ground.ImpactPoint + FVector(0.0, 0.0, ACoopRPGEnemyLoot::GroundToAnchorCm);
            if (GetWorld()->OverlapAnyTestByObjectType(Landing, FQuat::Identity, WorldGeometry,
                FCollisionShape::MakeSphere(28.0f), Query)
                || Locations.ContainsByPredicate([&Landing](const FVector& Other)
                    { return FVector::DistSquared2D(Landing, Other) < FMath::Square(85.0); }))
            {
                continue;
            }
            Locations.Add(Landing);
            if (Locations.Num() == DropCount)
            {
                break;
            }
        }
        if (Locations.Num() == DropCount)
        {
            break;
        }
    }

    // Keep the portal item obtainable even if the enemy dies against unusual level geometry.
    if (Locations.IsEmpty())
    {
        Locations.Add(Origin);
        UE_LOG(LogCoopRPGEnemy, Warning, TEXT("SkirmishLootLandingFallback Round=%s"), *RoundId.ToString());
    }
    if (bBoth && Locations.Num() == 1)
    {
        const FVector Second = Locations[0] + FVector(0.0, 0.0, 78.0);
        Locations.Add(Second);
        UE_LOG(LogCoopRPGEnemy, Warning, TEXT("SkirmishLootLandingFallback Round=%s SecondDropElevated=1"), *RoundId.ToString());
    }

    for (int32 Index = 0; Index < Locations.Num(); ++Index)
    {
        const bool bDropIsPotion = bBoth ? Index == 1 : bPotion;
        const FTransform Transform(Locations[Index]);
        auto* Drop = GetWorld()->SpawnActorDeferred<ACoopRPGEnemyLoot>(ACoopRPGEnemyLoot::StaticClass(), Transform,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Drop)
        {
            Drop->ConfigureSkirmishDrop(bDropIsPotion, Origin);
            Drop->FinishSpawning(Transform);
        }
        if (!State->RegisterEnemyLoot(Drop))
        {
            if (IsValid(Drop))
            {
                Drop->Destroy();
            }
            UE_LOG(LogCoopRPGEnemy, Error, TEXT("EnemyLootSpawnFailed Actor=%s Round=%s Kind=%s"),
                *GetName(), *RoundId.ToString(), bDropIsPotion ? TEXT("Potion") : TEXT("MagicStone"));
            continue;
        }

        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyLootSpawned Actor=%s Enemy=%s Round=%s DropId=%s Kind=%s Landing=%s"),
            *GetNameSafe(Drop), *GetName(), *RoundId.ToString(), *Drop->GetDropId().ToString(),
            bDropIsPotion ? TEXT("Potion") : TEXT("MagicStone"), *Locations[Index].ToCompactString());
    }
}

bool ACoopRPGEnemy::ApplyModel()
{
    if (Kind == ECoopRPGEnemyKind::Training)
    {
        return false;
    }
    USkeletalMesh* Asset = BodyMesh.LoadSynchronous();
    if (!Asset)
    {
        UE_LOG(LogCoopRPGEnemy, Warning, TEXT("Missing enemy mesh on %s"), *GetName());
        return false;
    }
    const FBoxSphereBounds Bounds = Asset->GetBounds();
    const bool bLargeModel = bLargeExpeditionBoss || (IsBlackKnight() && CoopRPG::Expedition::IsBoss(GetWorld()));
    const double Height = bLargeModel ? ModelHeight * ExpeditionBossModelScale : ModelHeight;
    const double Scale = Height / FMath::Max(Bounds.BoxExtent.Z * 2.0, 1.0);
    const FVector Offset(0.0, 0.0, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
        - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale);
    const FRotator Facing(0.0, ModelYaw, 0.0);
    auto* Body = GetMesh();
    Body->SetSkeletalMeshAsset(Asset);
    Body->SetRelativeScale3D(FVector(Scale));
    Body->SetRelativeLocationAndRotation(Offset, Facing);
    CacheInitialMeshOffset(Offset, Facing);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EnemyVisual->SetVisibility(false);
    FacingMarker->SetVisibility(false);
    ShieldVisual->SetVisibility(false);
    if (Kind == ECoopRPGEnemyKind::BlackKnight)
    {
        auto Attach = [Body](UStaticMeshComponent* Part, const TSoftObjectPtr<UStaticMesh>& Source, FName Socket)
        {
            if (!Body->DoesSocketExist(Socket))
            {
                UE_LOG(LogCoopRPGEnemy, Warning, TEXT("Missing equipment socket %s"), *Socket.ToString());
                return;
            }
            Part->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetIncludingScale, Socket);
            Part->SetRelativeScale3D(FVector::OneVector);
            Part->SetStaticMesh(Source.LoadSynchronous());
        };
        Attach(Axe, AxeMesh, AxeSocket);
        Attach(Shield, ShieldMesh, ShieldSocket);
        Shield->SetRelativeLocationAndRotation(FVector(0.0, 4.7875, 0.0), FRotator(0.0, 1.5791, 0.0));
    }
    return true;
}

void ACoopRPGEnemy::StartAnimation()
{
    if (!Locomotion || !AttackMontage || !HitMontage || !DeathMontage)
    {
        UE_LOG(LogCoopRPGEnemy, Warning, TEXT("Enemy animation set incomplete on %s"), *GetName());
        return;
    }
    GetMesh()->SetAnimInstanceClass(UCoopRPGLocomotionAnimInstance::StaticClass());
    Animation = Cast<UCoopRPGLocomotionAnimInstance>(GetMesh()->GetAnimInstance());
    if (!Animation)
    {
        return;
    }
    Animation->ConfigureLocomotion(Locomotion);
    const auto* Asset = GetMesh()->GetSkeletalMeshAsset();
    if (bMushroomEncounter)
    {
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("MushroomPresentationReady Mesh=%s AttackLength=%.3f Height=%.1f MeshBottom=%.2f MeshYawCorrection=%.2f MarkerVisible=%d"),
            *GetNameSafe(Asset), AttackMontage->GetPlayLength(), ModelHeight, Asset->GetBounds().Origin.Z - Asset->GetBounds().BoxExtent.Z, ModelYaw, FacingMarker->IsVisible());
    }
    else
    {
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("BlackKnightPresentationReady Mesh=%s AxeSocket=%s ShieldSocket=%s"),
            *GetNameSafe(Asset), *AxeSocket.ToString(), *ShieldSocket.ToString());
    }
}

void ACoopRPGEnemy::UpdateAttackPose()
{
    if (!Animation || bDead)
    {
        return;
    }
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State)
    {
        return;
    }
    const double Now = State->GetServerWorldTimeSeconds();
    if (!State->IsRoundPlaying())
    {
        KnightStrikeAt = 0.0;
    }
    else if (ShieldWarning.EndsAt > 0.0)
    {
        KnightStrikeAt = ShieldWarning.EndsAt;
        KnightFacingYaw = ShieldWarning.FacingYaw;
        KnightSequence = ShieldWarning.Sequence;
    }
    else if (KnightStrikeAt > Now + 0.12)
    {
        // An early clear cancels the windup. Normal impact clears can arrive slightly before local server time.
        KnightStrikeAt = 0.0;
    }
    if (ShouldPreserveAttackPose())
    {
        PendingHitUntil = 0.0;
        if (Animation->Montage_IsPlaying(HitMontage))
        {
            Animation->Montage_Stop(0.04f, HitMontage);
        }
    }

    const double StrikeAt = bMushroomEncounter ? MushroomStrike.At : KnightStrikeAt;
    const double Elapsed = Now - StrikeAt;
    const double Length = AttackMontage->GetPlayLength();
    // Map the server impact instant onto the pose that makes contact in each clip.
    constexpr double MushroomRecoveryPose = 0.28;
    constexpr double MushroomContact = 0.64;
    const double Position = bMushroomEncounter
        ? Length * (Elapsed <= 0.0 ? MushroomContact * (1.0 + Elapsed / EnemyWindupSeconds)
            : MushroomContact + (1.0 - MushroomContact) * Elapsed / MushroomRecoveryPose)
        : Elapsed + BlackKnightStrikeClipTime;
    const bool bAttack = State->IsRoundPlaying() && StrikeAt > 0.0 && Position >= 0.0 && Position < Length;
    const bool bGuard = !bAttack && IsShieldRaised() && GuardPose;
    const float Yaw = bAttack || bGuard ? (bMushroomEncounter ? MushroomStrike.FacingYaw : KnightFacingYaw) : GetActorRotation().Yaw;
    GetMesh()->SetWorldRotation(FRotator(0.0, Yaw + ModelYaw, 0.0));
    if (bAttack)
    {
        if (PresentedStrike != StrikeAt)
        {
            Animation->Montage_Play(AttackMontage);
            Animation->Montage_SetPlayRate(AttackMontage, 0.0f);
            PresentedStrike = StrikeAt;
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyAttackMontage Asset=%s StrikeAt=%.3f PresentedFacing=%.2f Pattern=%u"),
                *GetNameSafe(AttackMontage), StrikeAt, Yaw, KnightSequence);
        }
        Animation->Montage_SetPosition(AttackMontage, Position);
        if (!bMushroomEncounter && Now >= StrikeAt && KnightSequence != LastKnightImpact)
        {
            LastKnightImpact = KnightSequence;
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("BlackKnightStrikeFrame Pattern=%u ServerTime=%.3f StrikeAt=%.3f ClipTime=%.3f"),
                KnightSequence, Now, StrikeAt, Position);
        }
    }
    else if (PresentedStrike > 0.0)
    {
        Animation->Montage_Stop(0.04f, AttackMontage);
        PresentedStrike = 0.0;
    }
    if (!bAttack && PendingHitUntil > 0.0)
    {
        const double Remaining = PendingHitUntil - GetWorld()->GetTimeSeconds();
        PendingHitUntil = 0.0;
        if (Remaining > 0.0)
        {
            Animation->Montage_Play(HitMontage, 1.0f, EMontagePlayReturnType::MontageLength,
                FMath::Max(0.0, HitMontage->GetPlayLength() - Remaining));
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyHitMontage Asset=%s"), *GetNameSafe(HitMontage));
        }
    }
    const bool bHoldGuardPose = bGuard && !Animation->Montage_IsPlaying(HitMontage);
    if (bHoldGuardPose && !GuardLoop)
    {
        GuardLoop = Animation->PlaySlotAnimationAsDynamicMontage(GuardPose,
            UCoopRPGLocomotionAnimInstance::MeleeSlot, 0.06f, 0.04f, 1.0f, MAX_int32);
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyGuardPose Asset=%s Pattern=%u"), *GetNameSafe(GuardPose), KnightSequence);
    }
    else if (!bHoldGuardPose && GuardLoop)
    {
        Animation->Montage_Stop(0.04f, GuardLoop);
        GuardLoop = nullptr;
    }
    // Preserve the original clip observations. For locomotion, report the dominant blend sample.
    float Speed = 0.0f;
    float SampleTime = 0.0f;
    Animation->ReadLocomotion(Speed, SampleTime);
    const auto& Samples = Locomotion->GetBlendSamples();
    const UAnimSequenceBase* Observed = bAttack ? AttackMontage->GetFirstAnimReference() : nullptr;
    if (!Observed && Animation->Montage_IsPlaying(HitMontage))
    {
        Observed = HitMontage->GetFirstAnimReference();
    }
    if (!Observed && bGuard)
    {
        Observed = GuardPose;
    }
    if (!Observed && Samples.Num() == 2)
    {
        const float Middle = (Samples[0].SampleValue.X + Samples[1].SampleValue.X) * 0.5f;
        Observed = Samples[Speed >= Middle ? 1 : 0].Animation;
    }
    if (Observed && Observed != LastObservedClip)
    {
        LastObservedClip = Observed;
        if (bMushroomEncounter)
        {
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("MushroomClip Name=%s StrikeAt=%.3f ServerTime=%.3f PresentedFacing=%.2f Speed=%.2f"),
                *GetNameSafe(Observed), StrikeAt, Now, Yaw, Speed);
        }
        else
        {
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("BlackKnightClip Name=%s Pattern=%u ServerTime=%.3f StrikeAt=%.3f FacingYaw=%.2f Speed=%.2f"),
                *GetNameSafe(Observed), KnightSequence, Now, StrikeAt, Yaw, Speed);
        }
    }
}

void ACoopRPGEnemy::HideCorpse()
{
    GetMesh()->SetVisibility(false, true);
    SetActorTickEnabled(false);
    UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyCorpseHidden Actor=%s"), *GetName());
    if (bMushroomEncounter)
    {
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("MushroomCorpseHidden Actor=%s"), *GetName());
    }
}

void ACoopRPGEnemy::ApplyDeathPresentation()
{
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EnemyVisual->SetVisibility(false);
    FacingMarker->SetVisibility(false);
    UpdateShieldPresentation(false);
    GetWorldTimerManager().ClearTimer(ShieldPresentationTimer);
    if (Animation && DeathMontage && !GetWorldTimerManager().IsTimerActive(CorpseTimer))
    {
        Animation->Montage_Play(DeathMontage);
        if (bMushroomEncounter)
        {
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("MushroomClip Name=%s"), *GetNameSafe(DeathMontage->GetFirstAnimReference()));
        }
        GetWorldTimerManager().SetTimer(CorpseTimer, this, &ThisClass::HideCorpse, DeathMontage->GetPlayLength());
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyDeathMontage Asset=%s"), *GetNameSafe(DeathMontage));
    }
    else if (!Animation)
    {
        GetMesh()->SetVisibility(false, true);
    }
}

void ACoopRPGEnemy::SetPresentationTint(bool bDarkened)
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    if (!PresentationMaterial)
    {
        PresentationMaterial = EnemyVisual->CreateDynamicMaterialInstance(0);
    }
    if (PresentationMaterial)
    {
        const FLinearColor Color = CoopRPG::Presentation::EnemyColor();
        PresentationMaterial->SetVectorParameterValue(TEXT("Color"), bDarkened ? Color * 0.25f : Color);
    }
}

void ACoopRPGEnemy::RestorePresentationTint()
{
    SetPresentationTint(false);
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

void ACoopRPGEnemy::CreateGroundRing()
{
    if (!bShieldEncounter || GroundRing || !GroundRingMaterial || GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    using namespace UE::Geometry;
    FDynamicMesh3 RingMesh;
    RingMesh.EnableAttributes();
    RingMesh.Attributes()->EnableMaterialID();
    RingMesh.EnableVertexNormals(FVector3f(0.0f, 0.0f, 1.0f));

    const auto AddStroke = [&RingMesh](double Inner, double Outer)
    {
        AddRingArc(RingMesh, Inner - GroundRingOutline, Outer + GroundRingOutline,
            -GroundRingEndAngle, GroundRingEndAngle, 0.0, RingOutlineMaterial);
        AddRingArc(RingMesh, Inner, Outer, -GroundRingEndAngle, -ShieldHalfAngleDegrees, 0.15, RingSideMaterial);
        AddRingArc(RingMesh, Inner, Outer, -ShieldHalfAngleDegrees, ShieldHalfAngleDegrees, 0.15, RingFrontMaterial);
        AddRingArc(RingMesh, Inner, Outer, ShieldHalfAngleDegrees, GroundRingEndAngle, 0.15, RingSideMaterial);
    };
    const double Radius = bLargeExpeditionBoss ? ExpeditionBossRingRadius : GroundRingRadius;
    const double OuterInnerEdge = Radius - OuterRingWidth;
    const double InnerOuterEdge = OuterInnerEdge - RingPadding;
    AddStroke(OuterInnerEdge, Radius);
    AddStroke(InnerOuterEdge - InnerRingWidth, InnerOuterEdge);

    // Start the arrow inside the outer stroke so they read as one continuous shape.
    AddRingArrow(RingMesh, Radius - 3.0, 8.5, Radius + 15.0, 0.05, RingOutlineMaterial);
    AddRingArrow(RingMesh, Radius - 2.0, 6.5, Radius + 12.5, 0.25, RingFrontMaterial);
    FMeshNormals::InitializeOverlayToPerVertexNormals(RingMesh.Attributes()->PrimaryNormals());

    GroundRing = NewObject<UDynamicMeshComponent>(this);
    GroundRing->SetupAttachment(GetRootComponent());
    GroundRing->SetRelativeLocation(FVector(0.0, 0.0, 3.0 - GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
    GroundRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GroundRing->SetGenerateOverlapEvents(false);
    GroundRing->SetCanEverAffectNavigation(false);
    GroundRing->SetCastShadow(false);
    GroundRing->SetReceivesDecals(false);
    GroundRing->SetAffectDistanceFieldLighting(false);

    auto* Outline = UMaterialInstanceDynamic::Create(GroundRingMaterial, GroundRing);
    auto* Sides = UMaterialInstanceDynamic::Create(GroundRingMaterial, GroundRing);
    GroundRingFrontMaterial = UMaterialInstanceDynamic::Create(GroundRingMaterial, GroundRing);
    Outline->SetVectorParameterValue(TEXT("Color"), RingOutlineColor);
    Sides->SetVectorParameterValue(TEXT("Color"), RingBaseColor);
    GroundRingFrontMaterial->SetVectorParameterValue(TEXT("Color"), RingBaseColor);
    GroundRing->ConfigureMaterialSet({Outline, Sides, GroundRingFrontMaterial});
    GroundRing->SetMesh(MoveTemp(RingMesh));
    UpdateShieldPresentation(IsShieldRaised());
    GroundRing->RegisterComponent();
}

void ACoopRPGEnemy::UpdateShieldPresentation(bool bRaised)
{
    ShieldVisual->SetVisibility(bRaised && !GetMesh()->GetSkeletalMeshAsset());
    if (!GroundRing)
    {
        return;
    }

    GroundRing->SetVisibility(!bDead);
    GroundRing->SetAbsolute(false, bRaised, false);
    if (bRaised)
    {
        GroundRing->SetWorldRotation(FRotator(0.0, ShieldWarning.FacingYaw, 0.0));
    }
    else
    {
        GroundRing->SetRelativeRotation(FRotator::ZeroRotator);
    }
    GroundRingFrontMaterial->SetVectorParameterValue(TEXT("Color"), bRaised ? RingGuardColor : RingBaseColor);
}

void ACoopRPGEnemy::UpdateCounterTelegraph()
{
    if (!bLargeExpeditionBoss || GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const ACoopRPGCharacter* WarningTarget = nullptr;
    if (State && ShieldWarning.TargetSlot > 0 && IsShieldRaised())
    {
        for (const APlayerState* Participant : State->PlayerArray)
        {
            const auto* Hero = IsValid(Participant) ? Cast<ACoopRPGCharacter>(Participant->GetPawn()) : nullptr;
            if (IsValid(Hero) && Hero->GetDisplaySlot() == ShieldWarning.TargetSlot && Hero->GetHealth() > 0.0f)
            {
                WarningTarget = Hero;
                break;
            }
        }
    }

    const bool bWasVisible = CounterOutline && CounterOutline->IsVisible();
    if (!WarningTarget || !GroundRingMaterial)
    {
        if (bWasVisible)
        {
            CounterOutline->SetVisibility(false);
            CounterFill->SetVisibility(false);
            if (CounterOccludedOutline)
            {
                CounterOccludedOutline->SetVisibility(false);
                GetMesh()->SetRenderCustomDepth(false);
            }
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("CounterTelegraph Active=0 Pattern=%u"), DisplayedCounterPattern);
        }
        return;
    }

    const double Radius = GetAttackRange() + GetAttackDistanceOffset(*WarningTarget);
    if (!FMath::IsNearlyEqual(Radius, CounterTelegraphRadius))
    {
        CreateCounterTelegraph(Radius);
    }

    const double Progress = FMath::GetMappedRangeValueClamped(
        FVector2D(ShieldWarning.StartedAt, ShieldWarning.EndsAt), FVector2D(0.0, 1.0), State->GetServerWorldTimeSeconds());
    const bool bNewWarning = !bWasVisible || DisplayedCounterPattern != ShieldWarning.Sequence;
    const FVector Floor = ShieldWarning.Origin - FVector::UpVector * GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    if (bNewWarning)
    {
        const FRotator Facing(0.0, ShieldWarning.FacingYaw, 0.0);
        CounterOutline->SetWorldLocationAndRotation(Floor + FVector(0.0, 0.0, 2.3), Facing);
        CounterFill->SetWorldLocationAndRotation(Floor + FVector(0.0, 0.0, 1.7), Facing);
        if (CounterOccludedOutline)
        {
            CounterOccludedOutline->SetWorldLocationAndRotation(Floor + FVector(0.0, 0.0, 2.3), Facing);
        }
    }
    CounterFill->SetWorldScale3D(FVector(FMath::Max(Progress, 0.001), FMath::Max(Progress, 0.001), 1.0));
    CounterOutline->SetVisibility(true);
    if (CounterOccludedOutline && !bWasVisible)
    {
        GetMesh()->SetRenderCustomDepth(true);
        CounterOccludedOutline->SetVisibility(true);
    }
    CounterFill->SetVisibility(Progress > 0.0);
    if (bNewWarning)
    {
        DisplayedCounterPattern = ShieldWarning.Sequence;
        UE_LOG(LogCoopRPGEnemy, Log,
            TEXT("CounterTelegraph Active=1 Pattern=%u TargetSlot=%d Yaw=%.2f Radius=%.2f StartAt=%.3f EndsAt=%.3f Progress=%.3f OriginX=%.2f OriginY=%.2f OriginZ=%.2f"),
            DisplayedCounterPattern, ShieldWarning.TargetSlot, ShieldWarning.FacingYaw, Radius,
            ShieldWarning.StartedAt, ShieldWarning.EndsAt, Progress, Floor.X, Floor.Y, Floor.Z);
    }
}

void ACoopRPGEnemy::CreateCounterTelegraph(double Radius)
{
    using namespace UE::Geometry;
    const auto MakeLayer = [this](FName Name, UMaterialInterface* LayerMaterial, const FColor& Color)
    {
        auto* Layer = NewObject<UDynamicMeshComponent>(this, Name);
        Layer->SetupAttachment(GetRootComponent());
        Layer->SetAbsolute(true, true, true);
        Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Layer->SetGenerateOverlapEvents(false);
        Layer->SetCanEverAffectNavigation(false);
        Layer->SetCastShadow(false);
        Layer->SetReceivesDecals(false);
        Layer->SetAffectDistanceFieldLighting(false);
        if (LayerMaterial == GroundRingMaterial)
        {
            auto* Material = UMaterialInstanceDynamic::Create(LayerMaterial, Layer);
            Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color));
            Layer->SetMaterial(0, Material);
        }
        else
        {
            Layer->SetMaterial(0, LayerMaterial);
        }
        Layer->SetVisibility(false);
        Layer->RegisterComponent();
        return Layer;
    };
    if (!CounterOutline)
    {
        // Both layers sit below the existing ground ring (3 cm) and above the walkable floor.
        CounterOutline = MakeLayer(TEXT("CounterOutline"), GroundRingMaterial, FColor(255, 226, 74));
        CounterFill = MakeLayer(TEXT("CounterFill"), GroundRingMaterial, FColor(197, 155, 31));
        if (CounterOcclusionMaterial)
        {
            CounterOccludedOutline = MakeLayer(TEXT("CounterOccludedOutline"), CounterOcclusionMaterial, FColor::White);
        }
    }

    FDynamicMesh3 Outline;
    Outline.EnableAttributes();
    Outline.Attributes()->EnableMaterialID();
    Outline.EnableVertexNormals(FVector3f(0.0f, 0.0f, 1.0f));
    constexpr double StrokeWidth = 3.5;
    AddRingArc(Outline, Radius - StrokeWidth, Radius, -ShieldHalfAngleDegrees, ShieldHalfAngleDegrees, 0.0, 0);
    const auto AddSide = [](FDynamicMesh3& Geometry, double Angle, double StartDistance, double EndDistance, double Width)
    {
        const FVector Edge = FRotator(0.0, Angle, 0.0).Vector();
        const FVector Perp = FVector::CrossProduct(FVector::UpVector, Edge) * (Width * 0.5);
        const FVector Start = Edge * StartDistance;
        const FVector End = Edge * EndDistance;
        const int32 StartLeft = Geometry.AppendVertex(Start - Perp);
        const int32 StartRight = Geometry.AppendVertex(Start + Perp);
        const int32 EndLeft = Geometry.AppendVertex(End - Perp);
        const int32 EndRight = Geometry.AppendVertex(End + Perp);
        Geometry.AppendTriangle(StartLeft, StartRight, EndRight);
        Geometry.AppendTriangle(StartLeft, EndRight, EndLeft);
    };
    for (const double Angle : {-ShieldHalfAngleDegrees, ShieldHalfAngleDegrees})
    {
        AddSide(Outline, Angle, 0.0, Radius - StrokeWidth * 0.5, StrokeWidth);
    }
    FMeshNormals::InitializeOverlayToPerVertexNormals(Outline.Attributes()->PrimaryNormals());
    CounterOutline->SetMesh(MoveTemp(Outline));

    if (CounterOccludedOutline)
    {
        // Mirror the floor outline. The material draws these dashes only over pixels occluded by the knight.
        FDynamicMesh3 Dashes;
        Dashes.EnableAttributes();
        Dashes.Attributes()->EnableMaterialID();
        Dashes.EnableVertexNormals(FVector3f(0.0f, 0.0f, 1.0f));
        constexpr double DashWidth = 2.4;
        constexpr double DashLength = 9.0;
        constexpr double DashPeriod = 16.0;
        const double SideLength = Radius - StrokeWidth * 0.5;
        for (const double Angle : {-ShieldHalfAngleDegrees, ShieldHalfAngleDegrees})
        {
            for (double Distance = 0.0; Distance < SideLength; Distance += DashPeriod)
            {
                AddSide(Dashes, Angle, Distance, FMath::Min(Distance + DashLength, SideLength), DashWidth);
            }
        }
        const double DashAngle = FMath::RadiansToDegrees(DashLength / Radius);
        const double DashStep = FMath::RadiansToDegrees(DashPeriod / Radius);
        for (double Angle = -ShieldHalfAngleDegrees; Angle < ShieldHalfAngleDegrees; Angle += DashStep)
        {
            const double EndAngle = FMath::Min(Angle + DashAngle, ShieldHalfAngleDegrees);
            AddRingArc(Dashes, Radius - (StrokeWidth + DashWidth) * 0.5, Radius - (StrokeWidth - DashWidth) * 0.5,
                Angle, EndAngle, 0.0, 0);
        }
        FMeshNormals::InitializeOverlayToPerVertexNormals(Dashes.Attributes()->PrimaryNormals());
        CounterOccludedOutline->SetMesh(MoveTemp(Dashes));
    }

    FDynamicMesh3 Fill;
    Fill.EnableAttributes();
    Fill.EnableVertexNormals(FVector3f(0.0f, 0.0f, 1.0f));
    const int32 Center = Fill.AppendVertex(FVector::ZeroVector);
    constexpr int32 Segments = 24;
    int32 Previous = INDEX_NONE;
    for (int32 Index = 0; Index <= Segments; ++Index)
    {
        const double Angle = FMath::Lerp(-ShieldHalfAngleDegrees, ShieldHalfAngleDegrees, static_cast<double>(Index) / Segments);
        const int32 Vertex = Fill.AppendVertex(FRotator(0.0, Angle, 0.0).Vector() * Radius);
        if (Previous != INDEX_NONE)
        {
            Fill.AppendTriangle(Center, Vertex, Previous);
        }
        Previous = Vertex;
    }
    FMeshNormals::InitializeOverlayToPerVertexNormals(Fill.Attributes()->PrimaryNormals());
    CounterFill->SetMesh(MoveTemp(Fill));
    CounterTelegraphRadius = Radius;
}

void ACoopRPGEnemy::OnRep_ShieldEncounter()
{
    if (HasActorBegunPlay())
    {
        CreateGroundRing();
    }
}

void ACoopRPGEnemy::OnRep_ShieldWarning()
{
    GetWorldTimerManager().ClearTimer(ShieldPresentationTimer);
    const bool bVisible = IsShieldRaised();
    UpdateShieldPresentation(bVisible);
    UpdateCounterTelegraph();
    if (bVisible)
    {
        const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
        const float Remaining = ShieldWarning.EndsAt - State->GetServerWorldTimeSeconds();
        GetWorldTimerManager().SetTimer(ShieldPresentationTimer,
            FTimerDelegate::CreateWeakLambda(this, [this] { UpdateShieldPresentation(false); }), Remaining, false);
    }
    if (ShieldWarning.Sequence != 0)
    {
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("ShieldWarningReplicated Active=%d Pattern=%u TargetSlot=%d Yaw=%.2f EndsAt=%.3f Visible=%d RingVisible=%d"),
            ShieldWarning.EndsAt > 0.0, ShieldWarning.Sequence, ShieldWarning.TargetSlot, ShieldWarning.FacingYaw, ShieldWarning.EndsAt,
            bVisible, GroundRing && GroundRing->IsVisible());
    }
}
