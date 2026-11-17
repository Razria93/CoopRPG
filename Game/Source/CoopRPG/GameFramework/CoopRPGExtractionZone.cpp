#include "CoopRPGExtractionZone.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    const FVector ZoneHalfExtent(140.0f, 140.0f, 160.0f);
}

ACoopRPGExtractionZone::ACoopRPGExtractionZone()
{
    bReplicates = true;
    bAlwaysRelevant = true;

    auto* ZoneVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ZoneVisual"));
    SetRootComponent(ZoneVisual);
    ZoneVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ZoneVisual->SetRelativeScale3D(FVector(ZoneHalfExtent.X / 50.0f, ZoneHalfExtent.Y / 50.0f, 0.08f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    ZoneVisual->SetStaticMesh(Cube.Object);
}

bool ACoopRPGExtractionZone::ContainsPawn(const APawn& Pawn) const
{
    return FBox(-ZoneHalfExtent, ZoneHalfExtent).IsInsideOrOn(Pawn.GetActorLocation() - GetActorLocation());
}
