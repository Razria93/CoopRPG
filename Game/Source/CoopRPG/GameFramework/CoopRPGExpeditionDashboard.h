#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

namespace CoopRPG::ExpeditionUI { struct FAttackHudStatus; }

class ACoopRPGGameState;
class ACoopRPGPlayerController;
class AHUD;
class UCanvas;
class UTexture2D;
struct FInputKeyEventArgs;

enum class ECoopRPGDashboardLabel : uint8;

enum class ECoopRPGPartyCardAccent : uint8
{
    Local,
    HealthPotion,
    Equipped = 3, // Keep the equipped and unarmed cells at their atlas positions.
    Unarmed
};

enum class ECoopRPGResourceTrack : uint8
{
    Health,
    Stamina,
    Guard
};

class FCoopRPGExpeditionDashboard
{
public:
    FCoopRPGExpeditionDashboard();
    ~FCoopRPGExpeditionDashboard();

    bool Prepare();
    void Draw(AHUD& HUD, UCanvas& Canvas, const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller,
        int32 Stage, const FVector2D& ViewSize);
    static const TCHAR* StageTitle(int32 Stage);
    void DrawPanel(AHUD& HUD, FVector2D Position, FVector2D Size) const;
    void DrawDivider(AHUD& HUD, FVector2D Position, float Width) const;
    void DrawDarkPanel(AHUD& HUD, FVector2D Position, FVector2D Size) const;
    void DrawHealthBar(AHUD& HUD, FVector2D Position, float Width, float HealthFraction) const;
    void DrawResourceTrack(AHUD& HUD, FVector2D Position, FVector2D Size, float Fraction, ECoopRPGResourceTrack Resource) const;
    void DrawSkillSlot(AHUD& HUD, FVector2D Position, float Size) const;
    void DrawAttackIcon(AHUD& HUD, FVector2D Position, float Size, bool bDisabled) const;
    void DrawBasicAttackGlyph(AHUD& HUD, UCanvas& Canvas, const FBox2D& Slot,
        const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack, bool bSlash = false) const;
    void DrawPartyCardAccent(AHUD& HUD, FVector2D Position, FVector2D Size, ECoopRPGPartyCardAccent Accent) const;
    const FBox2D& GetPanelBounds() const { return StatusBounds; }
    const FBox2D& GetNoticeBounds() const { return NoticeBounds; }
    const FBox2D& GetBossHealthBounds() const { return BossHealthBounds; }
    void Hide();
    bool HandlePrimaryButton(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    void ReleasePointer();

private:
    struct FRecord
    {
        ECoopRPGDashboardLabel Label;
        int32 Value;
    };

    void AddRecord(ECoopRPGDashboardLabel Label, int32 Value = INDEX_NONE);
    void Update(const ACoopRPGGameState& State, int32 Stage, ECoopRPGDashboardLabel Goal);
    void DrawReadyParty(AHUD& HUD, UCanvas& Canvas, const ACoopRPGGameState& State,
        const ACoopRPGPlayerController& Controller, float Left, float Right, float Top, float BadgeSide, float PartyGap, bool bWide) const;
    void DrawNotice(AHUD& HUD, UCanvas& Canvas, const ACoopRPGGameState& State, ECoopRPGDashboardLabel Goal,
        const FVector2D& ViewSize, float Pad, float PanelWidth, bool bWide, bool bBossActive);
    void DrawBossHealth(AHUD& HUD, UCanvas& Canvas, const ACoopRPGGameState& State, bool bBossActive, bool bWide);
    void DrawNineSlice(AHUD& HUD, FVector2D Position, FVector2D Size, const FBox2D& Source,
        FVector2D StartInset, FVector2D EndInset) const;
    void DrawImage(AHUD& HUD, const FBox2D& Source, const FVector2D& Position, const FVector2D& Size) const;

private:
    TStrongObjectPtr<UTexture2D> Skin;
    TArray<FRecord> Records;
    FGuid Round;
    FBox2D StatusBounds{ForceInit};
    FBox2D NoticeBounds{ForceInit};
    FBox2D BossHealthBounds{ForceInit};
    TOptional<FIntPoint> DisplayedBossHealth;
    FBox2D LogBounds{ForceInit};
    FBox2D ToggleBounds{ForceInit};
    TOptional<ECoopRPGDashboardLabel> LastGoal;
    int32 LastDefeats = 0;
    int32 LastInside = 0;
    bool bLastStone = false;
    bool bLastBossRewards = false;
    bool bLastCountdown = false;
    bool bInitialized = false;
    bool bExpanded = false;
    bool bOwnsPrimaryButton = false;
    bool bPressedToggle = false;
    bool bLogVisible = false;
    bool bToggleVisible = false;
};
