#include "CoopRPGNameplateComposite.h"

#include "BatchedElements.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"
#include "SceneView.h"
#include "SimpleElementShaders.h"
#include "TextureResource.h"

namespace
{
    class FCoopRPGNameplatePS : public FGlobalShader
    {
        DECLARE_GLOBAL_SHADER(FCoopRPGNameplatePS);
        SHADER_USE_PARAMETER_STRUCT(FCoopRPGNameplatePS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER_TEXTURE(Texture2D, PlateTexture)
            SHADER_PARAMETER_SAMPLER(SamplerState, PlateSampler)
            SHADER_PARAMETER_TEXTURE(Texture2D, HeroDepth)
            SHADER_PARAMETER(FIntPoint, ViewOrigin)
            SHADER_PARAMETER(uint32, UseHeroDepth)
            SHADER_PARAMETER(float, DisplayGamma)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FCoopRPGNameplatePS, "/Project/Private/CoopRPGNameplate.usf", "Main", SF_Pixel);

    class FNameplateParameters final : public FBatchedElementParameters
    {
    public:
        explicit FNameplateParameters(const FTexture& InHeroDepth, FIntPoint InViewOrigin)
            : HeroDepth(InHeroDepth), ViewOrigin(InViewOrigin)
        {
        }

        void BindShaders(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& Pipeline,
            ERHIFeatureLevel::Type FeatureLevel, const FMatrix& Transform, float Gamma, const FMatrix&,
            const FTexture* Texture) override
        {
            TShaderMapRef<FSimpleElementVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
            TShaderMapRef<FCoopRPGNameplatePS> PixelShader(GetGlobalShaderMap(FeatureLevel));
            Pipeline.BoundShaderState.VertexDeclarationRHI = GSimpleElementVertexDeclaration.VertexDeclarationRHI;
            Pipeline.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            Pipeline.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            Pipeline.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
            SetGraphicsPipelineState(RHICmdList, Pipeline, 0);

            const FRHITexture* DepthTexture = HeroDepth.TextureRHI.GetReference();
            FCoopRPGNameplatePS::FParameters Parameters;
            Parameters.PlateTexture = Texture->TextureRHI;
            Parameters.PlateSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp>::GetRHI();
            Parameters.HeroDepth = HeroDepth.TextureRHI.IsValid()
                ? HeroDepth.TextureRHI : GBlackTexture->TextureRHI;
            Parameters.ViewOrigin = ViewOrigin;
            Parameters.UseHeroDepth = DepthTexture != nullptr;
            Parameters.DisplayGamma = Gamma;
            SetShaderParametersLegacyVS(RHICmdList, VertexShader, Transform);
            SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters);
        }

    private:
        const FTexture& HeroDepth;
        FIntPoint ViewOrigin;
    };
}

void CoopRPG::Nameplate::DrawMaskedPlate(UCanvas& Canvas, UTextureRenderTarget2D& Plate,
    UTextureRenderTarget2D& HeroDepth, const FBox2D& Bounds)
{
    const auto* Depth = HeroDepth.GetResource();
    const auto* Texture = Plate.GetResource();
    if (!Depth || !Texture || !Canvas.SceneView)
    {
        return;
    }

    FCanvasTileItem Tile(Bounds.Min, Texture, Bounds.GetSize(), FLinearColor::White);
    Tile.BatchedElementParameters = new FNameplateParameters(*Depth, Canvas.SceneView->UnscaledViewRect.Min);
    Tile.BlendMode = SE_BLEND_Translucent;
    Canvas.DrawItem(Tile);
}
