#include "CoopRPGLootHUD.h"

#include "CoopRPGGameState.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
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
    constexpr float TextX = Pad + 12.0f;
    const FLinearColor Accent(0.49f, 0.82f, 1.0f);
    const auto* Character = Cast<ACoopRPGCharacter>(Controller->GetPawn());
    const ACoopRPGSharedChest* Chest = Controller->GetOpenedChest();
    const bool bPlaying = State->IsRoundPlaying();
    const bool bShowStock = bPlaying && Chest;
    const int32 RecoveryCount = Controller->GetRecoveryCount();

    DrawRect(FLinearColor(0.02f, 0.04f, 0.08f, 0.90f), Pad, Pad, 306.0f, bShowStock ? 142.0f : 106.0f);
    DrawText(TEXT("SHARED CHEST"), Accent, TextX, Pad + 8.0f, nullptr, 1.15f);
    if (Character)
    {
        DrawText(FString::Printf(TEXT("HP: %.0f   |   Melee: %s"), Character->GetHealth(),
            Character->HasMeleeWeapon() ? TEXT("Equipped") : TEXT("Unarmed")), FLinearColor::White, TextX, Pad + 31.0f);
    }
    DrawText(FString::Printf(TEXT("Your recovery items: %d%s"), RecoveryCount,
        RecoveryCount > 0 && bPlaying ? TEXT("   [H] Use") : TEXT("")), FLinearColor::White, TextX, Pad + 51.0f);

    if (!bPlaying)
    {
        DrawText(Controller->IsRoundReady() ? TEXT("Waiting for the other player") : TEXT("R  Ready"),
            Accent, TextX, Pad + 78.0f);
        return;
    }

    if (bShowStock)
    {
        const int32 Weapons = Chest->GetWeaponStock();
        const int32 Medicines = Chest->GetRecoveryStock();
        DrawText(FString::Printf(TEXT("Melee weapons: %d%s"), Weapons,
            Weapons > 0 && Character && !Character->HasMeleeWeapon() ? TEXT("   [G] Equip") : TEXT("")),
            FLinearColor::White, TextX, Pad + 76.0f);
        DrawText(FString::Printf(TEXT("Recovery items: %d%s"), Medicines,
            Medicines > 0 ? TEXT("   [F] Take") : TEXT("")),
            FLinearColor::White, TextX, Pad + 96.0f);
        DrawText(Weapons == 0 && Medicines == 0 ? TEXT("Chest empty   |   Q  Close") : TEXT("Q  Close"),
            Accent, TextX, Pad + 116.0f);
        return;
    }

    DrawText(TEXT("Approach the chest and press E"), Accent, TextX, Pad + 78.0f);
    const FVector Screen = Project(State->GetSharedChest()->GetActorLocation() + FVector(0, 0, 40.0f));
    if (Screen.Z > 0.0f && Screen.X >= 0.0f && Screen.X < Canvas->ClipX - 60.0f
        && Screen.Y >= 0.0f && Screen.Y < Canvas->ClipY)
    {
        DrawText(TEXT("Chest"), Accent, Screen.X, Screen.Y);
    }
}
