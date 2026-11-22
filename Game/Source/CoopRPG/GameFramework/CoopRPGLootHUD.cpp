#include "CoopRPGLootHUD.h"

#include "CoopRPGExtractionZone.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPGGameState.h"
#include "CoopRPGGameViewportClient.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "Application/SlateApplicationBase.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
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
    constexpr float ShieldNameplateHalfWidth = 36.0f;
    constexpr float AttackTargetTextScale = 0.85f;

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
    if (!Canvas || !Controller || !State)
    {
        return;
    }

    const FString MapName = UGameplayStatics::GetCurrentLevelName(this);
    if (MapName == CoopRPG::Expedition::LobbyMap)
    {
        DrawRoundWaiting(*State, *Controller, 0);
        return;
    }
    if (MapName == CoopRPG::Expedition::PuzzleMap)
    {
        DrawPuzzle(*State, *Controller);
        return;
    }
    if (!State->GetSharedChest())
    {
        return;
    }

    DrawGameplayFeedback();

    const bool bBoss = MapName == CoopRPG::Expedition::BossMap;
    const int32 Stage = bBoss ? 3 : MapName == TEXT("L_Movement_Round2") ? 2 : 1;
    if (State->HasRoundResult())
    {
        if (bBoss)
        {
            DrawPartyStatus(*Controller);
        }
        DrawRoundResult(*State, *Controller, Stage);
        return;
    }
    if (!State->IsRoundPlaying())
    {
        if (bBoss)
        {
            DrawPartyStatus(*Controller);
        }
        DrawRoundWaiting(*State, *Controller, Stage);
        return;
    }

    if (bBoss || CoopRPG::Expedition::IsSkirmish(GetWorld()))
    {
        DrawExpeditionCombat(*State, *Controller, bBoss);
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
            Enemy->FlashPresentationHit(Feedback.bPreserveEnemyAttackPose);
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

void ACoopRPGLootHUD::DrawExpeditionCombat(const ACoopRPGGameState& State,
    const ACoopRPGPlayerController& Controller, bool bBoss)
{
    const auto* LocalHero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const auto* Chest = Controller.GetOpenedChest();
    const auto* Stone = State.GetEnemyLoot();
    const auto* Potion = State.GetEnemyPotion();
    const bool bHasStone = State.GetTeamLootCount() > 0;
    const bool bStoneOnGround = IsValid(Stone) && !Stone->IsClaimed();
    const bool bPotionOnGround = IsValid(Potion) && !Potion->IsClaimed();
    const int32 Remaining = FMath::CeilToInt(State.GetRoundRemainingSeconds());
    DrawRect(PanelBackground, PanelPad, PanelPad, 294.0f, IsValid(Chest) ? 115.0f : 65.0f);
    const TCHAR* Title = bBoss ? TEXT("BOSS") : TEXT("SKIRMISH");
    DrawText(FString::Printf(TEXT("%s  |  %d:%02d"), Title, Remaining / 60, Remaining % 60),
        PanelAccent, PanelTextX, PanelPad + 7.0f);
    if (IsValid(Chest))
    {
        DrawText(FString::Printf(TEXT("[G] Equip  ·  Weapons %d"), Chest->GetWeaponStock()),
            FLinearColor::White, PanelTextX, PanelPad + 32.0f);
        DrawText(FString::Printf(TEXT("[F] Take potion  ·  Stock %d"), Chest->GetRecoveryStock()),
            FLinearColor::White, PanelTextX, PanelPad + 54.0f);
        DrawText(TEXT("[Q] Close"), PanelAccent, PanelTextX, PanelPad + 80.0f);
    }
    else if (const auto* Zone = State.GetExtractionZone(); bHasStone && IsValid(Zone))
    {
        const double Seconds = FMath::CeilToDouble(State.GetExtractionRemainingSeconds() * 10.0) / 10.0;
        const FString Hint = State.IsExtractionInProgress()
            ? FString::Printf(TEXT("Extracting in %.1f s"), Seconds)
            : FString::Printf(TEXT("Portal open  ·  %d/2"), State.GetExtractionPlayersInside());
        DrawText(Hint, PanelAccent, PanelTextX, PanelPad + 33.0f);
        if (!State.IsExtractionInProgress())
        {
            DrawPortalGuide(*Zone);
        }
    }
    else
    {
        const FString Objective = bBoss ? FString(TEXT("Defeat the Black Knight"))
            : CoopRPG::Expedition::IsSkirmish(GetWorld())
                ? FString::Printf(TEXT("Mushrooms defeated  %d/%d"), State.GetSkirmishDefeats(),
                    CoopRPG::Expedition::SkirmishEnemyCount)
                : FString(TEXT("Defeat the mushroom"));
        const TCHAR* Hint = bStoneOnGround ? TEXT("[T] Pick up magic stone")
            : LocalHero && LocalHero->HasMeleeWeapon() ? *Objective : TEXT("[E] Open the chest");
        DrawText(Hint, PanelAccent, PanelTextX, PanelPad + 33.0f);
    }

    if (bStoneOnGround)
    {
        DrawLootDropLabel(Stone->GetActorLocation(), true);
    }
    if (bPotionOnGround)
    {
        DrawLootDropLabel(Potion->GetActorLocation(), false);
    }

    DrawPartyStatus(Controller);
    if (bBoss)
    {
        return;
    }
    constexpr float TeamStripWidth = 206.0f;
    DrawRect(PanelBackground, PanelPad, Canvas->ClipY - 33.0f, TeamStripWidth, 23.0f);
    DrawText(bHasStone ? TEXT("Team stone  ·  Acquired") : TEXT("Team stone  ·  Not acquired"),
        bHasStone ? AvailableColor : PanelAccent, PanelTextX, Canvas->ClipY - 29.0f);
}

void ACoopRPGLootHUD::DrawPuzzle(const ACoopRPGGameState& State, ACoopRPGPlayerController& Controller)
{
    if (!State.GetRoundId().IsValid())
    {
        return;
    }

    constexpr int32 PuzzleStage = 2;
    const FCoopRPGPuzzleStatus& Puzzle = State.GetPuzzleStatus();
    const auto DrawPlate = [this](const FVector& Center, const TCHAR* Label, uint8 Occupants)
    {
        const FString Text = FString::Printf(TEXT("%s  |  %s"), Label,
            Occupants == 3 ? TEXT("P1 + P2") : Occupants == 1 ? TEXT("P1")
                : Occupants == 2 ? TEXT("P2") : TEXT("Empty"));
        DrawWorldLabel(Center + FVector(0.0, 0.0, 7.0), Text,
            Occupants == 1 || Occupants == 2 ? CoopRPG::Presentation::PlayerColor(Occupants) : PanelAccent,
            FVector2D(0.0, 18.0));
    };
    DrawPlate(Puzzle.PlateA, TEXT("A"), Puzzle.OccupantsA);
    DrawPlate(Puzzle.PlateB, TEXT("B"), Puzzle.OccupantsB);
    DrawPartyStatus(Controller);
    if (State.HasRoundResult())
    {
        DrawRoundResult(State, Controller, PuzzleStage);
        return;
    }
    if (!State.IsRoundPlaying())
    {
        DrawRoundWaiting(State, Controller, PuzzleStage);
        return;
    }

    DrawRect(PanelBackground, PanelPad, PanelPad, 306.0f, 78.0f);
    DrawText(TEXT("PUZZLE  |  TWO PLATES"), PanelAccent, PanelTextX, PanelPad + 8.0f);
    DrawText(TEXT("Stand on separate plates."), FLinearColor::White, PanelTextX, PanelPad + 36.0f);
}

void ACoopRPGLootHUD::DrawPartyStatus(const ACoopRPGPlayerController& Controller)
{
    const auto* LocalHero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    for (TActorIterator<ACoopRPGCharacter> It(GetWorld()); It; ++It)
    {
        const int32 Slot = It->GetDisplaySlot();
        if (Slot < 1 || Slot > 2)
        {
            continue;
        }
        const bool bLocal = It.operator->() == LocalHero;
        const FString DisplayName = FString::Printf(TEXT("P%d"), Slot);
        const float Width = FMath::Clamp(Canvas->ClipX * 0.30f, 154.0f, 220.0f);
        const float X = PanelPad + (Slot - 1) * (Width + 8.0f);
        const float Y = Canvas->ClipY - 115.0f;
        DrawRect(PanelBackground, X, Y, Width, 76.0f);
        DrawText(FString::Printf(TEXT("%s%s"), *DisplayName, bLocal ? TEXT("  (You)") : TEXT("")),
            CoopRPG::Presentation::PlayerColor(Slot), X + 10.0f, Y + 7.0f);
        const int32 Health = FMath::CeilToInt(It->GetHealth());
        const FString Vitals = bLocal
            ? FString::Printf(TEXT("HP %d  ·  SP %d"), Health, FMath::CeilToInt(It->GetStamina()))
            : FString::Printf(TEXT("HP %d"), Health);
        DrawText(Vitals, FLinearColor::White, X + 10.0f, Y + 28.0f);
        const FString Inventory = bLocal
            ? FString::Printf(TEXT("%s  ·  Potion %d%s"), It->HasMeleeWeapon() ? TEXT("Sword") : TEXT("Unarmed"),
                Controller.GetRecoveryCount(), Controller.GetRecoveryCount() > 0 ? TEXT(" [H]") : TEXT(""))
            : (It->HasMeleeWeapon() ? TEXT("Sword") : TEXT("Unarmed"));
        DrawText(Inventory, FLinearColor::White, X + 10.0f, Y + 47.0f);
    }
}

void ACoopRPGLootHUD::DrawGameplayFeedback()
{
    const bool bSkirmish = CoopRPG::Expedition::IsSkirmish(GetWorld());
    if (!bSkirmish && !CoopRPG::Expedition::IsBoss(GetWorld()))
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
    }
    for (TActorIterator<ACoopRPGEnemy> It(GetWorld()); It; ++It)
    {
        if (!It->IsDead())
        {
            int32 AttackTargetSlot = INDEX_NONE;
            if (It->IsShieldEncounter())
            {
                AttackTargetSlot = It->IsShieldRaised() ? It->GetShieldWarning().TargetSlot : 0;
            }
            const double Height = bSkirmish ? -It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 76.0;
            DrawHealthNameplate(It->GetActorLocation() + FVector(0.0, 0.0, Height),
                bSkirmish ? TEXT("Mushroom") : TEXT("Enemy"), It->GetHealth(),
                CoopRPG::Presentation::EnemyColor(), AttackTargetSlot, bSkirmish, FMath::RoundToInt(It->GetMaxHealth()));
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

void ACoopRPGLootHUD::DrawLootDropLabel(const FVector& Location, bool bMagicStone)
{
    const FVector Screen = Project(Location);
    if (Screen.Z <= 0.0f || Screen.X < 0.0f || Screen.X >= Canvas->ClipX || Screen.Y < 0.0f || Screen.Y >= Canvas->ClipY)
    {
        return;
    }

    constexpr double TextGap = 4.0;
    const FLinearColor RareBlue(FColor(92, 168, 255));
    const FLinearColor UncommonGreen(FColor(154, 243, 169));
    const FSlateFontInfo NormalFont = GEngine->GetMediumFont()->GetLegacySlateFontInfo();
    FSlateFontInfo NameFont = NormalFont;
    if (bMagicStone)
    {
        NameFont.TypefaceFontName = TEXT("Bold");
    }
    FCanvasTextItem Key(FVector2D::ZeroVector, FText::FromString(bMagicStone ? TEXT("[T]") : TEXT("[Y]")),
        NormalFont, FLinearColor::White);
    FCanvasTextItem Name(FVector2D::ZeroVector, FText::FromString(bMagicStone ? TEXT("Magic stone") : TEXT("Potion")),
        NameFont, bMagicStone ? RareBlue : UncommonGreen);

    const float DPIScale = Canvas->Canvas->GetDPIScale();
    const auto FontMeasure = FSlateApplicationBase::Get().GetRenderer()->GetFontMeasureService();
    const FVector2D KeySize = FontMeasure->Measure(Key.Text, NormalFont, DPIScale);
    const FVector2D NameSize = FontMeasure->Measure(Name.Text, NameFont, DPIScale);
    const double Width = KeySize.X + TextGap + NameSize.X;
    const double Height = FMath::Max(KeySize.Y, NameSize.Y);
    const double X = FMath::Clamp(Screen.X - Width * 0.5, 4.0, FMath::Max(4.0, Canvas->ClipX - Width - 4.0));
    const double Y = FMath::Clamp(Screen.Y + 23.0, 4.0, FMath::Max(4.0, Canvas->ClipY - Height - 4.0));

    DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), X - 3.0, Y - 1.0, Width + 6.0, Height + 2.0);
    Canvas->DrawItem(Key, X, Y);
    Canvas->DrawItem(Name, X + KeySize.X + TextGap, Y);
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
    const FLinearColor& Color, int32 AttackTargetSlot, bool bBelowFeet, int32 MaxHealth)
{
    const FVector Screen = Project(Location);
    if (Screen.Z <= 0.0f || Screen.X < 0.0f || Screen.X >= Canvas->ClipX || Screen.Y < 0.0f || Screen.Y >= Canvas->ClipY)
    {
        return;
    }

    const FString NameLine = FString::Printf(TEXT("[%s]"), *Name);
    const FString HealthLine = FString::Printf(TEXT("%d/%d"), FMath::CeilToInt(Health), MaxHealth);
    float NameWidth, NameHeight, HealthWidth, HealthHeight;
    GetTextSize(NameLine, NameWidth, NameHeight);
    GetTextSize(HealthLine, HealthWidth, HealthHeight);

    const float RowSpacing = FMath::Max(HealthNameplateLineSpacing, NameHeight);
    const bool bShowAttackTarget = AttackTargetSlot != INDEX_NONE;
    const float HalfWidth = bShowAttackTarget ? ShieldNameplateHalfWidth : FMath::Max(NameWidth, HealthWidth) * 0.5f;
    const float Height = bShowAttackTarget ? RowSpacing * 3.0f : RowSpacing + HealthHeight;
    const float CenterX = FMath::Clamp(static_cast<float>(Screen.X), HalfWidth + 4.0f,
        FMath::Max(HalfWidth + 4.0f, Canvas->ClipX - HalfWidth - 4.0f));
    const float Top = FMath::Clamp(static_cast<float>(Screen.Y) + (bBelowFeet ? 8.0f : -RowSpacing), 4.0f,
        FMath::Max(4.0f, Canvas->ClipY - Height - 4.0f));
    DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), CenterX - HalfWidth - 3.0f, Top - 1.0f,
        HalfWidth * 2.0f + 6.0f, Height + 2.0f);
    DrawText(NameLine, Color, CenterX - NameWidth * 0.5f, Top);
    DrawText(HealthLine, Color, CenterX - HealthWidth * 0.5f, Top + RowSpacing);
    if (bShowAttackTarget)
    {
        const bool bHasTarget = AttackTargetSlot > 0;
        const FString TargetLine = bHasTarget ? FString::Printf(TEXT("Target P%d"), AttackTargetSlot)
            : TEXT("Target \u2014");
        float TargetWidth, TargetHeight;
        GetTextSize(TargetLine, TargetWidth, TargetHeight, nullptr, AttackTargetTextScale);
        const float TargetY = Top + 2.0f * RowSpacing + FMath::Max(0.0f, RowSpacing - TargetHeight) * 0.5f;
        DrawText(TargetLine, bHasTarget ? FLinearColor::White : PanelAccent,
            CenterX - TargetWidth * 0.5f, TargetY, nullptr, AttackTargetTextScale);
    }
}

void ACoopRPGLootHUD::DrawRoundWaiting(const ACoopRPGGameState& State,
    const ACoopRPGPlayerController& Controller, int32 Stage)
{
    const FCoopRPGReadySlots& Slots = State.GetReadySlots();
    DrawRect(PanelBackground, PanelPad, PanelPad, 306.0f, 128.0f);
    DrawText(Stage == 0 ? FString(TEXT("LOBBY | TEAM READY")) : FString::Printf(TEXT("STAGE %d | TEAM READY"), Stage), PanelAccent,
        PanelTextX, PanelPad + 8.0f, nullptr, 1.15f);
    DrawText(FString::Printf(TEXT("P1  %s"), ReadyStatusLabel(Slots.PlayerOne)),
        CoopRPG::Presentation::PlayerColor(1), PanelTextX, PanelPad + 37.0f);
    DrawText(FString::Printf(TEXT("P2  %s"), ReadyStatusLabel(Slots.PlayerTwo)),
        PanelAccent, PanelTextX, PanelPad + 58.0f);
    const bool bLeavingLobby = Stage == 0 && Slots.PlayerOne == ECoopRPGReadyStatus::Ready
        && Slots.PlayerTwo == ECoopRPGReadyStatus::Ready;
    const TCHAR* Hint = bLeavingLobby ? TEXT("Entering skirmish ...")
        : Controller.IsRoundReady() ? TEXT("Waiting for the other player") : TEXT("[R] Ready");
    DrawText(Hint, FLinearColor::White, PanelTextX, PanelPad + 94.0f);
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
    case ECoopRPGRoundEndReason::BossDefeated:
        Result = TEXT("EXPEDITION COMPLETE");
        Detail = TEXT("Black Knight defeated.");
        break;
    case ECoopRPGRoundEndReason::PuzzleSolved:
        Result = TEXT("PUZZLE SOLVED");
        Detail = TEXT("One player on each plate.");
        break;
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
    const bool bExpeditionStageComplete = Reason == ECoopRPGRoundEndReason::PuzzleSolved
        || Reason == ECoopRPGRoundEndReason::BossDefeated;
    const float X = bExpeditionStageComplete ? PanelPad : (Canvas->ClipX - Width) * 0.5f;
    const float Y = bExpeditionStageComplete ? PanelPad : FMath::Max(PanelPad, (Canvas->ClipY - 158.0f) * 0.5f);
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
        NextAction = Reason == ECoopRPGRoundEndReason::PuzzleSolved
            ? TEXT("Entering the boss stage ...") : TEXT("Next stage loads automatically.");
    }
    else if (Reason == ECoopRPGRoundEndReason::BossDefeated)
    {
        NextAction = TEXT("All stages cleared.");
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

    if (Zone->IsPortalOpen())
    {
        const int32 Count = State.GetExtractionPlayersInside();
        const int32 Capacity = ACoopRPGGameState::RoundCapacity;
        if (State.IsExtractionInProgress())
        {
            const double Seconds = FMath::CeilToDouble(State.GetExtractionRemainingSeconds() * 10.0) / 10.0;
            DrawText(FString::Printf(TEXT("공동 탈출 · %.1f초"), Seconds), ProgressColor, PanelTextX, HintY);
        }
        else
        {
            const TCHAR* Label = Count == 0 ? TEXT("포탈로 이동") : TEXT("동료 대기");
            DrawText(FString::Printf(TEXT("%s · %d/%d"), Label, Count, Capacity), AvailableColor, PanelTextX, HintY);
            DrawPortalGuide(*Zone);
        }
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


void ACoopRPGLootHUD::DrawPortalGuide(const ACoopRPGExtractionZone& Zone)
{
    const APlayerController* Controller = GetOwningPlayerController();
    const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    if (!Pawn || Zone.ContainsPawn(*Pawn))
    {
        return;
    }

    constexpr double Margin = 34.0;
    const FVector2D Center(Canvas->ClipX * 0.5, Canvas->ClipY * 0.5);
    const FVector2D Extent = Center - FVector2D(Margin, Margin);
    if (Extent.X <= 0.0 || Extent.Y <= 0.0)
    {
        return;
    }

    const FVector Screen = Project(Zone.GetActorLocation() + FVector(0.0, 0.0, 14.0));
    FVector2D Direction(Screen.X - Center.X, Screen.Y - Center.Y);
    if (Screen.Z <= 0.0)
    {
        FVector EyeLocation;
        FRotator EyeRotation;
        Controller->GetPlayerViewPoint(EyeLocation, EyeRotation);
        const FVector ToPortal = EyeRotation.UnrotateVector(Zone.GetActorLocation() - EyeLocation);
        Direction = FVector2D(ToPortal.Y, -ToPortal.Z);
        if (Direction.IsNearlyZero())
        {
            Direction = FVector2D(0.0, 1.0);
        }
    }

    FVector2D Aim(0.0, 1.0);
    FVector2D Position(Screen.X, Screen.Y);
    const double EdgeScale = FMath::Max(FMath::Abs(Direction.X) / Extent.X, FMath::Abs(Direction.Y) / Extent.Y);
    if (Screen.Z <= 0.0 || EdgeScale > 1.0)
    {
        Aim = Direction.GetSafeNormal();
        Position = Center + Direction / EdgeScale;
    }

    const FVector2D Side(-Aim.Y, Aim.X);
    const FVector2D Tail = Position - Aim * 12.0;
    const FVector2D Left = Tail + Side * 7.0;
    const FVector2D Right = Tail - Side * 7.0;
    const FLinearColor Outline(0.03f, 0.05f, 0.07f);
    const FLinearColor Highlight(0.45f, 1.0f, 0.78f);
    DrawLine(Left.X, Left.Y, Position.X, Position.Y, Outline, 6.0f);
    DrawLine(Right.X, Right.Y, Position.X, Position.Y, Outline, 6.0f);
    DrawLine(Left.X, Left.Y, Position.X, Position.Y, Highlight, 2.5f);
    DrawLine(Right.X, Right.Y, Position.X, Position.Y, Highlight, 2.5f);
}
