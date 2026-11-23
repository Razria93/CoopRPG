#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

namespace CoopRPG::ExpeditionUI { struct FAttackHudStatus; }

class FCoopRPGExpeditionDashboard;
class ACoopRPGPlayerController;
class AHUD;
class UCanvas;
class UTexture2D;
struct FInputKeyEventArgs;
enum class ECoopRPGScrollStat : uint8;

class FCoopRPGEquipmentPanel
{
public:
    FCoopRPGEquipmentPanel();
    ~FCoopRPGEquipmentPanel();

    void Draw(AHUD& HUD, UCanvas& Surface, ACoopRPGPlayerController& Controller, const FVector2D& ViewSize,
        const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack, const FCoopRPGExpeditionDashboard& Dashboard);
    FBox2D GetVisibleBounds() const;
    void DrawPanelFrame(AHUD& HUD, const FBox2D& Bounds) const;
    void Hide();
    bool HandlePrimaryButton(ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    bool HandleWheel(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    void ReleasePointer();

private:
    enum class EInventoryItem : uint8 { None, Potion, StaminaPotion, Sword, MartialPowerScroll, ProtectionScroll, SlashCapsule };
    using FInventoryItems = TArray<EInventoryItem, TInlineAllocator<6>>;

    static bool IsStatScroll(EInventoryItem Item);
    static ECoopRPGScrollStat ToScrollStat(EInventoryItem Item);
    void UpdateInventorySelection(const FInventoryItems& Items);

    FBox2D GetTabBounds(int32 Index) const;
    FBox2D GetPanelBounds() const;
    int32 FindTab(const FVector2D& Pointer) const;
    void DrawRegion(AHUD& HUD, const FBox2D& Bounds, int32 Row, float SourceX, float SourceWidth) const;

    void DrawStats(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller);
    void DrawAtlas(AHUD& HUD, const FBox2D& Bounds, const FBox2D& Source) const;
    void DrawInventory(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller);
    FBox2D GetInventoryGridBounds() const;
    FBox2D GetInventoryActionBounds() const;
    FBox2D GetInventorySlotBounds(int32 VisibleIndex) const;
    void DrawEquipment(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller);
    void DrawItemIcon(AHUD& HUD, int32 IconIndex, FVector2D Position, double Size, FLinearColor Tint) const;
    void DrawSkills(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller,
        const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack, const FCoopRPGExpeditionDashboard& Dashboard);
    FBox2D GetSkillGroupBounds(int32 Index) const;
    int32 FindSkillGroup(const FVector2D& Pointer) const;
    FBox2D GetSkillListBounds() const;
    void ResetSkillsView();
    FBox2D GetEquipmentListBounds() const;
    FBox2D GetEquipmentActionBounds(double RowY) const;
    bool IsShieldActionAt(const FVector2D& Pointer) const;
    bool IsScrollableTab() const;
    FIntPoint GetScrollRowCounts() const;
    int32 GetFirstScrollRow() const;
    int32& GetFirstScrollRow();
    FBox2D GetScrollContentBounds() const;
    FBox2D GetScrollTrack() const;
    FBox2D GetScrollThumb() const;
    void DrawScrollTrack(AHUD& HUD) const;
    int32 FindInventorySlot(const FVector2D& Pointer) const;
    void DragScrollTrack(double PointerY);
    void ResetInventoryView();

private:
    TStrongObjectPtr<UTexture2D> Skin;
    FVector2D Anchor = FVector2D::ZeroVector;
    float Width = 0.0f;
    float ButtonHeight = 0.0f;
    float ContentInset = 14.0f;
    int32 ActiveTab = INDEX_NONE;
    int32 PressedTab = INDEX_NONE;
    int32 FirstInventoryRow = 0;
    int32 SelectedInventorySlot = INDEX_NONE;
    EInventoryItem SelectedInventoryItem = EInventoryItem::None;
    int32 PressedInventorySlot = INDEX_NONE;
    EInventoryItem PressedActionItem = EInventoryItem::None;
    TOptional<bool> PressedInventoryAction;
    int32 FirstEquipmentRow = 0;
    int32 SkillGroup = 0;
    int32 PressedSkillGroup = INDEX_NONE;
    int32 FirstSkillRows[2] = {};
    FString LastSkillsDisplay;
    FString LastStatsDisplay;
    int32 LastDisplayedEquipmentRow = INDEX_NONE;
    TOptional<double> ScrollDragOffset;
    FGuid InventoryRound;
    FString LastInventoryDisplay;
    bool bInitialized = false;
    bool bVisible = false;
    bool bOwnsPrimaryButton = false;
    TOptional<bool> LastDisplayedWeaponState;
    TOptional<bool> PressedShieldAction;
    bool bLastDisplayedShield = false;
    bool bLastDisplayedGuard = false;
};
