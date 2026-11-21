#include "CoopRPGExtractionZone.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGExtractionZone, Log, All);

namespace
{
    const FVector ZoneHalfExtent(140.0f, 140.0f, 160.0f);
    const FName ForestPortalGateTag(TEXT("ForestPortalGate"));
    const FName ForestPortalVisualTag(TEXT("ForestPortalVisual"));
}

ACoopRPGExtractionZone::ACoopRPGExtractionZone()
{
    bReplicates = true;
    bAlwaysRelevant = true;

    ZoneVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ZoneVisual"));
    SetRootComponent(ZoneVisual);
    ZoneVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ZoneVisual->SetRelativeScale3D(FVector(ZoneHalfExtent.X / 50.0f, ZoneHalfExtent.Y / 50.0f, 0.08f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    ZoneVisual->SetStaticMesh(Cube.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> TintableMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    ZoneVisual->SetMaterial(0, TintableMaterial.Object);
}

bool ACoopRPGExtractionZone::ContainsPawn(const APawn& Pawn) const
{
    return FBox(-ZoneHalfExtent, ZoneHalfExtent).IsInsideOrOn(Pawn.GetActorLocation() - GetActorLocation());
}

void ACoopRPGExtractionZone::SetPresentationColor(const FLinearColor& Color)
{
    if (PresentationColor == Color)
    {
        return;
    }
    if (!PresentationMaterial)
    {
        PresentationMaterial = ZoneVisual->CreateDynamicMaterialInstance(0);
    }
    if (PresentationMaterial)
    {
        PresentationMaterial->SetVectorParameterValue(TEXT("Color"), Color);
        PresentationColor = Color;
    }
}

void ACoopRPGExtractionZone::OpenPortal()
{
    if (bPortalOpened)
    {
        return;
    }

    TArray<AActor*> GateActors;
    UGameplayStatics::GetAllActorsWithTag(this, ForestPortalGateTag, GateActors);
    if (GateActors.IsEmpty())
    {
        return;
    }

    bPortalOpened = true;
    for (AActor* Gate : GateActors)
    {
        Gate->SetActorEnableCollision(false);
        Gate->SetActorHiddenInGame(true);
    }

    UMaterialInterface* OpenMaterial = nullptr;
    if (GetNetMode() != NM_DedicatedServer)
    {
        TArray<AActor*> PortalActors;
        UGameplayStatics::GetAllActorsWithTag(this, ForestPortalVisualTag, PortalActors);
        for (AActor* Portal : PortalActors)
        {
            if (auto* Surface = Portal->FindComponentByClass<UStaticMeshComponent>(); Surface && Surface->GetStaticMesh())
            {
                OpenMaterial = Surface->GetStaticMesh()->GetMaterial(0);
                Surface->SetMaterial(0, OpenMaterial);
            }
        }
    }

    UE_LOG(LogCoopRPGExtractionZone, Log, TEXT("ForestPortalOpened GateActors=%d PortalMaterial=%s Authority=%d"),
        GateActors.Num(), *GetNameSafe(OpenMaterial), HasAuthority());
}
