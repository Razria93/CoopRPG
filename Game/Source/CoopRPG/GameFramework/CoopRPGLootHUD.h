#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CoopRPGEquipmentPanel.h"
#include "CoopRPGExpeditionDashboard.h"
#include "CoopRPGGameplayFeedback.h"
#include "CoopRPGLootHUD.generated.h"

class ACoopRPGCharacter;
class ACoopRPGEnemy;
class ACoopRPGExtractionZone;
class ACoopRPGGameState;
class ACoopRPGPlayerController;

UCLASS()
class COOPRPG_API ACoopRPGLootHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    void ShowGameplayFeedback(const FCoopRPGGameplayFeedback& Feedback);
    bool HandleHudPrimaryButton(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    bool HandleHudWheel(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
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

    void DrawExpeditionCombat(const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller, bool bBoss, bool bLegacyStatus);
    void DrawPuzzle(const ACoopRPGGameState& State, ACoopRPGPlayerController& Controller, bool bLegacyStatus);
    void DrawExpeditionNameplates(const ACoopRPGPlayerController& Controller, int32 Stage);
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
    void DrawExpeditionResult(const ACoopRPGGameState& State, int32 Stage);
    void DrawRoundResult(const ACoopRPGGameState& State, ACoopRPGPlayerController& Controller, int32 Stage);
    void DrawExtractionStatus(const ACoopRPGGameState& State, float HintY);
    void DrawPortalGuide(const ACoopRPGExtractionZone& Zone);

private:
    FCoopRPGEquipmentPanel EquipmentPanel;
    FCoopRPGExpeditionDashboard ExpeditionDashboard;
    FGuid LastPresentedExpeditionResult;
    FGuid LastPresentedPartyRound;
    TOptional<FIntVector4> PartyCardValues[2];
    struct FPartyCardId
    {
        FString Raw;
        FString Display;
        double Width = -1.0;
        float DpiScale = 0.0f;
        int32 Characters = 0;
    };
    FPartyCardId PartyCardIds[2];
    FGuid LastLocalCombatRound;
    // Log sampled visual changes for comparison with authoritative attack and cooldown events.
    FIntVector4 LastLocalCombatSample{-1, -1, -1, -1};
    TArray<FFeedbackEntry> FeedbackEntries;
    struct FNameplatePlayerName
    {
        FString Raw;
        FString Display;
    };
    FNameplatePlayerName NameplatePlayerNames[2];
    TArray<FBox2D, TInlineAllocator<8>> WorldLabelBounds;
};
