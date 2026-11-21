#include "CoopRPGEnemyLoot.h"

#include "CoopRPG/Debug/CoopRPGLogContext.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEnemyLoot, Log, All);

namespace
{
    constexpr double DropFloatCm = 13.0;
    constexpr double DropBobCm = 4.0;
    constexpr double BeamGroundClearanceCm = 2.0;
}

ACoopRPGEnemyLoot::ACoopRPGEnemyLoot()
{
    bReplicates = true;
    bAlwaysRelevant = true;

    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("LootAnchor")));
    LootVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootVisual"));
    LootVisual->SetupAttachment(GetRootComponent());
    RarityEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RarityEffect"));
    RarityEffect->SetupAttachment(GetRootComponent());
    RarityEffect->SetAutoActivate(false);
    LootVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    LootVisual->SetStaticMesh(Cube.Object);
}

void ACoopRPGEnemyLoot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGEnemyLoot, DropId);

    DOREPLIFETIME_CONDITION(ACoopRPGEnemyLoot, bSkirmishDrop, COND_InitialOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGEnemyLoot, bPotion, COND_InitialOnly);
    DOREPLIFETIME_CONDITION(ACoopRPGEnemyLoot, DropOrigin, COND_InitialOnly);
    DOREPLIFETIME(ACoopRPGEnemyLoot, bClaimed);
}

void ACoopRPGEnemyLoot::ConfigureSkirmishDrop(bool bIsPotion, const FVector& Origin)
{
    check(HasAuthority());
    bSkirmishDrop = true;
    bPotion = bIsPotion;
    DropOrigin = Origin;
}

bool ACoopRPGEnemyLoot::TryClaim()
{
    if (!HasAuthority() || bClaimed)
    {
        return false;
    }

    bClaimed = true;
    ApplyClaimPresentation();
    ForceNetUpdate();
    return true;
}

void ACoopRPGEnemyLoot::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Elapsed = GetWorld()->GetTimeSeconds() - AnimationStart;
    const double Alpha = FMath::Clamp(Elapsed / 0.48, 0.0, 1.0);
    const FVector StartOffset = FVector(DropOrigin) - GetActorLocation();
    const double Lift = FMath::Sin(Alpha * UE_PI) * 66.0;
    const double Bob = Alpha >= 1.0 ? FMath::Sin((Elapsed - 0.48) * 3.0) * DropBobCm : 0.0;
    LootVisual->SetRelativeLocation(StartOffset * (1.0 - Alpha)
        + FVector(0.0, 0.0, SettledVisualOffset * Alpha + Lift + Bob));
    LootVisual->SetRelativeRotation(FRotator(0.0, Elapsed * 55.0, 0.0));
    if (!bSettled && Alpha >= 1.0)
    {
        bSettled = true;
        UE_LOG(LogCoopRPGEnemyLoot, Log, TEXT("SkirmishDropSettled Kind=%s BaseFloorGap=%.1f BeamFloorGap=%.1f"),
            bPotion ? TEXT("Potion") : TEXT("MagicStone"), DropFloatCm, BeamGroundClearanceCm);
    }
}

void ACoopRPGEnemyLoot::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        DropId = FGuid::NewGuid();
    }
    OnRep_SkirmishAppearance();
}

void ACoopRPGEnemyLoot::OnRep_DropId()
{
    UE_LOG(LogCoopRPGEnemyLoot, Log, TEXT("EnemyLootReplicated Actor=%s DropId=%s %s"),
        *GetName(), *DropId.ToString(), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGEnemyLoot::OnRep_Claimed()
{
    ApplyClaimPresentation();
    UE_LOG(LogCoopRPGEnemyLoot, Log, TEXT("EnemyLootClaimReplicated DropId=%s Claimed=%d %s"),
        *DropId.ToString(), bClaimed ? 1 : 0, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGEnemyLoot::OnRep_SkirmishAppearance()
{
    if (!HasActorBegunPlay() || !bSkirmishDrop || bAppearanceReady || GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    bAppearanceReady = true;
    const TCHAR* MeshPath = bPotion
        ? TEXT("/Game/ThirdParty/Items/Potion/Meshes/SM_Potion_V1.SM_Potion_V1")
        : TEXT("/Game/ThirdParty/Items/MagicStone/Meshes/SM_MagicRock_V1.SM_MagicRock_V1");
    if (auto* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath))
    {
        LootVisual->SetStaticMesh(Mesh);
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        const double Scale = (bPotion ? 44.0 : 14.5) / FMath::Max(Bounds.BoxExtent.GetMax(), 1.0);
        const double Bottom = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
        LootVisual->SetRelativeScale3D(FVector(Scale));
        SettledVisualOffset = DropFloatCm - GroundToAnchorCm - Bottom;
    }
    const TCHAR* EffectPath = bPotion
        ? TEXT("/Game/ThirdParty/Effects/Loot/VFX/NS_LootDrop_Uncommon.NS_LootDrop_Uncommon")
        : TEXT("/Game/ThirdParty/Effects/Loot/VFX/NS_LootDrop_Rare.NS_LootDrop_Rare");
    RarityEffect->SetAsset(LoadObject<UNiagaraSystem>(nullptr, EffectPath));
    RarityEffect->SetRelativeLocation(FVector(0.0, 0.0, BeamGroundClearanceCm - GroundToAnchorCm));
    if (!bClaimed && RarityEffect->GetAsset())
    {
        RarityEffect->Activate();
    }
    AnimationStart = GetWorld()->GetTimeSeconds();
    SetActorTickEnabled(!bClaimed);
    ApplyClaimPresentation();
    UE_LOG(LogCoopRPGEnemyLoot, Log, TEXT("SkirmishDropPresentation Kind=%s Mesh=%s Effect=%s"),
        bPotion ? TEXT("Potion") : TEXT("MagicStone"), *GetNameSafe(LootVisual->GetStaticMesh()),
        *GetNameSafe(RarityEffect->GetAsset()));
}

void ACoopRPGEnemyLoot::ApplyClaimPresentation()
{
    LootVisual->SetVisibility(!bClaimed);
    if (bClaimed)
    {
        RarityEffect->DeactivateImmediate();
        SetActorTickEnabled(false);
    }
}
