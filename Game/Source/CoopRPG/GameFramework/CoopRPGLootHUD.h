#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CoopRPGEquipmentPanel.h"
#include "CoopRPGExpeditionDashboard.h"
#include "CoopRPGGameplayFeedback.h"
#include "CoopRPGPuzzleReward.h"
#include "CoopRPGPlayerState.h"
#include "CoopRPGLootHUD.generated.h"

class ACoopRPGCharacter;
class ACoopRPGEnemy;
class ACoopRPGExtractionZone;
class ACoopRPGGameState;
class ACoopRPGPlayerController;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS()
class COOPRPG_API ACoopRPGLootHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    void ShowGameplayFeedback(const FCoopRPGGameplayFeedback& Feedback);
    bool HandleHudPrimaryButton(ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    bool HandleHudWheel(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    bool IsPointerOverHud(const ACoopRPGPlayerController& Controller) const;
    void ReleaseHudPointer();

private:
    struct FFeedbackEntry
    {
        TWeakObjectPtr<AActor> Subject;
        FVector Location;
        FString Text;
        FLinearColor Color;
        double StartedAt;
        bool bNextToActor;
        int32 ItemRow = INDEX_NONE;
        bool bAtContact = false;
    };

    enum class EChestControl : uint8 { None, Potion, Sword, Take, StaminaPotion, MartialPowerScroll, ProtectionScroll, SlashCapsule, TauntCapsule, PersonalShield };

    bool HandleChestPrimaryButton(ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    void DrawChestPanel(const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller);
    void DrawExpeditionCombat(const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller, bool bBoss, bool bLegacyStatus);
    void DrawPuzzle(const ACoopRPGGameState& State, ACoopRPGPlayerController& Controller, bool bLegacyStatus);
    void DrawExpeditionNameplates(const ACoopRPGPlayerController& Controller, int32 Stage);
    UTextureRenderTarget2D* UpdateNameplateHeroDepth(ACoopRPGCharacter& Hero);
    void DrawMaskedNameplate(UTextureRenderTarget2D& HeroDepth, const FBox2D& Bounds, const FString& Label,
        const FLinearColor& Color, float Health, bool bBadge, TFunctionRef<void(FVector2D)> DrawPlate);
    void DrawTauntPulse(const FVector& Origin, double Elapsed);
    void DrawTauntMarker(const ACoopRPGEnemy& Enemy);
    void DrawCounterTargetMarker(const ACoopRPGCharacter& Target);
    void DrawPartyStatus(const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller, bool bShowTeamStone);
    void DrawLocalCombatStatus(const ACoopRPGGameState& State, const ACoopRPGCharacter& Hero,
        const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack);
    void DrawGameplayFeedback(bool bExpedition);
    void DrawLootDropLabel(const FVector& Location, bool bMagicStone);
    void DrawWorldLabel(const FVector& Location, const FString& Text, const FLinearColor& Color,
        FVector2D Offset = FVector2D::ZeroVector, int32 ItemRow = INDEX_NONE, bool bAvoidOverlap = false);
    void DrawHealthNameplate(const FVector& Location, const FString& Name, float Health, const FLinearColor& Color,
        int32 AttackTargetSlot = INDEX_NONE, int32 MaxHealth = 100);

    void DrawRoundWaiting(const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller, int32 Stage);
    bool HandlePuzzleRewardPrimaryButton(ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    void DrawPuzzleRewards(const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller);
    bool HandleBossReturnPrimaryButton(ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    void DrawExpeditionResult(const ACoopRPGGameState& State, int32 Stage);
    void DrawRoundResult(const ACoopRPGGameState& State, ACoopRPGPlayerController& Controller, int32 Stage);
    void DrawExtractionStatus(const ACoopRPGGameState& State, float HintY);
    void DrawPortalGuide(const ACoopRPGExtractionZone& Zone);

private:
    FCoopRPGEquipmentPanel EquipmentPanel;
    FCoopRPGExpeditionDashboard ExpeditionDashboard;
    FBox2D ChestPanelBounds{ForceInit};
    FBox2D ChestPotionBounds{ForceInit};
    FBox2D ChestSwordBounds{ForceInit};
    FBox2D ChestStaminaBounds{ForceInit};
    FBox2D ChestScrollBounds{ForceInit};
    FBox2D ChestProtectionBounds{ForceInit};
    FBox2D ChestSlashBounds{ForceInit};
    FBox2D ChestTauntBounds{ForceInit};
    FBox2D ChestShieldBounds{ForceInit};
    FBox2D ChestTakeBounds{ForceInit};
    FString LastChestPanelDisplay;
    EChestControl SelectedChestItem = EChestControl::None;
    bool bOwnsChestPointer = false;
    EChestControl PressedChestControl = EChestControl::None;
    FBox2D PuzzleRewardPanelBounds{ForceInit};
    TArray<FBox2D, TInlineAllocator<3>> PuzzleRewardButtonBounds;
    ECoopRPGPuzzleReward PressedPuzzleReward = ECoopRPGPuzzleReward::None;
    bool bOwnsPuzzleRewardPointer = false;
    FGuid LastPresentedPuzzleRewardsRound;
    FGuid LastPresentedExpeditionResult;
    FBox2D BossReturnPanelBounds{ForceInit};
    FBox2D BossReturnButtonBounds{ForceInit};
    bool bOwnsBossReturnPointer = false;
    bool bPressedBossReturnButton = false;
    uint8 LastBossReturnStatus = 0;
    FGuid LastPresentedPartyRound;
    struct FPartyCardValues
    {
        int32 Health;
        int32 Stamina;
        bool bArmed;
        ECoopRPGHero Hero;
        FIntPoint RecoveryUses;

        bool operator==(const FPartyCardValues& Other) const = default;
    };
    TOptional<FPartyCardValues> PartyCardValues[2];
    struct FPartyCardId
    {
        FString Raw;
        FString Display;
        double Width = -1.0;
        float DpiScale = 0.0f;
        int32 Characters = 0;
    };
    FPartyCardId PartyCardIds[2];
    FIntPoint LastPartyCardViewport{0, 0};
    FGuid LastLocalCombatRound;
    // Log sampled visual changes for comparison with authoritative attack and cooldown events.
    FIntVector4 LastLocalCombatSample{-1, -1, -1, -1};
    FIntPoint LastLocalSkillBindings{-1, -1};
    FIntPoint LastLocalSlashSample{-1, -1};
    FIntPoint LastLocalGuardSample{-1, -1};
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    FIntPoint LastGuardHudSample{-2, -1};
    FIntPoint LastGuardHelpViewport{0, 0};
    bool bLastGuardHelpVisible = false;
#endif
    TArray<FFeedbackEntry> FeedbackEntries;
    struct FNameplatePlayerName
    {
        FString Raw;
        FString Display;
    };
    FNameplatePlayerName NameplatePlayerNames[2];
    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> NameplateHeroCapture;
    TWeakObjectPtr<ACoopRPGCharacter> NameplateHero;
    struct FMaskedNameplateLayer
    {
        TWeakObjectPtr<UTextureRenderTarget2D> Target;
        FString Label;
        FLinearColor Color = FLinearColor::Transparent;
        float Health = -1.0f;
        bool bBadge = false;
    };
    TArray<FMaskedNameplateLayer> MaskedNameplateLayers;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextureRenderTarget2D>> MaskedNameplateTargets;
    int32 MaskedNameplatesUsed = 0;
    TArray<FBox2D, TInlineAllocator<8>> WorldLabelBounds;
    int32 FixedHudBoundsCount = 0;
};
