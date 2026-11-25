#pragma once

#include "CoreMinimal.h"
#include "CoopRPGLobbyNpc.h"
#include "UObject/StrongObjectPtr.h"

namespace CoopRPG::ExpeditionUI { struct FAttackHudStatus; }

class FCoopRPGExpeditionDashboard;
class ACoopRPGPlayerController;
class AHUD;
class UCanvas;
class UTexture2D;
struct FInputKeyEventArgs;
enum class ECoopRPGScrollStat : uint8;
enum class ECoopRPGActiveSkill : uint8;

class FCoopRPGEquipmentPanel
{
public:
    FCoopRPGEquipmentPanel();
    ~FCoopRPGEquipmentPanel();

    void Draw(AHUD& HUD, UCanvas& Surface, ACoopRPGPlayerController& Controller, const FVector2D& ViewSize,
        const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack, const FCoopRPGExpeditionDashboard& Dashboard);
    FBox2D GetVisibleBounds() const;
    FBox2D GetServiceBounds() const;
    void DrawPanelFrame(AHUD& HUD, const FBox2D& Bounds) const;
    void DrawRecoveryIcon(AHUD& HUD, FVector2D Position, double Size) const;
    void Hide();
    bool HandlePrimaryButton(ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    bool HandleWheel(const ACoopRPGPlayerController& Controller, const FInputKeyEventArgs& Input);
    void ReleasePointer();

private:
    enum class EInventoryItem : uint8 { None, Potion, StaminaPotion, Sword, MartialPowerScroll, ProtectionScroll, SlashCapsule, TauntCapsule, PersonalShield };
    struct FInventoryStack
    {
        EInventoryItem Item = EInventoryItem::None;
        int32 Count = 0;
        int32 Index = 0;
    };
    using FInventoryItems = TArray<FInventoryStack, TInlineAllocator<16>>;

    static ECoopRPGActiveSkill ToCapsuleSkill(EInventoryItem Item);
    static bool IsStatScroll(EInventoryItem Item);
    static ECoopRPGScrollStat ToScrollStat(EInventoryItem Item);
    void UpdateInventorySelection(const FInventoryItems& Items);

    FBox2D GetTabBounds(int32 Index) const;
    FBox2D GetPanelBounds() const;
    int32 FindTab(const FVector2D& Pointer) const;
    void DrawRegion(AHUD& HUD, const FBox2D& Bounds, int32 Row, float SourceX, float SourceWidth) const;

    void LogLayout(const FVector2D& ViewSize);
    void DrawStats(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller);
    void DrawStorage(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller);
    void DrawShop(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller);
    void DrawJob(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller,
        const FCoopRPGExpeditionDashboard& Dashboard);
    FBox2D GetJobChoiceBounds(int32 Index) const;
    FBox2D GetJobButtonBounds(int32 Index) const;
    int32 FindJobControl(const FVector2D& Pointer) const;
    void HandleShopControl(ACoopRPGPlayerController& Controller, int32 Control);
    FBox2D GetShopControlBounds(int32 Control) const;
    int32 FindShopControl(const FVector2D& Pointer) const;
    int32 FindStorageControl(const FVector2D& Pointer) const;
    int32 GetBagStorageItem(const ACoopRPGPlayerController& Controller) const;
    void DrawAtlas(AHUD& HUD, const FBox2D& Bounds, const FBox2D& Source) const;
    void DrawInventory(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller);
    double GetInventoryContentOffset() const;
    FBox2D GetInventoryGridBounds() const;
    FBox2D GetInventoryActionBounds() const;
    FBox2D GetRecoveryShortcutBounds(bool bStamina) const;
    int32 FindEmptyRecoveryShortcut(const ACoopRPGPlayerController& Controller, const FVector2D& Pointer) const;
    FBox2D GetInventorySlotBounds(int32 VisibleIndex) const;
    void DrawEquipment(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller);
    void DrawItemIcon(AHUD& HUD, int32 IconIndex, FVector2D Position, double Size, FLinearColor Tint) const;
    void DrawSkills(AHUD& HUD, UCanvas& Surface, const ACoopRPGPlayerController& Controller,
        const CoopRPG::ExpeditionUI::FAttackHudStatus& Attack, const FCoopRPGExpeditionDashboard& Dashboard);
    FBox2D GetSkillGroupBounds(int32 Index) const;
    FBox2D GetSkillBindingBounds(int32 Control) const;
    int32 FindSkillBinding(const FVector2D& Pointer) const;
    int32 FindSkillGroup(const FVector2D& Pointer) const;
    FBox2D GetSkillListBounds() const;
    void ResetSkillsView();
    FBox2D GetEquipmentListBounds() const;
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
    FVector2D Viewport = FVector2D::ZeroVector;
    float Width = 0.0f;
    float ButtonHeight = 0.0f;
    float ContentInset = 14.0f;
    int32 ActiveTab = INDEX_NONE;
    bool bLobby = false;
    ECoopRPGLobbyService ActiveService = ECoopRPGLobbyService::None;
    int32 PressedJobControl = INDEX_NONE;
    FString LastServiceLayout;
    FString LastJobDisplay;
    bool bPersonalStorage = false;
    int32 SelectedStorageItem = INDEX_NONE;
    bool bStorageFromBag = false;
    TArray<int32, TInlineAllocator<8>> VisibleStorageItems;
    int32 FirstStorageRow = 0;
    int32 StorageRows = 0;
    int32 PressedStorageControl = INDEX_NONE;
    int32 PressedStorageItem = INDEX_NONE;
    FString LastStorageDisplay;
    bool bShopBuying = true;
    int32 SelectedShopItem = INDEX_NONE;
    int32 ShopQuantity = 1;
    TArray<int32, TInlineAllocator<8>> ShopRows;
    int32 FirstShopRow = 0;
    int32 PressedShopControl = INDEX_NONE;
    FString LastShopDisplay;
    int32 PressedTab = INDEX_NONE;
    int32 FirstInventoryRow = 0;
    int32 SelectedInventorySlot = INDEX_NONE;
    EInventoryItem SelectedInventoryItem = EInventoryItem::None;
    int32 SelectedInventoryStack = 0;
    int32 PressedInventorySlot = INDEX_NONE;
    int32 PressedEmptyRecovery = INDEX_NONE;
    EInventoryItem PressedActionItem = EInventoryItem::None;
    TOptional<bool> PressedInventoryAction;
    int32 FirstEquipmentRow = 0;
    int32 SkillGroup = 0;
    int32 PressedSkillGroup = INDEX_NONE;
    int32 PressedSkillBinding = INDEX_NONE;
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
    bool bLastDisplayedShield = false;
    bool bLastDisplayedGuard = false;
    bool bLastDisplayedPersonalSword = false;
    bool bLastDisplayedPersonalShield = false;
};
