#pragma once

#include "CoreMinimal.h"
#include "CoopRPGLobbyStorage.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopRPGSessionEconomy.generated.h"

namespace CoopRPG::Shop
{
    inline constexpr int32 BossReward = 30;
    inline constexpr TCHAR InsufficientGoldMessage[] = TEXT("골드가 부족해요.");
    inline constexpr ECoopRPGStorageItem Offers[] = {ECoopRPGStorageItem::HealthPotion, ECoopRPGStorageItem::SlashCapsule};

    inline int32 BuyPrice(ECoopRPGStorageItem Item)
    {
        switch (Item)
        {
        case ECoopRPGStorageItem::HealthPotion: return 10;
        case ECoopRPGStorageItem::SlashCapsule: return 21;
        default: return 0;
        }
    }

    inline int32 SellPrice(ECoopRPGStorageItem Item) { return BuyPrice(Item) / 2; }
    inline int32 UnitPrice(ECoopRPGStorageItem Item, bool bBuy) { return bBuy ? BuyPrice(Item) : SellPrice(Item); }

    struct FQuantityLimit
    {
        int32 Maximum;
        const TCHAR* Reason;
        const TCHAR* Message;
    };

    inline FQuantityLimit QuantityLimit(ECoopRPGStorageItem Item, bool bBuy, int32 Held, int32 Gold,
        int32 BagRoom)
    {
        const int32 Price = UnitPrice(Item, bBuy);
        if (Price <= 0)
        {
            return {0, TEXT("NotForTrade"), TEXT("상점에서 거래할 수 없는 품목이에요.")};
        }
        if (!bBuy)
        {
            const int32 GoldRoom = (MAX_int32 - Gold) / Price;
            return Held <= GoldRoom
                ? FQuantityLimit{Held, TEXT("NoPersonalItem"), TEXT("판매할 개인 물품 수량이 부족해요.")}
                : FQuantityLimit{GoldRoom, TEXT("GoldLimit"), TEXT("골드를 더 보유할 수 없어요.")};
        }

        const bool bCapsule = Item == ECoopRPGStorageItem::SlashCapsule;
        if (bCapsule && Held > 0)
        {
            return {0, TEXT("CapsuleOwned"), TEXT("슬래시 캡슐을 이미 갖고 있어요.")};
        }
        const int32 Capacity = bCapsule ? FMath::Min(1, BagRoom) : BagRoom;
        const int32 Affordable = Gold / Price;
        if (Capacity <= Affordable)
        {
            if (bCapsule && Capacity > 0)
            {
                return {Capacity, TEXT("CapsuleLimit"), TEXT("캡슐은 한 개만 구매할 수 있어요.")};
            }
            return {Capacity, TEXT("BagFull"), CoopRPG::Bag::FullMessage};
        }
        return {Affordable, TEXT("InsufficientGold"), InsufficientGoldMessage};
    }
}

UCLASS()
class UCoopRPGSessionEconomy : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    int32 GetGold(const FUniqueNetIdRepl& OwnerId) const { return Balances.FindRef(OwnerId); }

    bool AwardBossClear(const FUniqueNetIdRepl& OwnerId, FGuid RoundId)
    {
        TSet<FGuid>& Claimed = BossClaims.FindOrAdd(OwnerId);
        if (Claimed.Contains(RoundId))
        {
            return false;
        }
        Claimed.Add(RoundId);
        int32& Gold = Balances.FindOrAdd(OwnerId);
        Gold += FMath::Min(CoopRPG::Shop::BossReward, MAX_int32 - Gold);
        return true;
    }

    bool TryChangeGold(const FUniqueNetIdRepl& OwnerId, int32 Delta)
    {
        const int64 Next = static_cast<int64>(GetGold(OwnerId)) + Delta;
        if (Next < 0 || Next > MAX_int32)
        {
            return false;
        }
        Balances.Add(OwnerId, static_cast<int32>(Next));
        return true;
    }

private:
    TMap<FUniqueNetIdRepl, int32> Balances;
    TMap<FUniqueNetIdRepl, TSet<FGuid>> BossClaims;
};
