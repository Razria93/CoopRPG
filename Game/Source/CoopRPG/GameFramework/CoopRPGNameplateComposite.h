#pragma once

#include "CoreMinimal.h"

class UCanvas;
class UTextureRenderTarget2D;

namespace CoopRPG::Nameplate
{
    void DrawMaskedPlate(UCanvas& Canvas, UTextureRenderTarget2D& Plate, UTextureRenderTarget2D& HeroDepth,
        const FBox2D& Bounds);
}
