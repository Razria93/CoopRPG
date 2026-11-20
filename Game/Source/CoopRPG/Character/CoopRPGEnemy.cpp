#include "CoopRPGEnemy.h"

#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
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
    constexpr float BlackKnightMeshScale = 0.52f;
    constexpr float BlackKnightMeshYaw = -90.0f;
    constexpr float BlackKnightStrikeClipTime = 0.4f;
    const FName BlackKnightAxeSocket(TEXT("AxeSocket"));
    const FName BlackKnightShieldSocket(TEXT("ShieldSocket"));

    template <typename AssetType>
    AssetType* LoadBlackKnightAsset(const TCHAR* Name)
    {
        const FString Path = FString::Printf(TEXT("/Game/ThirdParty/BlackKnight/%s.%s"), Name, Name);
        return TSoftObjectPtr<AssetType>(FSoftObjectPath(Path)).LoadSynchronous();
    }

    constexpr double GroundRingEndAngle = 135.0;
    constexpr double GroundRingOutline = 1.25;
    constexpr double GroundRingRadius = 82.0;
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

void ACoopRPGEnemy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateBlackKnightAnimation();

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
        if (ACoopRPGCharacter* InRange = FindTarget(AttackRangeCm))
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
        const float WindupSeconds = bShieldEncounter ? ShieldWindupSeconds : EnemyWindupSeconds;
        bAttackPending = true;
        NextActionTime = Now + WindupSeconds;
        if (bShieldEncounter)
        {
            const float FacingYaw = Offset.Rotation().Yaw;
            SetActorRotation(FRotator(0.0, FacingYaw, 0.0));
            SetShieldWarning(Player->GetDisplaySlot(), FacingYaw, NextActionTime);
        }
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
    DOREPLIFETIME(ACoopRPGEnemy, bShieldEncounter);
    DOREPLIFETIME(ACoopRPGEnemy, ShieldWarning);
}

void ACoopRPGEnemy::FlashPresentationHit()
{
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

void ACoopRPGEnemy::BeginPlay()
{
    Super::BeginPlay();
    FacingMarker->SetVisibility(bShowFacingMarker);
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
    ConfigureBlackKnightPresentation();
    SetActorTickEnabled(HasAuthority() || BlackKnightIdle.IsValid());
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

ACoopRPGCharacter* ACoopRPGEnemy::FindTarget(float Range) const
{
    ACoopRPGCharacter* Nearest = nullptr;
    double NearestDistanceSquared = FMath::Square(static_cast<double>(Range));
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PlayerController = It->Get();
        auto* Player = PlayerController ? Cast<ACoopRPGCharacter>(PlayerController->GetPawn()) : nullptr;
        if (IsLivePlayer(Player))
        {
            const double DistanceSquared = FVector::DistSquared2D(GetActorLocation(), Player->GetActorLocation());
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

void ACoopRPGEnemy::SetShieldWarning(int32 TargetSlot, float FacingYaw, double EndsAt)
{
    if (EndsAt > 0.0)
    {
        ++ShieldWarning.Sequence;
    }
    else if (ShieldWarning.EndsAt == 0.0)
    {
        return;
    }

    ShieldWarning.FacingYaw = FacingYaw;
    ShieldWarning.TargetSlot = TargetSlot;
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
    NextActionTime = 0.0;
    bAttackPending = false;
}

void ACoopRPGEnemy::ResolveAttack(ACoopRPGCharacter* Player)
{
    const bool bLivePlayer = IsLivePlayer(Player);
    const double Distance = bLivePlayer ? FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()) : 0.0;
    const bool bOutsideFacing = bShieldEncounter && bLivePlayer
        && GetShieldAngle(Player->GetActorLocation()) > ShieldHalfAngleDegrees + UE_DOUBLE_KINDA_SMALL_NUMBER;
    if (!bLivePlayer || Distance > AttackRangeCm || bOutsideFacing)
    {
        const TCHAR* Reason = !bLivePlayer ? TEXT("InvalidTarget")
            : Distance > AttackRangeCm ? TEXT("OutOfRange") : TEXT("OutsideFacing");
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("EnemyAttackMissed Actor=%s Target=%s TargetPlayerId=%d Reason=%s Distance=%.2f Range=%.2f %s"),
            *GetName(), *GetNameSafe(Player), GetTargetPlayerId(Player), Reason,
            Distance, AttackRangeCm, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
        return;
    }

    const FGameplayEffectSpecHandle Damage = AbilitySystem->MakeOutgoingSpec(UCoopRPGMeleeDamage::StaticClass(), 1.0f,
        AbilitySystem->MakeEffectContext());
    check(Damage.IsValid());
    const float HealthBefore = Player->GetHealth();
    AbilitySystem->ApplyGameplayEffectSpecToTarget(*Damage.Data.Get(), Player->GetAbilitySystemComponent());
    if (const float DamageAmount = HealthBefore - Player->GetHealth(); DamageAmount > 0.0f)
    {
        if (auto* State = GetWorld()->GetGameState<ACoopRPGGameState>())
        {
            State->MulticastGameplayFeedback({ECoopRPGFeedbackKind::Damage, Player, DamageAmount, 0});
        }
    }
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
    if (BlackKnightIdle)
    {
        GetMesh()->SetVisibility(false, true);
    }
    FacingMarker->SetVisibility(false);
    UpdateShieldPresentation(false);
    GetWorldTimerManager().ClearTimer(ShieldPresentationTimer);
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

void ACoopRPGEnemy::ConfigureBlackKnightPresentation()
{
    if (bBlackKnightPresentationAttempted || !bShieldEncounter || GetNetMode() == NM_DedicatedServer
        || UGameplayStatics::GetCurrentLevelName(this) != TEXT("L_Round_01"))
    {
        return;
    }
    bBlackKnightPresentationAttempted = true;

    USkeletalMesh* MeshAsset = LoadBlackKnightAsset<USkeletalMesh>(TEXT("BlackKnight_SK"));
    if (!MeshAsset)
    {
        return;
    }
    UStaticMesh* Axe = LoadBlackKnightAsset<UStaticMesh>(TEXT("Axe_SM"));
    UStaticMesh* Shield = LoadBlackKnightAsset<UStaticMesh>(TEXT("Shield_SM"));
    UAnimSequence* Idle = LoadBlackKnightAsset<UAnimSequence>(TEXT("BlackKnight_IdleNormal_ANIM"));
    UAnimSequence* Walk = LoadBlackKnightAsset<UAnimSequence>(TEXT("BlackKnight_WalkFWD_ANIM"));
    UAnimSequence* Guard = LoadBlackKnightAsset<UAnimSequence>(TEXT("BlackKnight_IdleDefense_ANIM"));
    UAnimSequence* Attack = LoadBlackKnightAsset<UAnimSequence>(TEXT("BlackKnight_Attack01_ANIM"));
    if (!Axe || !Shield || !Idle || !Walk || !Guard || !Attack
        || Idle->GetSkeleton() != MeshAsset->GetSkeleton() || Walk->GetSkeleton() != MeshAsset->GetSkeleton()
        || Guard->GetSkeleton() != MeshAsset->GetSkeleton() || Attack->GetSkeleton() != MeshAsset->GetSkeleton()
        || Attack->GetPlayLength() <= BlackKnightStrikeClipTime)
    {
        UE_LOG(LogCoopRPGEnemy, Warning, TEXT("BlackKnight presentation assets are incomplete or incompatible"));
        return;
    }

    USkeletalMeshComponent* KnightMesh = GetMesh();
    KnightMesh->SetSkeletalMeshAsset(MeshAsset);
    if (!KnightMesh->DoesSocketExist(BlackKnightAxeSocket) || !KnightMesh->DoesSocketExist(BlackKnightShieldSocket))
    {
        KnightMesh->SetSkeletalMeshAsset(nullptr);
        UE_LOG(LogCoopRPGEnemy, Warning, TEXT("BlackKnight presentation is missing its weapon sockets"));
        return;
    }

    KnightMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    KnightMesh->SetGenerateOverlapEvents(false);
    KnightMesh->SetCanEverAffectNavigation(false);
    const FVector FeetOffset(0.0, 0.0, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    const FRotator Forward(0.0, BlackKnightMeshYaw, 0.0);
    KnightMesh->SetRelativeLocationAndRotation(FeetOffset, Forward);
    CacheInitialMeshOffset(FeetOffset, Forward);
    KnightMesh->SetRelativeScale3D(FVector(BlackKnightMeshScale));

    auto AttachEquipment = [this, KnightMesh](UStaticMesh* Asset, FName Socket, const FTransform& Offset)
    {
        UStaticMeshComponent* Equipment = NewObject<UStaticMeshComponent>(this);
        Equipment->SetStaticMesh(Asset);
        Equipment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Equipment->SetGenerateOverlapEvents(false);
        Equipment->SetCanEverAffectNavigation(false);
        Equipment->SetupAttachment(KnightMesh, Socket);
        Equipment->SetRelativeTransform(Offset);
        Equipment->RegisterComponent();
    };
    AttachEquipment(Axe, BlackKnightAxeSocket, FTransform::Identity);
    AttachEquipment(Shield, BlackKnightShieldSocket, FTransform(FRotator(0.0, 1.5791, 0.0), FVector(0.0, 4.7875, 0.0)));

    BlackKnightIdle.Reset(Idle);
    BlackKnightWalk.Reset(Walk);
    BlackKnightGuard.Reset(Guard);
    BlackKnightAttack.Reset(Attack);
    EnemyVisual->SetVisibility(false);
    FacingMarker->SetVisibility(false);
    ShieldVisual->SetVisibility(false);
    KnightMesh->SetVisibility(!bDead, true);
    SetActorTickEnabled(HasAuthority() || !bDead);
    UpdateBlackKnightAnimation();
    UE_LOG(LogCoopRPGEnemy, Log, TEXT("BlackKnightPresentationReady Mesh=%s AxeSocket=%s ShieldSocket=%s"),
        *GetNameSafe(MeshAsset), *BlackKnightAxeSocket.ToString(), *BlackKnightShieldSocket.ToString());
}

void ACoopRPGEnemy::UpdateBlackKnightAnimation()
{
    if (!BlackKnightIdle || bDead)
    {
        return;
    }

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const bool bPlaying = State && State->IsRoundPlaying();
    const double Now = State ? State->GetServerWorldTimeSeconds() : 0.0;
    if (!bPlaying)
    {
        BlackKnightStrikeAt = 0.0;
    }
    else if (ShieldWarning.EndsAt > 0.0)
    {
        BlackKnightStrikeAt = ShieldWarning.EndsAt;
        BlackKnightFacingYaw = ShieldWarning.FacingYaw;
        BlackKnightAttackSequence = ShieldWarning.Sequence;
    }

    // Keep the local pose on the replicated server timeline, including after the warning clears.
    const double AttackClipTime = Now - BlackKnightStrikeAt + BlackKnightStrikeClipTime;
    const bool bAttack = BlackKnightStrikeAt > 0.0 && AttackClipTime >= 0.0
        && AttackClipTime < BlackKnightAttack->GetPlayLength();
    const bool bGuard = bPlaying && ShieldWarning.EndsAt > Now;
    UAnimSequence* Clip = bAttack ? BlackKnightAttack.Get() : BlackKnightIdle.Get();
    if (!bAttack && bGuard)
    {
        Clip = BlackKnightGuard.Get();
    }
    else if (!bAttack && bPlaying && GetVelocity().SizeSquared2D() > FMath::Square(5.0f))
    {
        Clip = BlackKnightWalk.Get();
    }

    USkeletalMeshComponent* KnightMesh = GetMesh();
    const float RelativeYaw = bAttack || bGuard
        ? BlackKnightFacingYaw - GetActorRotation().Yaw + BlackKnightMeshYaw : BlackKnightMeshYaw;
    KnightMesh->SetRelativeRotation(FRotator(0.0, RelativeYaw, 0.0));
    if (BlackKnightCurrentClip != Clip)
    {
        BlackKnightCurrentClip = Clip;
        KnightMesh->PlayAnimation(Clip, !bAttack);
        if (bAttack)
        {
            KnightMesh->SetPlayRate(0.0f);
        }
        UE_LOG(LogCoopRPGEnemy, Log, TEXT("BlackKnightClip Name=%s Pattern=%u ServerTime=%.3f StrikeAt=%.3f FacingYaw=%.2f"),
            *GetNameSafe(Clip), BlackKnightAttackSequence, Now, BlackKnightStrikeAt, RelativeYaw + GetActorRotation().Yaw - BlackKnightMeshYaw);
    }
    if (bAttack)
    {
        KnightMesh->SetPosition(static_cast<float>(AttackClipTime), false);
        if (Now >= BlackKnightStrikeAt && BlackKnightAttackSequence != LastBlackKnightStrikeFrame)
        {
            LastBlackKnightStrikeFrame = BlackKnightAttackSequence;
            UE_LOG(LogCoopRPGEnemy, Log, TEXT("BlackKnightStrikeFrame Pattern=%u ServerTime=%.3f StrikeAt=%.3f ClipTime=%.3f"),
                BlackKnightAttackSequence, Now, BlackKnightStrikeAt, AttackClipTime);
        }
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
    const double OuterInnerEdge = GroundRingRadius - OuterRingWidth;
    const double InnerOuterEdge = OuterInnerEdge - RingPadding;
    AddStroke(OuterInnerEdge, GroundRingRadius);
    AddStroke(InnerOuterEdge - InnerRingWidth, InnerOuterEdge);

    // Start the arrow inside the outer stroke so they read as one continuous shape.
    AddRingArrow(RingMesh, GroundRingRadius - 3.0, 8.5, GroundRingRadius + 15.0, 0.05, RingOutlineMaterial);
    AddRingArrow(RingMesh, GroundRingRadius - 2.0, 6.5, GroundRingRadius + 12.5, 0.25, RingFrontMaterial);
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
    ShieldVisual->SetVisibility(bRaised && !BlackKnightIdle.IsValid());
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

void ACoopRPGEnemy::OnRep_ShieldEncounter()
{
    if (HasActorBegunPlay())
    {
        CreateGroundRing();
        ConfigureBlackKnightPresentation();
    }
}

void ACoopRPGEnemy::OnRep_ShieldWarning()
{
    GetWorldTimerManager().ClearTimer(ShieldPresentationTimer);
    const bool bVisible = IsShieldRaised();
    UpdateShieldPresentation(bVisible);
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
