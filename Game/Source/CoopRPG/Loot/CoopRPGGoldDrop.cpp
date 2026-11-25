#include "CoopRPGGoldDrop.h"

#include "CoopRPG/Debug/CoopRPGLogContext.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGGoldDrop, Log, All);

namespace
{
    constexpr double SettleSeconds = 0.55;
    constexpr double PickupRadiusCm = 52.0;
    constexpr double PickupHeightCm = 115.0;
}

ACoopRPGGoldDrop::ACoopRPGGoldDrop()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Landing")));

    Rim = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rim"));
    Rim->SetupAttachment(GetRootComponent());
    Rim->SetRelativeScale3D(FVector(0.30, 0.30, 0.05));
    Rim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Rim->SetCastShadow(false);

    Face = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Face"));
    Face->SetupAttachment(GetRootComponent());
    Face->SetRelativeScale3D(FVector(0.24, 0.24, 0.025));
    Face->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Face->SetCastShadow(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Tint(
        TEXT("/Game/Presentation/Materials/M_EnemyGroundRing.M_EnemyGroundRing"));
    Rim->SetStaticMesh(Cylinder.Object);
    Rim->SetMaterial(0, Tint.Object);
    Face->SetStaticMesh(Cylinder.Object);
    Face->SetMaterial(0, Tint.Object);
}

void ACoopRPGGoldDrop::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(ACoopRPGGoldDrop, DropId, COND_InitialOnly);
}

void ACoopRPGGoldDrop::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Alpha = FMath::Clamp((GetWorld()->GetTimeSeconds() - SpawnTime) / SettleSeconds, 0.0, 1.0);
    const double Rise = (1.0 - Alpha) * 22.0 + FMath::Sin(Alpha * UE_PI) * 27.0;
    Rim->SetRelativeLocation(FVector(0.0, 0.0, 4.0 + Rise));
    Face->SetRelativeLocation(FVector(0.0, 0.0, 7.0 + Rise));
    if (Alpha >= 1.0)
    {
        SetActorTickEnabled(false);
    }
}

bool ACoopRPGGoldDrop::IsSettled() const
{
    return GetWorld()->GetTimeSeconds() - SpawnTime >= SettleSeconds;
}

bool ACoopRPGGoldDrop::IsInPickupRange(const APawn& Pawn) const
{
    const FVector Offset = Pawn.GetActorLocation() - GetActorLocation();
    return Offset.SizeSquared2D() <= FMath::Square(PickupRadiusCm) && FMath::Abs(Offset.Z) <= PickupHeightCm;
}

void ACoopRPGGoldDrop::BeginPlay()
{
    Super::BeginPlay();
    SpawnTime = GetWorld()->GetTimeSeconds();
    if (HasAuthority())
    {
        DropId = FGuid::NewGuid();
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        if (auto* Material = Rim->CreateAndSetMaterialInstanceDynamic(0))
        {
            Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(FColor(177, 105, 20)));
        }
        if (auto* Material = Face->CreateAndSetMaterialInstanceDynamic(0))
        {
            Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(FColor(255, 218, 69)));
        }
        Tick(0.0f);
        SetActorTickEnabled(true);
    }
}

void ACoopRPGGoldDrop::OnRep_DropId()
{
    UE_LOG(LogCoopRPGGoldDrop, Log, TEXT("SkirmishGoldReplicated DropId=%s %s"),
        *DropId.ToString(), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}
