#include "CoopRPGSharedChest.h"

#include "CoopRPG/GameFramework/CoopRPGStatProgress.h"

#include "CoopRPG/Debug/CoopRPGLogContext.h"
#include "CoopRPG/GameFramework/CoopRPGGameState.h"
#include "CoopRPG/GameFramework/CoopRPGExpeditionMaps.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGChest, Log, All);

namespace
{
    constexpr TCHAR ChestBodyAsset[] = TEXT("/Game/ThirdParty/CustomizableTreasureChest/Meshes/Meshes/SK_TreasureChest_Base.SK_TreasureChest_Base");
    constexpr TCHAR ChestLockAsset[] = TEXT("/Game/ThirdParty/CustomizableTreasureChest/Meshes/Meshes/SK_TreasureChest_Lock-1.SK_TreasureChest_Lock-1");
    constexpr TCHAR ChestOpenAsset[] = TEXT("/Game/ThirdParty/CustomizableTreasureChest/Animations/Animations/anim_TreasureChest_Open.anim_TreasureChest_Open");
    constexpr double ForestChestScale = 0.23;

    USkeletalMeshComponent* AddChestVisual(AActor& Owner, USceneComponent& Parent, USkeletalMesh& Mesh)
    {
        auto* Visual = NewObject<USkeletalMeshComponent>(&Owner);
        Visual->SetupAttachment(&Parent);
        Visual->SetSkeletalMeshAsset(&Mesh);
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visual->SetCanEverAffectNavigation(false);
        Visual->RegisterComponent();
        return Visual;
    }
}

ACoopRPGSharedChest::ACoopRPGSharedChest()
{
    bReplicates = true;
    bAlwaysRelevant = true;

    auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh"));
    SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetRelativeScale3D(FVector(0.9f, 0.6f, 0.48f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Mesh->SetStaticMesh(Cube.Object);
}

void ACoopRPGSharedChest::BeginPlay()
{
    Super::BeginPlay();
    const FString MapName = UGameplayStatics::GetCurrentLevelName(this);
    if (GetNetMode() != NM_DedicatedServer
        && (MapName == TEXT("L_Round_01") || MapName == CoopRPG::Expedition::SkirmishMap
            || MapName == CoopRPG::Expedition::BossMap))
    {
        InitializeForestPresentation();
    }
}

void ACoopRPGSharedChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGSharedChest, RecoveryStock);
    DOREPLIFETIME(ACoopRPGSharedChest, StaminaRecoveryStock);
    DOREPLIFETIME(ACoopRPGSharedChest, WeaponStock);
    DOREPLIFETIME(ACoopRPGSharedChest, MartialPowerScrollStock);
    DOREPLIFETIME(ACoopRPGSharedChest, ProtectionScrollStock);
    DOREPLIFETIME(ACoopRPGSharedChest, SlashCapsuleStock);
    DOREPLIFETIME(ACoopRPGSharedChest, TauntCapsuleStock);
    DOREPLIFETIME(ACoopRPGSharedChest, PersonalShieldStock);
    DOREPLIFETIME(ACoopRPGSharedChest, FirstOpenedAt);
}

bool ACoopRPGSharedChest::TryTakeRecovery(bool bStamina)
{
    return TryTakeStock(bStamina ? StaminaRecoveryStock : RecoveryStock);
}

bool ACoopRPGSharedChest::TryTakeWeapon()
{
    return TryTakeStock(WeaponStock);
}

int32 ACoopRPGSharedChest::GetStatScrollStock(ECoopRPGScrollStat Stat) const
{
    return Stat == ECoopRPGScrollStat::Protection ? ProtectionScrollStock : MartialPowerScrollStock;
}

bool ACoopRPGSharedChest::TryTakeStatScroll(ECoopRPGScrollStat Stat)
{
    return TryTakeStock(Stat == ECoopRPGScrollStat::Protection ? ProtectionScrollStock : MartialPowerScrollStock);
}

bool ACoopRPGSharedChest::TryTakeSlashCapsule()
{
    return TryTakeStock(SlashCapsuleStock);
}

bool ACoopRPGSharedChest::TryTakeTauntCapsule()
{
    return TryTakeStock(TauntCapsuleStock);
}

void ACoopRPGSharedChest::RecordFirstOpen()
{
    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    if (!HasAuthority() || FirstOpenedAt > 0.0 || !State)
    {
        return;
    }

    FirstOpenedAt = State->GetServerWorldTimeSeconds();
    UE_LOG(LogCoopRPGChest, Log, TEXT("ChestLidOpened Round=%s Chest=%s StartedAt=%.3f"),
        *State->GetRoundId().ToString(), *GetName(), FirstOpenedAt);
    ForceNetUpdate();
    OnRep_FirstOpenedAt();
}

bool ACoopRPGSharedChest::TryTakePersonalShield()
{
    return TryTakeStock(PersonalShieldStock);
}

void ACoopRPGSharedChest::SetPersonalShieldStock(int32 Stock)
{
    if (HasAuthority())
    {
        PersonalShieldStock = Stock;
        ForceNetUpdate();
    }
}

void ACoopRPGSharedChest::SetBossRewardStock()
{
    check(HasAuthority());
    RecoveryStock = 0;
    StaminaRecoveryStock = 0;
    WeaponStock = 0;
    MartialPowerScrollStock = 1;
    ProtectionScrollStock = 1;
    SlashCapsuleStock = 0;
    TauntCapsuleStock = 0;
    PersonalShieldStock = 1;
    ForceNetUpdate();
}

bool ACoopRPGSharedChest::TryTakeStock(int32& Stock)
{
    if (!HasAuthority() || Stock <= 0)
    {
        return false;
    }

    --Stock;
    ForceNetUpdate();
    return true;
}

void ACoopRPGSharedChest::InitializeForestPresentation()
{
    if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(FString(ChestBodyAsset))))
    {
        return;
    }

    auto* Body = LoadObject<USkeletalMesh>(nullptr, ChestBodyAsset);
    auto* Lock = LoadObject<USkeletalMesh>(nullptr, ChestLockAsset);
    auto* OpenAnimation = LoadObject<UAnimSequence>(nullptr, ChestOpenAsset);
    if (!Body || !Lock || !OpenAnimation)
    {
        return;
    }

    USceneComponent* Root = GetRootComponent();
    const double Ground = Root->Bounds.GetBox().Min.Z;
    ChestBody = AddChestVisual(*this, *Root, *Body);
    ChestBody->SetUsingAbsoluteScale(true);
    ChestBody->SetWorldScale3D(FVector(ForestChestScale));
    ChestBody->SetRelativeRotation(FRotator(0.0, 90.0, 0.0));
    const FVector Origin = GetActorLocation();
    ChestBody->SetWorldLocation(FVector(Origin.X, Origin.Y, Ground));
    ChestBody->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    ChestBody->SetAnimation(OpenAnimation);
    ChestBody->SetPosition(0.0f, false);

    auto* LockVisual = AddChestVisual(*this, *ChestBody, *Lock);
    LockVisual->SetLeaderPoseComponent(ChestBody);
    Root->SetHiddenInGame(true, false);

    UE_LOG(LogCoopRPGChest, Log, TEXT("ForestChestPresentationReady Body=%s Lock=%s Scale=%.2f Open=%d"),
        *Body->GetName(), *Lock->GetName(), ForestChestScale, FirstOpenedAt > 0.0);
    OnRep_FirstOpenedAt();
}

void ACoopRPGSharedChest::OnRep_FirstOpenedAt()
{
    if (!ChestBody || FirstOpenedAt <= 0.0)
    {
        return;
    }

    const auto* State = GetWorld()->GetGameState<ACoopRPGGameState>();
    const double Now = State ? State->GetServerWorldTimeSeconds() : FirstOpenedAt;
    const float Length = ChestBody->GetSingleNodeInstance()->GetLength();
    const float Position = static_cast<float>(FMath::Clamp(Now - FirstOpenedAt, 0.0, static_cast<double>(Length)));
    ChestBody->SetPosition(Position, false);
    if (Position < Length)
    {
        ChestBody->Play(false);
    }
    UE_LOG(LogCoopRPGChest, Log, TEXT("ChestLidPresented Chest=%s StartedAt=%.3f Position=%.3f Length=%.3f"),
        *GetName(), FirstOpenedAt, Position, Length);
}

void ACoopRPGSharedChest::OnRep_RecoveryStock()
{
    UE_LOG(LogCoopRPGChest, Log, TEXT("ChestStockReplicated Actor=%s Stock=%d %s"),
        *GetNameSafe(this), RecoveryStock, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}

void ACoopRPGSharedChest::OnRep_WeaponStock()
{
    UE_LOG(LogCoopRPGChest, Log, TEXT("WeaponStockReplicated Actor=%s Stock=%d %s"),
        *GetNameSafe(this), WeaponStock, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}
