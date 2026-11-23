#pragma once

#include "GameFramework/Actor.h"
#include "CoopRPGExtractionZone.generated.h"

class UDynamicMeshComponent;
class UNiagaraComponent;
class APawn;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UCLASS()
class COOPRPG_API ACoopRPGExtractionZone : public AActor
{
    GENERATED_BODY()

public:
    ACoopRPGExtractionZone();

    bool ContainsPawn(const APawn& Pawn) const;
    bool IsPortalOpen() const { return bPortalOpened; }
    void SetPresentationColor(const FLinearColor& Color);
    void PresentExpeditionPortal(bool bActive, bool bCountingDown);
    void OpenPortal();

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ZoneVisual;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> PresentationMaterial;

    UPROPERTY()
    TObjectPtr<UDynamicMeshComponent> PortalBoundary;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> PortalBoundaryMaterial;

    UPROPERTY()
    TObjectPtr<UNiagaraComponent> PortalFX;

    TWeakObjectPtr<UMaterialInstanceDynamic> PortalGlyphMaterial;
    uint8 PortalEffectPhase = MAX_uint8;

    FLinearColor PresentationColor = FLinearColor::Transparent;
    bool bPortalOpened = false;
};
