#include "CoopRPGLootHUD.h"

#include "CoopRPGExtractionZone.h"
#include "CoopRPGGameState.h"
#include "CoopRPGGameViewportClient.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"

void ACoopRPGLootHUD::DrawHUD()
{
    Super::DrawHUD();
    const auto* Viewport = Cast<UCoopRPGGameViewportClient>(GetWorld()->GetGameViewport());
    if (Viewport && Viewport->HasConnectionNotice())
    {
        return;
    }

    auto* Controller = Cast<ACoopRPGPlayerController>(GetOwningPlayerController());
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

    const ACoopRPGExtractionZone* Zone = State->GetExtractionZone();
    const bool bShowExtraction = bPlaying && IsValid(Zone) && State->GetTeamLootCount() > 0;
    const float PanelHeight = bShowStock ? (bShowExtraction ? 182.0f : 162.0f) : 126.0f;
    const bool bShowRoundClock = bPlaying && IsValid(Zone);
    DrawRect(FLinearColor(0.02f, 0.04f, 0.08f, 0.90f), Pad, Pad, State->HasRoundResult() ? 380.0f : 306.0f, PanelHeight + (bShowRoundClock ? 20.0f : 0.0f));
    DrawText(TEXT("SHARED CHEST"), Accent, TextX, Pad + 8.0f, nullptr, 1.15f);
    if (Character)
    {
        DrawText(FString::Printf(TEXT("HP: %.0f   |   Melee: %s"), Character->GetHealth(),
            Character->HasMeleeWeapon() ? TEXT("Equipped") : TEXT("Unarmed")), FLinearColor::White, TextX, Pad + 31.0f);
    }
    DrawText(FString::Printf(TEXT("Your recovery items: %d%s"), RecoveryCount,
        RecoveryCount > 0 && bPlaying ? TEXT("   [H] Use") : TEXT("")), FLinearColor::White, TextX, Pad + 51.0f);

    DrawText(FString::Printf(TEXT("Team loot: %d"), State->GetTeamLootCount()),
        FLinearColor::White, TextX, Pad + 71.0f);

    if (State->HasRoundResult())
    {
        const TCHAR* ResultText = TEXT("Extraction successful");
        switch (State->GetRoundEndReason())
        {
        case ECoopRPGRoundEndReason::Disconnected:
            ResultText = TEXT("Round aborted: player disconnected");
            break;
        case ECoopRPGRoundEndReason::Death:
            ResultText = TEXT("Round failed: player died");
            break;
        case ECoopRPGRoundEndReason::Timeout:
            ResultText = TEXT("Round failed: time expired");
            break;
        default:
            break;
        }
        DrawText(FString::Printf(TEXT("%s | Team score: %d"), ResultText, State->GetTeamScore()),
            Accent, TextX, Pad + 98.0f);
        Controller->AcknowledgeRoundResult(State->GetRoundId());
        return;
    }

    if (!bPlaying)
    {
        DrawText(Controller->IsRoundReady() ? TEXT("Waiting for the other player") : TEXT("R  Ready"),
            Accent, TextX, Pad + 98.0f);
        return;
    }

    if (bShowRoundClock)
    {
        const int32 Seconds = FMath::CeilToInt(State->GetRoundRemainingSeconds());
        DrawText(FString::Printf(TEXT("Time left: %d:%02d"), Seconds / 60, Seconds % 60),
            FLinearColor::White, TextX, Pad + PanelHeight - 4.0f);
    }

    if (bShowExtraction)
    {
        const FString Status = State->IsExtractionInProgress()
            ? FString::Printf(TEXT("Extracting: %.1f s"), State->GetExtractionRemainingSeconds())
            : TEXT("Both players to the extraction zone");
        DrawText(Status, Accent, TextX, Pad + (bShowStock ? 156.0f : 98.0f));

        const FVector ZoneScreen = Project(Zone->GetActorLocation() + FVector(0, 0, 20.0f));
        if (ZoneScreen.Z > 0.0f && ZoneScreen.X >= 0.0f && ZoneScreen.X < Canvas->ClipX - 60.0f
            && ZoneScreen.Y >= 0.0f && ZoneScreen.Y < Canvas->ClipY)
        {
            DrawText(TEXT("Extraction"), Accent, ZoneScreen.X, ZoneScreen.Y);
        }
    }

    if (bShowStock)
    {
        const int32 Weapons = Chest->GetWeaponStock();
        const int32 Medicines = Chest->GetRecoveryStock();
        DrawText(FString::Printf(TEXT("Melee weapons: %d%s"), Weapons,
            Weapons > 0 && Character && !Character->HasMeleeWeapon() ? TEXT("   [G] Equip") : TEXT("")),
            FLinearColor::White, TextX, Pad + 96.0f);
        DrawText(FString::Printf(TEXT("Recovery items: %d%s"), Medicines,
            Medicines > 0 ? TEXT("   [F] Take") : TEXT("")),
            FLinearColor::White, TextX, Pad + 116.0f);
        DrawText(Weapons == 0 && Medicines == 0 ? TEXT("Chest empty   |   Q  Close") : TEXT("Q  Close"),
            Accent, TextX, Pad + 136.0f);
        return;
    }

    if (bShowExtraction)
    {
        return;
    }

    const ACoopRPGEnemyLoot* Drop = State->GetEnemyLoot();
    const bool bShowDrop = IsValid(Drop) && !Drop->IsClaimed();
    DrawText(bShowDrop ? TEXT("Approach enemy loot and press T") : TEXT("Approach the chest and press E"),
        Accent, TextX, Pad + 98.0f);
    const FVector Location = bShowDrop ? Drop->GetActorLocation() : State->GetSharedChest()->GetActorLocation();
    const FVector Screen = Project(Location + FVector(0, 0, 40.0f));
    if (Screen.Z > 0.0f && Screen.X >= 0.0f && Screen.X < Canvas->ClipX - 60.0f
        && Screen.Y >= 0.0f && Screen.Y < Canvas->ClipY)
    {
        DrawText(bShowDrop ? TEXT("Enemy loot") : TEXT("Chest"), Accent, Screen.X, Screen.Y);
    }
}
