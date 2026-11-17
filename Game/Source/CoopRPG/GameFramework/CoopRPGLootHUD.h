#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CoopRPGGameplayFeedback.h"
#include "CoopRPGLootHUD.generated.h"

class ACoopRPGGameState;
class ACoopRPGPlayerController;

UCLASS()
class COOPRPG_API ACoopRPGLootHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    void ShowGameplayFeedback(const FCoopRPGGameplayFeedback& Feedback);

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
    };

    void DrawGameplayFeedback();
    void DrawWorldLabel(const FVector& Location, const FString& Text, const FLinearColor& Color,
        FVector2D Offset = FVector2D::ZeroVector, int32 ItemRow = INDEX_NONE);
    void DrawHealthNameplate(const FVector& Location, const FString& Name, float Health, const FLinearColor& Color);

    void DrawRoundWaiting(const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller, int32 Stage);
    void DrawRoundResult(const ACoopRPGGameState& State, ACoopRPGPlayerController& Controller, int32 Stage);
    void DrawExtractionStatus(const ACoopRPGGameState& State, float HintY);

private:
    TArray<FFeedbackEntry> FeedbackEntries;
};
