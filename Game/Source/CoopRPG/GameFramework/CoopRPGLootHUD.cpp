#include "CoopRPGLootHUD.h"

#include "CoopRPGExtractionZone.h"
#include "CoopRPGGameState.h"
#include "CoopRPGGameViewportClient.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGGameplayFeedback, Log, All);

namespace
{
    constexpr double FeedbackLifetimeSeconds = 1.4;
    constexpr int32 MaxItemRows = 4;
    constexpr float ItemRowSpacing = 20.0f;
    constexpr float ItemColumnWidth = 160.0f;
    constexpr float PanelPad = 18.0f;
    constexpr float PanelTextX = PanelPad + 12.0f;
    const FLinearColor PanelBackground(0.02f, 0.04f, 0.08f, 0.90f);
    const FLinearColor PanelAccent(0.49f, 0.82f, 1.0f);
    const FLinearColor LockedColor(0.36f, 0.39f, 0.47f);
    const FLinearColor AvailableColor(0.16f, 0.88f, 0.55f);
    const FLinearColor ProgressColor(1.0f, 0.71f, 0.12f);

    constexpr float HealthNameplateLineSpacing = 17.0f;

    void PlayShieldImpact(UWorld& World, const FVector& Position)
    {
        constexpr uint32 SampleRate = 24000;
        constexpr float Duration = 0.10f;
        TArray<int16> Samples;
        Samples.SetNumUninitialized(static_cast<int32>(SampleRate * Duration));
        for (int32 Index = 0; Index < Samples.Num(); ++Index)
        {
            const double Time = static_cast<double>(Index) / SampleRate;
            const double Envelope = FMath::Min(Time * 600.0, 1.0) * FMath::Exp(-50.0 * Time);
            Samples[Index] = static_cast<int16>(7000.0 * Envelope
                * (0.7 * FMath::Sin(2.0 * UE_DOUBLE_PI * 890.0 * Time)
                    + 0.3 * FMath::Sin(2.0 * UE_DOUBLE_PI * 1430.0 * Time)));
        }

        auto* Sound = NewObject<USoundWaveProcedural>(&World);
        Sound->SetSampleRate(SampleRate);
        Sound->NumChannels = 1;
        Sound->Duration = Duration;
        Sound->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
        if (UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(&World, Sound, Position, FRotator::ZeroRotator, 0.65f))
        {
            Audio->FadeOut(Duration, 0.0f);
        }
    }

    const TCHAR* ReadyStatusLabel(ECoopRPGReadyStatus Status)
    {
        switch (Status)
        {
        case ECoopRPGReadyStatus::Disconnected: return TEXT("Not connected");
        case ECoopRPGReadyStatus::Ready: return TEXT("Ready");
        default: return TEXT("Not ready");
        }
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

    const int32 Stage = UGameplayStatics::GetCurrentLevelName(this) == TEXT("L_Movement_Round2") ? 2 : 1;
    if (State->HasRoundResult())
    {
        DrawRoundResult(*State, *Controller, Stage);
        return;
    }
    if (!State->IsRoundPlaying())
    {
        DrawRoundWaiting(*State, *Controller, Stage);
        return;
    }

    constexpr float Pad = PanelPad;
    constexpr float TextX = PanelTextX;
    const FLinearColor& Accent = PanelAccent;
    const auto* Character = Cast<ACoopRPGCharacter>(Controller->GetPawn());
    const ACoopRPGSharedChest* Chest = Controller->GetOpenedChest();
    const bool bShowStock = IsValid(Chest);
    const int32 RecoveryCount = Controller->GetRecoveryCount();

    const ACoopRPGExtractionZone* Zone = State->GetExtractionZone();
    const bool bShowExtraction = IsValid(Zone) && State->GetTeamLootCount() > 0;
    constexpr float MeleeRowHeight = 20.0f;
    const float PanelHeight = (bShowStock ? (bShowExtraction ? 182.0f : 162.0f) : 126.0f) + MeleeRowHeight;
    const float InventoryY = Pad + MeleeRowHeight;
    const bool bShowRoundClock = IsValid(Zone);
    DrawRect(PanelBackground, Pad, Pad, 306.0f, PanelHeight + (bShowRoundClock ? 20.0f : 0.0f));
    DrawText(TEXT("SHARED CHEST"), Accent, TextX, Pad + 8.0f, nullptr, 1.15f);
    if (Character)
    {
        DrawText(FString::Printf(TEXT("HP: %d/%d | SP: %d/%d"), FMath::CeilToInt(Character->GetHealth()),
            static_cast<int32>(UCoopRPGCombatAttributes::MaxHealth), FMath::CeilToInt(Character->GetStamina()),
            static_cast<int32>(UCoopRPGCombatAttributes::MaxStamina)), FLinearColor::White, TextX, Pad + 31.0f);
        DrawText(Character->HasMeleeWeapon() ? TEXT("Melee: Equipped") : TEXT("Melee: Unarmed"),
            FLinearColor::White, TextX, Pad + 51.0f);
    }
    DrawText(FString::Printf(TEXT("Your recovery items: %d%s"), RecoveryCount,
        RecoveryCount > 0 ? TEXT("   [H] Use") : TEXT("")), FLinearColor::White, TextX, InventoryY + 51.0f);

    DrawText(FString::Printf(TEXT("Team loot: %d"), State->GetTeamLootCount()),
        FLinearColor::White, TextX, InventoryY + 71.0f);

    if (bShowRoundClock)
    {
        const int32 Seconds = FMath::CeilToInt(State->GetRoundRemainingSeconds());
        DrawText(FString::Printf(TEXT("Time left: %d:%02d"), Seconds / 60, Seconds % 60),
            FLinearColor::White, TextX, Pad + PanelHeight - 4.0f);
    }

    DrawExtractionStatus(*State, InventoryY + (bShowStock ? 156.0f : 98.0f));

    if (bShowStock)
    {
        const int32 Weapons = Chest->GetWeaponStock();
        const int32 Medicines = Chest->GetRecoveryStock();
        DrawText(FString::Printf(TEXT("Melee weapons: %d%s"), Weapons,
            Weapons > 0 && Character && !Character->HasMeleeWeapon() ? TEXT("   [G] Equip") : TEXT("")),
            FLinearColor::White, TextX, InventoryY + 96.0f);
        DrawText(FString::Printf(TEXT("Recovery items: %d%s"), Medicines,
            Medicines > 0 ? TEXT("   [F] Take") : TEXT("")),
            FLinearColor::White, TextX, InventoryY + 116.0f);
        DrawText(Weapons == 0 && Medicines == 0 ? TEXT("Chest empty   |   Q  Close") : TEXT("Q  Close"),
            Accent, TextX, InventoryY + 136.0f);
        return;
    }

    if (bShowExtraction)
    {
        return;
    }

    const ACoopRPGEnemyLoot* Drop = State->GetEnemyLoot();
    const bool bShowDrop = IsValid(Drop) && !Drop->IsClaimed();
    DrawText(bShowDrop ? TEXT("Approach enemy loot and press T") : TEXT("Approach the chest and press E"),
        Accent, TextX, InventoryY + 98.0f);
    const FVector Location = bShowDrop ? Drop->GetActorLocation() : State->GetSharedChest()->GetActorLocation();
    const FVector Screen = Project(Location + FVector(0, 0, 40.0f));
    if (Screen.Z > 0.0f && Screen.X >= 0.0f && Screen.X < Canvas->ClipX - 60.0f
        && Screen.Y >= 0.0f && Screen.Y < Canvas->ClipY)
    {
        DrawText(bShowDrop ? TEXT("Enemy loot") : TEXT("Chest"), Accent, Screen.X, Screen.Y);
    }
}

void ACoopRPGLootHUD::ShowGameplayFeedback(const FCoopRPGGameplayFeedback& Feedback)
{
    AActor* Subject = Feedback.Subject.Get();
    FString Text;
    FLinearColor Color(0.35f, 0.72f, 1.0f);
    bool bNextToActor = true;
    const bool bAtContact = Feedback.Kind == ECoopRPGFeedbackKind::ShieldBlocked;
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
    case ECoopRPGFeedbackKind::ShieldBlocked:
        Text = TEXT("Blocked");
        Color = FLinearColor(1.0f, 0.78f, 0.24f);
        bNextToActor = false;
        PlayShieldImpact(*GetWorld(), Feedback.Location);
        break;
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
    FeedbackEntries.Add({Subject, Feedback.Location, Text, Color, Now, bNextToActor, ItemRow, bAtContact});
    const auto* Player = Cast<ACoopRPGCharacter>(Subject);
    UE_LOG(LogCoopRPGGameplayFeedback, Log, TEXT("GameplayFeedback Kind=%s Subject=%s Slot=%d Amount=%.2f AttackerSlot=%d %s"),
        *UEnum::GetValueAsString(Feedback.Kind), *GetNameSafe(Subject), Player ? Player->GetDisplaySlot() : 0,
        Feedback.Amount, Feedback.AttackerSlot, *CoopRPG::Debug::DescribeLogContext(GetOwningPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGLootHUD::DrawGameplayFeedback()
{
    for (TActorIterator<ACoopRPGCharacter> It(GetWorld()); It; ++It)
    {
        if (const int32 Slot = It->GetDisplaySlot(); Slot > 0)
        {
            DrawHealthNameplate(It->GetActorLocation() + FVector(0.0f, 0.0f, 94.0f),
                FString::Printf(TEXT("P%d"), Slot), It->GetHealth(),
                CoopRPG::Presentation::PlayerColor(Slot));
        }
    }
    for (TActorIterator<ACoopRPGEnemy> It(GetWorld()); It; ++It)
    {
        if (!It->IsDead())
        {
            const bool bWarning = It->IsShieldRaised();
            const FString Label = bWarning ? FString::Printf(TEXT("Enemy | Target P%d"), It->GetShieldWarning().TargetSlot)
                : TEXT("Enemy");
            DrawHealthNameplate(It->GetActorLocation() + FVector(0.0f, 0.0f, 76.0f),
                Label, It->GetHealth(), CoopRPG::Presentation::EnemyColor());
        }
    }

    const double Now = GetWorld()->GetTimeSeconds();
    FeedbackEntries.RemoveAll([Now](const FFeedbackEntry& Entry) { return Now - Entry.StartedAt > FeedbackLifetimeSeconds; });
    TMap<TWeakObjectPtr<AActor>, int32> RowsBySubject;
    for (int32 Index = FeedbackEntries.Num() - 1; Index >= 0; --Index)
    {
        const FFeedbackEntry& Entry = FeedbackEntries[Index];
        if (Entry.bAtContact)
        {
            DrawWorldLabel(Entry.Location, Entry.Text, Entry.Color, FVector2D(36.0, 14.0));
            continue;
        }
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
        const FVector2D Offset(0.0, -15.0 - HealthNameplateLineSpacing - Row * 17.0 - (Now - Entry.StartedAt) * 14.0);
        DrawWorldLabel(Anchor, Entry.Text, Entry.Color, Offset);
    }
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

void ACoopRPGLootHUD::DrawHealthNameplate(const FVector& Location, const FString& Name, float Health,
    const FLinearColor& Color)
{
    const FVector Screen = Project(Location);
    if (Screen.Z <= 0.0f || Screen.X < 0.0f || Screen.X >= Canvas->ClipX || Screen.Y < 0.0f || Screen.Y >= Canvas->ClipY)
    {
        return;
    }

    const FString NameLine = FString::Printf(TEXT("[%s]"), *Name);
    const FString HealthLine = FString::Printf(TEXT("%d/%d"), FMath::CeilToInt(Health),
        static_cast<int32>(UCoopRPGCombatAttributes::MaxHealth));
    float NameWidth, NameHeight, HealthWidth, HealthHeight;
    GetTextSize(NameLine, NameWidth, NameHeight);
    GetTextSize(HealthLine, HealthWidth, HealthHeight);

    const float RowSpacing = FMath::Max(HealthNameplateLineSpacing, NameHeight);
    const float HalfWidth = FMath::Max(NameWidth, HealthWidth) * 0.5f;
    const float Height = RowSpacing + HealthHeight;
    const float CenterX = FMath::Clamp(static_cast<float>(Screen.X), HalfWidth + 4.0f,
        FMath::Max(HalfWidth + 4.0f, Canvas->ClipX - HalfWidth - 4.0f));
    const float Top = FMath::Clamp(static_cast<float>(Screen.Y) - RowSpacing, 4.0f,
        FMath::Max(4.0f, Canvas->ClipY - Height - 4.0f));
    DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), CenterX - HalfWidth - 3.0f, Top - 1.0f,
        HalfWidth * 2.0f + 6.0f, Height + 2.0f);
    DrawText(NameLine, Color, CenterX - NameWidth * 0.5f, Top);
    DrawText(HealthLine, Color, CenterX - HealthWidth * 0.5f, Top + RowSpacing);
}

void ACoopRPGLootHUD::DrawRoundWaiting(const ACoopRPGGameState& State,
    const ACoopRPGPlayerController& Controller, int32 Stage)
{
    const FCoopRPGReadySlots& Slots = State.GetReadySlots();
    DrawRect(PanelBackground, PanelPad, PanelPad, 306.0f, 128.0f);
    DrawText(FString::Printf(TEXT("STAGE %d | TEAM READY"), Stage), PanelAccent,
        PanelTextX, PanelPad + 8.0f, nullptr, 1.15f);
    DrawText(FString::Printf(TEXT("P1  %s"), ReadyStatusLabel(Slots.PlayerOne)),
        CoopRPG::Presentation::PlayerColor(1), PanelTextX, PanelPad + 37.0f);
    DrawText(FString::Printf(TEXT("P2  %s"), ReadyStatusLabel(Slots.PlayerTwo)),
        PanelAccent, PanelTextX, PanelPad + 58.0f);
    DrawText(Controller.IsRoundReady() ? TEXT("Waiting for the other player") : TEXT("[R] Ready"),
        FLinearColor::White, PanelTextX, PanelPad + 94.0f);
}

void ACoopRPGLootHUD::DrawRoundResult(const ACoopRPGGameState& State,
    ACoopRPGPlayerController& Controller, int32 Stage)
{
    const ECoopRPGRoundEndReason Reason = State.GetRoundEndReason();
    const TCHAR* Result = TEXT("EXTRACTION SUCCESSFUL");
    const TCHAR* Detail = TEXT("Both players extracted.");
    FLinearColor Accent = AvailableColor;
    switch (Reason)
    {
    case ECoopRPGRoundEndReason::Disconnected:
        Result = TEXT("ROUND ABORTED");
        Detail = TEXT("A player disconnected.");
        Accent = ProgressColor;
        break;
    case ECoopRPGRoundEndReason::Death:
        Result = TEXT("ROUND FAILED");
        Detail = TEXT("A player died.");
        Accent = FLinearColor(1.0f, 0.42f, 0.37f);
        break;
    case ECoopRPGRoundEndReason::Timeout:
        Result = TEXT("ROUND FAILED");
        Detail = TEXT("Time expired.");
        Accent = FLinearColor(1.0f, 0.42f, 0.37f);
        break;
    default:
        break;
    }

    const float Width = FMath::Min(440.0f, Canvas->ClipX - 2.0f * PanelPad);
    const float X = (Canvas->ClipX - Width) * 0.5f;
    const float Y = FMath::Max(PanelPad, (Canvas->ClipY - 158.0f) * 0.5f);
    DrawRect(PanelBackground, X, Y, Width, 158.0f);
    DrawRect(Accent, X, Y, Width, 3.0f);
    DrawText(Result, Accent, X + 18.0f, Y + 16.0f, nullptr, 1.45f);
    DrawText(Detail, FLinearColor::White, X + 18.0f, Y + 52.0f);
    DrawText(FString::Printf(TEXT("Stage %d   |   Team score: %d"), Stage, State.GetTeamScore()),
        FLinearColor::White, X + 18.0f, Y + 79.0f);

    const FCoopRPGReadySlots& Slots = State.GetReadySlots();
    const bool bPlayerMissing = Reason == ECoopRPGRoundEndReason::Disconnected
        || Slots.PlayerOne == ECoopRPGReadyStatus::Disconnected || Slots.PlayerTwo == ECoopRPGReadyStatus::Disconnected;
    const TCHAR* NextAction = TEXT("Expedition complete.");
    if (bPlayerMissing)
    {
        NextAction = TEXT("Reopen the client to join another session.");
    }
    else if (Reason == ECoopRPGRoundEndReason::Death || Reason == ECoopRPGRoundEndReason::Timeout)
    {
        NextAction = TEXT("This stage reloads automatically.");
    }
    else if (!State.GetRoundTravelDestination().IsEmpty())
    {
        NextAction = TEXT("Next stage loads automatically.");
    }
    DrawText(NextAction, PanelAccent, X + 18.0f, Y + 122.0f);
    Controller.AcknowledgeRoundResult(State.GetRoundId());
}

void ACoopRPGLootHUD::DrawExtractionStatus(const ACoopRPGGameState& State, float HintY)
{
    ACoopRPGExtractionZone* Zone = State.GetExtractionZone();
    if (!IsValid(Zone))
    {
        return;
    }

    FString Label(TEXT("Extraction locked"));
    FLinearColor Color = LockedColor;
    if (State.IsExtractionInProgress())
    {
        Label = FString::Printf(TEXT("Extracting: %.1f s"),
            FMath::CeilToDouble(State.GetExtractionRemainingSeconds() * 10.0) / 10.0);
        Color = ProgressColor;
        DrawText(Label, Color, PanelTextX, HintY);
    }
    else if (State.GetTeamLootCount() > 0)
    {
        Label = TEXT("Extraction available");
        Color = AvailableColor;
        DrawText(TEXT("Both players to the extraction zone"), Color, PanelTextX, HintY);
    }

    Zone->SetPresentationColor(Color);
    DrawWorldLabel(Zone->GetActorLocation() + FVector(0.0f, 0.0f, 12.0f), Label,
        Color == LockedColor ? FLinearColor(0.75f, 0.79f, 0.86f) : Color, FVector2D(0.0, -8.0));
}
