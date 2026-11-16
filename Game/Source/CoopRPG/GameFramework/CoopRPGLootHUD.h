#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CoopRPGGameplayFeedback.h"
#include "CoopRPGLootHUD.generated.h"

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

    TArray<FFeedbackEntry> FeedbackEntries;
};
