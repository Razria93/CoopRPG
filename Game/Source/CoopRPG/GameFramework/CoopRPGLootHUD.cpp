#include "CoopRPGLootHUD.h"

#include "CoopRPGExtractionZone.h"
#include "CoopRPGExpeditionAttackHUD.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPGExpeditionHUDText.h"
#include "CoopRPGGameState.h"
#include "CoopRPGGameViewportClient.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPGPlayerState.h"
#include "CoopRPG/AbilitySystem/Abilities/CoopRPGMeleeAbility.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGMeleeEffects.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "AbilitySystemComponent.h"
#include "Application/SlateApplicationBase.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Internationalization/BreakIterator.h"
#include "Kismet/GameplayStatics.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SlateRenderer.h"
#include "GlobalRenderResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGGameplayFeedback, Log, All);

namespace CoopRPG::ExpeditionUI
{
    FAttackHudStatus ReadAttackHud(const ACoopRPGGameState& State, const ACoopRPGCharacter& Hero, bool bCombatStage)
    {
        const auto* Source = Hero.GetAbilitySystemComponent();
        if (!Source)
        {
            return {};
        }
        const FGameplayTag CooldownTag = UCoopRPGMeleeCooldown::GetCooldownTag();
        FAttackHudStatus Result;
        Result.State = EAttackHudState::Ready;
        if (!bCombatStage || !State.IsRoundPlaying())
        {
            Result.State = EAttackHudState::Waiting;
        }
        else if (Hero.GetHealth() <= 0.0f)
        {
            Result.State = EAttackHudState::Down;
        }
        else if (!Hero.HasMeleeWeapon())
        {
            Result.State = EAttackHudState::Unarmed;
        }
        else if (Source->HasMatchingGameplayTag(CooldownTag))
        {
            Result.State = EAttackHudState::Cooling;
        }
        else if (!UCoopRPGMeleeAbility::CanPayPresentationCost(*Source))
        {
            Result.State = EAttackHudState::LowStamina;
        }

        if (Result.State == EAttackHudState::Cooling)
        {
            const auto Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(CooldownTag));
            for (const auto& Timing : Source->GetActiveEffectsTimeRemainingAndDuration(Query))
            {
                if (Timing.Key > Result.Remaining && Timing.Value > 0.0f)
                {
                    Result.Remaining = Timing.Key;
                    Result.Duration = Timing.Value;
                }
            }
        }
        return Result;
    }

    const TCHAR* AttackHudLabel(EAttackHudState State)
    {
        switch (State)
        {
        case EAttackHudState::Waiting: return TEXT("준비 대기");
        case EAttackHudState::Unarmed: return TEXT("미장착");
        case EAttackHudState::Down: return TEXT("행동 불가");
        case EAttackHudState::LowStamina: return TEXT("SP부족");
        default: return TEXT("");
        }
    }

    const TCHAR* AttackHudLogState(EAttackHudState State)
    {
        switch (State)
        {
        case EAttackHudState::Waiting: return TEXT("Waiting");
        case EAttackHudState::Unarmed: return TEXT("Unarmed");
        case EAttackHudState::Down: return TEXT("Down");
        case EAttackHudState::LowStamina: return TEXT("LowStamina");
        case EAttackHudState::Cooling: return TEXT("Cooling");
        default: return TEXT("Ready");
        }
    }

    void DrawCooldownSweep(UCanvas& Canvas, FVector2D Position, double Size, float ElapsedFraction)
    {
        const FVector2D Center = Position + FVector2D(Size * 0.5, Size * 0.5);
        const auto Edge = [Center, Size](double Turn)
        {
            const double Angle = Turn * UE_TWO_PI;
            const FVector2D Direction(FMath::Sin(Angle), -FMath::Cos(Angle));
            const double ToBorder = FMath::Max(FMath::Abs(Direction.X), FMath::Abs(Direction.Y));
            return Center + Direction * (Size * 0.5 / ToBorder);
        };
        // The dark sector recedes clockwise from twelve o'clock.
        constexpr int32 Segments = 8;
        FVector2D Start = Edge(ElapsedFraction);
        for (int32 Index = FMath::FloorToInt(ElapsedFraction * Segments) + 1; Index <= Segments; ++Index)
        {
            const FVector2D End = Edge(static_cast<double>(Index) / Segments);
            FCanvasTriangleItem Shade(Center, Start, End, GWhiteTexture);
            Shade.BlendMode = SE_BLEND_Translucent;
            Shade.SetColor(FLinearColor(0.025f, 0.020f, 0.018f, 0.77f));
            Canvas.DrawItem(Shade);
            Start = End;
        }
    }
}

namespace
{
    constexpr double FeedbackLifetimeSeconds = 1.4;
    constexpr int32 MaxItemRows = 4;
    constexpr float ItemRowSpacing = 20.0f;
    constexpr float ItemColumnWidth = 160.0f;
    constexpr float PanelPad = 18.0f;
    constexpr float PanelTextX = PanelPad + 12.0f;
    constexpr float WideCombatHudWidth = 246.0f;
    constexpr float NarrowCombatHudWidth = 188.0f;

    const FLinearColor PanelBackground(0.02f, 0.04f, 0.08f, 0.90f);
    const FLinearColor PanelAccent(0.49f, 0.82f, 1.0f);
    const FLinearColor LockedColor(0.36f, 0.39f, 0.47f);
    const FLinearColor AvailableColor(0.16f, 0.88f, 0.55f);
    const FLinearColor ProgressColor(1.0f, 0.71f, 0.12f);

    constexpr float HealthNameplateLineSpacing = 17.0f;
    constexpr float ShieldNameplateHalfWidth = 36.0f;

    FVector2D PartyCardInkBounds(const UCanvas& Canvas, FStringView Text, const FSlateFontInfo& Font)
    {
        // Match FCanvasSimpleTextItem's bitmap glyph positioning. Font Measure returns the line box.
        const auto FontCache = FSlateApplicationBase::Get().GetRenderer()->GetFontCache();
        FCharacterList& Characters = FontCache->GetCharacterList(Font, Canvas.Canvas->GetDPIScale());
        const float LineHeight = Characters.GetMaxHeight();
        double Top = LineHeight;
        double Bottom = 0.0;
        for (const TCHAR Letter : Text)
        {
            const FCharacterEntry& Glyph = Characters.GetCharacter(Letter, Font.FontFallback);
            if (Glyph.Valid && Glyph.VSize > 0 && !FChar::IsWhitespace(Letter))
            {
                const float GlyphTop = (LineHeight + Glyph.GlobalDescender) / Glyph.BitmapRenderScale - Glyph.VerticalOffset;
                Top = FMath::Min(Top, static_cast<double>(GlyphTop));
                Bottom = FMath::Max(Bottom, static_cast<double>(GlyphTop + Glyph.VSize * Glyph.BitmapRenderScale));
            }
        }
        return Bottom > Top ? FVector2D(Top, Bottom) : FVector2D(0.0, LineHeight);
    }

    FString FitPartyCardId(const UCanvas& Canvas, const FString& Name, const FSlateFontInfo& Font, double Width, int32& Count)
    {
        using CoopRPG::ExpeditionUI::Measure;
        constexpr int32 MaxCharacters = 10;
        const auto Boundaries = FBreakIterator::CreateCharacterBoundaryIterator();
        Boundaries->SetStringRef(Name);
        TArray<int32, TInlineAllocator<MaxCharacters + 1>> Ends{0};
        Boundaries->ResetToBeginning();
        for (int32 Index = 0; Index < MaxCharacters; ++Index)
        {
            const int32 End = Boundaries->MoveToNext();
            if (End == INDEX_NONE)
            {
                break;
            }
            Ends.Add(End);
        }
        for (Count = Ends.Num() - 1; Count >= 0; --Count)
        {
            FString Display = Name.Left(Ends[Count]);
            if (Ends[Count] < Name.Len())
            {
                Display += TEXT("...");
            }
            if (Measure(Canvas, Display, Font).X <= Width)
            {
                return Display;
            }
        }
        Count = 0;
        return FString();
    }

    FString FootName(const FString& Name, const FString& ConnectionName, int32 Slot)
    {
        int32 Separator = INDEX_NONE;
        FGuid ConnectionId;
        if ((Name == ConnectionName || Name == ConnectionName.Left(20))
            && ConnectionName.FindLastChar(TEXT('-'), Separator)
            && FGuid::ParseExact(ConnectionName.Mid(Separator + 1), EGuidFormats::Digits, ConnectionId))
        {
            return FString::Printf(TEXT("P%d"), Slot);
        }

        constexpr int32 MaxNameCharacters = 10;
        if (Name.Len() <= MaxNameCharacters)
        {
            return Name;
        }

        const auto Boundaries = FBreakIterator::CreateCharacterBoundaryIterator();
        Boundaries->SetStringRef(Name);
        Boundaries->ResetToBeginning();
        int32 End = 0;
        for (int32 Character = 0; Character < MaxNameCharacters; ++Character)
        {
            End = Boundaries->MoveToNext();
            if (End == INDEX_NONE)
            {
                return Name;
            }
        }
        return Name.Left(End);
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

bool ACoopRPGLootHUD::HandleHudPrimaryButton(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input)
{
    const auto* Viewport = Cast<UCoopRPGGameViewportClient>(GetWorld()->GetGameViewport());
    if (Viewport && Viewport->HasConnectionNotice())
    {
        EquipmentPanel.Hide();
        ExpeditionDashboard.Hide();
    }
    return EquipmentPanel.HandlePrimaryButton(Controller, Input)
        || ExpeditionDashboard.HandlePrimaryButton(Controller, Input);
}

bool ACoopRPGLootHUD::HandleHudWheel(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input)
{
    const auto* Viewport = Cast<UCoopRPGGameViewportClient>(GetWorld()->GetGameViewport());
    return (!Viewport || !Viewport->HasConnectionNotice()) && EquipmentPanel.HandleWheel(Controller, Input);
}

void ACoopRPGLootHUD::ReleaseHudPointer()
{
    EquipmentPanel.ReleasePointer();
    ExpeditionDashboard.ReleasePointer();
}

void ACoopRPGLootHUD::DrawHUD()
{
    Super::DrawHUD();
    const auto* Viewport = Cast<UCoopRPGGameViewportClient>(GetWorld()->GetGameViewport());
    if (Viewport && Viewport->HasConnectionNotice())
    {
        EquipmentPanel.Hide();
        ExpeditionDashboard.Hide();
        return;
    }

    auto* Controller = Cast<ACoopRPGPlayerController>(GetOwningPlayerController());
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!Canvas || !Controller || !State)
    {
        EquipmentPanel.Hide();
        ExpeditionDashboard.Hide();
        return;
    }

    const FString MapName = UGameplayStatics::GetCurrentLevelName(this);
    const bool bExpedition = CoopRPG::Expedition::IsExpedition(GetWorld());
    const bool bShowEquipment = bExpedition && !State->HasRoundResult();
    const bool bHasExpeditionSkin = bExpedition && ExpeditionDashboard.Prepare();
    const bool bShowDashboard = bShowEquipment && bHasExpeditionSkin;
    const int32 ExpeditionStage = MapName == CoopRPG::Expedition::LobbyMap ? 0
        : MapName == CoopRPG::Expedition::PuzzleMap ? 2 : MapName == CoopRPG::Expedition::BossMap ? 3 : 1;
    const auto* LocalHero = Cast<ACoopRPGCharacter>(Controller->GetPawn());
    const auto Attack = bShowEquipment && LocalHero ? CoopRPG::ExpeditionUI::ReadAttackHud(*State, *LocalHero,
        ExpeditionStage == 1 || ExpeditionStage == 3) : CoopRPG::ExpeditionUI::FAttackHudStatus{};
    WorldLabelBounds.Reset();
    if (bHasExpeditionSkin && State->GetRoundId().IsValid())
    {
        const bool bShowTeamStone = MapName == CoopRPG::Expedition::SkirmishMap
            && State->IsRoundPlaying() && !State->HasRoundResult();
        DrawPartyStatus(*State, *Controller, bShowTeamStone);
        if (bShowEquipment && LocalHero && LocalHero->GetAbilitySystemComponent())
        {
            DrawLocalCombatStatus(*State, *LocalHero, Attack);
        }
    }
    ON_SCOPE_EXIT
    {
        const FVector2D ViewSize(Canvas->ClipX, Canvas->ClipY);
        if (bShowEquipment)
        {
            EquipmentPanel.Draw(*this, *Canvas, *Controller, ViewSize, Attack, ExpeditionDashboard);
        }
        else
        {
            EquipmentPanel.Hide();
        }
        if (bShowDashboard)
        {
            ExpeditionDashboard.Draw(*this, *Canvas, *State, *Controller, ExpeditionStage, ViewSize);
            for (const FBox2D& Bounds : {EquipmentPanel.GetVisibleBounds(), ExpeditionDashboard.GetPanelBounds(),
                ExpeditionDashboard.GetNoticeBounds(), ExpeditionDashboard.GetBossHealthBounds()})
            {
                if (Bounds.bIsValid)
                {
                    WorldLabelBounds.Add(Bounds);
                }
            }
            DrawExpeditionNameplates(*Controller, ExpeditionStage);
        }
        else
        {
            ExpeditionDashboard.Hide();
        }
    };
    if (MapName == CoopRPG::Expedition::LobbyMap)
    {
        if (!bShowDashboard)
        {
            DrawRoundWaiting(*State, *Controller, 0);
        }
        return;
    }
    if (MapName == CoopRPG::Expedition::PuzzleMap)
    {
        DrawPuzzle(*State, *Controller, !bShowDashboard);
        return;
    }
    if (!State->GetSharedChest())
    {
        return;
    }

    DrawGameplayFeedback(bExpedition);

    const bool bBoss = MapName == CoopRPG::Expedition::BossMap;
    const int32 Stage = bBoss ? 3 : MapName == TEXT("L_Movement_Round2") ? 2 : 1;
    if (State->HasRoundResult())
    {
        DrawRoundResult(*State, *Controller, Stage);
        return;
    }
    if (!State->IsRoundPlaying())
    {
        if (!bShowDashboard)
        {
            DrawRoundWaiting(*State, *Controller, Stage);
        }
        return;
    }

    if (bBoss || CoopRPG::Expedition::IsSkirmish(GetWorld()))
    {
        DrawExpeditionCombat(*State, *Controller, bBoss, !bShowDashboard);
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
    const bool bExpedition = CoopRPG::Expedition::IsSkirmish(GetWorld()) || CoopRPG::Expedition::IsBoss(GetWorld());
    FString Text;
    FLinearColor Color(0.35f, 0.72f, 1.0f);
    bool bNextToActor = true;
    const bool bAtContact = Feedback.Kind == ECoopRPGFeedbackKind::ShieldBlocked;
    switch (Feedback.Kind)
    {
    case ECoopRPGFeedbackKind::Damage:
    {
        const FString Attacker = Feedback.AttackerSlot > 0 ? FString::Printf(TEXT("P%d"), Feedback.AttackerSlot)
            : (Feedback.AttackerSlot == 0 ? (bExpedition ? TEXT("적") : TEXT("Enemy")) : (bExpedition ? TEXT("플레이어") : TEXT("Player")));
        Text = bExpedition ? FString::Printf(TEXT("체력 -%.0f (%s)"), Feedback.Amount, *Attacker)
            : FString::Printf(TEXT("-%.0f HP (By %s)"), Feedback.Amount, *Attacker);
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
        Text = bExpedition ? FString::Printf(TEXT("체력 +%.0f"), Feedback.Amount)
            : FString::Printf(TEXT("+%.0f HP"), Feedback.Amount);
        Color = FLinearColor(0.36f, 1.0f, 0.55f);
        bNextToActor = false;
        break;
    case ECoopRPGFeedbackKind::WeaponAcquired: Text = bExpedition ? TEXT("한손검 획득") : TEXT("Melee weapon acquired"); break;
    case ECoopRPGFeedbackKind::RecoveryAcquired: Text = bExpedition ? TEXT("회복 물약 획득") : TEXT("Recovery item acquired"); break;
    case ECoopRPGFeedbackKind::RecoveryUsed:
        Text = bExpedition ? TEXT("회복 물약 사용") : TEXT("Recovery item used");
        Color = FLinearColor(1.0f, 0.83f, 0.32f);
        break;
    case ECoopRPGFeedbackKind::TeamLootAcquired: Text = bExpedition ? TEXT("팀 마법석 획득") : TEXT("Team loot acquired"); break;
    case ECoopRPGFeedbackKind::ShieldBlocked:
        Text = bExpedition ? TEXT("방패에 막힘") : TEXT("Blocked");
        Color = FLinearColor(1.0f, 0.78f, 0.24f);
        bNextToActor = false;
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
    const ACoopRPGPlayerController& Controller, bool bBoss, bool bLegacyStatus)
{
    const auto* LocalHero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const auto* Chest = Controller.GetOpenedChest();
    const auto* Stone = State.GetEnemyLoot();
    const auto* Potion = State.GetEnemyPotion();
    const bool bHasStone = State.GetTeamLootCount() > 0;
    const bool bStoneOnGround = IsValid(Stone) && !Stone->IsClaimed();
    const bool bPotionOnGround = IsValid(Potion) && !Potion->IsClaimed();
    if (bLegacyStatus)
    {
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
    }
    if (const auto* Zone = State.GetExtractionZone(); bHasStone && IsValid(Zone)
        && !IsValid(Chest) && !State.IsExtractionInProgress())
    {
        DrawPortalGuide(*Zone);
    }

    if (bStoneOnGround)
    {
        DrawLootDropLabel(Stone->GetActorLocation(), true);
    }
    if (bPotionOnGround)
    {
        DrawLootDropLabel(Potion->GetActorLocation(), false);
    }

    if (IsValid(Chest))
    {
        const FString Stock = FString::Printf(TEXT("[G] 검 ×%d  ·  [F] 물약 ×%d  ·  [Q] 닫기"),
            Chest->GetWeaponStock(), Chest->GetRecoveryStock());
        DrawWorldLabel(Chest->GetActorLocation() + FVector(0.0, 0.0, 46.0),
            Stock, FLinearColor::White, FVector2D(0, -30), INDEX_NONE, true);
    }
}

void ACoopRPGLootHUD::DrawPuzzle(const ACoopRPGGameState& State, ACoopRPGPlayerController& Controller, bool bLegacyStatus)
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
                : Occupants == 2 ? TEXT("P2") : TEXT("비어 있음"));
        DrawWorldLabel(Center + FVector(0.0, 0.0, 7.0), Text,
            Occupants == 1 || Occupants == 2 ? CoopRPG::Presentation::PlayerColor(Occupants) : PanelAccent,
            FVector2D(0.0, 18.0));
    };
    DrawPlate(Puzzle.PlateA, TEXT("발판 A"), Puzzle.OccupantsA);
    DrawPlate(Puzzle.PlateB, TEXT("발판 B"), Puzzle.OccupantsB);
    if (State.HasRoundResult())
    {
        DrawRoundResult(State, Controller, PuzzleStage);
        return;
    }
    if (!State.IsRoundPlaying())
    {
        if (bLegacyStatus)
        {
            DrawRoundWaiting(State, Controller, PuzzleStage);
        }
        return;
    }

    if (!bLegacyStatus)
    {
        return;
    }

    DrawRect(PanelBackground, PanelPad, PanelPad, 306.0f, 78.0f);
    DrawText(TEXT("PUZZLE  |  TWO PLATES"), PanelAccent, PanelTextX, PanelPad + 8.0f);
    DrawText(TEXT("Stand on separate plates."), FLinearColor::White, PanelTextX, PanelPad + 36.0f);
}

void ACoopRPGLootHUD::DrawExpeditionNameplates(const ACoopRPGPlayerController& Controller, int32 Stage)
{
    using namespace CoopRPG::ExpeditionUI;
    struct FNameplate
    {
        const APawn* Pawn;
        const UCapsuleComponent* Capsule;
        FString Name;
        FLinearColor Color;
        float HealthFraction;
        int32 Priority;
        double DistanceSquared;
    };

    const APawn* LocalHero = Controller.GetPawn();
    const FVector ViewOrigin = LocalHero ? LocalHero->GetActorLocation() : FVector::ZeroVector;
    TArray<FNameplate, TInlineAllocator<8>> Plates;
    for (TActorIterator<ACoopRPGCharacter> It(GetWorld()); It; ++It)
    {
        const int32 Slot = It->GetDisplaySlot();
        if (Slot < 1 || Slot > UE_ARRAY_COUNT(NameplatePlayerNames) || !It->GetPlayerState())
        {
            continue;
        }
        const FString RawName = PlayerName(**It, Slot);
        FNameplatePlayerName& Cached = NameplatePlayerNames[Slot - 1];
        if (Cached.Raw != RawName)
        {
            Cached.Raw = RawName;
            Cached.Display = FootName(RawName, It->GetPlayerState()->GetUniqueId().ToString(), Slot);
        }
        const auto* Player = *It;
        Plates.Add({Player, Player->GetCapsuleComponent(), Cached.Display, CoopRPG::Presentation::PlayerColor(Slot),
            Player->GetHealth() / UCoopRPGCombatAttributes::MaxHealth, Player == LocalHero ? 0 : 2,
            FVector::DistSquared2D(Player->GetActorLocation(), ViewOrigin)});
    }
    int32 CounterTargetSlot = 0;
    if (Stage == 1 || Stage == 3)
    {
        for (TActorIterator<ACoopRPGEnemy> It(GetWorld()); It; ++It)
        {
            const auto* Enemy = *It;
            if (Enemy->IsDead())
            {
                continue;
            }
            const int32 TargetSlot = Enemy->IsShieldEncounter() && Enemy->IsShieldRaised()
                ? Enemy->GetShieldWarning().TargetSlot : 0;
            if (Stage == 3 && TargetSlot > 0)
            {
                CounterTargetSlot = TargetSlot;
            }
            if (Stage == 3 && Enemy->IsBlackKnight())
            {
                continue;
            }
            const double DistanceSquared = FVector::DistSquared2D(Enemy->GetActorLocation(), ViewOrigin);
            constexpr double NearbyEnemyDistance = 225.0;
            const double NearbyDistance = NearbyEnemyDistance + Enemy->GetMeleeRangeBonus();
            const bool bNearbyOrTargeting = TargetSlot > 0 || (LocalHero && DistanceSquared <= FMath::Square(NearbyDistance));
            Plates.Add({Enemy, Enemy->GetCapsuleComponent(), Stage == 3 ? TEXT("흑기사") : TEXT("버섯"),
                CoopRPG::Presentation::EnemyColor(), Enemy->GetHealth() / FMath::Max(1.0f, Enemy->GetMaxHealth()),
                bNearbyOrTargeting ? 1 : 3, DistanceSquared});
        }
    }
    if (CounterTargetSlot > 0)
    {
        for (const FNameplate& Plate : Plates)
        {
            const auto* Hero = Cast<ACoopRPGCharacter>(Plate.Pawn);
            if (Hero && Hero->GetDisplaySlot() == CounterTargetSlot && Hero->GetHealth() > 0.0f)
            {
                DrawCounterTargetMarker(*Hero);
                break;
            }
        }
    }
    Plates.Sort([](const FNameplate& Left, const FNameplate& Right)
    {
        if (Left.Priority != Right.Priority)
        {
            return Left.Priority < Right.Priority;
        }
        if (Left.DistanceSquared != Right.DistanceSquared)
        {
            return Left.DistanceSquared < Right.DistanceSquared;
        }
        return Left.Pawn->GetUniqueID() < Right.Pawn->GetUniqueID();
    });

    constexpr double Width = 132.0;
    constexpr double SidePad = 8.0;
    constexpr double TopPad = 5.0;
    constexpr double BottomPad = 6.0;
    constexpr double TextGap = 4.0;
    constexpr double BarHeight = 7.0;
    constexpr double EdgePad = 4.0;
    const auto NameFont = Font(10.0f, true);
    const double NameHeight = Measure(*Canvas, TEXT("[가P]"), NameFont).Y;
    const double Height = TopPad + NameHeight + TextGap + BarHeight + BottomPad;
    const FBox2D View(FVector2D(EdgePad, EdgePad), FVector2D(Canvas->ClipX - EdgePad, Canvas->ClipY - EdgePad));
    const auto Fits = [&View, this](const FBox2D& Bounds)
    {
        return View.IsInsideOrOn(Bounds.Min) && View.IsInsideOrOn(Bounds.Max)
            && !WorldLabelBounds.ContainsByPredicate([&Bounds](const FBox2D& Other) { return Bounds.Intersect(Other); });
    };
    TArray<FBox2D, TInlineAllocator<8>> Placed;
    for (const FNameplate& Plate : Plates)
    {
        const FVector Feet = Plate.Pawn->GetActorLocation() - FVector(0.0, 0.0, Plate.Capsule->GetScaledCapsuleHalfHeight());
        const FVector Screen = Project(Feet);
        if (Screen.Z <= 0.0)
        {
            continue;
        }
        const FVector2D Anchor(Screen.X - Width * 0.5, Screen.Y + 8.0);
        const FVector2D Size(Width, Height);
        if (!Fits(FBox2D(Anchor, Anchor + Size)))
        {
            continue;
        }
        for (const double Shift : {0.0, -16.0, 16.0, -32.0, 32.0})
        {
            const FVector2D Position = Anchor + FVector2D(Shift, 0.0);
            const FBox2D Bounds(Position, Position + Size);
            if (!Fits(Bounds) || Placed.ContainsByPredicate([&Bounds](const FBox2D& Other) { return Bounds.Intersect(Other); }))
            {
                continue;
            }
            ExpeditionDashboard.DrawDarkPanel(*this, Position, Size);
            const FString Label = FString::Printf(TEXT("[%s]"), *Plate.Name);
            auto LabelFont = NameFont;
            FVector2D TextSize = Measure(*Canvas, Label, LabelFont);
            if (TextSize.X > Width - 2.0 * SidePad)
            {
                LabelFont = Font(10.0f * (Width - 2.0 * SidePad) / TextSize.X, true);
                TextSize = Measure(*Canvas, Label, LabelFont);
            }
            Text(*Canvas, Label, Position + FVector2D((Width - TextSize.X) * 0.5,
                TopPad + (NameHeight - TextSize.Y) * 0.5), LabelFont,
                Plate.Color);
            ExpeditionDashboard.DrawHealthBar(*this, Position + FVector2D(SidePad, TopPad + NameHeight + TextGap),
                Width - 2.0 * SidePad, FMath::Clamp(Plate.HealthFraction, 0.0f, 1.0f));
            Placed.Add(Bounds);
            break;
        }
    }
}

void ACoopRPGLootHUD::DrawCounterTargetMarker(const ACoopRPGCharacter& Target)
{
    const FVector Head = Target.GetActorLocation()
        + FVector(0.0, 0.0, Target.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 12.0);
    const FVector Screen = Project(Head);
    const FVector2D Tip(Screen.X, Screen.Y - 5.0);
    const FBox2D Bounds(Tip - FVector2D(11.0, 16.0), Tip + FVector2D(11.0, 0.0));
    if (Screen.Z <= 0.0 || Bounds.Min.X < 4.0 || Bounds.Max.X > Canvas->ClipX - 4.0 || Bounds.Min.Y < 4.0
        || Bounds.Max.Y > Canvas->ClipY - 4.0
        || WorldLabelBounds.ContainsByPredicate([&Bounds](const FBox2D& Other) { return Bounds.Intersect(Other); }))
    {
        return;
    }

    FCanvasTriangleItem Outline(Tip + FVector2D(-11.0, -16.0), Tip + FVector2D(11.0, -16.0), Tip, GWhiteTexture);
    Outline.SetColor(FLinearColor(FColor(56, 16, 16)));
    Canvas->DrawItem(Outline);
    FCanvasTriangleItem Marker(Tip + FVector2D(-7.5, -14.0), Tip + FVector2D(7.5, -14.0), Tip - FVector2D(0.0, 3.5), GWhiteTexture);
    Marker.SetColor(FLinearColor(FColor(255, 76, 76)));
    Canvas->DrawItem(Marker);
    WorldLabelBounds.Add(Bounds);
}

void ACoopRPGLootHUD::DrawPartyStatus(const ACoopRPGGameState& State,
    const ACoopRPGPlayerController& Controller, bool bShowTeamStone)
{
    using namespace CoopRPG::ExpeditionUI;
    if (State.GetRoundId() != LastPresentedPartyRound)
    {
        LastPresentedPartyRound = State.GetRoundId();
        for (auto& Values : PartyCardValues)
        {
            Values.Reset();
        }
    }

    const bool bWide = Canvas->ClipX >= 1200.0;
    const float Margin = bWide ? 22.0f : 12.0f;
    constexpr float CardGap = 8.0f;
    // Reserve the remaining two card slots and the planned lower-right controls at both sizes.
    const float RightReserve = bWide ? WideCombatHudWidth : NarrowCombatHudWidth;
    const float RightGap = bWide ? 14.0f : 16.0f;
    const float FourCardWidth = FMath::FloorToFloat((Canvas->ClipX - 2.0f * Margin - RightReserve - RightGap - 3.0f * CardGap) / 4.0f);
    const float TwoCardWidth = (Canvas->ClipX - 2.0f * Margin - CardGap) / 2.0f;
    const float Width = FMath::Min(TwoCardWidth, FMath::Max(160.0f, FourCardWidth));
    if (Width < 110.0f)
    {
        return;
    }

    const auto NameFont = Font(bWide ? 13.0f : 10.0f, true);
    const auto HeroFont = Font(bWide ? 12.0f : 10.0f);
    const auto VitalsFont = Font(bWide ? 11.0f : 9.0f);
    const auto BadgeFont = Font(bWide ? 10.0f : 9.0f, true);
    const auto PotionFont = Font(bWide ? 9.0f : 8.0f);
    const double RowHeight = FMath::CeilToDouble(Measure(*Canvas, TEXT("가"), HeroFont).Y);
    const double Inset = bWide ? 12.0 : 11.0;
    // The frame uses a thicker bottom edge. Give it its own inset on compact cards.
    const double TopInset = bWide ? Inset : 10.0;
    const double BottomInset = bWide ? Inset : 13.0;
    const double HeaderHeight = bWide ? 21.0 : 17.0;
    const double BadgeSide = bWide ? 58.0 : 48.0;
    const double SlotBadgeWidth = bWide ? 25.0 : 20.0;
    const double Gap = bWide ? 10.0 : 5.0;
    const double PotionWidth = bWide ? 34.0 : 27.0;
    const double PotionHeight = bWide ? 16.0 : 12.0;
    const double RowGap = bWide ? 5.0 : 4.0;
    const double HeaderGap = bWide ? 5.0 : 3.0;
    const double Height = TopInset + HeaderHeight + HeaderGap + BadgeSide + BottomInset;
    const double Y = Canvas->ClipY - Margin - Height;
    const FLinearColor Ink(FColor(61, 41, 25));
    const FLinearColor Paper(FColor(255, 241, 210));
    const FLinearColor Green(FColor(34, 99, 54));
    const FLinearColor Red(FColor(132, 45, 40));
    const FLinearColor Blue(FColor(39, 88, 110));

    const auto* LocalHero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const ACoopRPGCharacter* Heroes[2] = {};
    for (TActorIterator<ACoopRPGCharacter> It(GetWorld()); It; ++It)
    {
        const int32 Slot = It->GetDisplaySlot();
        if (Slot >= 1 && Slot <= UE_ARRAY_COUNT(Heroes))
        {
            Heroes[Slot - 1] = *It;
        }
    }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Heroes); ++Index)
    {
        const ACoopRPGCharacter* Hero = Heroes[Index];
        if (!Hero)
        {
            PartyCardValues[Index].Reset();
            continue;
        }

        const int32 Slot = Index + 1;
        const bool bLocal = Hero == LocalHero;
        const auto* Identity = Hero->GetPlayerState<ACoopRPGPlayerState>();
        const ECoopRPGHero Selected = Identity ? Identity->GetSelectedHero() : ECoopRPGHero::Unassigned;
        const TCHAR* HeroName = Selected == ECoopRPGHero::Adventurer ? TEXT("어드벤처러")
            : Selected == ECoopRPGHero::AnimalHero ? TEXT("애니멀 히어로") : TEXT("캐릭터 준비 중");
        const bool bArmed = Hero->HasMeleeWeapon();
        const int32 Recovery = Identity ? Identity->GetPartyRecoveryCount() : INDEX_NONE;
        const FIntVector4 Values(FMath::CeilToInt(Hero->GetHealth()),
            bLocal ? FMath::CeilToInt(Hero->GetStamina()) : Hero->GetPartyStamina(), bArmed ? 1 : 0, Recovery);
        const double X = Margin + Index * (Width + CardGap);
        const FVector2D Position(X, Y);
        const FVector2D Size(Width, Height);
        WorldLabelBounds.Emplace(Position, Position + Size);
        ExpeditionDashboard.DrawPanel(*this, Position, Size);

        const double Left = X + Inset;
        const double Right = X + Width - Inset;
        const double HeaderY = Y + TopInset;
        const double HeaderCenter = HeaderY + HeaderHeight * 0.5;
        const double BadgeLeft = Right - BadgeSide;
        const double BadgeTop = HeaderY + HeaderHeight + HeaderGap;
        const double BadgeBottom = BadgeTop + BadgeSide;
        const double BadgeCenter = (BadgeLeft + Right) * 0.5;
        const double DividerEnd = BadgeLeft - Gap;
        const double PotionLeft = DividerEnd - PotionWidth;
        const double SpTop = BadgeBottom - PotionHeight;
        const double HpTop = SpTop - RowGap - PotionHeight;
        const double DividerY = HpTop - 4.0;
        const auto CenterText = [&](FStringView Value, double TextX, double CenterY, const FSlateFontInfo& Face,
            const FLinearColor& Color)
        {
            const FVector2D InkBounds = PartyCardInkBounds(*Canvas, Value, Face);
            Text(*Canvas, Value, FVector2D(TextX, CenterY - (InkBounds.X + InkBounds.Y) * 0.5), Face, Color);
        };

        const FString SlotLabel = FString::Printf(TEXT("P%d"), Slot);
        const FLinearColor SlotColor = Slot == 1 ? FLinearColor(FColor(106, 73, 37)) : Blue;
        DrawRect(SlotColor, Left, HeaderY, SlotBadgeWidth, HeaderHeight);
        CenterText(SlotLabel, Left + (SlotBadgeWidth - Measure(*Canvas, SlotLabel, BadgeFont).X) * 0.5,
            HeaderCenter, BadgeFont, Paper);
        const double NameX = Left + SlotBadgeWidth + 6.0;
        const double NameWidth = BadgeLeft - Gap - NameX;
        const FString RawName = PlayerName(*Hero, Slot);
        FPartyCardId& CardId = PartyCardIds[Index];
        const float DpiScale = Canvas->Canvas->GetDPIScale();
        const bool bNewId = CardId.Raw != RawName || CardId.Width != NameWidth || CardId.DpiScale != DpiScale;
        if (bNewId)
        {
            CardId.Raw = RawName;
            CardId.Width = NameWidth;
            CardId.DpiScale = DpiScale;
            CardId.Display = FitPartyCardId(*Canvas, RawName, NameFont, NameWidth, CardId.Characters);
        }
        if (!CardId.Display.IsEmpty())
        {
            CenterText(CardId.Display, NameX, HeaderCenter, NameFont, Ink);
        }
        if (bLocal)
        {
            ExpeditionDashboard.DrawPartyCardAccent(*this, FVector2D(BadgeLeft, HeaderY), FVector2D(BadgeSide, HeaderHeight),
                ECoopRPGPartyCardAccent::Local);
            CenterText(TEXT("나"), BadgeCenter - Measure(*Canvas, TEXT("나"), BadgeFont).X * 0.5,
                HeaderCenter, BadgeFont, Green);
        }

        const bool bShowRecoveryHint = bLocal && Recovery > 0;
        const FStringView RecoveryHint(TEXT("[H]"));
        const double HintX = DividerEnd - Measure(*Canvas, RecoveryHint, BadgeFont).X;
        const double TitleRight = bShowRecoveryHint ? HintX - Gap : DividerEnd;
        const FString Title = Fit(*Canvas, HeroName, HeroFont, TitleRight - Left);
        const FVector2D HeroInk = PartyCardInkBounds(*Canvas, Title, HeroFont);
        const double HeroTextY = BadgeTop - HeroInk.X;
        Text(*Canvas, Title, FVector2D(Left, HeroTextY), HeroFont, Ink);
        if (bShowRecoveryHint)
        {
            const double HintCenterY = BadgeTop + (HeroInk.Y - HeroInk.X) * 0.5;
            CenterText(RecoveryHint, HintX, HintCenterY, BadgeFont, Green);
        }
        DrawRect(FLinearColor(FColor(197, 165, 110)), Left, DividerY, DividerEnd - Left, 1.0);

        const FString Health = FString::Printf(TEXT("HP %d/%d"), Values.X, static_cast<int32>(UCoopRPGCombatAttributes::MaxHealth));
        const FString Stamina = Values.Y >= 0
            ? FString::Printf(TEXT("SP %d/%d"), Values.Y, static_cast<int32>(UCoopRPGCombatAttributes::MaxStamina)) : FString(TEXT("SP —"));
        const FString HealthPotion = Recovery >= 0 ? FString::Printf(TEXT("×%d"), Recovery) : FString(TEXT("—"));
        const double HealthWidth = PotionLeft - Left - 5.0;
        const auto& HealthFace = Measure(*Canvas, Health, VitalsFont).X <= HealthWidth ? VitalsFont : PotionFont;
        CenterText(Fit(*Canvas, Health, HealthFace, HealthWidth), Left, HpTop + PotionHeight * 0.5, HealthFace, Red);
        CenterText(Stamina, Left, SpTop + PotionHeight * 0.5, VitalsFont, Blue);

        ExpeditionDashboard.DrawPartyCardAccent(*this, FVector2D(PotionLeft, HpTop), FVector2D(PotionWidth, PotionHeight),
            ECoopRPGPartyCardAccent::HealthPotion);
        const FString FittedPotion = Fit(*Canvas, HealthPotion, PotionFont, PotionWidth - 4.0);
        CenterText(FittedPotion, (PotionLeft + DividerEnd - Measure(*Canvas, FittedPotion, PotionFont).X) * 0.5,
            HpTop + PotionHeight * 0.5, PotionFont, Ink);

        ExpeditionDashboard.DrawPartyCardAccent(*this, FVector2D(BadgeLeft, BadgeTop), FVector2D(BadgeSide, BadgeSide),
            bArmed ? ECoopRPGPartyCardAccent::Equipped : ECoopRPGPartyCardAccent::Unarmed);
        const double IconSide = bWide ? 29.0 : 24.0;
        if (bArmed)
        {
            ExpeditionDashboard.DrawAttackIcon(*this, FVector2D(BadgeCenter - IconSide * 0.5, BadgeTop + 5.0), IconSide, false);
        }
        else
        {
            CenterText(TEXT("—"), BadgeCenter - Measure(*Canvas, TEXT("—"), BadgeFont).X * 0.5,
                BadgeTop + IconSide * 0.5 + 5.0, BadgeFont, Paper);
        }
        const TCHAR* Weapon = bArmed ? TEXT("한손검") : TEXT("미장착");
        CenterText(Weapon, BadgeCenter - Measure(*Canvas, Weapon, BadgeFont).X * 0.5,
            BadgeBottom - 10.0, BadgeFont, Paper);

        if (bNewId || !PartyCardValues[Index].IsSet() || PartyCardValues[Index].GetValue() != Values)
        {
            UE_LOG(LogCoopRPGGameplayFeedback, Log,
                TEXT("ExpeditionPartyCard Round=%s Slot=%d Local=%d HP=%d SP=%d Armed=%d X=%.0f Y=%.0f Width=%.0f Height=%.0f Recovery=%d IdLength=%d Truncated=%d IdPixels=%.1f IdLimit=%.1f DividerEnd=%.0f PotionRight=%.0f SPBottom=%.0f BadgeLeft=%.0f BadgeRight=%.0f BadgeTop=%.0f BadgeBottom=%.0f HeroTop=%.0f RecoveryHint=%d"),
                *LastPresentedPartyRound.ToString(), Slot, bLocal, Values.X, Values.Y, Values.Z, X, Y, Width, Height, Recovery,
                CardId.Characters, RawName != CardId.Display, Measure(*Canvas, CardId.Display, NameFont).X, NameWidth,
                DividerEnd, PotionLeft + PotionWidth, SpTop + PotionHeight, BadgeLeft, Right, BadgeTop, BadgeBottom,
                BadgeTop, bShowRecoveryHint);
            PartyCardValues[Index] = Values;
        }
    }

    if (bShowTeamStone)
    {
        const bool bHasStone = State.GetTeamLootCount() > 0;
        const FString Label = bHasStone ? TEXT("팀 마법석 · 획득") : TEXT("팀 마법석 · 미획득");
        const double StripHeight = RowHeight + 10.0;
        const FVector2D Position(Margin, Y - CardGap - StripHeight);
        const FVector2D Size(Width, StripHeight);
        WorldLabelBounds.Emplace(Position, Position + Size);
        ExpeditionDashboard.DrawDarkPanel(*this, Position, Size);
        Text(*Canvas, Fit(*Canvas, Label, HeroFont, Width - 2.0 * Inset),
            Position + FVector2D(Inset, 5.0), HeroFont, Paper);
    }
}

void ACoopRPGLootHUD::DrawLocalCombatStatus(const ACoopRPGGameState& State, const ACoopRPGCharacter& Hero,
    const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack)
{
    using namespace CoopRPG::ExpeditionUI;
    if (Canvas->ClipX < 560.0)
    {
        return;
    }

    const float Health = Hero.GetHealth();
    const float Stamina = Hero.GetStamina();
    const EAttackHudState Status = Attack.State;

    const bool bWide = Canvas->ClipX >= 1200.0;
    const double Margin = bWide ? 22.0 : 12.0;
    const double Width = bWide ? WideCombatHudWidth : NarrowCombatHudWidth;
    const double TrackHeight = bWide ? 20.0 : 17.0;
    const double PanelHeight = 2.0 * TrackHeight + 19.0;
    const double SlotSide = bWide ? 54.0 : 43.0;
    constexpr double SlotGap = 11.0;
    const FVector2D Panel(Canvas->ClipX - Margin - Width, Canvas->ClipY - Margin - PanelHeight);
    const double SlotY = Panel.Y - SlotSide - 29.0;
    const double SlotRight = Panel.X + Width - 1.0;
    const FVector2D EmptySlot(SlotRight - SlotSide, SlotY);
    const FVector2D AttackSlot(EmptySlot.X - SlotGap - SlotSide, SlotY);
    WorldLabelBounds.Emplace(Panel, Panel + FVector2D(Width, PanelHeight));
    WorldLabelBounds.Emplace(FVector2D(AttackSlot.X, SlotY), FVector2D(SlotRight, Panel.Y));

    const auto TrackFont = Font(bWide ? 11.0f : 9.0f, true);
    const auto LabelFont = Font(bWide ? 10.0f : 9.0f);
    const FLinearColor Paper(FColor(255, 241, 210));
    const FLinearColor DisabledInk(FColor(255, 206, 201));
    ExpeditionDashboard.DrawDarkPanel(*this, Panel, FVector2D(Width, PanelHeight));
    constexpr double PanelInset = 8.0;
    constexpr double ColumnGap = 6.0;
    const double LabelWidth = FMath::CeilToDouble(FMath::Max(
        Measure(*Canvas, TEXT("HP"), TrackFont).X, Measure(*Canvas, TEXT("SP"), TrackFont).X));
    const auto FullValueWidth = [&](float Maximum)
    {
        const int32 Cap = FMath::RoundToInt(Maximum);
        return Measure(*Canvas, FString::Printf(TEXT("%d/%d"), Cap, Cap), TrackFont).X;
    };
    const double ValueWidth = FMath::CeilToDouble(FMath::Max(
        FullValueWidth(UCoopRPGCombatAttributes::MaxHealth), FullValueWidth(UCoopRPGCombatAttributes::MaxStamina)));
    const double TrackX = Panel.X + PanelInset + LabelWidth + ColumnGap;
    const double ValueRight = Panel.X + Width - PanelInset;
    const double TrackWidth = ValueRight - ValueWidth - ColumnGap - TrackX;
    const auto DrawTrack = [&](double Y, const TCHAR* Label, float Current, float Maximum, bool bStamina)
    {
        constexpr double TrackInsetY = 2.0;
        ExpeditionDashboard.DrawResourceTrack(*this, FVector2D(TrackX, Y + TrackInsetY),
            FVector2D(TrackWidth, TrackHeight - 2.0 * TrackInsetY), Maximum > 0.0f ? Current / Maximum : 0.0f, bStamina);

        const FString Value = FString::Printf(TEXT("%d/%d"), FMath::CeilToInt(Current), FMath::RoundToInt(Maximum));
        const FVector2D TextSize = Measure(*Canvas, Value, TrackFont);
        const double MidY = Y + TrackHeight * 0.5;
        Text(*Canvas, Label, FVector2D(Panel.X + PanelInset, MidY - Measure(*Canvas, Label, TrackFont).Y * 0.5), TrackFont, Paper);
        Text(*Canvas, Value, FVector2D(ValueRight - TextSize.X, MidY - TextSize.Y * 0.5), TrackFont, FLinearColor::White);
    };
    DrawTrack(Panel.Y + 7.0, TEXT("HP"), Health, UCoopRPGCombatAttributes::MaxHealth, false);
    DrawTrack(Panel.Y + 12.0 + TrackHeight, TEXT("SP"), Stamina, UCoopRPGCombatAttributes::MaxStamina, true);

    ExpeditionDashboard.DrawSkillSlot(*this, AttackSlot, SlotSide);
    ExpeditionDashboard.DrawSkillSlot(*this, EmptySlot, SlotSide);
    const bool bDisabled = Status != EAttackHudState::Ready && Status != EAttackHudState::Cooling;
    ExpeditionDashboard.DrawBasicAttackGlyph(*this, *Canvas, FBox2D(AttackSlot, AttackSlot + FVector2D(SlotSide)), Attack);
    const FVector2D DashSize = Measure(*Canvas, TEXT("—"), TrackFont);
    Text(*Canvas, TEXT("—"), EmptySlot + FVector2D((SlotSide - DashSize.X) * 0.5, (SlotSide - DashSize.Y) * 0.5), TrackFont, Paper);

    const int32 RemainingTenths = Attack.GetRemainingTenths();
    if (Status == EAttackHudState::Cooling)
    {
        if (RemainingTenths > 0)
        {
            const FString Seconds = FString::Printf(TEXT("%.1f"), RemainingTenths / 10.0);
            const FVector2D Size = Measure(*Canvas, Seconds, TrackFont);
            const FVector2D Position = AttackSlot + FVector2D((SlotSide - Size.X) * 0.5, SlotSide - Size.Y - 5.0);
            DrawRect(FLinearColor(0.04f, 0.025f, 0.02f, 0.80f), Position.X - 3.0, Position.Y, Size.X + 6.0, Size.Y);
            Text(*Canvas, Seconds, Position, TrackFont, Paper);
        }
    }

    const auto DrawCaption = [&](FVector2D Slot, const TCHAR* Label, const FLinearColor& Color)
    {
        const FVector2D Size = Measure(*Canvas, Label, LabelFont);
        const FVector2D Position = Slot + FVector2D((SlotSide - Size.X) * 0.5, SlotSide + 4.0);
        DrawRect(FLinearColor(0.05f, 0.035f, 0.025f, 0.85f), Position.X - 4.0, Position.Y - 1.0, Size.X + 8.0, Size.Y + 2.0);
        Text(*Canvas, Label, Position, LabelFont, Color);
    };
    DrawCaption(AttackSlot, TEXT("기본 공격"), bDisabled ? DisabledInk : Paper);
    DrawCaption(EmptySlot, TEXT("미배정"), Paper);
    if (bDisabled)
    {
        const TCHAR* Reason = AttackHudLabel(Status);
        const FVector2D ReasonSize = Measure(*Canvas, Reason, LabelFont);
        const FVector2D Position(AttackSlot.X - ReasonSize.X - 13.0, SlotY + (SlotSide - ReasonSize.Y) * 0.5);
        DrawRect(FLinearColor(0.10f, 0.04f, 0.03f, 0.87f), Position.X - 5.0, Position.Y - 1.0,
            ReasonSize.X + 10.0, ReasonSize.Y + 2.0);
        Text(*Canvas, Reason, Position, LabelFont, DisabledInk);
        WorldLabelBounds.Emplace(Position - FVector2D(5.0, 1.0), Position + ReasonSize + FVector2D(5.0, 1.0));
    }

    const FIntVector4 Sample(FMath::CeilToInt(Health), FMath::CeilToInt(Stamina), static_cast<int32>(Status), RemainingTenths);
    if (Sample != LastLocalCombatSample || LastLocalCombatRound != State.GetRoundId())
    {
        LastLocalCombatSample = Sample;
        LastLocalCombatRound = State.GetRoundId();
        UE_LOG(LogCoopRPGGameplayFeedback, Log, TEXT("ExpeditionCombatHUD Round=%s Slot=%d HP=%d SP=%d State=%s Remaining=%.1f Duration=%.3f X=%.0f Y=%.0f Width=%.0f Bottom=%.0f"),
            *LastLocalCombatRound.ToString(), Hero.GetDisplaySlot(), Sample.X, Sample.Y, AttackHudLogState(Status),
            RemainingTenths / 10.0, Attack.Duration, Panel.X, SlotY, Width, Panel.Y + PanelHeight);
    }
}

void ACoopRPGLootHUD::DrawGameplayFeedback(bool bExpedition)
{
    if (!bExpedition)
    {
        for (TActorIterator<ACoopRPGCharacter> It(GetWorld()); It; ++It)
        {
            if (const int32 Slot = It->GetDisplaySlot(); Slot > 0)
            {
                DrawHealthNameplate(It->GetActorLocation() + FVector(0.0, 0.0, 94.0),
                    FString::Printf(TEXT("P%d"), Slot), It->GetHealth(), CoopRPG::Presentation::PlayerColor(Slot));
            }
        }
        for (TActorIterator<ACoopRPGEnemy> It(GetWorld()); It; ++It)
        {
            if (!It->IsDead())
            {
                const int32 TargetSlot = It->IsShieldEncounter()
                    ? (It->IsShieldRaised() ? It->GetShieldWarning().TargetSlot : 0) : INDEX_NONE;
                DrawHealthNameplate(It->GetActorLocation() + FVector(0.0, 0.0, 76.0), TEXT("Enemy"), It->GetHealth(),
                    CoopRPG::Presentation::EnemyColor(), TargetSlot, FMath::RoundToInt(It->GetMaxHealth()));
            }
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
    const FSlateFontInfo NormalFont = CoopRPG::ExpeditionUI::Font(12.0f);
    FSlateFontInfo NameFont = NormalFont;
    if (bMagicStone)
    {
        NameFont.TypefaceFontName = TEXT("Bold");
    }
    FCanvasTextItem Key(FVector2D::ZeroVector, FText::FromString(bMagicStone ? TEXT("[T]") : TEXT("[Y]")),
        NormalFont, FLinearColor::White);
    FCanvasTextItem Name(FVector2D::ZeroVector, FText::FromString(bMagicStone ? TEXT("마법석") : TEXT("회복 물약")),
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
    WorldLabelBounds.Emplace(FVector2D(X - 3.0, Y - 1.0), FVector2D(X + Width + 3.0, Y + Height + 1.0));
}

void ACoopRPGLootHUD::DrawWorldLabel(const FVector& Location, const FString& Text, const FLinearColor& Color,
    FVector2D Offset, int32 ItemRow, bool bAvoidOverlap)
{
    const FVector Screen = Project(Location);
    if (Screen.Z <= 0.0f || Screen.X < 0.0f || Screen.X >= Canvas->ClipX || Screen.Y < 0.0f || Screen.Y >= Canvas->ClipY)
    {
        return;
    }

    const auto LabelFont = CoopRPG::ExpeditionUI::Font(11.0f);
    const FVector2D Size = CoopRPG::ExpeditionUI::Measure(*Canvas, Text, LabelFont);
    const float Width = Size.X;
    const float Height = Size.Y;
    const bool bItem = ItemRow != INDEX_NONE;
    const float Left = static_cast<float>(Screen.X + Offset.X) - (bItem ? 0.0f : Width * 0.5f);
    const float ColumnWidth = bItem ? ItemColumnWidth : Width;
    const float X = FMath::Clamp(Left, 4.0f, FMath::Max(4.0f, Canvas->ClipX - ColumnWidth - 4.0f));
    const float ReservedRows = bItem ? ItemRowSpacing * (MaxItemRows - 1) : 0.0f;
    const float AnchorY = FMath::Clamp(static_cast<float>(Screen.Y + Offset.Y), 4.0f,
        FMath::Max(4.0f, Canvas->ClipY - Height - ReservedRows - 4.0f));
    const float Y = AnchorY + (bItem ? ItemRow * ItemRowSpacing : 0.0f);
    for (const float Shift : {0.0f, -Height - 5.0f, Height + 5.0f, -2.0f * Height - 10.0f})
    {
        const float LabelY = FMath::Clamp(Y + Shift, 4.0f, FMath::Max(4.0f, Canvas->ClipY - Height - 4.0f));
        const FBox2D Bounds(FVector2D(X - 3.0, LabelY - 1.0), FVector2D(X + Width + 3.0, LabelY + Height + 1.0));
        if (bAvoidOverlap && WorldLabelBounds.ContainsByPredicate([&Bounds](const FBox2D& Other) { return Bounds.Intersect(Other); }))
        {
            continue;
        }
        DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), X - 3.0f, LabelY - 1.0f, Width + 6.0f, Height + 2.0f);
        CoopRPG::ExpeditionUI::Text(*Canvas, Text, FVector2D(X, LabelY), LabelFont, Color);
        WorldLabelBounds.Add(Bounds);
        break;
    }
}

void ACoopRPGLootHUD::DrawHealthNameplate(const FVector& Location, const FString& Name, float Health,
    const FLinearColor& Color, int32 AttackTargetSlot, int32 MaxHealth)
{
    const FVector Screen = Project(Location);
    if (Screen.Z <= 0.0f || Screen.X < 0.0f || Screen.X >= Canvas->ClipX || Screen.Y < 0.0f || Screen.Y >= Canvas->ClipY)
    {
        return;
    }

    const FString NameLine = FString::Printf(TEXT("[%s]"), *Name);
    const FString HealthLine = FString::Printf(TEXT("%d/%d"), FMath::CeilToInt(Health), MaxHealth);
    using namespace CoopRPG::ExpeditionUI;
    const auto LabelFont = Font(11.0f);
    const auto TargetFont = Font(9.5f);
    const FVector2D NameSize = Measure(*Canvas, NameLine, LabelFont);
    const FVector2D HealthSize = Measure(*Canvas, HealthLine, LabelFont);
    const float RowSpacing = FMath::Max(HealthNameplateLineSpacing, NameSize.Y);
    const bool bShowAttackTarget = AttackTargetSlot != INDEX_NONE;
    const float HalfWidth = bShowAttackTarget ? ShieldNameplateHalfWidth : FMath::Max(NameSize.X, HealthSize.X) * 0.5f;
    const float Height = bShowAttackTarget ? RowSpacing * 3.0f : RowSpacing + HealthSize.Y;
    const float CenterX = FMath::Clamp(static_cast<float>(Screen.X), HalfWidth + 4.0f,
        FMath::Max(HalfWidth + 4.0f, Canvas->ClipX - HalfWidth - 4.0f));
    const float MaxTop = FMath::Max(4.0f, Canvas->ClipY - Height - 4.0f);
    const float Top = FMath::Clamp(static_cast<float>(Screen.Y) - RowSpacing, 4.0f, MaxTop);
    DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), CenterX - HalfWidth - 3.0f, Top - 1.0f,
        HalfWidth * 2.0f + 6.0f, Height + 2.0f);
    Text(*Canvas, NameLine, FVector2D(CenterX - NameSize.X * 0.5f, Top), LabelFont, Color);
    Text(*Canvas, HealthLine, FVector2D(CenterX - HealthSize.X * 0.5f, Top + RowSpacing), LabelFont, Color);
    if (bShowAttackTarget)
    {
        const bool bHasTarget = AttackTargetSlot > 0;
        const FString TargetLine = bHasTarget ? FString::Printf(TEXT("Target P%d"), AttackTargetSlot) : TEXT("Target —");
        const FVector2D TargetSize = Measure(*Canvas, TargetLine, TargetFont);
        const float TargetY = Top + 2.0f * RowSpacing + FMath::Max(0.0, RowSpacing - TargetSize.Y) * 0.5f;
        Text(*Canvas, TargetLine, FVector2D(CenterX - TargetSize.X * 0.5f, TargetY), TargetFont,
            bHasTarget ? FLinearColor::White : PanelAccent);
    }
    WorldLabelBounds.Emplace(FVector2D(CenterX - HalfWidth - 3.0f, Top - 1.0f),
        FVector2D(CenterX + HalfWidth + 3.0f, Top + Height + 1.0f));
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

void ACoopRPGLootHUD::DrawExpeditionResult(const ACoopRPGGameState& State, int32 Stage)
{
    using namespace CoopRPG::ExpeditionUI;
    const ECoopRPGRoundEndReason Reason = State.GetRoundEndReason();
    const auto& Slots = State.GetReadySlots();
    const bool bPlayerMissing = Reason == ECoopRPGRoundEndReason::Disconnected
        || Slots.PlayerOne == ECoopRPGReadyStatus::Disconnected || Slots.PlayerTwo == ECoopRPGReadyStatus::Disconnected;
    const bool bRetry = Reason == ECoopRPGRoundEndReason::Death || Reason == ECoopRPGRoundEndReason::Timeout;
    const bool bFinished = Reason == ECoopRPGRoundEndReason::BossDefeated;
    const bool bFailed = bRetry || Reason == ECoopRPGRoundEndReason::Disconnected;
    const TCHAR* Title = Reason == ECoopRPGRoundEndReason::Disconnected ? TEXT("원정 중단")
        : bFailed ? TEXT("스테이지 실패") : bFinished ? TEXT("원정 완료") : TEXT("스테이지 성공");
    const TCHAR* Detail = TEXT("두 사람이 함께 포탈로 탈출했어요.");
    switch (Reason)
    {
    case ECoopRPGRoundEndReason::PuzzleSolved: Detail = TEXT("서로 다른 발판을 동시에 밟아 퍼즐을 풀었어요."); break;
    case ECoopRPGRoundEndReason::BossDefeated: Detail = TEXT("흑기사를 처치하고 숲 원정을 마쳤어요."); break;
    case ECoopRPGRoundEndReason::Death: Detail = TEXT("파티원이 쓰러졌어요."); break;
    case ECoopRPGRoundEndReason::Timeout: Detail = TEXT("제한 시간이 끝났어요."); break;
    case ECoopRPGRoundEndReason::Disconnected: Detail = TEXT("파티원의 접속이 끊겼어요."); break;
    default: break;
    }

    FString Next = TEXT("모든 스테이지를 완료했어요.");
    if (bPlayerMissing)
    {
        Next = TEXT("다른 세션에 참가하려면 게임을 다시 실행해 주세요.");
    }
    else if (bRetry)
    {
        Next = TEXT("잠시 후 같은 스테이지에 다시 도전해요.");
    }
    else if (!State.GetRoundTravelDestination().IsEmpty())
    {
        Next = FString::Printf(TEXT("잠시 후 [%s]에 입장해요."), FCoopRPGExpeditionDashboard::StageTitle(Stage + 1));
    }

    const FLinearColor Ink(FColor(61, 41, 25));
    const FLinearColor Accent = bFailed ? FLinearColor(FColor(148, 42, 39)) : FLinearColor(FColor(34, 99, 54));
    const auto CaptionFont = Font(13.0f);
    const auto TitleFont = Font(22.0f, true);
    const auto BodyFont = Font(13.5f);
    const auto ScoreFont = Font(14.0f, true);
    const TCHAR* StageName = FCoopRPGExpeditionDashboard::StageTitle(Stage);
    const FString Score = FString::Printf(TEXT("팀 점수 %d점"), State.GetTeamScore());
    constexpr double Padding = 24.0;
    constexpr double DividerGap = 12.0;
    constexpr double DividerHeight = 8.0;

    const double NaturalWidth = FMath::Max(Measure(*Canvas, Detail, BodyFont).X, Measure(*Canvas, Next, BodyFont).X);
    const double Width = FMath::Min(Canvas->ClipX - 24.0, FMath::Max(432.0, NaturalWidth + 2.0 * Padding));
    const double TitleTop = Padding + Measure(*Canvas, StageName, CaptionFont).Y + 5.0;
    const double DetailTop = TitleTop + Measure(*Canvas, Title, TitleFont).Y + 9.0;
    const double ScoreTop = DetailTop + Measure(*Canvas, Detail, BodyFont).Y + 7.0;
    const double DividerTop = ScoreTop + Measure(*Canvas, Score, ScoreFont).Y + DividerGap;
    const double NextTop = DividerTop + DividerHeight + DividerGap;
    const double Height = NextTop + Measure(*Canvas, Next, BodyFont).Y + Padding;
    const double X = (Canvas->ClipX - Width) * 0.5;
    const double Y = FMath::Max(12.0, (Canvas->ClipY - Height) * 0.5);
    ExpeditionDashboard.DrawPanel(*this, FVector2D(X, Y), FVector2D(Width, Height));

    const auto DrawCentered = [&](FString Value, double Row, const FSlateFontInfo& FontInfo, const FLinearColor& Color)
    {
        Value = Fit(*Canvas, MoveTemp(Value), FontInfo, Width - 2.0 * Padding);
        Text(*Canvas, Value, FVector2D(X + (Width - Measure(*Canvas, Value, FontInfo).X) * 0.5, Y + Row), FontInfo, Color);
    };
    DrawCentered(StageName, Padding, CaptionFont, Ink);
    DrawCentered(Title, TitleTop, TitleFont, Accent);
    DrawCentered(Detail, DetailTop, BodyFont, Ink);
    DrawCentered(Score, ScoreTop, ScoreFont, Ink);
    ExpeditionDashboard.DrawDivider(*this, FVector2D(X + Padding, Y + DividerTop), Width - 2.0 * Padding);
    DrawCentered(MoveTemp(Next), NextTop, BodyFont, Ink);

    if (LastPresentedExpeditionResult != State.GetRoundId())
    {
        LastPresentedExpeditionResult = State.GetRoundId();
        UE_LOG(LogCoopRPGGameplayFeedback, Log, TEXT("ExpeditionResultPanel Round=%s Stage=%d Reason=%d Score=%d"),
            *State.GetRoundId().ToString(), Stage, static_cast<int32>(Reason), State.GetTeamScore());
    }
}

void ACoopRPGLootHUD::DrawRoundResult(const ACoopRPGGameState& State,
    ACoopRPGPlayerController& Controller, int32 Stage)
{
    const UWorld* World = GetWorld();
    if ((CoopRPG::Expedition::IsSkirmish(World) || CoopRPG::Expedition::IsPuzzle(World)
        || CoopRPG::Expedition::IsBoss(World)) && ExpeditionDashboard.Prepare())
    {
        DrawExpeditionResult(State, Stage);
        Controller.AcknowledgeRoundResult(State.GetRoundId());
        return;
    }

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
