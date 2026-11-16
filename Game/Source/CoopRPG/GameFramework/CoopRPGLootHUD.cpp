#include "CoopRPGLootHUD.h"

#include "CoopRPGExtractionZone.h"
#include "CoopRPGGameState.h"
#include "CoopRPGGameViewportClient.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGGameplayFeedback, Log, All);

namespace
{
    constexpr double FeedbackLifetimeSeconds = 1.4;
    constexpr int32 MaxItemRows = 4;
    constexpr float ItemRowSpacing = 20.0f;
    constexpr float ItemColumnWidth = 160.0f;
}

void ACoopRPGLootHUD::ShowGameplayFeedback(const FCoopRPGGameplayFeedback& Feedback)
{
    AActor* Subject = Feedback.Subject.Get();
    FString Text;
    FLinearColor Color(0.35f, 0.72f, 1.0f);
    bool bNextToActor = true;
    switch (Feedback.Kind)
    {
    case ECoopRPGFeedbackKind::Damage:
    {
        const FString Attacker = Feedback.AttackerSlot > 0 ? FString::Printf(TEXT("P%d"), Feedback.AttackerSlot)
            : (Feedback.AttackerSlot == 0 ? TEXT("Enemy") : TEXT("Player"));
        Text = FString::Printf(TEXT("-%.0f HP (By %s)"), Feedback.Amount, *Attacker);
        Color = FLinearColor(1.0f, 0.36f, 0.33f);
        bNextToActor = false;
        if (auto* Player = Cast<ACoopRPGCharacter>(Subject))
        {
            Player->FlashPresentationHit();
        }
        else if (auto* Enemy = Cast<ACoopRPGEnemy>(Subject))
        {
            Enemy->FlashPresentationHit();
        }
        break;
    }
    case ECoopRPGFeedbackKind::Recovery:
        Text = FString::Printf(TEXT("+%.0f HP"), Feedback.Amount);
        Color = FLinearColor(0.36f, 1.0f, 0.55f);
        bNextToActor = false;
        break;
    case ECoopRPGFeedbackKind::WeaponAcquired: Text = TEXT("Melee weapon acquired"); break;
    case ECoopRPGFeedbackKind::RecoveryAcquired: Text = TEXT("Recovery item acquired"); break;
    case ECoopRPGFeedbackKind::RecoveryUsed:
        Text = TEXT("Recovery item used");
        Color = FLinearColor(1.0f, 0.83f, 0.32f);
        break;
    case ECoopRPGFeedbackKind::TeamLootAcquired: Text = TEXT("Team loot acquired"); break;
    }

    if (FeedbackEntries.Num() >= 24)
    {
        FeedbackEntries.RemoveAt(0);
    }
    const double Now = GetWorld()->GetTimeSeconds();
    int32 ItemRow = INDEX_NONE;
    if (bNextToActor)
    {
        const TWeakObjectPtr<AActor> SubjectKey(Subject);
        int32 NextRow = 0;
        for (const FFeedbackEntry& Entry : FeedbackEntries)
        {
            if (Entry.Subject == SubjectKey && Entry.ItemRow != INDEX_NONE && Now - Entry.StartedAt <= FeedbackLifetimeSeconds)
            {
                NextRow = FMath::Max(NextRow, Entry.ItemRow + 1);
            }
        }
        if (NextRow < MaxItemRows)
        {
            ItemRow = NextRow;
        }
    }
    FeedbackEntries.Add({Subject, Feedback.Location, Text, Color, Now, bNextToActor, ItemRow});
    const auto* Player = Cast<ACoopRPGCharacter>(Subject);
    UE_LOG(LogCoopRPGGameplayFeedback, Log, TEXT("GameplayFeedback Kind=%s Subject=%s Slot=%d Amount=%.2f AttackerSlot=%d %s"),
        *UEnum::GetValueAsString(Feedback.Kind), *GetNameSafe(Subject), Player ? Player->GetDisplaySlot() : 0,
        Feedback.Amount, Feedback.AttackerSlot, *CoopRPG::Debug::DescribeLogContext(GetOwningPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGLootHUD::DrawWorldLabel(const FVector& Location, const FString& Text, const FLinearColor& Color,
    FVector2D Offset, int32 ItemRow)
{
    const FVector Screen = Project(Location);
    if (Screen.Z <= 0.0f || Screen.X < 0.0f || Screen.X >= Canvas->ClipX || Screen.Y < 0.0f || Screen.Y >= Canvas->ClipY)
    {
        return;
    }

    float Width, Height;
    GetTextSize(Text, Width, Height);
    const bool bItem = ItemRow != INDEX_NONE;
    const float Left = static_cast<float>(Screen.X + Offset.X) - (bItem ? 0.0f : Width * 0.5f);
    const float ColumnWidth = bItem ? ItemColumnWidth : Width;
    const float X = FMath::Clamp(Left, 4.0f, FMath::Max(4.0f, Canvas->ClipX - ColumnWidth - 4.0f));
    const float ReservedRows = bItem ? ItemRowSpacing * (MaxItemRows - 1) : 0.0f;
    const float AnchorY = FMath::Clamp(static_cast<float>(Screen.Y + Offset.Y), 4.0f,
        FMath::Max(4.0f, Canvas->ClipY - Height - ReservedRows - 4.0f));
    const float Y = AnchorY + (bItem ? ItemRow * ItemRowSpacing : 0.0f);
    DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), X - 3.0f, Y - 1.0f, Width + 6.0f, Height + 2.0f);
    DrawText(Text, Color, X, Y);
}

void ACoopRPGLootHUD::DrawGameplayFeedback()
{
    for (TActorIterator<ACoopRPGCharacter> It(GetWorld()); It; ++It)
    {
        if (const int32 Slot = It->GetDisplaySlot(); Slot > 0)
        {
            DrawWorldLabel(It->GetActorLocation() + FVector(0.0f, 0.0f, 94.0f),
                FString::Printf(TEXT("P%d"), Slot), CoopRPG::Presentation::PlayerColor(Slot));
        }
    }
    for (TActorIterator<ACoopRPGEnemy> It(GetWorld()); It; ++It)
    {
        if (!It->IsDead())
        {
            DrawWorldLabel(It->GetActorLocation() + FVector(0.0f, 0.0f, 76.0f), TEXT("Enemy"), CoopRPG::Presentation::EnemyColor());
        }
    }

    const double Now = GetWorld()->GetTimeSeconds();
    FeedbackEntries.RemoveAll([Now](const FFeedbackEntry& Entry) { return Now - Entry.StartedAt > FeedbackLifetimeSeconds; });
    TMap<TWeakObjectPtr<AActor>, int32> RowsBySubject;
    for (int32 Index = FeedbackEntries.Num() - 1; Index >= 0; --Index)
    {
        const FFeedbackEntry& Entry = FeedbackEntries[Index];
        const int32 Row = RowsBySubject.FindOrAdd(Entry.Subject)++;
        const AActor* Subject = Entry.Subject.Get();
        const FVector Location = Subject ? Subject->GetActorLocation() : Entry.Location;
        const FVector Anchor = Location + FVector(0.0f, 0.0f, 100.0f);
        if (Entry.bNextToActor)
        {
            if (Entry.ItemRow != INDEX_NONE)
            {
                DrawWorldLabel(Anchor, Entry.Text, Entry.Color, FVector2D(32.0, 0.0), Entry.ItemRow);
            }
            continue;
        }
        const FVector2D Offset(0.0, -15.0 - Row * 17.0 - (Now - Entry.StartedAt) * 14.0);
        DrawWorldLabel(Anchor, Entry.Text, Entry.Color, Offset);
    }
}

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

    DrawGameplayFeedback();

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
