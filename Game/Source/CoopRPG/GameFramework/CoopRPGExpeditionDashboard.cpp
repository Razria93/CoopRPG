#include "CoopRPGExpeditionDashboard.h"

#include "CoopRPGExpeditionAttackHUD.h"
#include "CoopRPGExpeditionHUDText.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPGGameState.h"
#include "CoopRPGPlayerController.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"
#include "CoopRPG/Loot/CoopRPGEnemyLoot.h"
#include "CoopRPG/Loot/CoopRPGSharedChest.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "ImageUtils.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGExpeditionHUD, Log, All);

enum class ECoopRPGDashboardLabel : uint8
{
    Lobby, Skirmish, Puzzle, Boss, Expedition, Ready, PrepareSkills, OpenChest, ChestActions, Mushrooms, Plates, Knight,
    Stone, Portal, WaitForPartner, Countdown, Start, Defeat, StoneGranted, CountStarted, CountReset,
    More, Fold, ReadyYes, ReadyWait, ReadyMissing, Objective, StoneAppeared, CloseChest, Departure, EquipSword,
    BossRewards, BossRewardsOpened, PartyWaiting, StageStart
};

namespace
{
    using ELabel = ECoopRPGDashboardLabel;
    using namespace CoopRPG::ExpeditionUI;

    constexpr int32 MaxRecords = 5;
    const FLinearColor DashboardTextColor(FColor(61, 41, 25));
    const FLinearColor NoticeInk(FColor(255, 241, 210));
    const FLinearColor WaitingInk(FColor(153, 41, 39));
    const FLinearColor ReadyInk(FColor(34, 99, 54));
    const FLinearColor SecondaryInk(FColor(100, 79, 52));

    ELabel GetGoal(const ACoopRPGGameState& State, const ACoopRPGPlayerController& Controller, int32 Stage)
    {
        if (Stage == 0)
        {
            if (State.IsLobbyDepartureScheduled())
            {
                return ELabel::Departure;
            }
        }
        if (!State.IsRoundPlaying())
        {
            if (State.IsRoundStartScheduled())
            {
                return ELabel::StageStart;
            }
            return Controller.IsRoundReady() ? ELabel::PartyWaiting : Stage == 1 ? ELabel::PrepareSkills : ELabel::Ready;
        }
        if (Stage == 2)
        {
            return ELabel::Plates;
        }
        if ((Stage == 1 || Stage == 3) && State.IsExtractionUnlocked())
        {
            if (State.IsExtractionInProgress())
            {
                return ELabel::Countdown;
            }
            if (State.GetExtractionPlayersInside() == 1)
            {
                return ELabel::WaitForPartner;
            }
            return Stage == 3 ? ELabel::BossRewards : ELabel::Portal;
        }
        if (const auto* Drop = State.GetEnemyLoot(); Stage == 1 && IsValid(Drop) && !Drop->IsClaimed())
        {
            return ELabel::Stone;
        }
        if (IsValid(Controller.GetOpenedChest()))
        {
            return ELabel::ChestActions;
        }
        const auto* Pawn = Cast<ACoopRPGCharacter>(Controller.GetPawn());
        if (!IsValid(Pawn) || !Pawn->HasMeleeWeapon())
        {
            return Controller.GetSwordOwnership() != ECoopRPGEquipmentOwnership::None ? ELabel::EquipSword : ELabel::OpenChest;
        }
        return Stage == 3 ? ELabel::Knight : ELabel::Mushrooms;
    }

    const TCHAR* RecordText(ELabel Label)
    {
        switch (Label)
        {
        case ELabel::Start: return TEXT("스테이지 시작");
        case ELabel::Defeat: return TEXT("버섯 처치");
        case ELabel::StoneAppeared: return TEXT("마법석 드롭");
        case ELabel::StoneGranted: return TEXT("마법석 획득 · 포탈 개방");
        case ELabel::BossRewardsOpened: return TEXT("흑기사 처치 · 보상과 포탈 개방");
        case ELabel::CountStarted: return TEXT("공동 탈출 시작");
        case ELabel::CountReset: return TEXT("공동 탈출 대기 취소");
        default: return TEXT("");
        }
    }

    const TCHAR* ReadyText(ECoopRPGReadyStatus Status)
    {
        switch (Status)
        {
        case ECoopRPGReadyStatus::Ready: return TEXT("준비 완료");
        case ECoopRPGReadyStatus::Disconnected: return TEXT("접속 대기");
        default: return TEXT("준비 대기");
        }
    }

    const FLinearColor& ReadyColor(ECoopRPGReadyStatus Status)
    {
        switch (Status)
        {
        case ECoopRPGReadyStatus::Ready: return ReadyInk;
        case ECoopRPGReadyStatus::Disconnected: return SecondaryInk;
        default: return WaitingInk;
        }
    }

    TArray<FString> GetNotice(const ACoopRPGGameState& State, ELabel Label, bool bBossActive)
    {
        switch (Label)
        {
        case ELabel::Ready:
            return {TEXT("시작할 준비가 되면 [R] 키를 눌러 주세요.")};
        case ELabel::PrepareSkills:
            return {TEXT("보급 상자는 [E]로 열어요."), TEXT("준비를 마치면 [R] 키를 눌러 주세요.")};
        case ELabel::PartyWaiting:
            return {TEXT("준비가 완료됐어요."), TEXT("파티원이 준비할 때까지 기다려 주세요.")};
        case ELabel::Departure:
            return {TEXT("곧 [스테이지 1 · 버섯 군락]에 입장합니다!"),
                FString::Printf(TEXT("남은 시간 %.1f초"), FMath::CeilToDouble(State.GetLobbyDepartureRemainingSeconds() * 10.0) / 10.0)};
        case ELabel::StageStart:
            return {TEXT("곧 스테이지가 시작돼요."),
                FString::Printf(TEXT("남은 시간 %.1f초"), FMath::CeilToDouble(State.GetRoundStartRemainingSeconds() * 10.0) / 10.0)};
        case ELabel::OpenChest:
            return bBossActive
                ? TArray<FString>{TEXT("상자에서 [E]로 장비·물약을 챙기세요.")}
                : TArray<FString>{TEXT("가운데 상자에서 [E] 키를 눌러"), TEXT("장비와 물약을 챙겨 주세요.")};
        case ELabel::ChestActions:
            return {bBossActive ? TEXT("상자에서 장비와 물약을 챙기세요.") : TEXT("열어 둔 상자에서 장비와 물약을 챙겨 주세요.")};
        case ELabel::EquipSword:
            return {TEXT("인벤토리에서 한손검을 장착해 주세요.")};
        case ELabel::Mushrooms:
            return {TEXT("전투 준비를 마쳤어요. 버섯을 모두 처치해 주세요.")};
        case ELabel::Stone:
            return {TEXT("떨어진 마법석에 다가가 [T] 키로 주워 주세요.")};
        case ELabel::BossRewards:
            return {TEXT("[E]로 보상 상자를 열 수 있어요."), TEXT("두 사람이 포탈에 서면 원정을 마쳐요.")};
        case ELabel::Portal:
            return {TEXT("마법석으로 포탈이 열렸어요."), TEXT("두 사람 모두 포탈의 마법진에 올라서 주세요.")};
        case ELabel::WaitForPartner:
            return {TEXT("포탈에 한 명이 도착했어요. (1/2)"), TEXT("동료가 마법진에 올 때까지 기다려 주세요.")};
        case ELabel::Countdown:
            return {TEXT("두 사람 모두 마법진 위에 머물러 주세요. (2/2)"),
                FString::Printf(TEXT("탈출까지 %.1f초"), FMath::CeilToDouble(State.GetExtractionRemainingSeconds() * 10.0) / 10.0)};
        case ELabel::Plates:
            return {TEXT("두 사람이 서로 다른 발판에 올라서 주세요."), TEXT("발판마다 한 명씩 있어야 길이 열려요.")};
        case ELabel::Knight:
            return {TEXT("방패 옆을 노려 흑기사를 처치하세요.")};
        default:
            return {};
        }
    }
}

FCoopRPGExpeditionDashboard::FCoopRPGExpeditionDashboard() = default;
FCoopRPGExpeditionDashboard::~FCoopRPGExpeditionDashboard() = default;

bool FCoopRPGExpeditionDashboard::Prepare()
{
    if (!bInitialized)
    {
        bInitialized = true;
        Skin.Reset(FImageUtils::ImportFileAsTexture2D(FPaths::Combine(
            FPaths::ProjectContentDir(), TEXT("UI/ExpeditionDashboard/DashboardSkin.png"))));
        if (!Skin.IsValid())
        {
            UE_LOG(LogCoopRPGExpeditionHUD, Error, TEXT("DashboardSkinMissing"));
        }
    }
    return Skin.IsValid();
}

void FCoopRPGExpeditionDashboard::Draw(AHUD& HUD, UCanvas& Canvas, const ACoopRPGGameState& State,
    const ACoopRPGPlayerController& Controller, int32 Stage, const FVector2D& ViewSize)
{
    if (!State.GetRoundId().IsValid())
    {
        Hide();
        return;
    }

    const ELabel Goal = GetGoal(State, Controller, Stage);
    Update(State, Stage, Goal);

    const bool bWide = ViewSize.X >= 1200.0;
    const float Pad = bWide ? 22.0f : 12.0f;
    const float Width = bWide ? 284.0f : 224.0f;
    const float Inset = bWide ? 19.0f : 16.0f;
    const float SectionGap = bWide ? 12.0f : 10.0f;
    constexpr float DividerHeight = 8.0f;
    const float Left = Pad + Inset;
    const float Right = Pad + Width - Inset;
    const bool bWaiting = !State.IsRoundPlaying();

    const auto TitleFont = Font(bWide ? 17.0f : 14.0f, true);
    const auto BodyFont = Font(bWide ? 13.0f : 11.0f);
    const auto SmallFont = Font(bWide ? 12.0f : 10.0f);
    const auto DetailFont = Font(SmallFont.Size, true);
    const float NameHeight = Measure(Canvas, TEXT("가"), Font(BodyFont.Size, true)).Y;
    const float SmallHeight = Measure(Canvas, TEXT("가"), SmallFont).Y;
    const float BodyHeight = Measure(Canvas, TEXT("가"), BodyFont).Y;
    const float BadgeSide = FMath::CeilToFloat(NameHeight + 3.0f + SmallHeight);
    constexpr float PartyGap = 8.0f;
    const FVector2D TimerSize(43.0, 20.0);
    const float HeaderTop = Pad + Inset;
    const float HeaderRowHeight = FMath::Max(static_cast<float>(TimerSize.Y), SmallHeight);
    const float TitleTop = HeaderTop + HeaderRowHeight + 4.0f;
    const float Icon = bWide ? 29.0f : 25.0f;
    const float TitleX = Left + Icon + 8.0f;
    auto StageFont = TitleFont;
    StageFont.Size *= FMath::Min(1.0, (Right - TitleX) / FMath::Max(Measure(Canvas, StageTitle(Stage), StageFont).X, 1.0));
    const float TitleBottom = TitleTop + static_cast<float>(Measure(Canvas, StageTitle(Stage), StageFont).Y);
    const float ObjectiveTop = TitleBottom + SectionGap;
    const float DetailTop = ObjectiveTop + BodyHeight + (Stage == 0 ? 3.0f : 7.0f);
    const float ObjectiveBottom = Stage == 0 ? DetailTop + BodyHeight :
        (Stage == 1 || Stage == 2 ? DetailTop + SmallHeight : ObjectiveTop + BodyHeight);

    const int32 Count = FMath::Min(Records.Num(), bWide || bExpanded ? MaxRecords : 2);
    const float LogGap = bWide ? 5.0f : 4.0f;
    const float LogHeight = Count > 0 ? Count * SmallHeight + (Count - 1) * LogGap : 0.0f;
    const float SectionTop = ObjectiveBottom + SectionGap;
    const float SectionBottom = SectionTop + DividerHeight;
    const float RowTop = SectionBottom + SectionGap;
    const float ContentBottom = bWaiting ? RowTop + 2.0f * BadgeSide + PartyGap
        : (Count > 0 ? RowTop + LogHeight : ObjectiveBottom);
    StatusBounds = FBox2D(FVector2D(Pad, Pad), FVector2D(Pad + Width, ContentBottom + Inset));
    DrawPanel(HUD, StatusBounds.Min, StatusBounds.GetSize());

    const float IconY = HeaderTop + (TitleBottom - HeaderTop - Icon) * 0.5f;
    DrawImage(HUD, FBox2D(FVector2D(284, 200), FVector2D(316, 232)), FVector2D(Left, IconY), FVector2D(Icon, Icon));
    Text(Canvas, TEXT("숲 원정"), FVector2D(TitleX, HeaderTop + (HeaderRowHeight - SmallHeight) * 0.5f), SmallFont, DashboardTextColor);
    Text(Canvas, StageTitle(Stage), FVector2D(TitleX, TitleTop), StageFont, DashboardTextColor);

    if (!bWaiting && (Stage == 1 || Stage == 3) && !State.AreBossRewardsAvailable())
    {
        const int32 Time = FMath::Max(0, FMath::CeilToInt(State.GetRoundRemainingSeconds()));
        const FString Clock = FString::Printf(TEXT("%d:%02d"), Time / 60, Time % 60);
        const FVector2D TimerPos(Right - TimerSize.X, HeaderTop + (HeaderRowHeight - TimerSize.Y) * 0.5);
        DrawImage(HUD, FBox2D(FVector2D(0, 334), FVector2D(43, 354)), TimerPos, TimerSize);
        const auto ClockFont = Font(10.0f);
        Text(Canvas, Clock, TimerPos + (TimerSize - Measure(Canvas, Clock, ClockFont)) * 0.5, ClockFont, NoticeInk);
    }

    if (Stage == 0)
    {
        Text(Canvas, TEXT("파티원 모두가 준비하면"), FVector2D(Left, ObjectiveTop), BodyFont, DashboardTextColor);
        Text(Canvas, TEXT("다음 스테이지로 출발합니다."), FVector2D(Left, DetailTop), BodyFont, DashboardTextColor);
    }
    else
    {
        const TCHAR* Objective = Stage == 1 ? TEXT("버섯을 모두 처치하세요")
            : Stage == 2 ? TEXT("각 발판에 한 명씩 서세요")
            : State.AreBossRewardsAvailable() ? TEXT("두 사람이 포탈로 탈출하세요") : TEXT("흑기사를 처치하세요");
        Text(Canvas, Objective, FVector2D(Left, ObjectiveTop), BodyFont, DashboardTextColor);
        if (Stage == 1 || Stage == 2)
        {
            const auto& Puzzle = State.GetPuzzleStatus();
            const int32 Progress = Stage == 1 ? State.GetSkirmishDefeats() : (Puzzle.OccupantsA != 0) + (Puzzle.OccupantsB != 0);
            const int32 Target = Stage == 1 ? CoopRPG::Expedition::SkirmishEnemyCount : 2;
            const FString CountText = FString::Printf(TEXT("%d/%d"), Progress, Target);
            Text(Canvas, Stage == 1 ? TEXT("- 버섯") : TEXT("- 발판"), FVector2D(Left, DetailTop), SmallFont, SecondaryInk);
            Text(Canvas, CountText, FVector2D(Right - Measure(Canvas, CountText, DetailFont).X, DetailTop), DetailFont, ReadyInk);
        }
    }

    if (bWaiting)
    {
        DrawDivider(HUD, FVector2D(Left, SectionTop), Right - Left);
        DrawReadyParty(HUD, Canvas, State, Controller, Left, Right, RowTop, BadgeSide, PartyGap, bWide);
    }

    bLogVisible = Count > 0 && !bWaiting;
    bToggleVisible = bLogVisible && !bWide && Records.Num() > 2;
    if (bLogVisible)
    {
        const float DividerCenter = SectionTop + DividerHeight * 0.5f;
        LogBounds = FBox2D(FVector2D(Left, SectionTop - 6.0f), FVector2D(Right, ContentBottom + 4.0f));
        constexpr double ToggleWidth = 65.0;
        DrawDivider(HUD, FVector2D(Left, SectionTop), Right - Left - (bToggleVisible ? ToggleWidth + 7.0 : 0.0));
        for (int32 Entry = 0; Entry < Count; ++Entry)
        {
            const FRecord& Record = Records[Records.Num() - Count + Entry];
            const float Y = RowTop + Entry * (SmallHeight + LogGap);
            HUD.DrawRect(SecondaryInk, Left, Y + (SmallHeight - 3.0f) * 0.5f, 3.0f, 3.0f);
            Text(Canvas, RecordText(Record.Label), FVector2D(Left + 9.0f, Y), SmallFont, DashboardTextColor);
            if (Record.Value != INDEX_NONE)
            {
                const FString Value = FString::Printf(TEXT("%d/%d"), Record.Value, CoopRPG::Expedition::SkirmishEnemyCount);
                Text(Canvas, Value, FVector2D(Right - Measure(Canvas, Value, SmallFont).X, Y), SmallFont, SecondaryInk);
            }
        }
        if (bToggleVisible)
        {
            ToggleBounds = FBox2D(FVector2D(Right - ToggleWidth, SectionTop - 6.0f), FVector2D(Right, SectionBottom + 6.0f));
            const TCHAR* Label = bExpanded ? TEXT("접기") : TEXT("더 보기");
            const FVector2D Size = Measure(Canvas, Label, SmallFont);
            Text(Canvas, Label, FVector2D(Right - 14.0 - Size.X, DividerCenter - Size.Y * 0.5), SmallFont, DashboardTextColor);
            const float SourceX = bExpanded ? 12.0f : 0.0f;
            DrawImage(HUD, FBox2D(FVector2D(SourceX, 297), FVector2D(SourceX + 10.0f, 305)),
                FVector2D(Right - 10.0f, SectionTop), FVector2D(10.0f, DividerHeight));
        }
    }

    const bool bBossActive = Stage == 3 && State.IsRoundPlaying() && !State.AreBossRewardsAvailable();
    DrawNotice(HUD, Canvas, State, Goal, ViewSize, Pad, Width, bWide, bBossActive);
    DrawBossHealth(HUD, Canvas, State, bBossActive, bWide);
}

const TCHAR* FCoopRPGExpeditionDashboard::StageTitle(int32 Stage)
{
    switch (Stage)
    {
    case 1: return TEXT("스테이지 1 · 버섯 군락");
    case 2: return TEXT("스테이지 2 · 쌍둥이 발판");
    case 3: return TEXT("스테이지 3 · 흑기사 결전");
    default: return TEXT("원정 대기소");
    }
}

void FCoopRPGExpeditionDashboard::DrawPanel(AHUD& HUD, FVector2D Position, FVector2D Size) const
{
    DrawNineSlice(HUD, Position, Size, FBox2D(FVector2D::ZeroVector, FVector2D(286, 195)),
        FVector2D(8, 7), FVector2D(8, 10));
}

void FCoopRPGExpeditionDashboard::DrawExpeditionIcon(AHUD& HUD, FVector2D Position, float Size) const
{
    DrawImage(HUD, FBox2D(FVector2D(284, 200), FVector2D(316, 232)), Position, FVector2D(Size));
}

void FCoopRPGExpeditionDashboard::DrawDivider(AHUD& HUD, FVector2D Position, float Width) const
{
    DrawImage(HUD, FBox2D(FVector2D(0, 285), FVector2D(250, 293)), Position, FVector2D(Width, 8.0f));
}

void FCoopRPGExpeditionDashboard::DrawDarkPanel(AHUD& HUD, FVector2D Position, FVector2D Size) const
{
    DrawNineSlice(HUD, Position, Size, FBox2D(FVector2D(0, 200), FVector2D(272, 252)),
        FVector2D(11.0, 11.0), FVector2D(11.0, 11.0));
}

void FCoopRPGExpeditionDashboard::DrawHealthBar(AHUD& HUD, FVector2D Position, float Width, float HealthFraction) const
{
    constexpr double Height = 7.0;
    HUD.DrawRect(FLinearColor(FColor(53, 34, 24)), Position.X - 1.0, Position.Y - 1.0, Width + 2.0, Height + 2.0);
    if (HealthFraction * Width >= 1.0f)
    {
        const FBox2D Source(FVector2D(0, 313), FVector2D(250.0 * HealthFraction, 327));
        DrawImage(HUD, Source, Position, FVector2D(Width * HealthFraction, Height));
    }
}

void FCoopRPGExpeditionDashboard::DrawResourceTrack(AHUD& HUD, FVector2D Position, FVector2D Size,
    float Fraction, ECoopRPGResourceTrack Resource) const
{
    const auto DrawRoundedRows = [](FVector2D Bounds, const auto& DrawRows)
    {
        const int32 Radius = FMath::FloorToInt(FMath::Min3(3.0, Bounds.X * 0.5, Bounds.Y * 0.5));
        for (int32 Row = 0; Row < Radius; ++Row)
        {
            const double Distance = Radius - Row - 0.5;
            const double Inset = Radius - FMath::Sqrt(Radius * Radius - Distance * Distance);
            DrawRows(Inset, Row, Bounds.X - 2.0 * Inset, 1.0);
            DrawRows(Inset, Bounds.Y - Row - 1.0, Bounds.X - 2.0 * Inset, 1.0);
        }
        DrawRows(0.0, Radius, Bounds.X, Bounds.Y - 2.0 * Radius);
    };

    const FLinearColor Track(FColor(41, 32, 28));
    DrawRoundedRows(Size, [&](double X, double Y, double Width, double Height)
    {
        HUD.DrawRect(Track, Position.X + X, Position.Y + Y, Width, Height);
    });

    const FVector2D Fill((Size.X - 2.0) * FMath::Clamp(Fraction, 0.0f, 1.0f), Size.Y - 2.0);
    if (Fill.X < 1.0)
    {
        return;
    }

    const double SourceY = Resource == ECoopRPGResourceTrack::Stamina ? 376.0 : 313.0;
    DrawRoundedRows(Fill, [&](double X, double Y, double Width, double Height)
    {
        if (Resource == ECoopRPGResourceTrack::Guard)
        {
            HUD.DrawRect(FLinearColor::White, Position.X + 1.0 + X, Position.Y + 1.0 + Y, Width, Height);
            return;
        }
        const double AtlasX = 250.0 / (Size.X - 2.0);
        const double AtlasY = 13.0 / Fill.Y;
        const FVector2D CropMin(X * AtlasX, SourceY + Y * AtlasY);
        const FVector2D CropSize(Width * AtlasX, Height * AtlasY);
        // Use continuous UV spans so the one-pixel strips match the texture in the center.
        HUD.DrawTexture(Skin.Get(), Position.X + 1.0 + X, Position.Y + 1.0 + Y, Width, Height,
            (CropMin.X + 0.5) / 512.0, (CropMin.Y + 0.5) / 768.0,
            CropSize.X / 512.0, CropSize.Y / 768.0);
    });
}

void FCoopRPGExpeditionDashboard::DrawSkillSlot(AHUD& HUD, FVector2D Position, float Size) const
{
    HUD.DrawRect(FLinearColor(FColor(64, 48, 35)), Position.X + 4.0, Position.Y + 4.0, Size - 8.0, Size - 8.0);
    DrawImage(HUD, FBox2D(FVector2D(0, 410), FVector2D(80, 490)), Position, FVector2D(Size, Size));
}

void FCoopRPGExpeditionDashboard::DrawAttackIcon(AHUD& HUD, FVector2D Position, float Size, bool bDisabled) const
{
    const double X = bDisabled ? 138.0 : 90.0;
    DrawImage(HUD, FBox2D(FVector2D(X, 410), FVector2D(X + 40.0, 450)), Position, FVector2D(Size, Size));
}

void FCoopRPGExpeditionDashboard::DrawBasicAttackGlyph(AHUD& HUD, UCanvas& Canvas, const FBox2D& Slot,
    const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack, bool bSlash) const
{
    using namespace CoopRPG::ExpeditionUI;
    if (!Skin.IsValid())
    {
        return;
    }

    const double Side = Slot.GetSize().X;
    const double IconSide = FMath::RoundToDouble(Side * 0.60);
    if (bSlash)
    {
        const double Inset = FMath::RoundToDouble(Side * 0.14);
        HUD.DrawRect(FLinearColor(FColor(49, 72, 93)), Slot.Min.X + Inset, Slot.Min.Y + Inset,
            Side - Inset * 2.0, Side - Inset * 2.0);
    }
    const bool bCooling = Attack.State == EAttackHudState::Cooling;
    DrawAttackIcon(HUD, Slot.GetCenter() - FVector2D(IconSide * 0.5), IconSide,
        Attack.State != EAttackHudState::Ready && !bCooling);
    if (bCooling)
    {
        const double Inset = FMath::RoundToDouble(Side * 0.10);
        DrawCooldownSweep(Canvas, Slot.Min + FVector2D(Inset), Side - 2.0 * Inset, Attack.GetElapsedFraction());
    }
}

void FCoopRPGExpeditionDashboard::DrawJobBadge(AHUD& HUD, UCanvas& Canvas, const FBox2D& Bounds,
    ECoopRPGHero Hero, bool bShowLabel) const
{
    using namespace CoopRPG::ExpeditionUI;
    const bool bGuardian = Hero == ECoopRPGHero::AnimalHero;
    const bool bWarrior = Hero == ECoopRPGHero::Adventurer;
    const FLinearColor Border(bGuardian ? FColor(38, 64, 84) : bWarrior ? FColor(90, 48, 40) : FColor(79, 71, 55));
    const FLinearColor Fill(bGuardian ? FColor(50, 88, 117) : bWarrior ? FColor(131, 68, 59) : FColor(99, 88, 66));
    const FLinearColor Inset(bGuardian ? FColor(103, 150, 178) : bWarrior ? FColor(181, 111, 94) : FColor(143, 128, 99));
    const FLinearColor Ink(bGuardian ? FColor(227, 246, 255) : FColor(255, 239, 203));
    const double Side = Bounds.GetSize().X;
    const auto Layer = [&](double Edge, const FLinearColor& Tint)
    {
        const double Inner = Side - 2.0 * Edge;
        const double Corner = FMath::Max(1.0, FMath::RoundToDouble(Side / 12.0 - Edge));
        const FVector2D Start = Bounds.Min + FVector2D(Edge);
        HUD.DrawRect(Tint, Start.X, Start.Y + Corner, Inner, Inner - 2.0 * Corner);
        for (int32 Row = 0; Row < static_cast<int32>(Corner); ++Row)
        {
            const double Vertical = Corner - Row - 0.5;
            const double InsetX = FMath::RoundToDouble(Corner - FMath::Sqrt(Corner * Corner - Vertical * Vertical));
            HUD.DrawRect(Tint, Start.X + InsetX, Start.Y + Row, Inner - 2.0 * InsetX, 1.0);
            HUD.DrawRect(Tint, Start.X + InsetX, Start.Y + Inner - Row - 1.0, Inner - 2.0 * InsetX, 1.0);
        }
    };
    Layer(0.0, Border);
    Layer(1.5, Inset);
    Layer(3.0, Fill);

    const double IconSide = Side * (bShowLabel ? 0.51 : 0.59);
    const FVector2D Center(Bounds.GetCenter().X, Bounds.Min.Y + Side * (bShowLabel ? 0.39 : 0.50));
    const FVector2D Origin = Center - FVector2D(IconSide * 0.5);
    const auto Stroke = [&](FVector2D Start, FVector2D End, double Weight)
    {
        FCanvasLineItem Segment(Origin + Start * IconSide, Origin + End * IconSide);
        Segment.SetColor(Ink);
        Segment.LineThickness = FMath::Max(1.0, IconSide * Weight);
        Canvas.DrawItem(Segment);
    };
    if (bGuardian)
    {
        const FVector2D Outline[] = {{0.10, 0.19}, {0.50, 0.06}, {0.90, 0.19}, {0.82, 0.63},
            {0.50, 0.91}, {0.18, 0.63}};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Outline); ++Index)
        {
            Stroke(Outline[Index], Outline[(Index + 1) % UE_ARRAY_COUNT(Outline)], 0.075);
        }
        Stroke({0.50, 0.28}, {0.50, 0.66}, 0.080);
        Stroke({0.33, 0.46}, {0.67, 0.46}, 0.080);
    }
    else if (bWarrior)
    {
        Stroke({0.23, 0.84}, {0.73, 0.20}, 0.13);
        Stroke({0.44, 0.55}, {0.78, 0.15}, 0.20);
        Stroke({0.23, 0.48}, {0.57, 0.72}, 0.09);
    }
    if (bShowLabel)
    {
        const TCHAR* Label = bGuardian ? TEXT("수호기사") : bWarrior ? TEXT("전사") : TEXT("미선택");
        const float Size = Side >= 52.0 ? 10.0f : 8.5f;
        const auto Face = Measure(Canvas, Label, Font(Size, true)).X <= Side - 8.0
            ? Font(Size, true) : Font(Size - 1.0f, true);
        const FVector2D TextSize = Measure(Canvas, Label, Face);
        Text(Canvas, Label, Bounds.Min + FVector2D((Side - TextSize.X) * 0.5, Side * 0.78 - TextSize.Y * 0.5), Face, Ink);
    }
}

void FCoopRPGExpeditionDashboard::DrawPartyCardAccent(AHUD& HUD, FVector2D Position, FVector2D Size,
    ECoopRPGPartyCardAccent Accent) const
{
    const double X = 72.0 * static_cast<uint8>(Accent);
    DrawNineSlice(HUD, Position, Size, FBox2D(FVector2D(X, 504.0), FVector2D(X + 64.0, 568.0)),
        FVector2D(5.0, 5.0), FVector2D(5.0, 5.0));
}

void FCoopRPGExpeditionDashboard::Hide()
{
    StatusBounds = FBox2D(ForceInit);
    NoticeBounds = FBox2D(ForceInit);
    BossHealthBounds = FBox2D(ForceInit);
    if (DisplayedBossHealth.IsSet())
    {
        UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("BossHealthHUD Round=%s Visible=0"), *Round.ToString());
        DisplayedBossHealth.Reset();
    }
    bLogVisible = false;
    bToggleVisible = false;
}

bool FCoopRPGExpeditionDashboard::HandlePrimaryButton(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input)
{
    FVector2D Pointer;
    const bool bHasPointer = bLogVisible && Controller.GetMousePosition(Pointer.X, Pointer.Y);
    const bool bInsideToggle = bHasPointer && bToggleVisible && ToggleBounds.IsInsideOrOn(Pointer);
    const bool bInsideLog = bHasPointer && LogBounds.IsInsideOrOn(Pointer);
    if (bOwnsPrimaryButton)
    {
        if (Input.Event == IE_Released)
        {
            if (bPressedToggle && bInsideToggle)
            {
                bExpanded = !bExpanded;
                UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("DashboardToggle Round=%s Expanded=%d Shown=%d"), *Round.ToString(),
                    bExpanded, FMath::Min(Records.Num(), bExpanded ? MaxRecords : 2));
            }
            else if (!bPressedToggle && bInsideLog)
            {
                UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("DashboardLogClick Round=%s"), *Round.ToString());
            }
            ReleasePointer();
        }
        return true;
    }
    bOwnsPrimaryButton = bInsideLog && (Input.Event == IE_Pressed || Input.Event == IE_DoubleClick);
    bPressedToggle = bOwnsPrimaryButton && bInsideToggle;
    return bOwnsPrimaryButton;
}

void FCoopRPGExpeditionDashboard::ReleasePointer()
{
    bOwnsPrimaryButton = false;
    bPressedToggle = false;
}

void FCoopRPGExpeditionDashboard::AddRecord(ELabel Label, int32 Value)
{
    if (Records.Num() == MaxRecords)
    {
        Records.RemoveAt(0);
    }
    Records.Add({Label, Value});
}

void FCoopRPGExpeditionDashboard::Update(const ACoopRPGGameState& State, int32 Stage, ELabel Goal)
{
    const int32 Defeats = State.GetSkirmishDefeats();
    const int32 Inside = State.GetExtractionPlayersInside();
    const bool bStone = State.GetTeamLootCount() > 0;
    const bool bCountdown = State.IsExtractionInProgress();
    if (Round != State.GetRoundId())
    {
        Round = State.GetRoundId();
        Records.Reset();
        LastGoal.Reset();
        bExpanded = false;
        ReleasePointer();
        LastDefeats = Defeats;
        LastInside = Inside;
        bLastStone = bStone;
        bLastBossRewards = false;
        bLastCountdown = bCountdown;
        UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("DashboardReset Round=%s Stage=%d Records=0 Defeats=%d Stone=%d Inside=%d"),
            *Round.ToString(), Stage, Defeats, bStone, Inside);
    }

    for (int32 Defeat = LastDefeats + 1; Defeat <= Defeats; ++Defeat)
    {
        AddRecord(ELabel::Defeat, Defeat);
    }
    if (bStone && !bLastStone)
    {
        AddRecord(ELabel::StoneGranted);
    }
    if (State.AreBossRewardsAvailable() && !bLastBossRewards)
    {
        AddRecord(ELabel::BossRewardsOpened);
        bLastBossRewards = true;
    }
    if (bCountdown != bLastCountdown)
    {
        AddRecord(bCountdown ? ELabel::CountStarted : ELabel::CountReset);
    }
    if (!LastGoal.IsSet() || LastGoal.GetValue() != Goal)
    {
        if (State.IsRoundPlaying() && LastGoal.IsSet()
            && (LastGoal.GetValue() == ELabel::StageStart || LastGoal.GetValue() == ELabel::PartyWaiting
                || LastGoal.GetValue() == ELabel::Ready))
        {
            AddRecord(ELabel::Start);
        }
        if (Goal == ELabel::Stone)
        {
            AddRecord(ELabel::StoneAppeared);
        }
        LastGoal = Goal;
        UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("DashboardGoal Round=%s Stage=%d Goal=%d"), *Round.ToString(), Stage, static_cast<int32>(Goal));
    }
    if (Defeats != LastDefeats || Inside != LastInside || bStone != bLastStone || bCountdown != bLastCountdown)
    {
        UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("DashboardProgress Round=%s Stage=%d Defeats=%d Stone=%d Inside=%d Countdown=%d Records=%d"),
            *Round.ToString(), Stage, Defeats, bStone, Inside, bCountdown, Records.Num());
    }
    LastDefeats = Defeats;
    LastInside = Inside;
    bLastStone = bStone;
    bLastCountdown = bCountdown;
}

void FCoopRPGExpeditionDashboard::DrawReadyParty(AHUD& HUD, UCanvas& Canvas, const ACoopRPGGameState& State,
    const ACoopRPGPlayerController& Controller, float Left, float Right, float Top, float BadgeSide, float PartyGap, bool bWide) const
{
    using namespace CoopRPG::ExpeditionUI;
    FString Names[2];
    for (TActorIterator<ACoopRPGCharacter> It(Controller.GetWorld()); It; ++It)
    {
        const int32 Slot = It->GetDisplaySlot();
        if (Slot >= 1 && Slot <= 2)
        {
            Names[Slot - 1] = PlayerName(**It, Slot);
        }
    }

    const auto NameFont = Font(bWide ? 13.0f : 11.0f, true);
    const auto SmallFont = Font(bWide ? 12.0f : 10.0f);
    const float NameHeight = Measure(Canvas, TEXT("가"), NameFont).Y;
    const auto& Slots = State.GetReadySlots();
    const ECoopRPGReadyStatus Statuses[] = {Slots.PlayerOne, Slots.PlayerTwo};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const float Y = Top + Index * (BadgeSide + PartyGap);
        const FString Badge = FString::Printf(TEXT("P%d"), Index + 1);
        const FVector2D BadgePos(Left, Y);
        const FVector2D BadgeSize(BadgeSide, BadgeSide);
        const FVector2D BadgeInkBounds = InkBounds(Canvas, Badge, SmallFont);
        const FVector2D BadgeTextOffset((BadgeSize.X - Measure(Canvas, Badge, SmallFont).X) * 0.5,
            (BadgeSize.Y - BadgeInkBounds.X - BadgeInkBounds.Y) * 0.5);
        HUD.DrawRect(FLinearColor(PartySlotBadgeFill), BadgePos.X, BadgePos.Y, BadgeSize.X, BadgeSize.Y);
        Text(Canvas, Badge, BadgePos + BadgeTextOffset, SmallFont, PartySlotBadgeInk);
        const float NameX = Left + BadgeSide + 8.0f;
        if (Statuses[Index] != ECoopRPGReadyStatus::Disconnected && !Names[Index].IsEmpty())
        {
            Text(Canvas, Fit(Canvas, Names[Index], NameFont, Right - NameX), FVector2D(NameX, Y), NameFont, DashboardTextColor);
        }
        Text(Canvas, ReadyText(Statuses[Index]), FVector2D(NameX, Y + NameHeight + 3.0f), SmallFont, ReadyColor(Statuses[Index]));
    }
}

void FCoopRPGExpeditionDashboard::DrawNotice(AHUD& HUD, UCanvas& Canvas, const ACoopRPGGameState& State,
    ECoopRPGDashboardLabel Goal, const FVector2D& ViewSize, float Pad, float PanelWidth, bool bWide, bool bBossActive)
{
    NoticeBounds = FBox2D(ForceInit);
    constexpr double HudGap = 14.0;
    const double MinX = Pad + PanelWidth + HudGap;
    const double MaxX = ViewSize.X - Pad - (bWide ? 364.0 : 300.0) - HudGap;
    const double Inset = bWide ? 12.0 : 13.0;
    const double Available = MaxX - MinX;
    if (Available < Inset * 2.0 + 60.0)
    {
        return;
    }

    const auto Paragraphs = GetNotice(State, Goal, bBossActive);
    const auto NoticeFont = Font(bWide ? 14.0f : 12.0f);
    const auto CaptionFont = Font(bWide ? 11.0f : 10.0f, true);
    const TCHAR* Title = TEXT("목표 안내");
    const FVector2D TitleSize = Measure(Canvas, Title, CaptionFont);
    double NaturalWidth = TitleSize.X;
    for (const FString& Paragraph : Paragraphs)
    {
        NaturalWidth = FMath::Max(NaturalWidth, Measure(Canvas, Paragraph, NoticeFont).X);
    }
    const double Minimum = bWide ? 320.0 : 260.0;
    const double BossNoticeWidth = bWide ? 344.0 : 304.0;
    const double DesiredWidth = bBossActive ? BossNoticeWidth : FMath::Max(Minimum, FMath::CeilToDouble(NaturalWidth) + 2.0 * Inset);
    const double Width = FMath::Min(Available, DesiredWidth);
    const double ContentWidth = Width - 2.0 * Inset;

    TArray<FString, TInlineAllocator<4>> Lines;
    for (const FString& Paragraph : Paragraphs)
    {
        if (Measure(Canvas, Paragraph, NoticeFont).X <= ContentWidth)
        {
            Lines.Add(Paragraph);
            continue;
        }
        TArray<FString> Words;
        Paragraph.ParseIntoArrayWS(Words);
        FString Line;
        for (const FString& Word : Words)
        {
            FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
            if (!Line.IsEmpty() && Measure(Canvas, Candidate, NoticeFont).X > ContentWidth)
            {
                Lines.Add(MoveTemp(Line));
                Line = Word;
            }
            else
            {
                Line = MoveTemp(Candidate);
            }
        }
        Lines.Add(MoveTemp(Line));
    }

    const double TitleGap = bWide ? 8.0 : 6.0;
    const double LineGap = bWide ? 6.0 : 5.0;
    const double LineHeight = Measure(Canvas, TEXT("가"), NoticeFont).Y;
    const double Height = 2.0 * Inset + TitleSize.Y + TitleGap + Lines.Num() * LineHeight
        + FMath::Max(0, Lines.Num() - 1) * LineGap;
    const FVector2D Position(FMath::Clamp((ViewSize.X - Width) * 0.5, MinX, MaxX - Width), Pad);
    const FVector2D Size(Width, Height);
    NoticeBounds = FBox2D(Position, Position + Size);
    DrawDarkPanel(HUD, Position, Size);
    const double X = Position.X;
    Text(Canvas, Title, FVector2D(X + (Width - TitleSize.X) * 0.5, Position.Y + Inset), CaptionFont, NoticeInk);
    double Y = Position.Y + Inset + TitleSize.Y + TitleGap;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    static const bool bLogNoticeLayout = FParse::Param(FCommandLine::Get(), TEXT("CoopCombatTest"));
    if (bLogNoticeLayout)
    {
        FString Layout = FString::Printf(TEXT("Round=%s Goal=%d Width=%.0f Height=%.0f X=%.0f Y=%.0f Right=%.0f Bottom=%.0f"),
            *State.GetRoundId().ToString(), static_cast<int32>(Goal), ViewSize.X, ViewSize.Y, X, Position.Y,
            NoticeBounds.Max.X, NoticeBounds.Max.Y);
        if (Layout != LastNoticeLayout)
        {
            LastNoticeLayout = MoveTemp(Layout);
            UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("ExpeditionNoticeLayout %s"), *LastNoticeLayout);
        }
    }
#endif
    for (FString& Line : Lines)
    {
        Line = Fit(Canvas, MoveTemp(Line), NoticeFont, ContentWidth);
        const double LineWidth = Measure(Canvas, Line, NoticeFont).X;
        Text(Canvas, Line, FVector2D(X + (Width - LineWidth) * 0.5, Y), NoticeFont, NoticeInk);
        Y += LineHeight + LineGap;
    }
}

void FCoopRPGExpeditionDashboard::DrawBossHealth(AHUD& HUD, UCanvas& Canvas, const ACoopRPGGameState& State,
    bool bBossActive, bool bWide)
{
    BossHealthBounds = FBox2D(ForceInit);
    const ACoopRPGEnemy* Boss = nullptr;
    if (bBossActive && NoticeBounds.bIsValid)
    {
        for (TActorIterator<ACoopRPGEnemy> It(State.GetWorld()); It; ++It)
        {
            if (It->IsBlackKnight() && !It->IsDead() && It->GetHealth() > 0.0f)
            {
                Boss = *It;
                break;
            }
        }
    }
    if (!Boss)
    {
        if (DisplayedBossHealth.IsSet())
        {
            UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("BossHealthHUD Round=%s Visible=0"), *Round.ToString());
            DisplayedBossHealth.Reset();
        }
        return;
    }

    const FIntPoint Health(FMath::CeilToInt(Boss->GetHealth()), FMath::CeilToInt(Boss->GetMaxHealth()));
    const float Fraction = FMath::Clamp(Boss->GetHealth() / FMath::Max(1.0f, Boss->GetMaxHealth()), 0.0f, 1.0f);
    const auto NameFont = Font(bWide ? 14.0f : 12.0f, true);
    const auto HealthFont = Font(bWide ? 12.0f : 10.0f);
    const TCHAR* Name = TEXT("흑기사");
    const FString Value = FString::Printf(TEXT("%d / %d"), Health.X, Health.Y);
    const FVector2D NameSize = Measure(Canvas, Name, NameFont);
    const FVector2D ValueSize = Measure(Canvas, Value, HealthFont);
    const double Width = NoticeBounds.GetSize().X;
    constexpr double Inset = 12.0;
    constexpr double TopPad = 8.0;
    constexpr double BarGap = 5.0;
    constexpr double BarHeight = 7.0;
    const double PositionY = NoticeBounds.Max.Y + 8.0;
    const double PositionX = NoticeBounds.Min.X;
    const double Height = TopPad * 2.0 + NameSize.Y + BarGap + BarHeight;
    BossHealthBounds = FBox2D(FVector2D(PositionX, PositionY), FVector2D(PositionX + Width, PositionY + Height));
    DrawDarkPanel(HUD, BossHealthBounds.Min, BossHealthBounds.GetSize());
    Text(Canvas, Name, FVector2D(PositionX + (Width - NameSize.X) * 0.5, PositionY + TopPad), NameFont, NoticeInk);
    Text(Canvas, Value, FVector2D(PositionX + Width - Inset - ValueSize.X, PositionY + TopPad + (NameSize.Y - ValueSize.Y) * 0.5),
        HealthFont, NoticeInk);
    DrawHealthBar(HUD, FVector2D(PositionX + Inset, PositionY + TopPad + NameSize.Y + BarGap), Width - Inset * 2.0, Fraction);
    if (!DisplayedBossHealth.IsSet() || DisplayedBossHealth.GetValue() != Health)
    {
        UE_LOG(LogCoopRPGExpeditionHUD, Log, TEXT("BossHealthHUD Round=%s Visible=1 HP=%d Max=%d"), *Round.ToString(), Health.X, Health.Y);
        DisplayedBossHealth = Health;
    }
}

void FCoopRPGExpeditionDashboard::DrawNineSlice(AHUD& HUD, FVector2D Position, FVector2D Size, const FBox2D& Source,
    FVector2D StartInset, FVector2D EndInset) const
{
    const double SourceX[] = {Source.Min.X, Source.Min.X + StartInset.X, Source.Max.X - EndInset.X, Source.Max.X};
    const double SourceY[] = {Source.Min.Y, Source.Min.Y + StartInset.Y, Source.Max.Y - EndInset.Y, Source.Max.Y};
    const double X[] = {0, StartInset.X, Size.X - EndInset.X, Size.X};
    const double Y[] = {0, StartInset.Y, Size.Y - EndInset.Y, Size.Y};
    for (int32 Row = 0; Row < 3; ++Row)
    {
        for (int32 Col = 0; Col < 3; ++Col)
        {
            DrawImage(HUD, FBox2D(FVector2D(SourceX[Col], SourceY[Row]), FVector2D(SourceX[Col + 1], SourceY[Row + 1])),
                Position + FVector2D(X[Col], Y[Row]), FVector2D(X[Col + 1] - X[Col], Y[Row + 1] - Y[Row]));
        }
    }
}

void FCoopRPGExpeditionDashboard::DrawImage(AHUD& HUD, const FBox2D& Source, const FVector2D& Position, const FVector2D& Size) const
{
    HUD.DrawTexture(Skin.Get(), Position.X, Position.Y, Size.X, Size.Y, (Source.Min.X + 0.5) / 512.0,
        (Source.Min.Y + 0.5) / 768.0, (Source.GetSize().X - 1.0) / 512.0, (Source.GetSize().Y - 1.0) / 768.0);
}
