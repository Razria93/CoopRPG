#include "CoopRPGLootHUD.h"

#include "CoopRPGGameState.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"

void ACoopRPGLootHUD::DrawHUD()
{
    Super::DrawHUD();

    const auto* Controller = Cast<ACoopRPGPlayerController>(GetOwningPlayerController());
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!Canvas || !Controller || !State || !State->GetSharedChest())
    {
        return;
    }

    constexpr float Pad = 18.0f;
    const FLinearColor Accent(0.49f, 0.82f, 1.0f);
    const ACoopRPGSharedChest* Chest = Controller->GetOpenedChest();
    DrawRect(FLinearColor(0.02f, 0.04f, 0.08f, 0.90f), Pad, Pad, 280.f, Chest ? 102.f : 78.f);
    DrawText(TEXT("SHARED CHEST"), Accent, Pad + 12.f, Pad + 8.f, nullptr, 1.15f);
    DrawText(FString::Printf(TEXT("Your recovery items: %d"), Controller->GetRecoveryCount()),
        FLinearColor::White, Pad + 12.f, Pad + 31.f);

    if (!State->IsRoundPlaying())
    {
        DrawText(Controller->IsRoundReady() ? TEXT("Waiting for the other player") : TEXT("R  Ready"),
            FLinearColor::White, Pad + 12.f, Pad + 53.f);
        return;
    }

    if (Chest)
    {
        const int32 Stock = Chest->GetRecoveryStock();
        DrawText(Stock > 0 ? FString::Printf(TEXT("Recovery item: %d"), Stock) : TEXT("Chest empty"),
            FLinearColor::White, Pad + 12.f, Pad + 53.f);
        DrawText(Stock > 0 ? TEXT("F  Take     Q  Close") : TEXT("Q  Close"),
            Accent, Pad + 12.f, Pad + 75.f);
        return;
    }

    DrawText(TEXT("Approach the chest and press E"), FLinearColor::White, Pad + 12.f, Pad + 53.f);
    const FVector Screen = Project(State->GetSharedChest()->GetActorLocation() + FVector(0, 0, 40.f));
    if (Screen.Z > 0.f && Screen.X >= 0.f && Screen.X < Canvas->ClipX - 60.f
        && Screen.Y >= 0.f && Screen.Y < Canvas->ClipY)
    {
        DrawText(TEXT("Chest"), Accent, Screen.X, Screen.Y);
    }
}
