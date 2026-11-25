#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopRPGLobbyStorage.generated.h"

UENUM()
enum class ECoopRPGStorageItem : uint8
{
    HealthPotion,
    StaminaPotion,
    UnboundSword,
    BoundSword,
    MartialScroll,
    ProtectionScroll,
    SlashCapsule,
    TauntCapsule,
    UnboundShield,
    BoundShield,
    Count UMETA(Hidden)
};

namespace CoopRPG::Bag
{
    inline constexpr int32 SlotCount = 16;
    inline constexpr TCHAR FullMessage[] = TEXT("가방에 빈자리가 없어요.");

    constexpr int32 StackSize(ECoopRPGStorageItem Item)
    {
        switch (Item)
        {
        case ECoopRPGStorageItem::HealthPotion:
        case ECoopRPGStorageItem::StaminaPotion:
        case ECoopRPGStorageItem::MartialScroll:
        case ECoopRPGStorageItem::ProtectionScroll:
            return 10;
        default:
            return 1;
        }
    }

    constexpr int32 SlotsUsed(ECoopRPGStorageItem Item, int32 Count)
    {
        return Count > 0 ? (Count - 1) / StackSize(Item) + 1 : 0;
    }

    constexpr int32 RoomFor(ECoopRPGStorageItem Item, int32 Held, int32 UsedSlots)
    {
        if (UsedSlots > SlotCount)
        {
            return 0;
        }

        const int32 Stack = StackSize(Item);
        const int32 PartialStackRoom = (Stack - Held % Stack) % Stack;
        return (SlotCount - UsedSlots) * Stack + PartialStackRoom;
    }
}

USTRUCT()
struct FCoopRPGStorageStock
{
    GENERATED_BODY()

    static constexpr int32 ItemCount = static_cast<int32>(ECoopRPGStorageItem::Count);

    FCoopRPGStorageStock() { Counts.Init(0, ItemCount); }

    int32 Get(ECoopRPGStorageItem Item) const
    {
        const int32 Index = static_cast<int32>(Item);
        return Counts.IsValidIndex(Index) ? Counts[Index] : 0;
    }

    void Adjust(ECoopRPGStorageItem Item, int32 Delta) { Counts[static_cast<int32>(Item)] += Delta; }

private:
    UPROPERTY()
    TArray<int32> Counts;
};

UCLASS()
class UCoopRPGLobbyStorage : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    const FCoopRPGStorageStock& GetShared() const { return Shared; }
    const FCoopRPGStorageStock* FindPersonal(const FUniqueNetIdRepl& Owner) const { return Personal.Find(Owner); }
    FCoopRPGStorageStock& EditShared() { return Shared; }
    FCoopRPGStorageStock& EditPersonal(const FUniqueNetIdRepl& Owner) { return Personal.FindOrAdd(Owner); }

private:
    FCoopRPGStorageStock Shared;
    TMap<FUniqueNetIdRepl, FCoopRPGStorageStock> Personal;
};
