#include "CoopRPGExtractionZone.h"

#include "CoopRPGExpeditionMaps.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
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
    constexpr double SkirmishZoneRadius = 140.0;
    constexpr double PortalCountdownScale = 1.35;
    // The texture is padded. Its 100 cm plane is slightly larger than the 280 cm guide.
    constexpr double PortalGlyphPlaneScale = 3.0;
    constexpr double PortalGuideOutlineWidth = 8.0;
    const FLinearColor PortalGuideLockedTeal(FColor(92, 119, 105));
    const FLinearColor PortalGuideOpenTeal(FColor(46, 95, 95));
    const FName ForestPortalGateTag(TEXT("ForestPortalGate"));
    const FName ForestPortalVisualTag(TEXT("ForestPortalVisual"));

    UStaticMeshComponent* CreatePortalFloorVisual(AActor& Owner, UStaticMesh& Plane, UMaterialInterface& Material,
        const FVector& Position, double PlaneScale)
    {
        auto* Visual = NewObject<UStaticMeshComponent>(&Owner, NAME_None, RF_Transient);
        Visual->SetupAttachment(Owner.GetRootComponent());
        Visual->SetAbsolute(false, false, true);
        Visual->SetRelativeScale3D(FVector(PlaneScale, PlaneScale, 1.0));
        Visual->SetStaticMesh(&Plane);
        Visual->SetMaterial(0, &Material);
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visual->SetCanEverAffectNavigation(false);
        Visual->SetCastShadow(false);
        Owner.AddInstanceComponent(Visual);
        Visual->RegisterComponent();
        Visual->SetWorldLocation(Position);
        return Visual;
    }
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
    PortalBoundary = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("PortalBoundary"));
    PortalBoundary->SetupAttachment(GetRootComponent());
    PortalBoundary->SetAbsolute(false, false, true);
    PortalBoundary->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PortalBoundary->SetCastShadow(false);
    PortalBoundary->SetCanEverAffectNavigation(false);

    PortalFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PortalFX"));
    PortalFX->SetupAttachment(GetRootComponent());
    PortalFX->SetAbsolute(false, false, true);
    PortalFX->SetRelativeScale3D(FVector(PortalCountdownScale));
    PortalFX->SetAutoActivate(false);

    ZoneVisual->SetMaterial(0, TintableMaterial.Object);
}

bool ACoopRPGExtractionZone::ContainsPawn(const APawn& Pawn) const
{
    const FVector Offset = Pawn.GetActorLocation() - GetActorLocation();
    if (CoopRPG::Expedition::IsSkirmish(GetWorld()))
    {
        return FMath::Abs(Offset.Z) <= ZoneHalfExtent.Z
            && Offset.SizeSquared2D() <= FMath::Square(SkirmishZoneRadius);
    }
    return FBox(-ZoneHalfExtent, ZoneHalfExtent).IsInsideOrOn(Offset);
}

void ACoopRPGExtractionZone::BeginPlay()
{
    Super::BeginPlay();
    if (!CoopRPG::Expedition::IsSkirmish(GetWorld()))
    {
        return;
    }

    // The original map hides the zone actor with its old box. Keep only that box hidden in the skirmish.
    SetActorHiddenInGame(false);
    ZoneVisual->SetVisibility(false);
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    const FVector PresentationOrigin = GetActorLocation() + FVector(0.0, 0.0, 3.0);
    PortalBoundary->SetWorldLocation(PresentationOrigin);
    PortalFX->SetWorldLocation(PresentationOrigin);
    using namespace UE::Geometry;
    FDynamicMesh3 Ring;
    TArray<int32, TInlineAllocator<96>> Vertices;
    constexpr int32 Segments = 48;
    for (int32 Index = 0; Index < Segments; ++Index)
    {
        const FVector Radial = FRotator(0.0, 360.0 * Index / Segments, 0.0).Vector();
        Vertices.Add(Ring.AppendVertex(Radial * (SkirmishZoneRadius - PortalGuideOutlineWidth)));
        Vertices.Add(Ring.AppendVertex(Radial * SkirmishZoneRadius));
    }
    for (int32 Index = 0; Index < Segments; ++Index)
    {
        const int32 Current = Index * 2;
        const int32 Next = ((Index + 1) % Segments) * 2;
        Ring.AppendTriangle(Vertices[Current], Vertices[Next + 1], Vertices[Current + 1]);
        Ring.AppendTriangle(Vertices[Current], Vertices[Next], Vertices[Next + 1]);
    }
    PortalBoundary->SetMesh(MoveTemp(Ring));
    if (auto* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Presentation/Materials/M_EnemyGroundRing.M_EnemyGroundRing")))
    {
        PortalBoundaryMaterial = UMaterialInstanceDynamic::Create(Material, this);
        PortalBoundary->SetMaterial(0, PortalBoundaryMaterial);
    }

    if (auto* System = LoadObject<UNiagaraSystem>(nullptr,
        TEXT("/Game/ThirdParty/Environment/Portal/VFX/NS_MagicCircle_19_Countdown.NS_MagicCircle_19_Countdown")))
    {
        PortalFX->SetAsset(System);
        int32 EnabledEmitters = 0;
        int32 AllowedEmitters = 0;
        for (const FNiagaraEmitterHandle& Emitter : System->GetEmitterHandles())
        {
            EnabledEmitters += Emitter.GetIsEnabled();
            AllowedEmitters += Emitter.GetIsEnabled() && Emitter.IsAllowedByScalability();
        }
        UE_LOG(LogCoopRPGExtractionZone, Log,
            TEXT("SkirmishPortalSystem Asset=%s Emitters=%d Enabled=%d Allowed=%d Scale=%.2f"),
            *System->GetName(), System->GetEmitterHandles().Num(), EnabledEmitters, AllowedEmitters,
            PortalCountdownScale);
    }

    auto* GlyphMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Presentation/Materials/M_PortalGroundGlyph.M_PortalGroundGlyph"));
    auto* GlyphTexture = LoadObject<UTexture>(nullptr,
        TEXT("/Game/ThirdParty/Environment/Portal/Textures/T_Circle_04.T_Circle_04"));
    auto* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (GlyphMaterial && GlyphTexture && Plane)
    {
        auto* Glyph = UMaterialInstanceDynamic::Create(GlyphMaterial, this);
        Glyph->SetTextureParameterValue(TEXT("GlyphTexture"), GlyphTexture);
        CreatePortalFloorVisual(*this, *Plane, *Glyph, PresentationOrigin - FVector(0.0, 0.0, 0.5), PortalGlyphPlaneScale);
        PortalGlyphMaterial = Glyph;
    }

    PresentSkirmishPortal(false, false);
}

void ACoopRPGExtractionZone::PresentSkirmishPortal(bool bActive, bool bCountingDown)
{
    if (!CoopRPG::Expedition::IsSkirmish(GetWorld()) || GetNetMode() == NM_DedicatedServer || !HasActorBegunPlay())
    {
        return;
    }

    const uint8 Phase = bCountingDown ? 2 : bActive ? 1 : 0;
    if (PortalEffectPhase == Phase)
    {
        return;
    }
    PortalEffectPhase = Phase;
    const FLinearColor& WaitingTint = bActive ? PortalGuideOpenTeal : PortalGuideLockedTeal;
    const FLinearColor& GuideTint = bCountingDown ? FLinearColor::White : WaitingTint;
    const TCHAR* WaitingTone = bActive ? TEXT("Teal") : TEXT("MutedTeal");
    const TCHAR* GuideTone = bCountingDown ? TEXT("White") : WaitingTone;
    if (PortalBoundaryMaterial)
    {
        PortalBoundaryMaterial->SetVectorParameterValue(TEXT("Color"), GuideTint);
    }
    if (auto* Glyph = PortalGlyphMaterial.Get())
    {
        Glyph->SetVectorParameterValue(TEXT("GlyphColor"), GuideTint);
        Glyph->SetScalarParameterValue(TEXT("GlyphOpacity"), bCountingDown ? 0.78f : bActive ? 0.86f : 0.40f);
    }

    const bool bShowCountdown = bCountingDown && PortalFX->GetAsset();
    if (bShowCountdown)
    {
        PortalFX->Activate(true);
    }
    else
    {
        PortalFX->DeactivateImmediate();
    }
    UE_LOG(LogCoopRPGExtractionZone, Log,
        TEXT("SkirmishPortalPresentation Phase=%d Asset=%s Niagara=%d Boundary=%s Glyph=%d GlyphTone=%s"),
        Phase, *GetNameSafe(PortalFX->GetAsset()), bShowCountdown,
        GuideTone, PortalGlyphMaterial.IsValid(), GuideTone);
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
