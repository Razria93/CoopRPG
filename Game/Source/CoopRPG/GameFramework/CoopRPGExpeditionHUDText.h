#pragma once

#include "Application/SlateApplicationBase.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

namespace CoopRPG::ExpeditionUI
{
    inline const FColor PartySlotBadgeFill(59, 57, 55);
    inline const FLinearColor PartySlotBadgeInk(FColor(255, 241, 210));

    inline FSlateFontInfo Font(float Size, bool bBold = false)
    {
        auto Info = GEngine->GetMediumFont()->GetLegacySlateFontInfo();
        Info.Size = Size;
        Info.TypefaceFontName = bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
        return Info;
    }

    inline FVector2D Measure(const UCanvas& Canvas, FStringView Text, const FSlateFontInfo& FontInfo)
    {
        const auto FontMeasure = FSlateApplicationBase::Get().GetRenderer()->GetFontMeasureService();
        return FontMeasure->Measure(FString(Text), FontInfo, Canvas.Canvas->GetDPIScale());
    }

    inline FVector2D InkBounds(const UCanvas& Canvas, FStringView Text, const FSlateFontInfo& Font)
    {
        // Match FCanvasSimpleTextItem's bitmap glyph positioning. Font Measure returns the line box.
        const auto FontCache = FSlateApplicationBase::Get().GetRenderer()->GetFontCache();
        FCharacterList& Characters = FontCache->GetCharacterList(Font, Canvas.Canvas->GetDPIScale());
        const float LineHeight = Characters.GetMaxHeight();
        double Top = LineHeight;
        double Bottom = 0.0;
        for (const TCHAR Letter : Text)
        {
            const FCharacterEntry& Glyph = Characters.GetCharacter(Letter, Font.FontFallback);
            if (Glyph.Valid && Glyph.VSize > 0 && !FChar::IsWhitespace(Letter))
            {
                const float GlyphTop = (LineHeight + Glyph.GlobalDescender) / Glyph.BitmapRenderScale - Glyph.VerticalOffset;
                Top = FMath::Min(Top, static_cast<double>(GlyphTop));
                Bottom = FMath::Max(Bottom, static_cast<double>(GlyphTop + Glyph.VSize * Glyph.BitmapRenderScale));
            }
        }
        return Bottom > Top ? FVector2D(Top, Bottom) : FVector2D(0.0, LineHeight);
    }

    inline void Text(UCanvas& Canvas, FStringView Value, FVector2D Position, const FSlateFontInfo& FontInfo,
        const FLinearColor& Color)
    {
        FCanvasTextItem Item(Position, FText::FromString(FString(Value)), FontInfo, Color);
        Canvas.DrawItem(Item);
    }

    inline FString Fit(const UCanvas& Canvas, FString Value, const FSlateFontInfo& FontInfo, double Width)
    {
        if (Measure(Canvas, Value, FontInfo).X <= Width)
        {
            return Value;
        }

        const FString Ellipsis(TEXT("…"));
        while (!Value.IsEmpty())
        {
            const int32 Units = Value.Len() >= 2 && Value[Value.Len() - 1] >= 0xDC00 && Value[Value.Len() - 1] <= 0xDFFF
                && Value[Value.Len() - 2] >= 0xD800 && Value[Value.Len() - 2] <= 0xDBFF ? 2 : 1;
            Value.LeftChopInline(Units);
            if (Measure(Canvas, Value + Ellipsis, FontInfo).X <= Width)
            {
                return Value + Ellipsis;
            }
        }
        return Ellipsis;
    }

    inline FString PlayerName(const APawn& Pawn, int32 Slot)
    {
        if (const APlayerState* State = Pawn.GetPlayerState(); IsValid(State) && !State->GetPlayerName().IsEmpty())
        {
            return State->GetPlayerName();
        }
        return FString::Printf(TEXT("P%d"), Slot);
    }
}
