#pragma once

#include "GameFramework/Actor.h"
#include "CoopRPGExtractionZone.generated.h"

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
    void OpenPortal();

private:
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ZoneVisual;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> PresentationMaterial;

    FLinearColor PresentationColor = FLinearColor::Transparent;
    bool bPortalOpened = false;
};
