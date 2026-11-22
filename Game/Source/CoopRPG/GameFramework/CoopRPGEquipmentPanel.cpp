#include "CoopRPGEquipmentPanel.h"

#include "CoopRPGExpeditionAttackHUD.h"
#include "CoopRPGExpeditionDashboard.h"
#include "CoopRPGExpeditionHUDText.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGRecoveryEffect.h"
#include "CoopRPGGameState.h"
#include "CoopRPGPlayerState.h"
#include "Engine/World.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPGPlayerController.h"
#include "Engine/Texture2D.h"
#include "GameFramework/HUD.h"
#include "ImageUtils.h"
#include "InputKeyEventArgs.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEquipmentHUD, Log, All);

namespace
{
    constexpr int32 TabCount = 4;
    constexpr int32 SkillsTab = 1;
    constexpr int32 EquipmentTab = 2;
    constexpr int32 SkillRows = 4;
    constexpr int32 VisibleSkillRows = 2;
    constexpr double SkillRowHeight = 70.0;
    constexpr double SkillRowPitch = 82.0;
    constexpr const TCHAR* SkillGroupLabels[] = {TEXT("액티브"), TEXT("패시브")};
    constexpr int32 InventoryTab = 3;
    constexpr int32 InventoryColumns = 4;
    constexpr int32 InventoryRows = 4;
    constexpr int32 VisibleInventoryRows = 2;
    constexpr int32 VisibleInventorySlots = InventoryColumns * VisibleInventoryRows;
    constexpr int32 InventorySlotCount = InventoryColumns * InventoryRows;
    constexpr double InventorySlotHeight = 47.0;
    constexpr double InventoryRowPitch = 52.0;
    constexpr double InventoryColumnGap = 4.0;
    constexpr int32 EquipmentRows = 6;
    constexpr int32 VisibleEquipmentRows = 4;
    constexpr double EquipmentRowHeight = 43.0;
    constexpr double EquipmentRowPitch = 49.0;
    constexpr double ScrollTrackWidth = 5.0;
    constexpr double InventoryTrackGap = 5.0;
    constexpr double InventoryFooterY = 221.0;
    constexpr double InventoryFooterHeight = 34.0;
    constexpr float PanelHeight = 272.0f;
    constexpr int32 BlankFrameRow = 7;
    constexpr float Gap = 5.0f;
    constexpr float PanelGap = 6.0f;
    constexpr float AtlasWidth = 364.0f;
    constexpr float AtlasRowHeight = 214.0f;
    constexpr float AtlasHeight = AtlasRowHeight * 8.0f;
    constexpr float TabUnits[] = { 1.0f, 1.0f, 1.0f, 1.38f };
    constexpr const TCHAR* TabLabels[] = { TEXT("스탯"), TEXT("스킬"), TEXT("장비"), TEXT("인벤토리") };
    constexpr const TCHAR* TabNames[] = { TEXT("Stats"), TEXT("Skills"), TEXT("Equipment"), TEXT("Inventory") };

    FLinearColor Color(uint8 R, uint8 G, uint8 B)
    {
        return FLinearColor(FColor(R, G, B));
    }

    void DrawPanelFill(AHUD& HUD, const FBox2D& Box, const FLinearColor& Tint)
    {
        const FVector2D Size = Box.GetSize();
        HUD.DrawRect(Tint, Box.Min.X, Box.Min.Y, Size.X, Size.Y);
    }

    void DrawPanelBox(AHUD& HUD, const FBox2D& Bounds, const FLinearColor& Fill, const FLinearColor& Border)
    {
        DrawPanelFill(HUD, FBox2D(Bounds.Min + FVector2D(2, 0), Bounds.Max - FVector2D(2, 0)), Border);
        DrawPanelFill(HUD, FBox2D(Bounds.Min + FVector2D(0, 2), Bounds.Max - FVector2D(0, 2)), Border);
        DrawPanelFill(HUD, FBox2D(Bounds.Min + FVector2D(1, 1), Bounds.Max - FVector2D(1, 1)), Fill);
    }

    void DrawPanelLabel(UCanvas& Surface, FStringView Value, double X, double Y, const FSlateFontInfo& Face,
        const FLinearColor& Tint)
    {
        using namespace CoopRPG::ExpeditionUI;
        Text(Surface, Value, FVector2D(X, Y - Measure(Surface, Value, Face).Y * 0.5), Face, Tint);
    }
}

FCoopRPGEquipmentPanel::FCoopRPGEquipmentPanel() = default;
FCoopRPGEquipmentPanel::~FCoopRPGEquipmentPanel() = default;

void FCoopRPGEquipmentPanel::Draw(AHUD& HUD, UCanvas& Surface, ACoopRPGPlayerController& Controller, const FVector2D& ViewSize,
    const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack, const FCoopRPGExpeditionDashboard& Dashboard)
{
    if (!bInitialized)
    {
        bInitialized = true;
        Skin.Reset(FImageUtils::ImportFileAsTexture2D(FPaths::Combine(
            FPaths::ProjectContentDir(), TEXT("UI/ExpeditionEquipment/EquipmentSkin.png"))));
        if (!Skin.IsValid())
        {
            UE_LOG(LogCoopRPGEquipmentHUD, Error, TEXT("EquipmentSkinMissing"));
            return;
        }
        Controller.bShowMouseCursor = true;
        FInputModeGameAndUI InputMode;
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        InputMode.SetHideCursorDuringCapture(false);
        Controller.SetInputMode(InputMode);
    }
    if (!Skin.IsValid())
    {
        return;
    }

    const bool bWide = ViewSize.X >= 1200.0f;
    const float Padding = bWide ? 22.0f : 12.0f;
    Width = bWide ? 364.0f : 300.0f;
    ButtonHeight = bWide ? 33.0f : 29.0f;
    ContentInset = bWide ? 17.0f : 14.0f;
    Width = FMath::Min(Width, ViewSize.X - 2.0f * Padding);
    bVisible = Width >= 180.0f && ViewSize.Y >= PanelHeight + ButtonHeight + PanelGap + 2.0f * Padding;
    if (!bVisible)
    {
        return;
    }
    Anchor = FVector2D(ViewSize.X - Padding - Width, Padding);

    using namespace CoopRPG::ExpeditionUI;
    const auto TabFont = Font(bWide ? 11.0f : 10.0f, true);
    const FLinearColor TabInk(FColor(255, 240, 202));
    for (int32 Index = 0; Index < TabCount; ++Index)
    {
        const FBox2D Bounds = GetTabBounds(Index);
        DrawRegion(HUD, Bounds, Index == ActiveTab ? 1 : 0, 0.0f, 91.0f);
        const FString Label = Fit(Surface, TabLabels[Index], TabFont, Bounds.GetSize().X - 10.0);
        Text(Surface, Label, Bounds.GetCenter() - Measure(Surface, Label, TabFont) * 0.5, TabFont, TabInk);
    }
    if (ActiveTab == INDEX_NONE)
    {
        return;
    }

    FVector2D Pointer = FVector2D::ZeroVector;
    if (ScrollDragOffset.IsSet() && Controller.GetMousePosition(Pointer.X, Pointer.Y))
    {
        DragScrollTrack(Pointer.Y);
    }
    DrawPanelFrame(HUD, GetPanelBounds());
    if (ActiveTab == InventoryTab)
    {
        DrawInventory(HUD, Surface, Controller);
        return;
    }

    if (ActiveTab == EquipmentTab)
    {
        DrawEquipment(HUD, Surface, Controller);
        return;
    }
    if (ActiveTab == SkillsTab)
    {
        DrawSkills(HUD, Surface, Controller, Attack, Dashboard);
        return;
    }
    DrawStats(HUD, Surface, Controller);
}

FBox2D FCoopRPGEquipmentPanel::GetVisibleBounds() const
{
    if (!bVisible)
    {
        return FBox2D(ForceInit);
    }
    const FVector2D End = ActiveTab == INDEX_NONE ? Anchor + FVector2D(Width, ButtonHeight) : GetPanelBounds().Max;
    return FBox2D(Anchor, End);
}

void FCoopRPGEquipmentPanel::Hide()
{
    bVisible = false;
    ActiveTab = INDEX_NONE;
    ResetInventoryView();
    ResetSkillsView();
    FirstEquipmentRow = 0;
    LastDisplayedWeaponState.Reset();
}

bool FCoopRPGEquipmentPanel::HandlePrimaryButton(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input)
{
    FVector2D Pointer = FVector2D::ZeroVector;
    const bool bHasPointer = bVisible && Controller.GetMousePosition(Pointer.X, Pointer.Y);
    const int32 Tab = bHasPointer ? FindTab(Pointer) : INDEX_NONE;

    if (bOwnsPrimaryButton)
    {
        if (Input.Event == IE_Released)
        {
            if (Tab != INDEX_NONE && Tab == PressedTab)
            {
                ActiveTab = ActiveTab == Tab ? INDEX_NONE : Tab;
                ResetInventoryView();
                ResetSkillsView();
                FirstEquipmentRow = 0;
                LastDisplayedWeaponState.Reset();
                UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("EquipmentPanelTab Tab=%s Open=%d %s"),
                    TabNames[Tab], ActiveTab == Tab ? 1 : 0,
                    *CoopRPG::Debug::DescribeLogContext(Controller.GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
            }
            else if (bHasPointer && ActiveTab == InventoryTab && PressedInventorySlot != INDEX_NONE
                && PressedInventorySlot == FindInventorySlot(Pointer))
            {
                SelectedInventorySlot = PressedInventorySlot;
                SelectedInventoryItem = EInventoryItem::None;
            }
            else if (bHasPointer && ActiveTab == SkillsTab && PressedSkillGroup != INDEX_NONE
                && PressedSkillGroup == FindSkillGroup(Pointer))
            {
                SkillGroup = PressedSkillGroup;
            }
            else if (bHasPointer && ScrollDragOffset.IsSet())
            {
                DragScrollTrack(Pointer.Y);
            }
            ReleasePointer();
        }
        return true;
    }
    if (Input.Event != IE_Pressed && Input.Event != IE_DoubleClick)
    {
        return false;
    }
    if (!bHasPointer || (Tab == INDEX_NONE && (ActiveTab == INDEX_NONE || !GetPanelBounds().IsInsideOrOn(Pointer))))
    {
        return false;
    }
    bOwnsPrimaryButton = true;
    PressedTab = Input.Event == IE_DoubleClick ? INDEX_NONE : Tab;
    if (Input.Event == IE_Pressed && Tab == INDEX_NONE)
    {
        if (ActiveTab == InventoryTab)
        {
            PressedInventorySlot = FindInventorySlot(Pointer);
        }
        if (ActiveTab == SkillsTab)
        {
            PressedSkillGroup = FindSkillGroup(Pointer);
        }
        if (IsScrollableTab() && GetScrollTrack().IsInsideOrOn(Pointer))
        {
            const FBox2D Thumb = GetScrollThumb();
            ScrollDragOffset = Thumb.IsInsideOrOn(Pointer) ? Pointer.Y - Thumb.Min.Y : Thumb.GetSize().Y * 0.5;
            DragScrollTrack(Pointer.Y);
        }
    }
    UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("EquipmentPointerConsumed Region=%s %s"),
        Tab == INDEX_NONE ? TEXT("Panel") : TabNames[Tab],
        *CoopRPG::Debug::DescribeLogContext(Controller.GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
    return true;
}

void FCoopRPGEquipmentPanel::ReleasePointer()
{
    bOwnsPrimaryButton = false;
    PressedTab = INDEX_NONE;
    PressedInventorySlot = INDEX_NONE;
    PressedSkillGroup = INDEX_NONE;
    ScrollDragOffset.Reset();
}

FBox2D FCoopRPGEquipmentPanel::GetTabBounds(int32 Index) const
{
    const float Unit = (Width - 3.0f * Gap) / 4.38f;
    const FVector2D Start = Anchor + FVector2D(Index * (Unit + Gap), 0.0f);
    return FBox2D(Start, Start + FVector2D(Unit * TabUnits[Index], ButtonHeight));
}

FBox2D FCoopRPGEquipmentPanel::GetPanelBounds() const
{
    const FVector2D Start = Anchor + FVector2D(0.0f, ButtonHeight + PanelGap);
    return FBox2D(Start, Start + FVector2D(Width, PanelHeight));
}

int32 FCoopRPGEquipmentPanel::FindTab(const FVector2D& Pointer) const
{
    for (int32 Index = 0; Index < TabCount; ++Index)
    {
        if (GetTabBounds(Index).IsInsideOrOn(Pointer))
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

void FCoopRPGEquipmentPanel::DrawRegion(AHUD& HUD, const FBox2D& Bounds, int32 Row, float SourceX, float SourceWidth) const
{
    const float SourceY = Row * AtlasRowHeight;
    DrawAtlas(HUD, Bounds, FBox2D(FVector2D(SourceX, SourceY), FVector2D(SourceX + SourceWidth, SourceY + 33.0f)));
}

void FCoopRPGEquipmentPanel::DrawAtlas(AHUD& HUD, const FBox2D& Bounds, const FBox2D& Source) const
{
    const FVector2D Size = Bounds.GetSize();
    const FVector2D SourceSize = Source.GetSize();
    HUD.DrawTexture(Skin.Get(), Bounds.Min.X, Bounds.Min.Y, Size.X, Size.Y,
        (Source.Min.X + 0.5) / AtlasWidth, (Source.Min.Y + 0.5) / AtlasHeight,
        (SourceSize.X - 1.0) / AtlasWidth, (SourceSize.Y - 1.0) / AtlasHeight);
}

void FCoopRPGEquipmentPanel::DrawPanelFrame(AHUD& HUD, const FBox2D& Bounds) const
{
    if (!Skin.IsValid())
    {
        DrawPanelBox(HUD, Bounds, FLinearColor(FColor(245, 230, 190)), FLinearColor(FColor(104, 72, 41)));
        return;
    }

    const double TextureY = BlankFrameRow * AtlasRowHeight;
    const double SourceX[] = {0.0, 11.0, AtlasWidth - 11.0, AtlasWidth};
    const double SourceY[] = {TextureY, TextureY + 10.0, TextureY + AtlasRowHeight - 12.0, TextureY + AtlasRowHeight};
    const double X[] = {Bounds.Min.X, Bounds.Min.X + 11.0, Bounds.Max.X - 11.0, Bounds.Max.X};
    const double Y[] = {Bounds.Min.Y, Bounds.Min.Y + 10.0, Bounds.Max.Y - 12.0, Bounds.Max.Y};
    for (int32 Row = 0; Row < 3; ++Row)
    {
        for (int32 Col = 0; Col < 3; ++Col)
        {
            DrawAtlas(HUD, FBox2D(FVector2D(X[Col], Y[Row]), FVector2D(X[Col + 1], Y[Row + 1])),
                FBox2D(FVector2D(SourceX[Col], SourceY[Row]), FVector2D(SourceX[Col + 1], SourceY[Row + 1])));
        }
    }
}

FBox2D FCoopRPGEquipmentPanel::GetInventoryGridBounds() const
{
    const FBox2D Panel = GetPanelBounds();
    const FVector2D Start = Panel.Min + FVector2D(ContentInset, 66.0);
    return FBox2D(Start, FVector2D(Panel.Max.X - ContentInset, Start.Y + InventoryRowPitch + InventorySlotHeight));
}

FBox2D FCoopRPGEquipmentPanel::GetInventorySlotBounds(int32 VisibleIndex) const
{
    const FBox2D Grid = GetInventoryGridBounds();
    const double TileWidth = FMath::FloorToDouble((Grid.GetSize().X - ScrollTrackWidth - InventoryTrackGap
        - InventoryColumnGap * (InventoryColumns - 1)) / InventoryColumns);
    const FVector2D Start = Grid.Min + FVector2D((VisibleIndex % InventoryColumns) * (TileWidth + InventoryColumnGap),
        (VisibleIndex / InventoryColumns) * InventoryRowPitch);
    return FBox2D(Start, Start + FVector2D(TileWidth, InventorySlotHeight));
}

FBox2D FCoopRPGEquipmentPanel::GetEquipmentListBounds() const
{
    const FBox2D Panel = GetPanelBounds();
    const double Top = Panel.Min.Y + 55.0;
    return FBox2D(FVector2D(Panel.Min.X + ContentInset, Top),
        FVector2D(Panel.Max.X - ContentInset, Top + (VisibleEquipmentRows - 1) * EquipmentRowPitch + EquipmentRowHeight));
}

FBox2D FCoopRPGEquipmentPanel::GetSkillGroupBounds(int32 Index) const
{
    const FBox2D Panel = GetPanelBounds();
    const double GroupWidth = FMath::FloorToDouble((Panel.GetSize().X - 2.0 * ContentInset - 6.0) * 0.5);
    const double X = Panel.Min.X + ContentInset + Index * (GroupWidth + 6.0);
    return FBox2D(FVector2D(X, Panel.Min.Y + 50.0),
        FVector2D(Index == 0 ? X + GroupWidth : Panel.Max.X - ContentInset, Panel.Min.Y + 76.0));
}

int32 FCoopRPGEquipmentPanel::FindSkillGroup(const FVector2D& Pointer) const
{
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(SkillGroupLabels); ++Index)
    {
        if (GetSkillGroupBounds(Index).IsInsideOrOn(Pointer))
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

FBox2D FCoopRPGEquipmentPanel::GetSkillListBounds() const
{
    const FBox2D Panel = GetPanelBounds();
    const double Top = Panel.Min.Y + 89.0;
    return FBox2D(FVector2D(Panel.Min.X + ContentInset, Top),
        FVector2D(Panel.Max.X - ContentInset, Top + (VisibleSkillRows - 1) * SkillRowPitch + SkillRowHeight));
}

void FCoopRPGEquipmentPanel::ResetSkillsView()
{
    SkillGroup = 0;
    PressedSkillGroup = INDEX_NONE;
    FirstSkillRows[0] = 0;
    FirstSkillRows[1] = 0;
    LastSkillsDisplay.Reset();
}

bool FCoopRPGEquipmentPanel::IsScrollableTab() const
{
    return ActiveTab == SkillsTab || ActiveTab == EquipmentTab || ActiveTab == InventoryTab;
}

FIntPoint FCoopRPGEquipmentPanel::GetScrollRowCounts() const
{
    if (ActiveTab == SkillsTab)
    {
        return FIntPoint(SkillRows, VisibleSkillRows);
    }
    return ActiveTab == EquipmentTab ? FIntPoint(EquipmentRows, VisibleEquipmentRows) : FIntPoint(InventoryRows, VisibleInventoryRows);
}

int32 FCoopRPGEquipmentPanel::GetFirstScrollRow() const
{
    if (ActiveTab == SkillsTab)
    {
        return FirstSkillRows[SkillGroup];
    }
    return ActiveTab == EquipmentTab ? FirstEquipmentRow : FirstInventoryRow;
}

int32& FCoopRPGEquipmentPanel::GetFirstScrollRow()
{
    if (ActiveTab == SkillsTab)
    {
        return FirstSkillRows[SkillGroup];
    }
    return ActiveTab == EquipmentTab ? FirstEquipmentRow : FirstInventoryRow;
}

FBox2D FCoopRPGEquipmentPanel::GetScrollContentBounds() const
{
    if (ActiveTab == SkillsTab)
    {
        return GetSkillListBounds();
    }
    return ActiveTab == EquipmentTab ? GetEquipmentListBounds() : GetInventoryGridBounds();
}

FBox2D FCoopRPGEquipmentPanel::GetScrollTrack() const
{
    const FBox2D Content = GetScrollContentBounds();
    return FBox2D(FVector2D(Content.Max.X - ScrollTrackWidth, Content.Min.Y), Content.Max);
}

FBox2D FCoopRPGEquipmentPanel::GetScrollThumb() const
{
    const FIntPoint Rows = GetScrollRowCounts();
    const int32 First = GetFirstScrollRow();
    const FBox2D Track = GetScrollTrack();
    const double TrackTravel = Track.GetSize().Y - 4.0;
    const double ThumbHeight = TrackTravel * Rows.Y / Rows.X;
    const double ThumbTop = Track.Min.Y + 2.0 + (TrackTravel - ThumbHeight) * First / (Rows.X - Rows.Y);
    return FBox2D(FVector2D(Track.Min.X, ThumbTop), FVector2D(Track.Max.X, ThumbTop + ThumbHeight));
}

void FCoopRPGEquipmentPanel::DrawScrollTrack(AHUD& HUD) const
{
    DrawPanelFill(HUD, GetScrollTrack(), Color(228, 208, 164));
    DrawPanelFill(HUD, GetScrollThumb(), Color(170, 133, 87));
}

int32 FCoopRPGEquipmentPanel::FindInventorySlot(const FVector2D& Pointer) const
{
    for (int32 Index = 0; Index < VisibleInventorySlots; ++Index)
    {
        if (GetInventorySlotBounds(Index).IsInsideOrOn(Pointer))
        {
            return FirstInventoryRow * InventoryColumns + Index;
        }
    }
    return INDEX_NONE;
}

void FCoopRPGEquipmentPanel::DragScrollTrack(double PointerY)
{
    const FIntPoint Rows = GetScrollRowCounts();
    const FBox2D Track = GetScrollTrack();
    const double Travel = Track.GetSize().Y - 4.0 - GetScrollThumb().GetSize().Y;
    const double Fraction = (PointerY - ScrollDragOffset.GetValue() - Track.Min.Y - 2.0) / Travel;
    GetFirstScrollRow() = FMath::RoundToInt(FMath::Clamp(Fraction, 0.0, 1.0) * (Rows.X - Rows.Y));
}

bool FCoopRPGEquipmentPanel::HandleWheel(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input)
{
    FVector2D Pointer = FVector2D::ZeroVector;
    if (!bVisible || !IsScrollableTab() || !Controller.GetMousePosition(Pointer.X, Pointer.Y)
        || !GetPanelBounds().IsInsideOrOn(Pointer))
    {
        return false;
    }
    if (Input.Event == IE_Pressed && !bOwnsPrimaryButton && GetScrollContentBounds().IsInsideOrOn(Pointer))
    {
        const int32 Direction = Input.Key == EKeys::MouseScrollDown ? 1 : -1;
        const FIntPoint Rows = GetScrollRowCounts();
        int32& FirstRow = GetFirstScrollRow();
        FirstRow = FMath::Clamp(FirstRow + Direction, 0, Rows.X - Rows.Y);
    }
    return true;
}

void FCoopRPGEquipmentPanel::ResetInventoryView()
{
    FirstInventoryRow = 0;
    SelectedInventorySlot = INDEX_NONE;
    SelectedInventoryItem = EInventoryItem::None;
    PressedInventorySlot = INDEX_NONE;
    ScrollDragOffset.Reset();
    LastInventoryDisplay.Reset();
}

FCoopRPGEquipmentPanel::EInventoryItem FCoopRPGEquipmentPanel::GetInventoryItem(int32 Slot, int32 PotionSlot, int32 SwordSlot)
{
    if (Slot == INDEX_NONE)
    {
        return EInventoryItem::None;
    }
    if (Slot == PotionSlot)
    {
        return EInventoryItem::Potion;
    }
    return Slot == SwordSlot ? EInventoryItem::Sword : EInventoryItem::None;
}

void FCoopRPGEquipmentPanel::UpdateInventorySelection(int32 PotionSlot, int32 SwordSlot)
{
    if (SelectedInventoryItem == EInventoryItem::Potion)
    {
        SelectedInventorySlot = PotionSlot;
    }
    else if (SelectedInventoryItem == EInventoryItem::Sword)
    {
        SelectedInventorySlot = SwordSlot;
    }
    SelectedInventoryItem = GetInventoryItem(SelectedInventorySlot, PotionSlot, SwordSlot);
}

void FCoopRPGEquipmentPanel::DrawItemIcon(AHUD& HUD, int32 IconIndex, FVector2D Position, double Size, FLinearColor Tint) const
{
    const float SourceX = 95.0f + IconIndex * 30.0f;
    HUD.DrawTexture(Skin.Get(), Position.X, Position.Y, Size, Size, (SourceX + 0.5f) / AtlasWidth,
        1.5f / AtlasHeight, 27.0f / AtlasWidth, 27.0f / AtlasHeight, Tint);
}

void FCoopRPGEquipmentPanel::DrawStats(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller)
{
    using namespace CoopRPG::ExpeditionUI;
    const auto* Hero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const bool bHasHero = IsValid(Hero) && Hero->GetAbilitySystemComponent();
    const FStringView Dash(TEXT("—"));
    const FString MaxHealth = bHasHero ? FString::FromInt(FMath::RoundToInt(UCoopRPGCombatAttributes::MaxHealth)) : FString(Dash);
    const FString MaxStamina = bHasHero ? FString::FromInt(FMath::RoundToInt(UCoopRPGCombatAttributes::MaxStamina)) : FString(Dash);
    const FBox2D Panel = GetPanelBounds();
    const double Left = Panel.Min.X + ContentInset;
    const double Right = Panel.Max.X - ContentInset;
    const double Top = Panel.Min.Y;
    const auto TitleFont = Font(13.0f, true);
    const auto SectionFont = Font(10.0f, true);
    const auto LabelFont = Font(10.0f);
    const auto ValueFont = Font(11.0f, true);
    const auto Ink = Color(73, 48, 30);
    const auto Muted = Color(101, 80, 57);
    const auto Border = Color(211, 183, 126);

    const auto DrawStat = [&](FStringView Label, FStringView Value, double RowTop, double RowHeight,
        const FLinearColor& Fill, const FLinearColor& ValueTint)
    {
        DrawPanelBox(HUD, FBox2D(FVector2D(Left, RowTop), FVector2D(Right, RowTop + RowHeight)), Fill, Border);
        const double CenterY = RowTop + RowHeight * 0.5;
        DrawPanelLabel(Surface, Label, Left + 10.0, CenterY, LabelFont, Ink);
        DrawPanelLabel(Surface, Value, Right - 10.0 - Measure(Surface, Value, ValueFont).X, CenterY, ValueFont, ValueTint);
    };

    DrawPanelLabel(Surface, TEXT("스탯"), Left, Top + 23.0, TitleFont, Ink);
    HUD.DrawRect(Color(191, 157, 100), Left, Top + 41.0, Right - Left, 1.0);
    DrawPanelLabel(Surface, TEXT("자원 한도"), Left, Top + 56.0, SectionFont, Ink);
    DrawStat(TEXT("최대 HP"), MaxHealth, Top + 69.0, 29.0, Color(250, 237, 202), bHasHero ? Color(138, 56, 46) : Muted);
    DrawStat(TEXT("최대 SP"), MaxStamina, Top + 103.0, 29.0, Color(250, 237, 202), bHasHero ? Color(55, 87, 66) : Muted);

    DrawPanelLabel(Surface, TEXT("기본 능력치"), Left, Top + 149.0, SectionFont, Ink);
    constexpr const TCHAR* BaseStatLabels[] = { TEXT("힘"), TEXT("수호"), TEXT("기동"), TEXT("집중") };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(BaseStatLabels); ++Index)
    {
        DrawStat(BaseStatLabels[Index], Dash, Top + 162.0 + Index * 24.0, 21.0, Color(240, 226, 189), Muted);
    }

    FString Display = FString::Printf(TEXT("MaxHP=%s MaxSP=%s Str=%s Dex=%s Int=%s Luck=%s X=%.0f Y=%.0f Width=%.0f Height=%.0f"),
        *MaxHealth, *MaxStamina, TEXT("—"), TEXT("—"), TEXT("—"), TEXT("—"), Left - ContentInset, Top, Panel.GetSize().X, Panel.GetSize().Y);
    if (Display != LastStatsDisplay)
    {
        LastStatsDisplay = MoveTemp(Display);
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("StatsPanelState %s %s"), *LastStatsDisplay,
            *CoopRPG::Debug::DescribeLogContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void FCoopRPGEquipmentPanel::DrawEquipment(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller)
{
    using namespace CoopRPG::ExpeditionUI;
    const auto* Hero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const bool bEquipped = IsValid(Hero) && Hero->HasMeleeWeapon();
    const FBox2D Panel = GetPanelBounds();
    const FBox2D List = GetEquipmentListBounds();
    const double Left = List.Min.X;
    const double Right = GetScrollTrack().Min.X - 6.0;
    const auto TitleFont = Font(13.0f, true);
    const auto ItemFont = Font(10.0f, true);
    const auto HintFont = Font(9.0f);
    const auto Ink = Color(73, 48, 30);
    const auto Muted = Color(101, 80, 57);
    const auto Border = Color(211, 183, 126);

    DrawPanelLabel(Surface, TEXT("장비"), Left, Panel.Min.Y + 23.0, TitleFont, Ink);
    HUD.DrawRect(Color(191, 157, 100), Left, Panel.Min.Y + 41.0, List.GetSize().X, 1.0);
    for (int32 Index = 0; Index < VisibleEquipmentRows; ++Index)
    {
        const int32 Slot = FirstEquipmentRow + Index;
        const bool bWeapon = Slot == 0;
        const bool bShield = Slot == 1;
        const bool bNamed = bWeapon || bShield;
        const bool bArmed = bWeapon && bEquipped;
        const double Y = List.Min.Y + Index * EquipmentRowPitch;
        DrawPanelBox(HUD, FBox2D(FVector2D(Left, Y), FVector2D(Right, Y + EquipmentRowHeight)),
            bNamed ? Color(250, 237, 202) : Color(240, 226, 189), Border);
        const FVector2D IconPosition(Left + 7.0, Y + 6.0);
        DrawPanelBox(HUD, FBox2D(IconPosition, IconPosition + FVector2D(32.0, 31.0)),
            bArmed ? Color(113, 82, 57) : Color(225, 208, 170), Color(193, 165, 115));
        if (bArmed || bShield)
        {
            DrawItemIcon(HUD, bWeapon ? 1 : 4, IconPosition + FVector2D(4.0, 4.0), 26.0,
                bArmed ? FLinearColor::White : Color(197, 183, 149));
        }
        else
        {
            const FStringView Dash(TEXT("—"));
            DrawPanelLabel(Surface, Dash, IconPosition.X + (32.0 - Measure(Surface, Dash, HintFont).X) * 0.5,
                Y + 21.0, HintFont, Muted);
        }
        if (bNamed)
        {
            DrawPanelLabel(Surface, bWeapon ? TEXT("무기") : TEXT("방패"), Left + 49.0, Y + 13.0, HintFont, Muted);
            const TCHAR* Item = bArmed ? TEXT("한손검") : TEXT("미장착");
            DrawPanelLabel(Surface, Item, Left + 49.0, Y + 29.0, ItemFont, Ink);
        }
        else
        {
            DrawPanelLabel(Surface, TEXT("빈 슬롯"), Left + 49.0, Y + 21.0, HintFont, Muted);
        }
        if (bArmed)
        {
            const FBox2D Badge(FVector2D(Right - 76.0, Y + 11.0), FVector2D(Right - 9.0, Y + 32.0));
            DrawPanelBox(HUD, Badge, Color(219, 232, 211), Color(146, 167, 126));
            const FStringView Equipped(TEXT("장착 중"));
            DrawPanelLabel(Surface, Equipped, Badge.GetCenter().X - Measure(Surface, Equipped, HintFont).X * 0.5f,
                Badge.GetCenter().Y, HintFont, Color(55, 87, 66));
        }
    }
    DrawScrollTrack(HUD);

    if (!LastDisplayedWeaponState.IsSet() || LastDisplayedWeaponState.GetValue() != bEquipped
        || LastDisplayedEquipmentRow != FirstEquipmentRow)
    {
        LastDisplayedWeaponState = bEquipped;
        LastDisplayedEquipmentRow = FirstEquipmentRow;
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("EquipmentPanelState Equipped=%d Shield=0 First=%d Visible=%d Slots=%d X=%.0f Y=%.0f Width=%.0f Height=%.0f %s"),
            bEquipped, FirstEquipmentRow + 1, VisibleEquipmentRows, EquipmentRows, Panel.Min.X, Panel.Min.Y,
            Panel.GetSize().X, Panel.GetSize().Y, *CoopRPG::Debug::DescribeLogContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void FCoopRPGEquipmentPanel::DrawSkills(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller,
    const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack, const FCoopRPGExpeditionDashboard& Dashboard)
{
    using namespace CoopRPG::ExpeditionUI;
    const FBox2D Panel = GetPanelBounds();
    const FBox2D List = GetSkillListBounds();
    const double Left = List.Min.X;
    const double Right = GetScrollTrack().Min.X - 6.0;
    const auto TitleFont = Font(13.0f, true);
    const auto RowFont = Font(11.0f, true);
    const auto HintFont = Font(9.0f);
    const auto ChipFont = Font(9.0f, true);
    const auto Ink = Color(73, 48, 30);
    const auto Muted = Color(101, 80, 57);
    const auto Border = Color(211, 183, 126);

    const auto Center = [&](FStringView Value, const FVector2D& Position, const FSlateFontInfo& Face, const FLinearColor& Tint)
    {
        DrawPanelLabel(Surface, Value, Position.X - Measure(Surface, Value, Face).X * 0.5, Position.Y, Face, Tint);
    };
    constexpr double IconSide = 35.0;
    constexpr double RowInset = 9.0;
    constexpr double ColumnGap = 9.0;
    constexpr double ChipHeight = 21.0;
    constexpr double ChipGap = 5.0;
    const double ChipWidth = Width >= 260.0 ? 85.0 : 66.0;
    const double ChipRight = Right - RowInset;
    const double ChipLeft = ChipRight - ChipWidth;
    const double TextLeft = Left + RowInset + IconSide + ColumnGap;
    const double TextWidth = ChipLeft - ColumnGap - TextLeft;
    const auto Chip = [&](const FString& Value, double CenterY, const FLinearColor& Fill, const FLinearColor& Tint)
    {
        const FBox2D Bounds(FVector2D(ChipLeft, CenterY - ChipHeight * 0.5), FVector2D(ChipRight, CenterY + ChipHeight * 0.5));
        DrawPanelBox(HUD, Bounds, Fill, Border);
        Center(Fit(Surface, Value, ChipFont, ChipWidth - 6.0), Bounds.GetCenter(), ChipFont, Tint);
    };

    DrawPanelLabel(Surface, TEXT("스킬"), Left, Panel.Min.Y + 23.0, TitleFont, Ink);
    HUD.DrawRect(Color(191, 157, 100), Left, Panel.Min.Y + 41.0, List.GetSize().X, 1.0);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(SkillGroupLabels); ++Index)
    {
        const FBox2D Bounds = GetSkillGroupBounds(Index);
        const bool bSelected = SkillGroup == Index;
        DrawPanelBox(HUD, Bounds, bSelected ? Color(65, 87, 63) : Color(239, 224, 189), Border);
        Center(SkillGroupLabels[Index], Bounds.GetCenter(), RowFont, bSelected ? Color(255, 240, 202) : Muted);
    }

    const bool bActiveList = SkillGroup == 0;
    const int32 First = FirstSkillRows[SkillGroup];
    const int32 RemainingTenths = Attack.GetRemainingTenths();
    const bool bFirstSlotVisible = bActiveList && First == 0;
    for (int32 Index = 0; Index < VisibleSkillRows; ++Index)
    {
        const int32 Row = First + Index;
        const double Y = List.Min.Y + Index * SkillRowPitch;
        const FBox2D Bounds(FVector2D(Left, Y), FVector2D(Right, Y + SkillRowHeight));
        const bool bAttackRow = bActiveList && Row == 0;
        DrawPanelBox(HUD, Bounds, bAttackRow ? Color(250, 237, 202) : Color(240, 226, 189), Border);
        const double MidY = Bounds.GetCenter().Y;
        const FVector2D Icon(Left + RowInset, MidY - IconSide * 0.5);
        const FBox2D IconBounds(Icon, Icon + FVector2D(IconSide));
        DrawPanelBox(HUD, IconBounds, bAttackRow ? Color(113, 82, 57) : Color(225, 208, 170), Color(193, 165, 115));

        if (!bAttackRow)
        {
            Center(TEXT("—"), IconBounds.GetCenter(), HintFont, Muted);
            const bool bUnassigned = bActiveList && Row == 1;
            const TCHAR* Label = bUnassigned ? TEXT("미배정")
                : !bActiveList && Row == 0 ? TEXT("등록한 패시브 없음") : TEXT("빈 슬롯");
            const double LabelWidth = bUnassigned ? TextWidth : Right - RowInset - TextLeft;
            DrawPanelLabel(Surface, Fit(Surface, Label, HintFont, LabelWidth), TextLeft, MidY, HintFont, Muted);
            if (bUnassigned)
            {
                Chip(TEXT("슬롯 2"), MidY, Color(237, 226, 194), Muted);
            }
            continue;
        }

        const auto& SlotStatus = Attack;
        const bool bReady = SlotStatus.State == EAttackHudState::Ready;
        const bool bCooling = SlotStatus.State == EAttackHudState::Cooling;
        Dashboard.DrawBasicAttackGlyph(HUD, Surface, IconBounds, SlotStatus);

        const FString Name = Fit(Surface, TEXT("기본 공격"), RowFont, TextWidth);
        const FString Hint = Fit(Surface, TEXT("무기를 휘둘러 공격"), HintFont, TextWidth);
        constexpr double LineGap = 4.0;
        const double NameHeight = Measure(Surface, Name, RowFont).Y;
        const double HintHeight = Measure(Surface, Hint, HintFont).Y;
        DrawPanelLabel(Surface, Name, TextLeft, MidY - (HintHeight + LineGap) * 0.5, RowFont, Ink);
        DrawPanelLabel(Surface, Hint, TextLeft, MidY + (NameHeight + LineGap) * 0.5, HintFont, Muted);
        const double ChipOffset = (ChipHeight + ChipGap) * 0.5;
        Chip(TEXT("슬롯 1"), MidY - ChipOffset, Color(237, 226, 194), Muted);
        const FString Status = bCooling ? FString::Printf(TEXT("쿨다운 %.1f초"), RemainingTenths / 10.0)
            : FString(bReady ? TEXT("사용 가능") : AttackHudLabel(Attack.State));
        Chip(Status, MidY + ChipOffset, bReady ? Color(219, 232, 211) : Color(239, 216, 203),
            bReady ? Color(55, 87, 66) : Color(115, 55, 47));
    }
    DrawScrollTrack(HUD);

    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State)
    {
        return;
    }
    FString Display = FString::Printf(TEXT("Round=%s Group=%s First=%d Visible=%d Rows=%d Slot=%d State=%s Remaining=%.1f Duration=%.3f X=%.0f Y=%.0f Width=%.0f Height=%.0f"),
        *State->GetRoundId().ToString(), bActiveList ? TEXT("Active") : TEXT("Passive"), First + 1, VisibleSkillRows,
        SkillRows, bFirstSlotVisible ? 1 : 0, bFirstSlotVisible ? AttackHudLogState(Attack.State) : TEXT("Empty"),
        bFirstSlotVisible ? RemainingTenths / 10.0 : 0.0, bFirstSlotVisible ? Attack.Duration : 0.0,
        Panel.Min.X, Panel.Min.Y, Panel.GetSize().X, Panel.GetSize().Y);
    if (LastSkillsDisplay != Display)
    {
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("SkillsPanelState %s"), *Display);
        LastSkillsDisplay = MoveTemp(Display);
    }
}

void FCoopRPGEquipmentPanel::DrawInventory(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller)
{
    using namespace CoopRPG::ExpeditionUI;
    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State)
    {
        return;
    }
    if (InventoryRound != State->GetRoundId())
    {
        InventoryRound = State->GetRoundId();
        ResetInventoryView();
    }

    const auto* Hero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const auto* PartyState = Controller.GetPlayerState<ACoopRPGPlayerState>();
    const int32 PotionCount = PartyState ? PartyState->GetPartyRecoveryCount() : Controller.GetRecoveryCount();
    const bool bHasSword = IsValid(Hero) && Hero->HasMeleeWeapon();
    const bool bHasStone = State->GetTeamLootCount() > 0;
    const int32 PotionSlot = PotionCount > 0 ? 0 : INDEX_NONE;
    const int32 SwordSlot = bHasSword ? (PotionCount > 0 ? 1 : 0) : INDEX_NONE;
    UpdateInventorySelection(PotionSlot, SwordSlot);
    const FBox2D Panel = GetPanelBounds();
    const double Left = Panel.Min.X + ContentInset;
    const double Right = Panel.Max.X - ContentInset;
    const double Top = Panel.Min.Y;
    const auto TitleFont = Font(13.0f, true);
    const auto BodyFont = Font(10.0f, true);
    const auto SmallFont = Font(9.0f);
    const auto TileFont = Font(8.0f, true);
    const FLinearColor Ink(FColor(73, 48, 30));
    const FLinearColor Muted(FColor(101, 80, 57));
    const FLinearColor Outline(FColor(211, 183, 126));
    const FLinearColor Green(FColor(54, 88, 64));

    const auto Box = [&](const FBox2D& Bounds, const FLinearColor& FillColor, const FLinearColor& Border)
    {
        DrawPanelBox(HUD, Bounds, FillColor, Border);
    };
    const auto Label = [&](FStringView Value, double X, double Y, const FSlateFontInfo& Face, const FLinearColor& Tint)
    {
        DrawPanelLabel(Surface, Value, X, Y, Face, Tint);
    };
    const auto RightLabel = [&](FStringView Value, double X, double Y, const FSlateFontInfo& Face, const FLinearColor& Tint)
    {
        Label(Value, X - Measure(Surface, Value, Face).X, Y, Face, Tint);
    };
    const auto Icon = [&](int32 IconIndex, FVector2D Position, double Size, FLinearColor Tint)
    {
        DrawItemIcon(HUD, IconIndex, Position, Size, Tint);
    };

    Label(TEXT("인벤토리"), Left, Top + 23.0, TitleFont, Ink);
    Box(FBox2D(FVector2D(Right - 81.0, Top + 13.0), FVector2D(Right, Top + 34.0)), Color(244, 227, 183), Outline);
    Icon(3, FVector2D(Right - 76.0, Top + 14.0), 27.0, FLinearColor::White);
    Label(TEXT("골드"), Right - 57.0, Top + 23.0, SmallFont, Muted);
    RightLabel(TEXT("—"), Right - 7.0, Top + 23.0, BodyFont, Muted);
    HUD.DrawRect(Color(191, 157, 100), Left, Top + 41.0, Right - Left, 1.0);
    Label(TEXT("내 아이템"), Left, Top + 54.0, SmallFont, Muted);
    const FString Capacity = FString::Printf(TEXT("%d / %d칸"), (PotionCount > 0 ? 1 : 0) + (bHasSword ? 1 : 0), InventorySlotCount);
    RightLabel(Capacity, Right - 9.0, Top + 54.0, SmallFont, Muted);
    if (PotionCount == 0 && Width >= 260.0f)
    {
        const double CapacityLeft = Right - 9.0 - Measure(Surface, Capacity, SmallFont).X;
        RightLabel(TEXT("물약 ×0"), CapacityLeft - 12.0, Top + 54.0, SmallFont, Muted);
    }

    for (int32 Index = 0; Index < VisibleInventorySlots; ++Index)
    {
        const FBox2D Tile = GetInventorySlotBounds(Index);
        const int32 Slot = FirstInventoryRow * InventoryColumns + Index;
        const EInventoryItem Item = GetInventoryItem(Slot, PotionSlot, SwordSlot);
        const bool bPotion = Item == EInventoryItem::Potion;
        const bool bSword = Item == EInventoryItem::Sword;
        const bool bSelected = Slot == SelectedInventorySlot;
        Box(Tile, bSelected ? Color(255, 240, 191) : Color(238, 221, 183), bSelected ? Color(166, 115, 60) : Outline);
        if (bPotion || bSword)
        {
            const double IconSize = FMath::Min(27.0, Tile.GetSize().X - 8.0);
            const FVector2D IconPosition = Tile.Min + FVector2D(4.0, 5.0);
            Box(FBox2D(IconPosition, IconPosition + FVector2D(IconSize, 27.0)), Color(113, 82, 57), Color(140, 103, 65));
            Icon(bPotion ? 0 : 1, IconPosition + FVector2D(2.0, 2.0), IconSize - 2.0, FLinearColor::White);
            if (Tile.GetSize().X >= 52.0)
            {
                const FString Status = bPotion ? FString::Printf(TEXT("×%d"), PotionCount) : FString(TEXT("장착"));
                RightLabel(Status, Tile.Max.X - 4.0, Tile.Min.Y + 13.0, TileFont, Muted);
            }
            const FString Name = Fit(Surface, bPotion ? TEXT("회복 물약") : TEXT("한손검"), TileFont, Tile.GetSize().X - 4.0);
            Label(Name, Tile.GetCenter().X - Measure(Surface, Name, TileFont).X * 0.5, Tile.Min.Y + 39.0, TileFont, Ink);
        }
    }

    DrawScrollTrack(HUD);

    Box(FBox2D(FVector2D(Left, Top + 176.0), FVector2D(Right, Top + 213.0)), Color(248, 237, 206), Outline);
    const bool bPotionSelected = SelectedInventoryItem == EInventoryItem::Potion;
    const bool bSwordSelected = SelectedInventoryItem == EInventoryItem::Sword;
    const TCHAR* DetailName = bPotionSelected ? TEXT("회복 물약") : bSwordSelected ? TEXT("한손검")
        : SelectedInventorySlot == INDEX_NONE ? TEXT("아이템을 선택하세요") : TEXT("빈 슬롯");
    Label(DetailName, Left + 8.0, Top + 194.0, BodyFont, Ink);
    if (bPotionSelected || bSwordSelected)
    {
        const FString Detail = bPotionSelected ? FString::Printf(TEXT("체력 %d 회복"),
            FMath::RoundToInt(UCoopRPGRecoveryEffect::HealthRestored)) : FString(TEXT("장착 중"));
        RightLabel(Detail, Right - 8.0, Top + 194.0, SmallFont, Muted);
    }

    Box(FBox2D(FVector2D(Left, Top + InventoryFooterY), FVector2D(Right, Top + InventoryFooterY + InventoryFooterHeight)),
        bHasStone ? Color(233, 234, 213) : Color(238, 228, 200), bHasStone ? Color(161, 172, 134) : Outline);
    Icon(2, FVector2D(Left + 7.0, Top + InventoryFooterY + 5.0), 25.0,
        bHasStone ? FLinearColor::White : Color(190, 174, 141));
    const double FooterCenter = Top + InventoryFooterY + InventoryFooterHeight * 0.5;
    Label(TEXT("팀 마법석"), Left + 38.0, FooterCenter, BodyFont, Ink);
    Box(FBox2D(FVector2D(Right - 67.0, FooterCenter - 10.0), FVector2D(Right - 7.0, FooterCenter + 10.0)),
        bHasStone ? Color(217, 232, 212) : Color(234, 224, 197), bHasStone ? Color(150, 169, 132) : Outline);
    const TCHAR* StoneStatus = bHasStone ? TEXT("획득") : TEXT("미획득");
    Label(StoneStatus, Right - 37.0 - Measure(Surface, StoneStatus, SmallFont).X * 0.5, FooterCenter, SmallFont, bHasStone ? Green : Muted);

    FString Display = FString::Printf(TEXT("Round=%s Potion=%d Stone=%d Sword=%d PotionSlot=%d SwordSlot=%d First=%d Last=%d Slots=%d Selected=%d X=%.0f Y=%.0f Width=%.0f Height=%.0f FooterBottom=%.0f"),
        *InventoryRound.ToString(), PotionCount, bHasStone, bHasSword, PotionSlot + 1, SwordSlot + 1,
        FirstInventoryRow * InventoryColumns + 1,
        FirstInventoryRow * InventoryColumns + VisibleInventorySlots, InventorySlotCount, SelectedInventorySlot + 1,
        Panel.Min.X, Panel.Min.Y, Panel.GetSize().X, Panel.GetSize().Y, Top + InventoryFooterY + InventoryFooterHeight);
    if (LastInventoryDisplay != Display)
    {
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("ExpeditionInventory %s"), *Display);
        LastInventoryDisplay = MoveTemp(Display);
    }
}
