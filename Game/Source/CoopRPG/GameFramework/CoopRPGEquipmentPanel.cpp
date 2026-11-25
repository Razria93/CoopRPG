#include "CoopRPGEquipmentPanel.h"

#include "CoopRPGExpeditionAttackHUD.h"
#include "CoopRPGExpeditionDashboard.h"
#include "CoopRPGExpeditionHUDText.h"
#include "CoopRPGExpeditionMaps.h"
#include "CoopRPG/AbilitySystem/Attributes/CoopRPGCombatAttributes.h"
#include "CoopRPG/AbilitySystem/Effects/CoopRPGRecoveryEffect.h"
#include "CoopRPGGameState.h"
#include "CoopRPGSessionEconomy.h"
#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPGPlayerController.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "ImageUtils.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEquipmentHUD, Log, All);

namespace
{
    constexpr int32 TabCount = 4;
    constexpr int32 ShopOfferCount = UE_ARRAY_COUNT(CoopRPG::Shop::Offers);
    enum EShopControl : int32
    {
        ShopBuyMode = ShopOfferCount,
        ShopSellMode,
        ShopMinus,
        ShopPlus,
        ShopCommit
    };
    constexpr int32 StorageAreaShared = 8;
    constexpr int32 StorageAreaPersonal = StorageAreaShared + 1;
    constexpr int32 StorageDeposit = StorageAreaPersonal + 1;
    constexpr int32 StorageWithdraw = StorageDeposit + 1;
    constexpr int32 SkillsTab = 1;
    constexpr int32 EquipmentTab = 2;
    constexpr int32 SkillRows = 4;
    constexpr int32 VisibleSkillRows = 2;
    constexpr double SkillRowHeight = 70.0;
    constexpr double SkillRowPitch = 82.0;
    constexpr const TCHAR* SkillGroupLabels[] = {TEXT("액티브"), TEXT("패시브")};
    constexpr int32 InventoryTab = 3;
    constexpr int32 InventoryColumns = 4;
    constexpr int32 InventoryRows = CoopRPG::Bag::SlotCount / InventoryColumns;
    constexpr int32 VisibleInventoryRows = 2;
    constexpr int32 VisibleInventorySlots = InventoryColumns * VisibleInventoryRows;
    constexpr int32 InventorySlotCount = CoopRPG::Bag::SlotCount;
    static_assert(InventorySlotCount % InventoryColumns == 0);
    static_assert(StorageAreaShared == VisibleInventorySlots);
    constexpr double InventorySlotHeight = 47.0;
    constexpr double InventoryRowPitch = 52.0;
    constexpr double InventoryColumnGap = 4.0;
    constexpr int32 EquipmentRows = 6;
    constexpr int32 VisibleEquipmentRows = 4;
    constexpr double EquipmentRowHeight = 43.0;
    constexpr double EquipmentRowPitch = 49.0;
    constexpr double ScrollTrackWidth = 5.0;
    constexpr double InventoryTrackGap = 5.0;
    constexpr double PanelTitleY = 23.0;
    constexpr double PanelRuleY = 41.0;
    constexpr double PairedGridY = 80.0;
    constexpr double PairedDetailY = 186.0;
    constexpr double PairedDetailHeight = 28.0;
    constexpr double PairedFooterY = 219.0;
    constexpr double PairedFooterHeight = 25.0;
    constexpr int32 VisibleShopRows = 3;
    constexpr double ShopListY = 99.0;
    constexpr double ShopRowHeight = 25.0;
    constexpr double ShopRowPitch = 28.0;
    constexpr double InventoryFooterY = 221.0;
    constexpr double InventoryRecoveryShortcutsHeight = 38.0;
    constexpr double InventoryFooterHeight = 34.0;
    constexpr float EquipmentPanelHeight = 272.0f;
    constexpr int32 BlankFrameRow = 7;
    constexpr float EquipmentTabGap = 5.0f;
    constexpr float PanelGap = 6.0f;
    constexpr float AtlasWidth = 364.0f;
    constexpr float AtlasRowHeight = 214.0f;
    constexpr float AtlasHeight = AtlasRowHeight * 8.0f;
    constexpr const TCHAR* TabLabels[] = { TEXT("스탯"), TEXT("스킬"), TEXT("장비"), TEXT("인벤토리") };
    constexpr const TCHAR* TabNames[] = { TEXT("Stats"), TEXT("Skills"), TEXT("Equipment"), TEXT("Inventory") };
    constexpr const TCHAR* ShopLabels[] = { TEXT("HP 물약"), TEXT("슬래시 캡슐") };
    static_assert(UE_ARRAY_COUNT(ShopLabels) == ShopOfferCount);

    constexpr const TCHAR* StorageLabels[] = { TEXT("HP 물약"), TEXT("SP 물약"), TEXT("한손검 · 미귀속"), TEXT("한손검 · 귀속"),
        TEXT("무력 스크롤"), TEXT("수호 스크롤"), TEXT("슬래시 캡슐"), TEXT("도발 캡슐"), TEXT("하급 방패 · 미귀속"), TEXT("하급 방패 · 귀속") };
    static_assert(UE_ARRAY_COUNT(StorageLabels) == FCoopRPGStorageStock::ItemCount);
    constexpr ECoopRPGStorageItem StorageDisplayOrder[] = {
        ECoopRPGStorageItem::HealthPotion, ECoopRPGStorageItem::StaminaPotion,
        ECoopRPGStorageItem::UnboundSword, ECoopRPGStorageItem::BoundSword,
        ECoopRPGStorageItem::UnboundShield, ECoopRPGStorageItem::BoundShield,
        ECoopRPGStorageItem::MartialScroll, ECoopRPGStorageItem::ProtectionScroll,
        ECoopRPGStorageItem::SlashCapsule, ECoopRPGStorageItem::TauntCapsule };
    static_assert(UE_ARRAY_COUNT(StorageDisplayOrder) == FCoopRPGStorageStock::ItemCount);

    FBox2D InventoryTileBounds(const FBox2D& Grid, int32 Index)
    {
        const double TileWidth = FMath::FloorToDouble((Grid.GetSize().X - ScrollTrackWidth - InventoryTrackGap
            - InventoryColumnGap * (InventoryColumns - 1)) / InventoryColumns);
        const FVector2D Start = Grid.Min + FVector2D((Index % InventoryColumns) * (TileWidth + InventoryColumnGap),
            (Index / InventoryColumns) * InventoryRowPitch);
        return FBox2D(Start, Start + FVector2D(TileWidth, InventorySlotHeight));
    }

    ECoopRPGActiveSkill ReadJobSkill(const ACoopRPGPlayerController& Controller)
    {
        const auto* Identity = Controller.GetPlayerState<ACoopRPGPlayerState>();
        const auto Role = Identity ? Identity->GetSelectedHero() : ECoopRPGHero::Unassigned;
        if (Role == ECoopRPGHero::AnimalHero)
        {
            return ECoopRPGActiveSkill::Taunt;
        }
        return Role == ECoopRPGHero::Adventurer ? ECoopRPGActiveSkill::Slash : ECoopRPGActiveSkill::None;
    }

    FBox2D StorageControlBounds(const FBox2D& Panel, float Inset, int32 Control)
    {
        const double Left = Panel.Min.X + Inset;
        const double Right = Panel.Max.X - Inset;
        const double Top = Panel.Min.Y;
        const double Half = (Left + Right) * 0.5;
        if (Control < VisibleInventorySlots)
        {
            return InventoryTileBounds(FBox2D(FVector2D(Left, Top + PairedGridY), FVector2D(Right, Top + PairedDetailY)), Control);
        }
        if (Control == StorageAreaShared || Control == StorageAreaPersonal)
        {
            return FBox2D(FVector2D(Control == StorageAreaPersonal ? Left : Half + 3.0, Top + 49.0),
                FVector2D(Control == StorageAreaPersonal ? Half - 3.0 : Right, Top + 73.0));
        }
        return FBox2D(FVector2D(Control == StorageDeposit ? Left : Half + 3.0, Top + PairedFooterY),
            FVector2D(Control == StorageDeposit ? Half - 3.0 : Right, Top + PairedFooterY + PairedFooterHeight));
    }

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
    bLobby = CoopRPG::Expedition::IsLobby(Controller.GetWorld());
    const auto* Npc = Controller.GetOpenedLobbyNpc();
    const auto Service = bLobby && IsValid(Npc) ? Npc->GetService() : ECoopRPGLobbyService::None;
    if (Service != ActiveService)
    {
        ReleasePointer();
        ActiveService = Service;
        LastStorageDisplay.Reset();
        LastShopDisplay.Reset();
        LastJobDisplay.Reset();
        bPersonalStorage = true;
        SelectedStorageItem = INDEX_NONE;
        bStorageFromBag = ActiveService == ECoopRPGLobbyService::Storage && ActiveTab == InventoryTab
            && SelectedInventoryItem != EInventoryItem::None;
        VisibleStorageItems.Reset();
        bShopBuying = true;
        SelectedShopItem = INDEX_NONE;
        ShopQuantity = 1;
        ShopRows.Reset();
        Controller.ClearShopFeedback();
        FirstShopRow = 0;
        if (ActiveTab != InventoryTab && (ActiveService == ECoopRPGLobbyService::Storage || ActiveService == ECoopRPGLobbyService::Shop))
        {
            ActiveTab = InventoryTab;
            ResetInventoryView();
        }
    }
    if (bLobby)
    {
        Width = FMath::Min(Width, (ViewSize.X - 2.0f * Padding - 8.0f) * 0.5f);
    }
    const double ExtraHeight = ActiveTab == InventoryTab ? GetInventoryContentOffset() : 0.0;
    bVisible = Width >= 180.0f && ViewSize.Y >= EquipmentPanelHeight + ExtraHeight + ButtonHeight + PanelGap + 2.0f * Padding;
    if (!bVisible)
    {
        return;
    }
    Anchor = FVector2D(ViewSize.X - Padding - Width, Padding);
    Viewport = ViewSize;
    LogLayout(ViewSize);
    if (ActiveService == ECoopRPGLobbyService::Job)
    {
        DrawPanelFrame(HUD, GetServiceBounds());
        DrawJob(HUD, Surface, Controller, Dashboard);
        return;
    }

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
    const FBox2D Inventory = GetPanelBounds();
    const FBox2D ServicePanel = GetServiceBounds();
    if (ServicePanel.bIsValid && ActiveTab == InventoryTab)
    {
        DrawPanelFrame(HUD, Inventory);
        DrawInventory(HUD, Surface, Controller);
        DrawPanelFrame(HUD, ServicePanel);
        if (ActiveService == ECoopRPGLobbyService::Storage || ActiveService == ECoopRPGLobbyService::Shop)
        {
            DrawPanelFill(HUD, FBox2D(ServicePanel.Min + FVector2D(7.0), ServicePanel.Max - FVector2D(7.0, 8.0)),
                Color(234, 217, 180));
        }
        switch (ActiveService)
        {
        case ECoopRPGLobbyService::Storage: DrawStorage(HUD, Surface, Controller); break;
        case ECoopRPGLobbyService::Shop: DrawShop(HUD, Surface, Controller); break;
        default: break;
        }
        return;
    }
    DrawPanelFrame(HUD, Inventory);
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
    if (!bVisible || ActiveService == ECoopRPGLobbyService::Job)
    {
        return FBox2D(ForceInit);
    }
    const FVector2D End = ActiveTab == INDEX_NONE ? Anchor + FVector2D(Width, ButtonHeight) : GetPanelBounds().Max;
    return FBox2D(Anchor, End);
}

FBox2D FCoopRPGEquipmentPanel::GetServiceBounds() const
{
    if (!bVisible || ActiveService == ECoopRPGLobbyService::None)
    {
        return FBox2D(ForceInit);
    }
    if (ActiveService == ECoopRPGLobbyService::Job)
    {
        const FVector2D Size(Width, EquipmentPanelHeight);
        const FVector2D TopLeft = (Viewport - Size) * 0.5;
        return FBox2D(TopLeft, TopLeft + Size);
    }
    const FBox2D Inventory = GetPanelBounds();
    const FVector2D Position = Inventory.Min - FVector2D(Width + 8.0, 0.0);
    return FBox2D(Position, Position + FVector2D(Width, EquipmentPanelHeight));
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

void FCoopRPGEquipmentPanel::DrawRecoveryIcon(AHUD& HUD, FVector2D Position, double Size) const
{
    if (Skin.IsValid())
    {
        DrawItemIcon(HUD, 0, Position, Size, FLinearColor::White);
    }
}

void FCoopRPGEquipmentPanel::Hide()
{
    bVisible = false;
    ActiveTab = INDEX_NONE;
    ActiveService = ECoopRPGLobbyService::None;
    LastServiceLayout.Reset();
    ResetInventoryView();
    ResetSkillsView();
    FirstEquipmentRow = 0;
    LastDisplayedWeaponState.Reset();
    LastStorageDisplay.Reset();
    LastShopDisplay.Reset();
    bShopBuying = true;
    SelectedShopItem = INDEX_NONE;
    ShopQuantity = 1;
    ShopRows.Reset();
    FirstShopRow = 0;
    bPersonalStorage = false;
    SelectedStorageItem = INDEX_NONE;
    bStorageFromBag = false;
    VisibleStorageItems.Reset();
    FirstStorageRow = 0;
    StorageRows = 0;
}

bool FCoopRPGEquipmentPanel::HandlePrimaryButton(ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input)
{
    FVector2D Pointer = FVector2D::ZeroVector;
    const bool bHasPointer = bVisible && Controller.GetMousePosition(Pointer.X, Pointer.Y);
    const bool bJob = ActiveService == ECoopRPGLobbyService::Job;
    const int32 Tab = bHasPointer && !bJob ? FindTab(Pointer) : INDEX_NONE;

    if (bOwnsPrimaryButton)
    {
        if (Input.Event == IE_Released)
        {
            if (Tab != INDEX_NONE && Tab == PressedTab)
            {
                if (ActiveService != ECoopRPGLobbyService::None)
                {
                    if (Tab == InventoryTab)
                    {
                        ReleasePointer();
                        return true;
                    }
                    Controller.CloseLobbyService();
                }
                ActiveTab = ActiveTab == Tab ? INDEX_NONE : Tab;
                ResetInventoryView();
                ResetSkillsView();
                FirstEquipmentRow = 0;
                LastDisplayedWeaponState.Reset();
                UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("EquipmentPanelTab Tab=%s Open=%d %s"),
                    TabNames[Tab], ActiveTab == Tab ? 1 : 0,
                    *CoopRPG::Debug::DescribeLogContext(Controller.GetPawn(), ANSI_TO_TCHAR(__FUNCTION__)));
            }
            else if (bHasPointer && ActiveService == ECoopRPGLobbyService::Shop && PressedShopControl != INDEX_NONE
                && PressedShopControl == FindShopControl(Pointer))
            {
                HandleShopControl(Controller, PressedShopControl);
            }
            else if (bHasPointer && ActiveService == ECoopRPGLobbyService::Storage && PressedStorageControl != INDEX_NONE
                && PressedStorageControl == FindStorageControl(Pointer))
            {
                if (PressedStorageControl < VisibleInventorySlots)
                {
                    const int32 Item = VisibleStorageItems.IsValidIndex(PressedStorageControl)
                        ? VisibleStorageItems[PressedStorageControl] : INDEX_NONE;
                    if (Item == PressedStorageItem)
                    {
                        SelectedStorageItem = Item;
                        bStorageFromBag = false;
                        Controller.ClearStorageFeedback();
                    }
                }
                else if (PressedStorageControl == StorageAreaShared || PressedStorageControl == StorageAreaPersonal)
                {
                    bPersonalStorage = PressedStorageControl == StorageAreaPersonal;
                    FirstStorageRow = 0;
                    if (!bStorageFromBag)
                    {
                        SelectedStorageItem = INDEX_NONE;
                    }
                    Controller.ClearStorageFeedback();
                }
                else
                {
                    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
                    const bool bDeposit = PressedStorageControl == StorageDeposit;
                    const int32 Item = bDeposit ? GetBagStorageItem(Controller) : SelectedStorageItem;
                    if (State && !State->IsLobbyDepartureScheduled() && bDeposit == bStorageFromBag && Item != INDEX_NONE)
                    {
                        Controller.RequestStorageTransfer(static_cast<ECoopRPGStorageItem>(Item), bPersonalStorage, bDeposit);
                    }
                }
            }
            else if (bHasPointer && ActiveService == ECoopRPGLobbyService::Job && PressedJobControl != INDEX_NONE
                && PressedJobControl == FindJobControl(Pointer))
            {
                Controller.RequestLobbyJob(PressedJobControl == 0 ? ECoopRPGHero::Adventurer : ECoopRPGHero::AnimalHero);
            }
            else if (bHasPointer && !bJob && ActiveTab == InventoryTab && PressedEmptyRecovery != INDEX_NONE
                && PressedEmptyRecovery == FindEmptyRecoveryShortcut(Controller, Pointer))
            {
                Controller.RequestRecoveryRegistration(PressedEmptyRecovery == 1, false);
            }
            else if (bHasPointer && !bJob && ActiveTab == InventoryTab && PressedInventoryAction.IsSet()
                && SelectedInventoryItem == PressedActionItem && GetInventoryActionBounds().IsInsideOrOn(Pointer))
            {
                if (SelectedInventoryItem == EInventoryItem::Sword)
                {
                    Controller.RequestSwordEquipped(PressedInventoryAction.GetValue());
                }
                else if (SelectedInventoryItem == EInventoryItem::PersonalShield)
                {
                    Controller.RequestShieldEquipped(PressedInventoryAction.GetValue());
                }
                else if (const auto Skill = ToCapsuleSkill(SelectedInventoryItem); Skill != ECoopRPGActiveSkill::None)
                {
                    Controller.RequestUseSkillCapsule(Skill);
                }
                else if (IsStatScroll(SelectedInventoryItem))
                {
                    Controller.RequestUseStatScroll(ToScrollStat(SelectedInventoryItem));
                }
                else
                {
                    Controller.RequestRecoveryRegistration(SelectedInventoryItem == EInventoryItem::StaminaPotion,
                        PressedInventoryAction.GetValue());
                }
            }
            else if (bHasPointer && !bJob && ActiveTab == InventoryTab && PressedInventorySlot != INDEX_NONE
                && PressedInventorySlot == FindInventorySlot(Pointer))
            {
                SelectedInventorySlot = PressedInventorySlot;
                SelectedInventoryItem = EInventoryItem::None;
                if (ActiveService == ECoopRPGLobbyService::Storage)
                {
                    bStorageFromBag = true;
                    Controller.ClearStorageFeedback();
                }
            }
            else if (bHasPointer && ActiveTab == SkillsTab && PressedSkillBinding != INDEX_NONE
                && PressedSkillBinding == FindSkillBinding(Pointer))
            {
                const auto Skill = ReadJobSkill(Controller);
                if (Controller.HasLearnedSkill(Skill))
                {
                    Controller.RequestActiveSkillSlot(Skill, static_cast<uint8>(PressedSkillBinding));
                }
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
    const FBox2D ServicePanel = GetServiceBounds();
    const bool bOnService = bHasPointer && ServicePanel.bIsValid && ServicePanel.IsInsideOrOn(Pointer);
    if (!bHasPointer || (!bJob && Tab == INDEX_NONE && !bOnService
        && (ActiveTab == INDEX_NONE || !GetPanelBounds().IsInsideOrOn(Pointer))))
    {
        return false;
    }
    bOwnsPrimaryButton = true;
    PressedTab = Input.Event == IE_DoubleClick ? INDEX_NONE : Tab;
    if (Input.Event == IE_Pressed && Tab == INDEX_NONE)
    {
        if (bOnService)
        {
            switch (ActiveService)
            {
            case ECoopRPGLobbyService::Storage:
                PressedStorageControl = FindStorageControl(Pointer);
                if (VisibleStorageItems.IsValidIndex(PressedStorageControl))
                {
                    PressedStorageItem = VisibleStorageItems[PressedStorageControl];
                }
                break;
            case ECoopRPGLobbyService::Shop: PressedShopControl = FindShopControl(Pointer); break;
            case ECoopRPGLobbyService::Job: PressedJobControl = FindJobControl(Pointer); break;
            default: break;
            }
            return true;
        }
        if (bJob)
        {
            return true;
        }
        if (ActiveTab == InventoryTab)
        {
            PressedEmptyRecovery = FindEmptyRecoveryShortcut(Controller, Pointer);
            PressedInventorySlot = FindInventorySlot(Pointer);
            if (GetInventoryActionBounds().IsInsideOrOn(Pointer))
            {
                PressedActionItem = SelectedInventoryItem;
                if (SelectedInventoryItem == EInventoryItem::Sword)
                {
                    PressedInventoryAction = !Controller.IsPersonalSwordEquipped();
                }
                else if (SelectedInventoryItem == EInventoryItem::PersonalShield && Controller.CanUsePersonalShield())
                {
                    PressedInventoryAction = !Controller.IsPersonalShieldEquipped();
                }
                else if (const auto Skill = ToCapsuleSkill(SelectedInventoryItem); Skill != ECoopRPGActiveSkill::None)
                {
                    if (Controller.CanLearnSkill(Skill))
                    {
                        PressedInventoryAction = true;
                    }
                }
                else if (IsStatScroll(SelectedInventoryItem))
                {
                    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
                    if (State && State->IsRoundPlaying() && Controller.GetStatScrollCount(ToScrollStat(SelectedInventoryItem)) > 0)
                    {
                        PressedInventoryAction = true;
                    }
                }
                else if (SelectedInventoryItem == EInventoryItem::Potion || SelectedInventoryItem == EInventoryItem::StaminaPotion)
                {
                    if (CoopRPG::Expedition::IsExpedition(Controller.GetWorld()) && !CoopRPG::Expedition::IsLobby(Controller.GetWorld()))
                    {
                        PressedInventoryAction = !Controller.IsRecoveryRegistered(SelectedInventoryItem == EInventoryItem::StaminaPotion);
                    }
                }
            }
        }
        if (ActiveTab == SkillsTab)
        {
            PressedSkillGroup = FindSkillGroup(Pointer);
            PressedSkillBinding = FindSkillBinding(Pointer);
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

bool FCoopRPGEquipmentPanel::HandleWheel(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input)
{
    FVector2D Pointer = FVector2D::ZeroVector;
    if (!bVisible || !Controller.GetMousePosition(Pointer.X, Pointer.Y))
    {
        return false;
    }
    if (ActiveService == ECoopRPGLobbyService::Job)
    {
        return true;
    }
    const int32 Direction = Input.Key == EKeys::MouseScrollDown ? 1 : -1;
    const bool bCanScroll = Input.Event == IE_Pressed && !bOwnsPrimaryButton;
    const FBox2D Service = GetServiceBounds();
    if (Service.bIsValid && Service.IsInsideOrOn(Pointer))
    {
        const double LocalY = Pointer.Y - Service.Min.Y;
        if (ActiveService == ECoopRPGLobbyService::Shop && bCanScroll && LocalY >= ShopListY && LocalY < PairedDetailY)
        {
            FirstShopRow = FMath::Clamp(FirstShopRow + Direction, 0, FMath::Max(0, ShopRows.Num() - VisibleShopRows));
        }
        else if (ActiveService == ECoopRPGLobbyService::Storage && bCanScroll
            && LocalY >= PairedGridY && LocalY < PairedDetailY)
        {
            FirstStorageRow = FMath::Clamp(FirstStorageRow + Direction, 0, FMath::Max(0, StorageRows - VisibleInventoryRows));
        }
        return true;
    }
    if (!IsScrollableTab() || !GetPanelBounds().IsInsideOrOn(Pointer))
    {
        return false;
    }
    if (bCanScroll && GetScrollContentBounds().IsInsideOrOn(Pointer))
    {
        const FIntPoint Rows = GetScrollRowCounts();
        int32& FirstRow = GetFirstScrollRow();
        FirstRow = FMath::Clamp(FirstRow + Direction, 0, Rows.X - Rows.Y);
    }
    return true;
}

void FCoopRPGEquipmentPanel::ReleasePointer()
{
    bOwnsPrimaryButton = false;
    PressedTab = INDEX_NONE;
    PressedStorageControl = INDEX_NONE;
    PressedStorageItem = INDEX_NONE;
    PressedShopControl = INDEX_NONE;
    PressedJobControl = INDEX_NONE;
    PressedInventorySlot = INDEX_NONE;
    PressedEmptyRecovery = INDEX_NONE;
    PressedActionItem = EInventoryItem::None;
    PressedInventoryAction.Reset();
    PressedSkillGroup = INDEX_NONE;
    PressedSkillBinding = INDEX_NONE;
    ScrollDragOffset.Reset();
}

ECoopRPGActiveSkill FCoopRPGEquipmentPanel::ToCapsuleSkill(EInventoryItem Item)
{
    switch (Item)
    {
    case EInventoryItem::SlashCapsule: return ECoopRPGActiveSkill::Slash;
    case EInventoryItem::TauntCapsule: return ECoopRPGActiveSkill::Taunt;
    default: return ECoopRPGActiveSkill::None;
    }
}

bool FCoopRPGEquipmentPanel::IsStatScroll(EInventoryItem Item)
{
    return Item == EInventoryItem::MartialPowerScroll || Item == EInventoryItem::ProtectionScroll;
}

ECoopRPGScrollStat FCoopRPGEquipmentPanel::ToScrollStat(EInventoryItem Item)
{
    return Item == EInventoryItem::ProtectionScroll ? ECoopRPGScrollStat::Protection : ECoopRPGScrollStat::MartialPower;
}

void FCoopRPGEquipmentPanel::UpdateInventorySelection(const FInventoryItems& Items)
{
    if (SelectedInventoryItem != EInventoryItem::None)
    {
        SelectedInventorySlot = Items.IndexOfByPredicate([this](const FInventoryStack& Stack)
        {
            return Stack.Item == SelectedInventoryItem && Stack.Index == SelectedInventoryStack;
        });
    }
    const FInventoryStack Selection = Items.IsValidIndex(SelectedInventorySlot) ? Items[SelectedInventorySlot] : FInventoryStack{};
    SelectedInventoryItem = Selection.Item;
    SelectedInventoryStack = Selection.Index;
}

FBox2D FCoopRPGEquipmentPanel::GetTabBounds(int32 Index) const
{
    constexpr float InventoryUnits = 1.38f;
    constexpr int32 VisibleTabs = TabCount;
    const float Unit = (Width - (VisibleTabs - 1) * EquipmentTabGap) / (VisibleTabs - 1 + InventoryUnits);
    const float Offset = Index * (Unit + EquipmentTabGap) + (Index > InventoryTab ? (InventoryUnits - 1.0f) * Unit : 0.0f);
    const FVector2D Start = Anchor + FVector2D(Offset, 0.0f);
    return FBox2D(Start, Start + FVector2D(Unit * (Index == InventoryTab ? InventoryUnits : 1.0f), ButtonHeight));
}

FBox2D FCoopRPGEquipmentPanel::GetPanelBounds() const
{
    const FVector2D Start = Anchor + FVector2D(0.0f, ButtonHeight + PanelGap);
    const double ExtraHeight = ActiveTab == InventoryTab ? GetInventoryContentOffset() : 0.0;
    return FBox2D(Start, Start + FVector2D(Width, EquipmentPanelHeight + ExtraHeight));
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

void FCoopRPGEquipmentPanel::LogLayout(const FVector2D& ViewSize)
{
    const auto ServicePanel = GetServiceBounds();
    const bool bInventoryVisible = ActiveService != ECoopRPGLobbyService::Job && ActiveTab == InventoryTab;
    const auto Inventory = bInventoryVisible ? GetPanelBounds() : FBox2D(ForceInit);
    const bool bStorage = ActiveService == ECoopRPGLobbyService::Storage;
    const double ServiceTop = ServicePanel.Min.Y;
    const double BagTop = Inventory.Min.Y;
    const FString Layout = FString::Printf(TEXT("Service=%s Width=%.0f Height=%.0f ServiceX=%.0f ServiceRight=%.0f ServiceY=%.0f BagX=%.0f BagY=%.0f "
        "InventoryVisible=%d ServiceBottom=%.0f BagBottom=%.0f ServiceTitleY=%.0f BagTitleY=%.0f ServiceRuleY=%.0f BagRuleY=%.0f "
        "ServiceGridY=%.0f BagGridY=%.0f ServiceDetailY=%.0f BagDetailY=%.0f ServiceFooterY=%.0f BagFooterY=%.0f"),
        *UEnum::GetValueAsString(ActiveService), ViewSize.X, ViewSize.Y, ServicePanel.Min.X, ServicePanel.Max.X, ServiceTop,
        Inventory.Min.X, BagTop, bInventoryVisible, ServicePanel.Max.Y, Inventory.Max.Y, ServiceTop + PanelTitleY, BagTop + PanelTitleY,
        ServiceTop + PanelRuleY, BagTop + PanelRuleY, bStorage ? StorageControlBounds(ServicePanel, ContentInset, 0).Min.Y : 0.0,
        bInventoryVisible ? GetInventoryGridBounds().Min.Y : 0.0, ServiceTop + PairedDetailY, BagTop + PairedDetailY,
        bStorage ? StorageControlBounds(ServicePanel, ContentInset, StorageDeposit).Min.Y : 0.0, BagTop + PairedFooterY);
    if (Layout != LastServiceLayout)
    {
        LastServiceLayout = Layout;
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("LobbyServiceLayout %s"), *Layout);
    }
}

void FCoopRPGEquipmentPanel::DrawStats(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller)
{
    using namespace CoopRPG::ExpeditionUI;
    const auto* Hero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const bool bHasHero = IsValid(Hero) && Hero->GetAbilitySystemComponent();
    const FStringView Dash(TEXT("—"));
    const FString MartialPower = bHasHero ? FString::FromInt(Controller.GetMartialPower()) : FString(Dash);
    const FString Protection = bHasHero ? FString::FromInt(Controller.GetProtection()) : FString(Dash);
    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
    const FGuid Round = State ? State->GetRoundId() : FGuid();
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
    constexpr const TCHAR* BaseStatLabels[] = { TEXT("무력"), TEXT("수호"), TEXT("기동"), TEXT("집중") };
    const FStringView BaseStatValues[] = { FStringView(MartialPower), FStringView(Protection), Dash, Dash };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(BaseStatLabels); ++Index)
    {
        DrawStat(BaseStatLabels[Index], BaseStatValues[Index], Top + 162.0 + Index * 24.0, 21.0,
            Color(240, 226, 189), Index < 2 && bHasHero ? Ink : Muted);
    }

    FString Display = FString::Printf(TEXT("MaxHP=%s MaxSP=%s MartialPower=%s Protection=%s Mobility=%s Focus=%s X=%.0f Y=%.0f Width=%.0f Height=%.0f Round=%s"),
        *MaxHealth, *MaxStamina, *MartialPower, *Protection, TEXT("—"), TEXT("—"), Left - ContentInset, Top, Panel.GetSize().X, Panel.GetSize().Y,
        *Round.ToString());
    if (Display != LastStatsDisplay)
    {
        LastStatsDisplay = MoveTemp(Display);
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("StatsPanelState %s %s"), *LastStatsDisplay,
            *CoopRPG::Debug::DescribeLogContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void FCoopRPGEquipmentPanel::DrawStorage(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller)
{
    using namespace CoopRPG::ExpeditionUI;
    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State)
    {
        return;
    }
    const auto& Stock = bPersonalStorage ? Controller.GetPersonalStorage() : State->GetSharedStorage();
    const FBox2D Panel = GetServiceBounds();
    const double Left = Panel.Min.X + ContentInset;
    const double Right = Panel.Max.X - ContentInset;
    const double Top = Panel.Min.Y;
    const auto Ink = Color(73, 48, 30);
    const auto Muted = Color(101, 80, 57);
    const auto Outline = Color(211, 183, 126);
    const auto Green = Color(54, 88, 64);
    const auto Cream = Color(255, 244, 210);
    const auto SlotFill = Color(247, 232, 199);
    const auto SlotOutline = Color(180, 148, 102);
    const auto SelectedSlotFill = Color(255, 241, 194);
    const auto SelectedSlotOutline = Color(128, 86, 47);
    const auto InactiveButtonFill = Color(218, 205, 177);
    const auto InactiveButtonOutline = Color(181, 158, 123);
    const auto Title = Font(13.0f, true);
    const auto Body = Font(10.0f, true);
    const auto Caption = Font(9.0f);
    const auto TileFont = Font(8.0f, true);
    const auto Bounds = [&](int32 Control) { return StorageControlBounds(Panel, ContentInset, Control); };
    const auto Center = [&](FStringView Label, FVector2D Position, const FSlateFontInfo& Face, const FLinearColor& Tint)
    {
        DrawPanelLabel(Surface, Label, Position.X - Measure(Surface, Label, Face).X * 0.5, Position.Y, Face, Tint);
    };
    const auto Button = [&](int32 Control, const TCHAR* Label, bool bEmphasized, bool bEnabled)
    {
        const FBox2D Box = Bounds(Control);
        const bool bHighlight = bEmphasized && bEnabled;
        DrawPanelBox(HUD, Box, bEnabled ? (bHighlight ? Green : SlotFill) : InactiveButtonFill,
            bEnabled ? SlotOutline : InactiveButtonOutline);
        Center(Label, Box.GetCenter(), Body, bHighlight ? Cream : bEnabled ? Ink : Muted);
    };

    DrawPanelLabel(Surface, TEXT("원정 창고"), Left, Top + PanelTitleY, Title, Ink);
    HUD.DrawRect(Color(191, 157, 100), Left, Top + PanelRuleY, Right - Left, 1.0);
    Button(StorageAreaPersonal, TEXT("개인 창고"), bPersonalStorage, true);
    Button(StorageAreaShared, TEXT("공용 창고"), !bPersonalStorage, true);

    TArray<int32, TInlineAllocator<FCoopRPGStorageStock::ItemCount>> Occupied;
    for (const auto Item : StorageDisplayOrder)
    {
        if (Stock.Get(Item) > 0)
        {
            Occupied.Add(static_cast<int32>(Item));
        }
    }
    StorageRows = FMath::DivideAndRoundUp(Occupied.Num(), InventoryColumns);
    FirstStorageRow = FMath::Min(FirstStorageRow, FMath::Max(0, StorageRows - VisibleInventoryRows));
    VisibleStorageItems.Reset();
    for (int32 Index = FirstStorageRow * InventoryColumns;
        Index < FMath::Min(Occupied.Num(), FirstStorageRow * InventoryColumns + VisibleInventorySlots); ++Index)
    {
        VisibleStorageItems.Add(Occupied[Index]);
    }
    if (StorageRows > VisibleInventoryRows)
    {
        const double TrackTop = Bounds(0).Min.Y;
        const double TrackHeight = Bounds(VisibleInventorySlots - 1).Max.Y - TrackTop;
        const double RowHeight = TrackHeight / StorageRows;
        HUD.DrawRect(Color(228, 208, 164), Right - ScrollTrackWidth, TrackTop, ScrollTrackWidth, TrackHeight);
        HUD.DrawRect(Color(170, 133, 87), Right - ScrollTrackWidth, TrackTop + FirstStorageRow * RowHeight,
            ScrollTrackWidth, VisibleInventoryRows * RowHeight);
    }
    const int32 Selection = bStorageFromBag ? GetBagStorageItem(Controller) : SelectedStorageItem;
    for (int32 Index = 0; Index < VisibleInventorySlots; ++Index)
    {
        const FBox2D Tile = Bounds(Index);
        const bool bOccupied = VisibleStorageItems.IsValidIndex(Index);
        const bool bSelected = bOccupied && !bStorageFromBag && VisibleStorageItems[Index] == Selection;
        DrawPanelBox(HUD, Tile, bSelected ? SelectedSlotFill : SlotFill, bSelected ? SelectedSlotOutline : SlotOutline);
        if (!bOccupied)
        {
            continue;
        }
        const int32 StockIndex = VisibleStorageItems[Index];
        const auto Item = static_cast<ECoopRPGStorageItem>(StockIndex);
        const bool bPotion = Item == ECoopRPGStorageItem::HealthPotion || Item == ECoopRPGStorageItem::StaminaPotion;
        const bool bSword = Item == ECoopRPGStorageItem::UnboundSword || Item == ECoopRPGStorageItem::BoundSword;
        const bool bShield = Item == ECoopRPGStorageItem::UnboundShield || Item == ECoopRPGStorageItem::BoundShield;
        const FVector2D Icon = Tile.Min + FVector2D(4.0, 5.0);
        const FBox2D IconBox(Icon, Icon + FVector2D(27.0));
        DrawPanelBox(HUD, IconBox, bPotion || bSword || bShield ? Color(113, 82, 57) : Color(57, 87, 106), Outline);
        if (bPotion || bSword || bShield)
        {
            DrawItemIcon(HUD, bPotion ? 0 : bShield ? 4 : 1, Icon + FVector2D(2.0), 25.0, FLinearColor::White);
        }
        else
        {
            const bool bSkillCapsule = Item == ECoopRPGStorageItem::SlashCapsule || Item == ECoopRPGStorageItem::TauntCapsule;
            const TCHAR* Label = bSkillCapsule ? TEXT("스킬")
                : Item == ECoopRPGStorageItem::ProtectionScroll ? TEXT("수호") : TEXT("무력");
            Center(Label, IconBox.GetCenter(), TileFont, Cream);
        }
        const FString Count = FString::Printf(TEXT("×%d"), Stock.Get(Item));
        DrawPanelLabel(Surface, Count, Tile.Max.X - 4.0 - Measure(Surface, Count, TileFont).X, Tile.Min.Y + 16.0, TileFont, Muted);
        Center(Fit(Surface, bSword ? TEXT("한손검") : bShield ? TEXT("하급 방패") : StorageLabels[StockIndex], TileFont, Tile.GetSize().X - 6.0),
            FVector2D(Tile.GetCenter().X, Tile.Min.Y + 39.0), TileFont, Ink);
    }

    const bool bOpen = !State->IsLobbyDepartureScheduled();
    const auto SelectedItem = static_cast<ECoopRPGStorageItem>(Selection);
    const bool bDeposit = Selection != INDEX_NONE && bStorageFromBag && Controller.GetStorageBagCount(SelectedItem) > 0;
    const bool bWithdraw = Selection != INDEX_NONE && !bStorageFromBag && Stock.Get(SelectedItem) > 0;
    DrawPanelBox(HUD, FBox2D(FVector2D(Left, Top + PairedDetailY), FVector2D(Right, Top + PairedDetailY + PairedDetailHeight)), Color(248, 237, 206), Outline);
    const FString Detail = !bOpen ? FString(TEXT("원정 출발 대기 중")) : !Controller.GetStorageFeedback().IsEmpty()
        ? Controller.GetStorageFeedback() : Selection != INDEX_NONE
        ? FString::Printf(TEXT("%s · %s %d"), StorageLabels[Selection], bStorageFromBag ? TEXT("가방") : TEXT("창고"),
            bStorageFromBag ? Controller.GetStorageBagCount(SelectedItem) : Stock.Get(SelectedItem))
        : FString(TEXT("가방에서 넣고 창고에서 꺼내요."));
    DrawPanelLabel(Surface, Fit(Surface, Detail, Caption, Right - Left - 16.0), Left + 8.0, Top + PairedDetailY + PairedDetailHeight * 0.5, Caption, Ink);
    const bool bDepositEnabled = bOpen && bDeposit;
    const bool bWithdrawEnabled = bOpen && bWithdraw;
    Button(StorageDeposit, TEXT("1개 넣기"), true, bDepositEnabled);
    Button(StorageWithdraw, TEXT("1개 꺼내기"), true, bWithdrawEnabled);
    DrawPanelLabel(Surface, TEXT("[Q] 닫기"), Left, Top + 256.0, Caption, Muted);

    const FVector2D FirstTile = Bounds(0).GetCenter();
    FString Display = FString::Printf(
        TEXT("Round=%s Area=%s Selected=%d BagSelection=%d Occupied=%d StockHP=%d BagHP=%d StockBound=%d BagBound=%d StockSword=%d BagSword=%d "
            "X=%.0f Y=%.0f Width=%.0f Tile0X=%.0f Tile0Y=%.0f SharedX=%.0f PersonalX=%.0f TabY=%.0f DepositX=%.0f WithdrawX=%.0f ActionY=%.0f "
            "ViewWidth=%.0f ViewHeight=%.0f DepositEnabled=%d WithdrawEnabled=%d SelectedTile=%d"),
        *State->GetRoundId().ToString(), bPersonalStorage ? TEXT("Personal") : TEXT("Shared"), Selection, bStorageFromBag,
        VisibleStorageItems.Num(), Stock.Get(ECoopRPGStorageItem::HealthPotion), Controller.GetStorageBagCount(ECoopRPGStorageItem::HealthPotion),
        Stock.Get(ECoopRPGStorageItem::BoundSword), Controller.GetStorageBagCount(ECoopRPGStorageItem::BoundSword),
        Stock.Get(ECoopRPGStorageItem::UnboundSword), Controller.GetStorageBagCount(ECoopRPGStorageItem::UnboundSword),
        Panel.Min.X, Top, Panel.GetSize().X, FirstTile.X, FirstTile.Y, Bounds(StorageAreaShared).GetCenter().X,
        Bounds(StorageAreaPersonal).GetCenter().X, Bounds(StorageAreaShared).GetCenter().Y, Bounds(StorageDeposit).GetCenter().X,
        Bounds(StorageWithdraw).GetCenter().X, Bounds(StorageWithdraw).GetCenter().Y, Surface.ClipX, Surface.ClipY,
        bDepositEnabled, bWithdrawEnabled, bWithdraw);
    if (LastStorageDisplay != Display)
    {
        LastStorageDisplay = MoveTemp(Display);
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("StoragePanel %s"), *LastStorageDisplay);
    }
}

void FCoopRPGEquipmentPanel::DrawShop(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller)
{
    using namespace CoopRPG::ExpeditionUI;
    using namespace CoopRPG::Shop;
    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State)
    {
        return;
    }

    ShopRows.Reset();
    for (int32 Index = 0; Index < ShopOfferCount; ++Index)
    {
        const auto Item = Offers[Index];
        if (bShopBuying || (SellPrice(Item) > 0 && Controller.GetStorageBagCount(Item) > 0))
        {
            ShopRows.Add(Index);
        }
    }
    FirstShopRow = FMath::Clamp(FirstShopRow, 0, FMath::Max(0, ShopRows.Num() - VisibleShopRows));
    if (!ShopRows.Contains(SelectedShopItem))
    {
        SelectedShopItem = ShopRows.IsEmpty() ? INDEX_NONE : ShopRows[0];
        ShopQuantity = 1;
    }

    const bool bHasSelection = SelectedShopItem != INDEX_NONE;
    const auto Selected = bHasSelection ? Offers[SelectedShopItem] : ECoopRPGStorageItem::Count;
    const int32 Gold = Controller.GetSessionGold();
    const FQuantityLimit Limit = QuantityLimit(Selected, bShopBuying, Controller.GetStorageBagCount(Selected), Gold,
        Controller.GetBagRoom(Selected));
    ShopQuantity = FMath::Clamp(ShopQuantity, 1, FMath::Max(1, Limit.Maximum));
    const int64 Total = static_cast<int64>(UnitPrice(Selected, bShopBuying)) * ShopQuantity;
    const bool bPending = Controller.IsShopTradePending();
    const bool bDeparting = State->IsLobbyDepartureScheduled();
    const bool bEditable = !bPending && !bDeparting;

    const FBox2D Panel = GetServiceBounds();
    const double Left = Panel.Min.X + ContentInset;
    const double Right = Panel.Max.X - ContentInset;
    const double Top = Panel.Min.Y;
    const double PriceX = Right - (bShopBuying ? 26.0 : 81.0);
    const double CountX = Right - 26.0;
    const auto Ink = Color(60, 41, 25);
    const auto Muted = Color(101, 80, 57);
    const auto Outline = Color(211, 183, 126);
    const auto Green = Color(54, 88, 64);
    const auto Cream = Color(255, 244, 210);
    const auto Title = Font(13.0f, true);
    const auto Body = Font(11.0f);
    const auto Bold = Font(11.0f, true);
    const auto Caption = Font(9.0f);
    const auto AlertFont = Font(9.0f, true);
    const auto Bounds = [&](int32 Control) { return GetShopControlBounds(Control); };
    const auto Center = [&](FStringView Value, FVector2D Position, const FSlateFontInfo& Face, const FLinearColor& Tint)
    {
        DrawPanelLabel(Surface, Value, Position.X - Measure(Surface, Value, Face).X * 0.5, Position.Y, Face, Tint);
    };
    const auto Button = [&](int32 Control, const TCHAR* Label, bool bEnabled, bool bHighlight)
    {
        const auto Box = Bounds(Control);
        DrawPanelBox(HUD, Box, bEnabled ? (bHighlight ? Green : Color(247, 232, 199)) : Color(218, 205, 177), Outline);
        Center(Label, Box.GetCenter(), Bold, bEnabled ? (bHighlight ? Cream : Ink) : Muted);
    };

    DrawPanelLabel(Surface, TEXT("원정 상점"), Left, Top + PanelTitleY, Title, Ink);
    HUD.DrawRect(Color(191, 157, 100), Left, Top + PanelRuleY, Right - Left, 1.0);
    Button(ShopBuyMode, TEXT("구매"), bEditable, bShopBuying);
    Button(ShopSellMode, TEXT("판매"), bEditable, !bShopBuying);
    DrawPanelLabel(Surface, TEXT("품목"), Left + 8.0, Top + 87.0, Caption, Muted);
    Center(TEXT("단가"), FVector2D(PriceX, Top + 87.0), Caption, Muted);
    if (!bShopBuying)
    {
        Center(TEXT("보유"), FVector2D(CountX, Top + 87.0), Caption, Muted);
    }

    const int32 LastRow = FMath::Min(FirstShopRow + VisibleShopRows, ShopRows.Num());
    for (int32 RowIndex = FirstShopRow; RowIndex < LastRow; ++RowIndex)
    {
        const int32 Index = ShopRows[RowIndex];
        const auto Item = Offers[Index];
        const FBox2D Row = Bounds(Index);
        const bool bSelected = Index == SelectedShopItem;
        DrawPanelBox(HUD, Row, bSelected ? Color(255, 241, 194) : Color(247, 232, 199),
            bSelected ? Color(128, 86, 47) : Outline);
        const double MidY = Row.GetCenter().Y;
        DrawPanelLabel(Surface, Fit(Surface, ShopLabels[Index], Body, PriceX - Left - 33.0), Left + 9.0, MidY, Body, Ink);
        Center(FString::FromInt(UnitPrice(Item, bShopBuying)), FVector2D(PriceX, MidY), Body, Ink);
        if (!bShopBuying)
        {
            Center(FString::FromInt(Controller.GetStorageBagCount(Item)), FVector2D(CountX, MidY), Bold, Ink);
        }
    }
    if (ShopRows.Num() > VisibleShopRows)
    {
        const double TrackY = Top + ShopListY;
        const double TrackHeight = (VisibleShopRows - 1) * ShopRowPitch + ShopRowHeight;
        const double ThumbHeight = TrackHeight * VisibleShopRows / ShopRows.Num();
        const double ThumbY = TrackY + (TrackHeight - ThumbHeight) * FirstShopRow / (ShopRows.Num() - VisibleShopRows);
        HUD.DrawRect(Outline, Right - 4.0, TrackY, 4.0, TrackHeight);
        HUD.DrawRect(Color(153, 125, 82), Right - 4.0, ThumbY, 4.0, ThumbHeight);
    }
    if (!bHasSelection)
    {
        DrawPanelLabel(Surface, TEXT("판매할 물품이 없어요."), Left + 8.0, Top + ShopListY + 14.0, Body, Muted);
    }

    const double SummaryCenter = Top + PairedDetailY + PairedDetailHeight * 0.5;
    DrawPanelBox(HUD, FBox2D(FVector2D(Left, Top + PairedDetailY), FVector2D(Right, Top + PairedDetailY + PairedDetailHeight)),
        Color(248, 237, 206), Outline);
    Button(ShopMinus, TEXT("−"), bEditable && ShopQuantity > 1, false);
    Button(ShopPlus, TEXT("+"), bEditable && ShopQuantity < Limit.Maximum, false);
    const double CountCenter = (Bounds(ShopMinus).Max.X + Bounds(ShopPlus).Min.X) * 0.5;
    const auto QuantityLabel = bHasSelection ? FString::FromInt(ShopQuantity) : FString(TEXT("—"));
    const double QuantityWidth = Bounds(ShopPlus).Min.X - Bounds(ShopMinus).Max.X - 6.0;
    Center(Fit(Surface, QuantityLabel, Bold, QuantityWidth), FVector2D(CountCenter, SummaryCenter), Bold, Ink);
    const double TotalLeft = Bounds(ShopPlus).Max.X + 8.0;
    const FString TotalLabel = bHasSelection ? FString::Printf(
        TEXT("%s %lld"), bShopBuying ? TEXT("지불할 골드") : TEXT("받을 골드"), Total) : FString(TEXT("받을 골드 —"));
    const FString DisplayTotal = Fit(Surface, TotalLabel, Body, Right - 8.0 - TotalLeft);
    DrawPanelLabel(Surface, DisplayTotal, Right - 8.0 - Measure(Surface, DisplayTotal, Body).X, SummaryCenter, Body, Ink);

    const bool bCommit = bEditable && bHasSelection && ShopQuantity <= Limit.Maximum;
    Button(ShopCommit, bShopBuying ? TEXT("구매 확정") : TEXT("판매 확정"), bCommit, true);
    const FString Feedback = Controller.GetShopFeedback();
    FString Message;
    if (bDeparting) { Message = TEXT("원정 출발 대기 중"); }
    else if (bPending) { Message = TEXT("거래 확인 중"); }
    else if (Limit.Maximum == 0 && FCString::Strcmp(Limit.Reason, TEXT("InsufficientGold")) == 0)
    {
        Message = InsufficientGoldMessage;
    }
    else if (!Feedback.IsEmpty()) { Message = Feedback; }
    else if (bHasSelection && Limit.Maximum == 0) { Message = Limit.Message; }
    DrawPanelLabel(Surface, TEXT("[Q] 닫기"), Left, Top + 256.0, Caption, Ink);
    const double MessageLeft = Left + Measure(Surface, TEXT("[Q] 닫기"), Caption).X + 8.0;
    const bool bGoldAlert = Message == InsufficientGoldMessage;
    const auto& MessageFont = bGoldAlert ? AlertFont : Caption;
    const FString DisplayMessage = Fit(Surface, Message, MessageFont, Right - MessageLeft);
    const double MessageX = bGoldAlert ? Right - Measure(Surface, DisplayMessage, MessageFont).X : MessageLeft;
    DrawPanelLabel(Surface, DisplayMessage, MessageX, Top + 256.0, MessageFont, bGoldAlert ? Color(156, 41, 36) : Muted);

    const auto VisibleCenterY = [&](int32 ItemIndex)
    {
        const int32 Row = ShopRows.IndexOfByKey(ItemIndex);
        return Row >= FirstShopRow && Row < LastRow ? Bounds(ItemIndex).GetCenter().Y : 0.0;
    };
    const auto CommitCenter = Bounds(ShopCommit).GetCenter();
    FString Display = FString::Printf(TEXT("Round=%s Mode=%s Gold=%d HP=%d Capsule=%d Selected=%d Quantity=%d Max=%d Total=%lld "
        "Rows=%d Pending=%d CanTrade=%d GoldAlert=%d Limit=%s X=%.0f Width=%.0f ViewWidth=%.0f ViewHeight=%.0f PotionY=%.0f CapsuleY=%.0f "
        "BuyTabX=%.0f SellTabX=%.0f TabY=%.0f MinusX=%.0f PlusX=%.0f QuantityY=%.0f ConfirmX=%.0f ConfirmY=%.0f"),
        *State->GetRoundId().ToString(), bShopBuying ? TEXT("Buy") : TEXT("Sell"), Gold,
        Controller.GetStorageBagCount(ECoopRPGStorageItem::HealthPotion), Controller.GetStorageBagCount(ECoopRPGStorageItem::SlashCapsule),
        SelectedShopItem, ShopQuantity, Limit.Maximum, Total, ShopRows.Num(), bPending, bCommit, bGoldAlert, Limit.Reason,
        Panel.Min.X, Panel.GetSize().X, Surface.ClipX, Surface.ClipY, VisibleCenterY(0), VisibleCenterY(1),
        Bounds(ShopBuyMode).GetCenter().X, Bounds(ShopSellMode).GetCenter().X, Bounds(ShopBuyMode).GetCenter().Y,
        Bounds(ShopMinus).GetCenter().X, Bounds(ShopPlus).GetCenter().X, SummaryCenter, CommitCenter.X, CommitCenter.Y);
    if (Display != LastShopDisplay)
    {
        LastShopDisplay = MoveTemp(Display);
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("ShopPanel %s"), *LastShopDisplay);
    }
}

void FCoopRPGEquipmentPanel::DrawJob(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller,
    const FCoopRPGExpeditionDashboard& Dashboard)
{
    using namespace CoopRPG::ExpeditionUI;
    const auto* Identity = Controller.GetPlayerState<ACoopRPGPlayerState>();
    const ECoopRPGHero Current = Identity ? Identity->GetSelectedHero() : ECoopRPGHero::Unassigned;
    const FBox2D Panel = GetServiceBounds();
    const double Left = Panel.Min.X + ContentInset;
    const double Right = Panel.Max.X - ContentInset;
    const auto Title = Font(13.0f, true);
    const auto Body = Font(10.0f);
    const auto Label = Font(9.0f, true);
    const auto Ink = Color(73, 48, 30);
    const auto Outline = Color(211, 183, 126);
    DrawPanelLabel(Surface, TEXT("직업 교관"), Left, Panel.Min.Y + 23.0, Title, Ink);
    HUD.DrawRect(Color(191, 157, 100), Left, Panel.Min.Y + 41.0, Right - Left, 1.0);
    DrawPanelLabel(Surface, TEXT("직업을 바꾸면 준비 상태가 풀려요."), Left, Panel.Min.Y + 56.0, Body, Ink);

    for (int32 Index = 0; Index < 2; ++Index)
    {
        const bool bGuardian = Index == 1;
        const auto Hero = bGuardian ? ECoopRPGHero::AnimalHero : ECoopRPGHero::Adventurer;
        const bool bSelected = Current == Hero;
        const FBox2D Card = GetJobChoiceBounds(Index);
        const FBox2D Button = GetJobButtonBounds(Index);
        const auto Accent = bGuardian ? Color(51, 88, 119) : Color(122, 66, 54);
        DrawPanelBox(HUD, Card, bSelected ? Color(236, 232, 195) : Color(249, 237, 204), Outline);
        constexpr double BadgeSize = 48.0;
        const double MidY = Card.GetCenter().Y;
        const FVector2D Badge(Card.Min.X + 10.0, MidY - BadgeSize * 0.5);
        Dashboard.DrawJobBadge(HUD, Surface, FBox2D(Badge, Badge + FVector2D(BadgeSize)), Hero, false);
        const double TextX = Badge.X + BadgeSize + 10.0;
        const double TextWidth = Button.Min.X - TextX - 8.0;
        DrawPanelLabel(Surface, bGuardian ? TEXT("수호기사") : TEXT("전사"), TextX, MidY - 17.0, Title, Accent);
        DrawPanelLabel(Surface, Fit(Surface, bGuardian ? TEXT("한손검 · 방패") : TEXT("한손검"), Body, TextWidth), TextX, MidY, Body, Ink);
        DrawPanelLabel(Surface, Fit(Surface, bGuardian ? TEXT("가드 · 도발") : TEXT("슬래시"), Body, TextWidth), TextX, MidY + 17.0, Body, Ink);
        DrawPanelBox(HUD, Button, bSelected ? Color(225, 230, 201) : Color(54, 88, 64), Outline);
        const TCHAR* ButtonText = bSelected ? TEXT("현재 직업") : TEXT("선택");
        DrawPanelLabel(Surface, ButtonText, Button.GetCenter().X - Measure(Surface, ButtonText, Label).X * 0.5,
            MidY, Label, bSelected ? Color(54, 88, 64) : Color(255, 244, 210));
    }
    DrawPanelLabel(Surface, TEXT("[Q] 닫기"), Left, Panel.Min.Y + 256.0, Body, Ink);
    const auto Warrior = GetJobButtonBounds(0).GetCenter();
    const auto Guardian = GetJobButtonBounds(1).GetCenter();
    const FString Display = FString::Printf(TEXT("Hero=%s X=%.0f WarriorY=%.0f GuardianY=%.0f BadgeSide=48"),
        *UEnum::GetValueAsString(Current), Warrior.X, Warrior.Y, Guardian.Y);
    if (LastJobDisplay != Display)
    {
        LastJobDisplay = Display;
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("LobbyJobPanel %s"), *Display);
    }
}

FBox2D FCoopRPGEquipmentPanel::GetJobChoiceBounds(int32 Index) const
{
    const FBox2D Panel = GetServiceBounds();
    const FVector2D Min = Panel.Min + FVector2D(ContentInset, 73.0 + Index * 84.0);
    return FBox2D(Min, FVector2D(Panel.Max.X - ContentInset, Min.Y + 74.0));
}

FBox2D FCoopRPGEquipmentPanel::GetJobButtonBounds(int32 Index) const
{
    const auto Row = GetJobChoiceBounds(Index);
    const double Y = Row.GetCenter().Y;
    return FBox2D(FVector2D(Row.Max.X - 80.0, Y - 13.0), FVector2D(Row.Max.X - 10.0, Y + 13.0));
}

int32 FCoopRPGEquipmentPanel::FindJobControl(const FVector2D& Pointer) const
{
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (GetJobButtonBounds(Index).IsInsideOrOn(Pointer))
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

void FCoopRPGEquipmentPanel::HandleShopControl(ACoopRPGPlayerController& Controller, int32 Control)
{
    using namespace CoopRPG::Shop;
    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!State || State->IsLobbyDepartureScheduled() || Controller.IsShopTradePending())
    {
        return;
    }
    if (Control == ShopBuyMode || Control == ShopSellMode)
    {
        const bool bNextBuying = Control == ShopBuyMode;
        if (bNextBuying != bShopBuying)
        {
            bShopBuying = bNextBuying;
            SelectedShopItem = INDEX_NONE;
            ShopQuantity = 1;
            FirstShopRow = 0;
            Controller.ClearShopFeedback();
        }
        return;
    }
    if (ShopRows.Contains(Control))
    {
        if (SelectedShopItem != Control)
        {
            SelectedShopItem = Control;
            ShopQuantity = 1;
            Controller.ClearShopFeedback();
        }
        return;
    }
    if (SelectedShopItem == INDEX_NONE)
    {
        return;
    }
    const auto Item = Offers[SelectedShopItem];
    const FQuantityLimit Limit = QuantityLimit(Item, bShopBuying, Controller.GetStorageBagCount(Item),
        Controller.GetSessionGold(), Controller.GetBagRoom(Item));
    if (Control == ShopMinus && ShopQuantity > 1)
    {
        --ShopQuantity;
        Controller.ClearShopFeedback();
    }
    else if (Control == ShopPlus && ShopQuantity < Limit.Maximum)
    {
        ++ShopQuantity;
        Controller.ClearShopFeedback();
    }
    else if (Control == ShopCommit && ShopQuantity <= Limit.Maximum)
    {
        Controller.RequestShopTrade(Item, bShopBuying, ShopQuantity);
        ShopQuantity = 1;
    }
}

FBox2D FCoopRPGEquipmentPanel::GetShopControlBounds(int32 Control) const
{
    const FBox2D Panel = GetServiceBounds();
    const double Left = Panel.Min.X + ContentInset;
    const double Right = Panel.Max.X - ContentInset;
    const double Top = Panel.Min.Y;
    const double Half = (Left + Right) * 0.5;
    if (Control < ShopOfferCount)
    {
        const double Y = Top + ShopListY + (ShopRows.IndexOfByKey(Control) - FirstShopRow) * ShopRowPitch;
        return FBox2D(FVector2D(Left, Y), FVector2D(Right - (ShopRows.Num() > VisibleShopRows ? 8.0 : 0.0), Y + ShopRowHeight));
    }
    if (Control == ShopBuyMode || Control == ShopSellMode)
    {
        const bool bBuy = Control == ShopBuyMode;
        return FBox2D(FVector2D(bBuy ? Left : Half + 3.0, Top + 49.0),
            FVector2D(bBuy ? Half - 3.0 : Right, Top + 74.0));
    }
    if (Control == ShopMinus || Control == ShopPlus)
    {
        const double X = Left + (Control == ShopMinus ? 5.0 : 72.0);
        return FBox2D(FVector2D(X, Top + PairedDetailY + 3.0),
            FVector2D(X + 24.0, Top + PairedDetailY + PairedDetailHeight - 3.0));
    }
    return FBox2D(FVector2D(Left, Top + PairedFooterY), FVector2D(Right, Top + PairedFooterY + PairedFooterHeight));
}

int32 FCoopRPGEquipmentPanel::FindShopControl(const FVector2D& Pointer) const
{
    const int32 LastRow = FMath::Min(FirstShopRow + VisibleShopRows, ShopRows.Num());
    for (int32 Row = FirstShopRow; Row < LastRow; ++Row)
    {
        if (GetShopControlBounds(ShopRows[Row]).IsInsideOrOn(Pointer))
        {
            return ShopRows[Row];
        }
    }
    for (int32 Control = ShopBuyMode; Control <= ShopCommit; ++Control)
    {
        if (GetShopControlBounds(Control).IsInsideOrOn(Pointer))
        {
            return Control;
        }
    }
    return INDEX_NONE;
}

int32 FCoopRPGEquipmentPanel::FindStorageControl(const FVector2D& Pointer) const
{
    for (int32 Index = 0; Index <= StorageWithdraw; ++Index)
    {
        if (StorageControlBounds(GetServiceBounds(), ContentInset, Index).IsInsideOrOn(Pointer))
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

int32 FCoopRPGEquipmentPanel::GetBagStorageItem(const ACoopRPGPlayerController& Controller) const
{
    switch (SelectedInventoryItem)
    {
    case EInventoryItem::Potion: return static_cast<int32>(ECoopRPGStorageItem::HealthPotion);
    case EInventoryItem::StaminaPotion: return static_cast<int32>(ECoopRPGStorageItem::StaminaPotion);
    case EInventoryItem::Sword: return static_cast<int32>(Controller.GetSwordOwnership() == ECoopRPGEquipmentOwnership::Bound
        ? ECoopRPGStorageItem::BoundSword : ECoopRPGStorageItem::UnboundSword);
    case EInventoryItem::PersonalShield: return static_cast<int32>(Controller.GetShieldOwnership() == ECoopRPGEquipmentOwnership::Bound
        ? ECoopRPGStorageItem::BoundShield : ECoopRPGStorageItem::UnboundShield);
    case EInventoryItem::MartialPowerScroll: return static_cast<int32>(ECoopRPGStorageItem::MartialScroll);
    case EInventoryItem::ProtectionScroll: return static_cast<int32>(ECoopRPGStorageItem::ProtectionScroll);
    case EInventoryItem::SlashCapsule: return static_cast<int32>(ECoopRPGStorageItem::SlashCapsule);
    case EInventoryItem::TauntCapsule: return static_cast<int32>(ECoopRPGStorageItem::TauntCapsule);
    default: return INDEX_NONE;
    }
}

void FCoopRPGEquipmentPanel::DrawAtlas(AHUD& HUD, const FBox2D& Bounds, const FBox2D& Source) const
{
    const FVector2D Size = Bounds.GetSize();
    const FVector2D SourceSize = Source.GetSize();
    HUD.DrawTexture(Skin.Get(), Bounds.Min.X, Bounds.Min.Y, Size.X, Size.Y,
        (Source.Min.X + 0.5) / AtlasWidth, (Source.Min.Y + 0.5) / AtlasHeight,
        (SourceSize.X - 1.0) / AtlasWidth, (SourceSize.Y - 1.0) / AtlasHeight);
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
    const int32 PotionCount = Controller.GetRecoveryCount();
    const int32 StaminaCount = Controller.GetStaminaRecoveryCount();
    const bool bHealthRegistered = Controller.IsRecoveryRegistered(false);
    const bool bStaminaRegistered = Controller.IsRecoveryRegistered(true);
    const bool bStage = CoopRPG::Expedition::IsExpedition(Controller.GetWorld()) && !CoopRPG::Expedition::IsLobby(Controller.GetWorld());
    const auto SwordOwnership = Controller.GetSwordOwnership();
    const bool bHasSword = SwordOwnership != ECoopRPGEquipmentOwnership::None;
    const bool bSwordBound = SwordOwnership == ECoopRPGEquipmentOwnership::Bound;
    const bool bSwordEquipped = Controller.IsPersonalSwordEquipped();
    const auto ShieldOwnership = Controller.GetShieldOwnership();
    const bool bHasShield = ShieldOwnership != ECoopRPGEquipmentOwnership::None;
    const bool bShieldBound = ShieldOwnership == ECoopRPGEquipmentOwnership::Bound;
    const bool bPersonalShield = Controller.IsPersonalShieldEquipped();
    const bool bHasStone = State->GetTeamLootCount() > 0;
    const int32 BagSlotsUsed = Controller.GetBagSlotsUsed();
    int32 EmptyDisplaySlots = FMath::Max(0, InventorySlotCount - BagSlotsUsed);
    FInventoryItems Items;
    const auto AddStacks = [&](EInventoryItem Item, ECoopRPGStorageItem BagItem, int32 Count, bool bRegistered = false)
    {
        const int32 FirstSlot = Items.Num();
        if (bRegistered && Count == 0 && EmptyDisplaySlots > 0)
        {
            Items.Add({Item, 0, 0});
            --EmptyDisplaySlots;
        }

        const int32 StackSize = CoopRPG::Bag::StackSize(BagItem);
        for (int32 Index = 0; Count > 0 && Items.Num() < InventorySlotCount; ++Index)
        {
            const int32 StackCount = FMath::Min(Count, StackSize);
            Items.Add({Item, StackCount, Index});
            Count -= StackCount;
        }
        return Items.Num() > FirstSlot ? FirstSlot : INDEX_NONE;
    };
    const int32 PotionSlot = AddStacks(EInventoryItem::Potion, ECoopRPGStorageItem::HealthPotion, PotionCount, bHealthRegistered);
    const int32 StaminaSlot = AddStacks(EInventoryItem::StaminaPotion, ECoopRPGStorageItem::StaminaPotion, StaminaCount, bStaminaRegistered);
    const int32 SwordSlot = AddStacks(EInventoryItem::Sword, ECoopRPGStorageItem::UnboundSword, bHasSword ? 1 : 0);
    const int32 ShieldSlot = AddStacks(EInventoryItem::PersonalShield, ECoopRPGStorageItem::UnboundShield, bHasShield ? 1 : 0);
    const int32 ScrollCount = Controller.GetStatScrollCount(ECoopRPGScrollStat::MartialPower);
    const int32 ScrollSlot = AddStacks(EInventoryItem::MartialPowerScroll, ECoopRPGStorageItem::MartialScroll, ScrollCount);
    const int32 ProtectionCount = Controller.GetStatScrollCount(ECoopRPGScrollStat::Protection);
    const int32 ProtectionSlot = AddStacks(EInventoryItem::ProtectionScroll, ECoopRPGStorageItem::ProtectionScroll, ProtectionCount);
    const int32 CapsuleSlot = AddStacks(EInventoryItem::SlashCapsule, ECoopRPGStorageItem::SlashCapsule,
        Controller.HasSkillCapsule(ECoopRPGActiveSkill::Slash) ? 1 : 0);
    const int32 TauntCapsuleSlot = AddStacks(EInventoryItem::TauntCapsule, ECoopRPGStorageItem::TauntCapsule,
        Controller.HasSkillCapsule(ECoopRPGActiveSkill::Taunt) ? 1 : 0);
    UpdateInventorySelection(Items);
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

    Label(TEXT("인벤토리"), Left, Top + PanelTitleY, TitleFont, Ink);
    const FString GoldValue = FString::FromInt(Controller.GetSessionGold());
    const double GoldWidth = FMath::Max(81.0, Measure(Surface, GoldValue, BodyFont).X + 50.0);
    Box(FBox2D(FVector2D(Right - GoldWidth, Top + 13.0), FVector2D(Right, Top + 34.0)), Color(244, 227, 183), Outline);
    Icon(3, FVector2D(Right - GoldWidth + 5.0, Top + 14.0), 27.0, FLinearColor::White);
    Label(TEXT("골드"), Right - GoldWidth + 24.0, Top + 23.0, SmallFont, Muted);
    RightLabel(GoldValue, Right - 7.0, Top + 23.0, BodyFont, Ink);
    HUD.DrawRect(Color(191, 157, 100), Left, Top + PanelRuleY, Right - Left, 1.0);
    const FIntPoint Supplies = Controller.GetStageRecoverySupplies();
    const FIntPoint Uses = Controller.GetStageRecoveryUsesRemaining();
    const FIntPoint AvailableUses = Controller.GetAvailableRecoveryUses();
    const double ItemsTop = Top + GetInventoryContentOffset();
    const double DetailTop = ItemsTop + (bLobby ? PairedDetailY : 176.0);
    const double DetailHeight = bLobby ? PairedDetailHeight : 37.0;
    const double DetailCenter = DetailTop + DetailHeight * 0.5;
    const double FooterTop = ItemsTop + (bLobby ? PairedFooterY : InventoryFooterY);
    const double FooterHeight = bLobby ? PairedFooterHeight : InventoryFooterHeight;
    if (bStage)
    {
        const auto DrawShortcut = [&](bool bStamina, bool bRegistered)
        {
            const TCHAR* Name = bStamina ? (bRegistered ? TEXT("[J] 개인 SP 물약") : TEXT("[J] 보급 SP 물약"))
                : (bRegistered ? TEXT("[H] 개인 HP 물약") : TEXT("[H] 보급 HP 물약"));
            const int32 Stock = Controller.GetSelectedRecoveryStock(bStamina);
            const bool bCanClearEmpty = bRegistered && Stock == 0;
            const int32 Available = bStamina ? AvailableUses.Y : AvailableUses.X;
            const FString Detail = bCanClearEmpty ? FString(TEXT("재고 없음 · 0회 · 해제"))
                : FString::Printf(TEXT("%d 회복 · 사용 가능 %d회"),
                    bRegistered ? CoopRPG::Recovery::PersonalAmount : CoopRPG::Recovery::SupplyAmount, Available);
            const FBox2D Bounds = GetRecoveryShortcutBounds(bStamina);
            const double ColumnWidth = Bounds.GetSize().X;
            Box(Bounds, bCanClearEmpty ? Color(239, 227, 194) : Color(248, 237, 206), Outline);
            Label(Fit(Surface, Name, BodyFont, ColumnWidth - 14.0), Bounds.Min.X + 7.0, Top + 55.0, BodyFont, Ink);
            Label(Fit(Surface, Detail, SmallFont, ColumnWidth - 14.0), Bounds.Min.X + 7.0, Top + 71.0, SmallFont, Muted);
        };
        DrawShortcut(false, bHealthRegistered);
        DrawShortcut(true, bStaminaRegistered);
    }
    Label(TEXT("내 아이템"), Left, ItemsTop + (bLobby ? 61.0 : 54.0), SmallFont, Muted);
    const FString Capacity = FString::Printf(TEXT("%d / %d칸"), BagSlotsUsed, InventorySlotCount);
    RightLabel(Capacity, Right - 9.0, ItemsTop + (bLobby ? 61.0 : 54.0), SmallFont, Muted);

    for (int32 Index = 0; Index < VisibleInventorySlots; ++Index)
    {
        const FBox2D Tile = GetInventorySlotBounds(Index);
        const int32 Slot = FirstInventoryRow * InventoryColumns + Index;
        const FInventoryStack Stack = Items.IsValidIndex(Slot) ? Items[Slot] : FInventoryStack{};
        const EInventoryItem Item = Stack.Item;
        const bool bStamina = Item == EInventoryItem::StaminaPotion;
        const bool bPotion = Item == EInventoryItem::Potion || bStamina;
        const bool bSword = Item == EInventoryItem::Sword;
        const bool bShield = Item == EInventoryItem::PersonalShield;
        const bool bCapsule = ToCapsuleSkill(Item) != ECoopRPGActiveSkill::None;
        const bool bScroll = IsStatScroll(Item);
        const bool bProtection = Item == EInventoryItem::ProtectionScroll;
        const bool bSelected = Slot == SelectedInventorySlot;
        Box(Tile, bSelected ? Color(255, 240, 191) : Color(238, 221, 183), bSelected ? Color(166, 115, 60) : Outline);
        if (bPotion || bSword || bShield || bScroll || bCapsule)
        {
            const double IconSize = FMath::Min(27.0, Tile.GetSize().X - 8.0);
            const FVector2D IconPosition = Tile.Min + FVector2D(4.0, 5.0);
            Box(FBox2D(IconPosition, IconPosition + FVector2D(IconSize, 27.0)),
                bCapsule ? Color(43, 74, 109) : bProtection ? Color(51, 89, 100) : bScroll ? Color(87, 65, 112) : bStamina ? Color(57, 87, 106) : Color(113, 82, 57),
                bProtection ? Color(76, 120, 130) : bScroll ? Color(120, 94, 147) : bStamina ? Color(76, 111, 130) : Color(140, 103, 65));
            if (bScroll || bCapsule)
            {
                const FStringView StatIcon(bCapsule ? TEXT("스킬") : bProtection ? TEXT("수호") : TEXT("무력"));
                Label(StatIcon, IconPosition.X + (IconSize - Measure(Surface, StatIcon, BodyFont).X) * 0.5,
                    IconPosition.Y + 13.5, BodyFont, Color(255, 244, 210));
            }
            else
            {
                Icon(bPotion ? 0 : bShield ? 4 : 1, IconPosition + FVector2D(2.0, 2.0), IconSize - 2.0, FLinearColor::White);
            }
            if (Tile.GetSize().X >= 52.0)
            {
                const FString Status = bSword || bShield
                    ? FString((bShield ? bPersonalShield : bSwordEquipped) ? TEXT("장착")
                        : (bShield ? bShieldBound : bSwordBound) ? TEXT("귀속") : TEXT(""))
                    : FString::Printf(TEXT("×%d"), Stack.Count);
                RightLabel(Status, Tile.Max.X - 4.0, Tile.Min.Y + 13.0, TileFont, Muted);
            }
            const TCHAR* ItemName = bCapsule ? (Item == EInventoryItem::TauntCapsule ? TEXT("도발 캡슐") : TEXT("슬래시 캡슐")) : bProtection ? TEXT("수호 스크롤") : bScroll ? TEXT("무력 스크롤") : bStamina ? TEXT("SP 물약") : bPotion ? TEXT("HP 물약") : bShield ? TEXT("하급 방패") : TEXT("한손검");
            const FString Name = Fit(Surface, ItemName, TileFont, Tile.GetSize().X - 4.0);
            Label(Name, Tile.GetCenter().X - Measure(Surface, Name, TileFont).X * 0.5, Tile.Min.Y + 39.0, TileFont, Ink);
        }
    }

    DrawScrollTrack(HUD);

    Box(FBox2D(FVector2D(Left, DetailTop), FVector2D(Right, DetailTop + DetailHeight)), Color(248, 237, 206), Outline);
    const bool bStaminaSelected = SelectedInventoryItem == EInventoryItem::StaminaPotion;
    const bool bPotionSelected = SelectedInventoryItem == EInventoryItem::Potion || bStaminaSelected;
    const bool bSwordSelected = SelectedInventoryItem == EInventoryItem::Sword;
    const bool bShieldSelected = SelectedInventoryItem == EInventoryItem::PersonalShield;
    const bool bScrollSelected = IsStatScroll(SelectedInventoryItem);
    const bool bProtectionSelected = SelectedInventoryItem == EInventoryItem::ProtectionScroll;
    const auto SelectedCapsule = ToCapsuleSkill(SelectedInventoryItem);
    const bool bCapsuleSelected = SelectedCapsule != ECoopRPGActiveSkill::None;
    const TCHAR* DetailName = bStaminaSelected ? TEXT("SP 물약") : bPotionSelected ? TEXT("HP 물약")
        : bCapsuleSelected ? (SelectedCapsule == ECoopRPGActiveSkill::Taunt ? TEXT("도발 캡슐") : TEXT("슬래시 캡슐")) : bProtectionSelected ? TEXT("수호 스크롤") : bScrollSelected ? TEXT("무력 스크롤") : bSwordSelected ? TEXT("한손검") : bShieldSelected ? TEXT("하급 방패") : SelectedInventorySlot == INDEX_NONE ? TEXT("아이템을 선택하세요") : TEXT("빈 슬롯");
    Label(DetailName, Left + 8.0, DetailCenter, BodyFont, Ink);
    FBox2D Action = GetInventoryActionBounds();
    const auto DrawAction = [&](FStringView Caption, bool bEnabled = true)
    {
        Box(Action, bEnabled ? Green : Muted, bEnabled ? Green : Muted);
        Label(Caption, Action.GetCenter().X - Measure(Surface, Caption, BodyFont).X * 0.5,
            Action.GetCenter().Y, BodyFont, Color(255, 244, 210));
    };
    if (bPotionSelected)
    {
        constexpr int32 Recovery = CoopRPG::Recovery::PersonalAmount;
        if (bStage)
        {
            const bool bRegistered = bStaminaSelected ? bStaminaRegistered : bHealthRegistered;
            const FString Detail = FString::Printf(TEXT("%d 회복 · 남은 %d회"), Recovery, bStaminaSelected ? Uses.Y : Uses.X);
            const double DetailX = Left + 15.0 + Measure(Surface, DetailName, BodyFont).X;
            Label(Fit(Surface, Detail, SmallFont, Action.Min.X - DetailX - 6.0), DetailX, DetailCenter, SmallFont, Muted);
            DrawAction(bRegistered ? TEXT("등록 해제") : TEXT("등록"));
        }
        else
        {
            RightLabel(FString::Printf(TEXT("%d 회복"), Recovery), Right - 8.0, DetailCenter, SmallFont, Muted);
        }
    }
    else if (bCapsuleSelected)
    {
        const double DetailX = Left + 15.0 + Measure(Surface, DetailName, BodyFont).X;
        const bool bCanLearn = Controller.CanLearnSkill(SelectedCapsule);
        const TCHAR* JobName = SelectedCapsule == ECoopRPGActiveSkill::Taunt ? TEXT("수호기사만 습득") : TEXT("전사만 습득");
        Label(Fit(Surface, JobName, SmallFont, Action.Min.X - DetailX - 6.0), DetailX, DetailCenter, SmallFont, Muted);
        DrawAction(Controller.HasLearnedSkill(SelectedCapsule) ? TEXT("습득 완료") : TEXT("배우기"), bCanLearn);
    }
    else if (bScrollSelected)
    {
        const double DetailX = Left + 15.0 + Measure(Surface, DetailName, BodyFont).X;
        const FString Bonus = FString::Printf(TEXT("%s +%d"), bProtectionSelected ? TEXT("수호") : TEXT("무력"),
            FCoopRPGStatProgress::PointsPerScroll);
        Label(Bonus, DetailX, DetailCenter, SmallFont, Muted);
        DrawAction(State->IsRoundPlaying() ? TEXT("사용") : TEXT("시작 후 사용"), State->IsRoundPlaying());
    }
    else if (bSwordSelected || bShieldSelected)
    {
        const bool bBound = bShieldSelected ? bShieldBound : bSwordBound;
        const bool bEquipped = bShieldSelected ? bPersonalShield : bSwordEquipped;
        const bool bCanEquip = !bShieldSelected || Controller.CanUsePersonalShield();
        const FStringView Caption = !bCanEquip ? TEXT("수호기사 전용") : bEquipped ? TEXT("장착 해제") : TEXT("장착");
        if (!bCanEquip)
        {
            constexpr double CaptionPadding = 8.0;
            const double CaptionWidth = Measure(Surface, Caption, BodyFont).X + 2.0 * CaptionPadding;
            Action.Min.X = FMath::Min(Action.Min.X, Action.Max.X - CaptionWidth);
        }
        const double DetailX = Left + 15.0 + Measure(Surface, DetailName, BodyFont).X;
        Label(Fit(Surface, bBound ? TEXT("귀속") : TEXT("장착 시 귀속"), SmallFont,
            Action.Min.X - DetailX - 6.0), DetailX, DetailCenter, SmallFont, Muted);
        DrawAction(Caption, bCanEquip);
    }

    Box(FBox2D(FVector2D(Left, FooterTop), FVector2D(Right, FooterTop + FooterHeight)),
        bHasStone ? Color(233, 234, 213) : Color(238, 228, 200), bHasStone ? Color(161, 172, 134) : Outline);
    const double FooterCenter = FooterTop + FooterHeight * 0.5;
    const double FooterIcon = bLobby ? 19.0 : 25.0;
    Icon(2, FVector2D(Left + 8.0, FooterCenter - FooterIcon * 0.5), FooterIcon,
        bHasStone ? FLinearColor::White : Color(190, 174, 141));
    constexpr double StatusHeight = 18.0;
    const FBox2D StatusBounds(FVector2D(Right - 67.0, FooterCenter - StatusHeight * 0.5),
        FVector2D(Right - 7.0, FooterCenter + StatusHeight * 0.5));
    Box(StatusBounds, bHasStone ? Color(219, 231, 213) : Color(228, 218, 192),
        bHasStone ? Color(144, 162, 130) : Color(192, 171, 127));

    const auto AlignFooterText = [&](FStringView Value, double X, const FSlateFontInfo& Face)
    {
        const FVector2D VerticalInk = InkBounds(Surface, Value, Face);
        return FVector2D(X, FooterCenter - (VerticalInk.X + VerticalInk.Y) * 0.5);
    };
    const FVector2D TitlePosition = AlignFooterText(TEXT("팀 마법석"), Left + 8.0 + FooterIcon + 6.0, BodyFont);
    Text(Surface, TEXT("팀 마법석"), TitlePosition, BodyFont, Ink);
    const TCHAR* StoneStatus = bHasStone ? TEXT("획득") : TEXT("미획득");
    const double StatusX = StatusBounds.GetCenter().X - Measure(Surface, StoneStatus, SmallFont).X * 0.5;
    const FVector2D StatusPosition = AlignFooterText(StoneStatus, StatusX, SmallFont);
    Text(Surface, StoneStatus, StatusPosition, SmallFont, bHasStone ? Green : Muted);

    FString Display = FString::Printf(TEXT("Round=%s Potion=%d Stone=%d Sword=%d PotionSlot=%d SwordSlot=%d First=%d Last=%d Slots=%d Selected=%d X=%.0f Y=%.0f Width=%.0f Height=%.0f FooterBottom=%.0f SupplyHP=%d SupplySP=%d SwordBound=%d SwordEquipped=%d PotionSP=%d StaminaSlot=%d RegisteredHP=%d RegisteredSP=%d UsesHP=%d UsesSP=%d MartialPowerScrolls=%d ScrollSlot=%d ProtectionScrolls=%d ProtectionSlot=%d SlashCapsule=%d CapsuleSlot=%d SlashRegistered=%d Gold=%d GridY=%.1f DetailY=%.1f FooterY=%.1f FooterHeight=%.1f ViewWidth=%.0f ViewHeight=%.0f AvailableHP=%d AvailableSP=%d TauntCapsule=%d TauntCapsuleSlot=%d TauntLearned=%d PersonalShield=%d ShieldSlot=%d ShieldBound=%d ShieldEquipped=%d"),
        *InventoryRound.ToString(), PotionCount, bHasStone, bHasSword, PotionSlot + 1, SwordSlot + 1,
        FirstInventoryRow * InventoryColumns + 1,
        FirstInventoryRow * InventoryColumns + VisibleInventorySlots, InventorySlotCount, SelectedInventorySlot + 1,
        Panel.Min.X, Panel.Min.Y, Panel.GetSize().X, Panel.GetSize().Y, FooterTop + FooterHeight, Supplies.X, Supplies.Y, bSwordBound, bSwordEquipped,
        StaminaCount, StaminaSlot + 1, bHealthRegistered, bStaminaRegistered, Uses.X, Uses.Y, ScrollCount, ScrollSlot + 1, ProtectionCount, ProtectionSlot + 1, Controller.HasSkillCapsule(ECoopRPGActiveSkill::Slash), CapsuleSlot + 1, Controller.HasLearnedSkill(ECoopRPGActiveSkill::Slash), Controller.GetSessionGold(),
        GetInventoryGridBounds().Min.Y, DetailTop, FooterTop, FooterHeight, Surface.ClipX, Surface.ClipY, AvailableUses.X, AvailableUses.Y,
        Controller.HasSkillCapsule(ECoopRPGActiveSkill::Taunt), TauntCapsuleSlot + 1, Controller.HasLearnedSkill(ECoopRPGActiveSkill::Taunt),
        bHasShield, ShieldSlot + 1, bShieldBound, bPersonalShield);
    if (LastInventoryDisplay != Display)
    {
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("ExpeditionInventory %s"), *Display);
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
        static const bool bReportStoneBadge = FParse::Param(FCommandLine::Get(), TEXT("CoopCombatTest"));
        if (bReportStoneBadge)
        {
            const FVector2D StatusInk = InkBounds(Surface, StoneStatus, SmallFont);
            UE_LOG(LogCoopRPGEquipmentHUD, Log,
                TEXT("TeamStoneBadge Round=%s Acquired=%d Width=%.0f Height=%.0f BadgeHeight=%.0f PadTop=%.1f PadBottom=%.1f LabelCenter=%.1f BadgeCenter=%.1f"),
                *InventoryRound.ToString(), bHasStone, Surface.ClipX, Surface.ClipY, StatusBounds.GetSize().Y,
                StatusBounds.Min.Y - FooterTop, FooterTop + FooterHeight - StatusBounds.Max.Y,
                StatusPosition.Y + (StatusInk.X + StatusInk.Y) * 0.5, StatusBounds.GetCenter().Y);
        }
#endif
        LastInventoryDisplay = MoveTemp(Display);
    }
}

double FCoopRPGEquipmentPanel::GetInventoryContentOffset() const
{
    return bLobby ? 0.0 : InventoryRecoveryShortcutsHeight;
}

FBox2D FCoopRPGEquipmentPanel::GetInventoryGridBounds() const
{
    const FBox2D Panel = GetPanelBounds();
    const double GridTop = bLobby ? PairedGridY : 66.0;
    const FVector2D Start = Panel.Min + FVector2D(ContentInset, GridTop + GetInventoryContentOffset());
    return FBox2D(Start, FVector2D(Panel.Max.X - ContentInset, Start.Y + InventoryRowPitch + InventorySlotHeight));
}

FBox2D FCoopRPGEquipmentPanel::GetInventoryActionBounds() const
{
    const FBox2D Panel = GetPanelBounds();
    const double Right = Panel.Max.X - ContentInset - 7.0;
    const double Top = Panel.Min.Y + GetInventoryContentOffset();
    const double ActionTop = bLobby ? PairedDetailY + (PairedDetailHeight - 25.0) * 0.5 : 182.0;
    return FBox2D(FVector2D(Right - 73.0, Top + ActionTop), FVector2D(Right, Top + ActionTop + 25.0));
}

FBox2D FCoopRPGEquipmentPanel::GetRecoveryShortcutBounds(bool bStamina) const
{
    const FBox2D Panel = GetPanelBounds();
    const double Left = Panel.Min.X + ContentInset;
    const double Right = Panel.Max.X - ContentInset;
    const double ColumnWidth = (Right - Left - 6.0) * 0.5;
    const double X = bStamina ? Right - ColumnWidth : Left;
    return FBox2D(FVector2D(X, Panel.Min.Y + 46.0), FVector2D(X + ColumnWidth, Panel.Min.Y + 80.0));
}

int32 FCoopRPGEquipmentPanel::FindEmptyRecoveryShortcut(const ACoopRPGPlayerController& Controller, const FVector2D& Pointer) const
{
    if (!CoopRPG::Expedition::IsExpedition(Controller.GetWorld()) || CoopRPG::Expedition::IsLobby(Controller.GetWorld()))
    {
        return INDEX_NONE;
    }

    for (int32 Resource = 0; Resource < 2; ++Resource)
    {
        const bool bStamina = Resource == 1;
        if (Controller.IsRecoveryRegistered(bStamina) && Controller.GetSelectedRecoveryStock(bStamina) == 0
            && GetRecoveryShortcutBounds(bStamina).IsInsideOrOn(Pointer))
        {
            return Resource;
        }
    }
    return INDEX_NONE;
}

FBox2D FCoopRPGEquipmentPanel::GetInventorySlotBounds(int32 VisibleIndex) const
{
    return InventoryTileBounds(GetInventoryGridBounds(), VisibleIndex);
}

void FCoopRPGEquipmentPanel::DrawEquipment(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller)
{
    using namespace CoopRPG::ExpeditionUI;
    const auto* Hero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const bool bEquipped = IsValid(Hero) && Hero->HasMeleeWeapon();
    const bool bShieldEquipped = IsValid(Hero) && Hero->HasShieldEquipped();
    const bool bGuarding = IsValid(Hero) && Hero->IsGuarding();
    const bool bPersonalSword = Controller.IsPersonalSwordEquipped();
    const bool bPersonalShield = Controller.IsPersonalShieldEquipped();
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
        const bool bArmed = (bWeapon && bEquipped) || (bShield && bShieldEquipped);
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
            const TCHAR* Item = TEXT("미장착");
            if (bWeapon && bArmed)
            {
                Item = bPersonalSword ? TEXT("개인 한손검") : TEXT("기본 한손검");
            }
            else if (bShield && bArmed)
            {
                Item = bPersonalShield ? (bGuarding ? TEXT("하급 방패 · 가드 중") : TEXT("하급 방패"))
                    : (bGuarding ? TEXT("기본 방패 · 가드 중") : TEXT("기본 방패"));
            }
            DrawPanelLabel(Surface, Item, Left + 49.0, Y + 29.0, ItemFont, Ink);
        }
        else
        {
            DrawPanelLabel(Surface, TEXT("빈 슬롯"), Left + 49.0, Y + 21.0, HintFont, Muted);
        }
    }
    DrawScrollTrack(HUD);

    if (!LastDisplayedWeaponState.IsSet() || LastDisplayedWeaponState.GetValue() != bEquipped
        || LastDisplayedEquipmentRow != FirstEquipmentRow || bLastDisplayedShield != bShieldEquipped || bLastDisplayedGuard != bGuarding
        || bLastDisplayedPersonalSword != bPersonalSword || bLastDisplayedPersonalShield != bPersonalShield)
    {
        LastDisplayedWeaponState = bEquipped;
        LastDisplayedEquipmentRow = FirstEquipmentRow;
        bLastDisplayedShield = bShieldEquipped;
        bLastDisplayedGuard = bGuarding;
        bLastDisplayedPersonalSword = bPersonalSword;
        bLastDisplayedPersonalShield = bPersonalShield;
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("EquipmentPanelState Equipped=%d Shield=%d First=%d Visible=%d Slots=%d X=%.0f Y=%.0f Width=%.0f Height=%.0f Guard=%d ShieldActionX=%.0f ShieldActionY=%.0f SupplySword=%d SupplyEquipped=%d IssuedShield=%d SupplyActionX=%.0f SupplyActionY=%.0f PersonalShield=%d %s"),
            bEquipped, bShieldEquipped, FirstEquipmentRow + 1, VisibleEquipmentRows, EquipmentRows, Panel.Min.X, Panel.Min.Y,
            Panel.GetSize().X, Panel.GetSize().Y, bGuarding, 0.0, 0.0, 0, 0, 0, 0.0, 0.0, bPersonalShield,
            *CoopRPG::Debug::DescribeLogContext(Hero, ANSI_TO_TCHAR(__FUNCTION__)));
    }
}

void FCoopRPGEquipmentPanel::DrawItemIcon(AHUD& HUD, int32 IconIndex, FVector2D Position, double Size, FLinearColor Tint) const
{
    const float SourceX = 95.0f + IconIndex * 30.0f;
    HUD.DrawTexture(Skin.Get(), Position.X, Position.Y, Size, Size, (SourceX + 0.5f) / AtlasWidth,
        1.5f / AtlasHeight, 27.0f / AtlasWidth, 27.0f / AtlasHeight, Tint);
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

    const auto* State = Controller.GetWorld()->GetGameState<ACoopRPGGameState>();
    const auto* Hero = Cast<ACoopRPGCharacter>(Controller.GetPawn());
    const bool bCombatMap = CoopRPG::Expedition::IsSkirmish(Controller.GetWorld()) || CoopRPG::Expedition::IsBoss(Controller.GetWorld());
    const auto Slash = State && Hero ? ReadAttackHud(*State, *Hero, bCombatMap, true) : FAttackHudStatus{};
    const auto Taunt = State && Hero ? ReadTauntHud(*State, *Hero, bCombatMap) : FAttackHudStatus{};
    const auto JobSkill = ReadJobSkill(Controller);
    const bool bLearned = Controller.HasLearnedSkill(JobSkill);
    const uint8 Binding = Controller.GetActiveSkillSlot(JobSkill);
    const bool bActiveList = SkillGroup == 0;
    const bool bHasGuard = Hero && Hero->HasShieldEquipped();
    constexpr int32 GuardRow = 2;
    const int32 First = FirstSkillRows[SkillGroup];
    const int32 RemainingTenths = Attack.GetRemainingTenths();
    const bool bFirstSlotVisible = bActiveList && First == 0;
    const FIntPoint PotionBonus = Controller.GetPotionCapacityBonus();
    TArray<const TCHAR*, TInlineAllocator<2>> PassiveNames;
    if (PotionBonus.X > 0)
    {
        PassiveNames.Add(TEXT("HP 물약 한도 +1"));
    }
    if (PotionBonus.Y > 0)
    {
        PassiveNames.Add(TEXT("SP 물약 한도 +1"));
    }
    for (int32 Index = 0; Index < VisibleSkillRows; ++Index)
    {
        const int32 Row = First + Index;
        const double Y = List.Min.Y + Index * SkillRowPitch;
        const FBox2D Bounds(FVector2D(Left, Y), FVector2D(Right, Y + SkillRowHeight));
        const bool bJobRow = bActiveList && Row == 1 && JobSkill != ECoopRPGActiveSkill::None;
        const bool bSlashRow = bJobRow && JobSkill == ECoopRPGActiveSkill::Slash;
        const bool bTauntRow = bJobRow && JobSkill == ECoopRPGActiveSkill::Taunt;
        const bool bGuardRow = bActiveList && Row == GuardRow && bHasGuard;
        const bool bActiveSkillRow = bActiveList && (Row == 0 || bJobRow || bGuardRow);
        DrawPanelBox(HUD, Bounds, bActiveSkillRow ? Color(250, 237, 202) : Color(240, 226, 189), Border);
        const double MidY = Bounds.GetCenter().Y;
        const FVector2D Icon(Left + RowInset, MidY - IconSide * 0.5);
        const FBox2D IconBounds(Icon, Icon + FVector2D(IconSide));
        DrawPanelBox(HUD, IconBounds, bActiveSkillRow ? Color(113, 82, 57) : Color(225, 208, 170), Color(193, 165, 115));

        if (!bActiveList && PassiveNames.IsValidIndex(Row))
        {
            DrawRecoveryIcon(HUD, Icon + FVector2D(3.0), IconSide - 6.0);
            DrawPanelLabel(Surface, Fit(Surface, PassiveNames[Row], RowFont, Right - RowInset - TextLeft),
                TextLeft, MidY - 9.0, RowFont, Ink);
            DrawPanelLabel(Surface, TEXT("세션 종료까지"), TextLeft, MidY + 9.0, HintFont, Muted);
            continue;
        }
        if (!bActiveSkillRow)
        {
            Center(TEXT("—"), IconBounds.GetCenter(), HintFont, Muted);
            const TCHAR* Label = !bActiveList && Row == 0 && PassiveNames.IsEmpty() ? TEXT("등록한 패시브 없음") : TEXT("빈 슬롯");
            DrawPanelLabel(Surface, Fit(Surface, Label, HintFont, Right - RowInset - TextLeft), TextLeft, MidY, HintFont, Muted);
            continue;
        }

        const double ChipOffset = (ChipHeight + ChipGap) * 0.5;
        if (bGuardRow)
        {
            const bool bExhausted = Hero->IsGuardExhausted();
            const bool bReady = State && State->IsRoundPlaying() && bCombatMap && Hero->GetHealth() > 0.0f && !bExhausted;
            const TCHAR* Status = bExhausted
                ? (Hero->GetGuardPoints() >= ACoopRPGCharacter::GuardRestartPoints ? TEXT("키 떼기") : TEXT("GP 부족"))
                : Hero->IsGuarding() ? TEXT("가드 중")
                : bReady ? TEXT("사용 가능")
                : Hero->GetHealth() <= 0.0f ? TEXT("행동 불가") : TEXT("준비 대기");
            DrawShieldOutline(Surface, IconBounds, Color(255, 241, 210));
            DrawPanelLabel(Surface, TEXT("가드"), TextLeft, MidY - 9.0, RowFont, Ink);
            DrawPanelLabel(Surface, Fit(Surface, TEXT("방패로 정면 방어"), HintFont, TextWidth),
                TextLeft, MidY + 9.0, HintFont, Muted);
            Chip(TEXT("우클릭"), MidY - ChipOffset, Color(237, 226, 194), Muted);
            Chip(Status, MidY + ChipOffset, bReady ? Color(219, 232, 211) : Color(239, 216, 203),
                bReady ? Color(55, 87, 66) : Color(115, 55, 47));
            continue;
        }

        const auto& SlotStatus = bTauntRow ? Taunt : bSlashRow ? Slash : Attack;
        const bool bReady = SlotStatus.State == EAttackHudState::Ready;
        const bool bCooling = SlotStatus.State == EAttackHudState::Cooling;
        if (bTauntRow)
        {
            DrawTauntGlyph(Surface, IconBounds, SlotStatus);
        }
        else
        {
            Dashboard.DrawBasicAttackGlyph(HUD, Surface, IconBounds, SlotStatus, bSlashRow);
        }

        const FString Name = Fit(Surface, bTauntRow ? TEXT("도발") : bSlashRow ? TEXT("한손검 슬래시") : TEXT("기본 공격"), RowFont, TextWidth);
        const TCHAR* Description = bJobRow && !bLearned ? TEXT("캡슐로 습득")
            : bTauntRow ? TEXT("주변 적 · SP 15") : bSlashRow ? TEXT("최대 3명 · SP 25") : TEXT("무기를 휘둘러 공격");
        const FString Hint = Fit(Surface, Description, HintFont, TextWidth);
        constexpr double LineGap = 4.0;
        const double NameHeight = Measure(Surface, Name, RowFont).Y;
        const double HintHeight = Measure(Surface, Hint, HintFont).Y;
        DrawPanelLabel(Surface, Name, TextLeft, MidY - (HintHeight + LineGap) * 0.5, RowFont, Ink);
        DrawPanelLabel(Surface, Hint, TextLeft, MidY + (NameHeight + LineGap) * 0.5, HintFont, Muted);
        if (!bJobRow)
        {
            Chip(TEXT("슬롯 1"), MidY - ChipOffset, Color(237, 226, 194), Muted);
        }
        else if (bLearned)
        {
            const int32 Controls[] = {2, 3, 0};
            const TCHAR* Labels[] = {TEXT("2"), TEXT("3"), TEXT("해제")};
            for (int32 ControlIndex = 0; ControlIndex < UE_ARRAY_COUNT(Controls); ++ControlIndex)
            {
                const FBox2D Button = GetSkillBindingBounds(Controls[ControlIndex]);
                const bool bSelected = Binding == Controls[ControlIndex] && Binding != 0;
                DrawPanelBox(HUD, Button, bSelected ? Color(65, 87, 63) : Color(239, 224, 189), Border);
                Center(Labels[ControlIndex], Button.GetCenter(), ChipFont, bSelected ? Color(255, 240, 202) : Muted);
            }
        }
        const bool bUsable = bReady && (!bJobRow || Binding != 0);
        const FString Status = bCooling ? FString::Printf(TEXT("쿨다운 %.1f초"), SlotStatus.GetRemainingTenths() / 10.0)
            : bJobRow && !bLearned ? FString(TEXT("미습득"))
            : bJobRow && Binding == 0 ? FString(TEXT("미배정"))
            : FString(bReady ? TEXT("사용 가능") : AttackHudLabel(SlotStatus.State));
        Chip(Status, MidY + (bJobRow && !bLearned ? 0.0 : ChipOffset), bUsable ? Color(219, 232, 211) : Color(239, 216, 203),
            bUsable ? Color(55, 87, 66) : Color(115, 55, 47));
    }
    DrawScrollTrack(HUD);

    if (!State)
    {
        return;
    }
    const FVector2D Bind2 = GetSkillBindingBounds(2).GetCenter();
    const FVector2D Bind3 = GetSkillBindingBounds(3).GetCenter();
    const FVector2D Unbind = GetSkillBindingBounds(0).GetCenter();
    FString Display = FString::Printf(TEXT("Round=%s Group=%s First=%d Visible=%d Rows=%d Slot=%d State=%s Remaining=%.1f Duration=%.3f X=%.0f Y=%.0f Width=%.0f Height=%.0f Slash=%s JobSkill=%s Learned=%d Binding=%d Bind2X=%.0f Bind3X=%.0f UnbindX=%.0f BindingY=%.0f HPUsePassive=%d SPUsePassive=%d GuardVisible=%d"),
        *State->GetRoundId().ToString(), bActiveList ? TEXT("Active") : TEXT("Passive"), First + 1, VisibleSkillRows,
        SkillRows, bFirstSlotVisible ? 1 : 0, bFirstSlotVisible ? AttackHudLogState(Attack.State) : TEXT("Empty"),
        bFirstSlotVisible ? RemainingTenths / 10.0 : 0.0, bFirstSlotVisible ? Attack.Duration : 0.0,
        Panel.Min.X, Panel.Min.Y, Panel.GetSize().X, Panel.GetSize().Y, AttackHudLogState(Slash.State),
        *UEnum::GetValueAsString(JobSkill), bLearned, Binding, Bind2.X, Bind3.X, Unbind.X, Bind3.Y, PotionBonus.X, PotionBonus.Y,
        bActiveList && bHasGuard && First <= GuardRow && GuardRow < First + VisibleSkillRows);
    if (LastSkillsDisplay != Display)
    {
        UE_LOG(LogCoopRPGEquipmentHUD, Log, TEXT("SkillsPanelState %s"), *Display);
        LastSkillsDisplay = MoveTemp(Display);
    }
}

FBox2D FCoopRPGEquipmentPanel::GetSkillGroupBounds(int32 Index) const
{
    const FBox2D Panel = GetPanelBounds();
    const double GroupWidth = FMath::FloorToDouble((Panel.GetSize().X - 2.0 * ContentInset - 6.0) * 0.5);
    const double X = Panel.Min.X + ContentInset + Index * (GroupWidth + 6.0);
    return FBox2D(FVector2D(X, Panel.Min.Y + 50.0),
        FVector2D(Index == 0 ? X + GroupWidth : Panel.Max.X - ContentInset, Panel.Min.Y + 76.0));
}

FBox2D FCoopRPGEquipmentPanel::GetSkillBindingBounds(int32 Control) const
{
    const double ChipRight = GetScrollTrack().Min.X - 15.0;
    const double ChipWidth = Width >= 260.0 ? 85.0 : 66.0;
    const double Y = GetSkillListBounds().Min.Y + (1 - FirstSkillRows[0]) * SkillRowPitch + 11.5;
    constexpr double Gap = 3.0;
    const double Small = (ChipWidth - 2.0 * Gap) * 0.26;
    const double Left = ChipRight - ChipWidth;
    const double X = Control == 2 ? Left : Control == 3 ? Left + Small + Gap : Left + 2.0 * (Small + Gap);
    return FBox2D(FVector2D(X, Y), FVector2D(Control == 0 ? ChipRight : X + Small, Y + 21.0));
}

int32 FCoopRPGEquipmentPanel::FindSkillBinding(const FVector2D& Pointer) const
{
    if (SkillGroup != 0 || FirstSkillRows[0] > 1)
    {
        return INDEX_NONE;
    }
    const int32 Controls[] = {2, 3, 0};
    for (const int32 Control : Controls)
    {
        if (GetSkillBindingBounds(Control).IsInsideOrOn(Pointer))
        {
            return Control;
        }
    }
    return INDEX_NONE;
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
    PressedSkillBinding = INDEX_NONE;
    FirstSkillRows[0] = 0;
    FirstSkillRows[1] = 0;
    LastSkillsDisplay.Reset();
}

FBox2D FCoopRPGEquipmentPanel::GetEquipmentListBounds() const
{
    const FBox2D Panel = GetPanelBounds();
    const double Top = Panel.Min.Y + 55.0;
    return FBox2D(FVector2D(Panel.Min.X + ContentInset, Top),
        FVector2D(Panel.Max.X - ContentInset, Top + (VisibleEquipmentRows - 1) * EquipmentRowPitch + EquipmentRowHeight));
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

void FCoopRPGEquipmentPanel::ResetInventoryView()
{
    FirstInventoryRow = 0;
    SelectedInventorySlot = INDEX_NONE;
    SelectedInventoryItem = EInventoryItem::None;
    PressedInventorySlot = INDEX_NONE;
    ScrollDragOffset.Reset();
    LastInventoryDisplay.Reset();
}
