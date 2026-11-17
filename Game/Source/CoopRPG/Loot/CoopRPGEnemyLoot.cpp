#include "CoopRPGEnemyLoot.h"

#include "CoopRPG/Debug/CoopRPGLogContext.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGEnemyLoot, Log, All);

ACoopRPGEnemyLoot::ACoopRPGEnemyLoot()
{
    bReplicates = true;
    bAlwaysRelevant = true;

    auto* LootVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootVisual"));
    SetRootComponent(LootVisual);
    LootVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    LootVisual->SetStaticMesh(Cube.Object);
}

void ACoopRPGEnemyLoot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGEnemyLoot, DropId);
}

void ACoopRPGEnemyLoot::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        DropId = FGuid::NewGuid();
    }
}

void ACoopRPGEnemyLoot::OnRep_DropId()
{
    UE_LOG(LogCoopRPGEnemyLoot, Log, TEXT("EnemyLootReplicated Actor=%s DropId=%s %s"),
        *GetName(), *DropId.ToString(), *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}
