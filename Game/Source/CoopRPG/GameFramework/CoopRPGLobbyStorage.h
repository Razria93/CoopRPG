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
    Count UMETA(Hidden)
};

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
