#pragma once

#include "CoreMinimal.h"
#include "CoopRPGLobbyStorage.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopRPGSessionEconomy.generated.h"

namespace CoopRPG::Shop
{
    inline constexpr int32 BossReward = 30;
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
