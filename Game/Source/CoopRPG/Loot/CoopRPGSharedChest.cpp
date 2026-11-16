#include "CoopRPGSharedChest.h"

#include "CoopRPG/Debug/CoopRPGLogContext.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGChest, Log, All);

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

void ACoopRPGSharedChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGSharedChest, RecoveryStock);
}

bool ACoopRPGSharedChest::TryTakeRecovery()
{
    if (!HasAuthority() || RecoveryStock <= 0)
    {
        return false;
    }

    --RecoveryStock;
    ForceNetUpdate();
    return true;
}

void ACoopRPGSharedChest::OnRep_RecoveryStock()
{
    UE_LOG(LogCoopRPGChest, Log, TEXT("ChestStockReplicated Actor=%s Stock=%d %s"),
        *GetNameSafe(this), RecoveryStock, *CoopRPG::Debug::DescribeLogContext(this, ANSI_TO_TCHAR(__FUNCTION__)));
}
