#include "CoopRPGLobbyNpc.h"

#include "Animation/AnimSequence.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGLobbyNpc, Log, All);

namespace
{
    constexpr double LobbyServiceRange = 205.0;
}

ACoopRPGLobbyNpc::ACoopRPGLobbyNpc()
{
    bReplicates = true;
    Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("NpcMesh"));
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Mesh->SetupAttachment(RootComponent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACoopRPGLobbyNpc::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACoopRPGLobbyNpc, Service);
}

void ACoopRPGLobbyNpc::InitializeService(ECoopRPGLobbyService InService)
{
    check(HasAuthority());
    Service = InService;
    OnRep_Service();
}

const TCHAR* ACoopRPGLobbyNpc::GetServiceLabel() const
{
    switch (Service)
    {
    case ECoopRPGLobbyService::Storage: return TEXT("창고 관리인");
    case ECoopRPGLobbyService::Shop: return TEXT("상인");
    case ECoopRPGLobbyService::Job: return TEXT("직업 교관");
    default: return TEXT("");
    }
}

bool ACoopRPGLobbyNpc::IsInRange(const APawn& Player) const
{
    const FVector Offset = Player.GetActorLocation() - GetActorLocation();
    return GetWorld() == Player.GetWorld() && FMath::Abs(Offset.Z) <= 160.0
        && Offset.SizeSquared2D() <= FMath::Square(LobbyServiceRange);
}

ACoopRPGLobbyNpc* ACoopRPGLobbyNpc::FindClosest(const APawn& Player)
{
    ACoopRPGLobbyNpc* Closest = nullptr;
    double BestDistance = TNumericLimits<double>::Max();
    for (TActorIterator<ACoopRPGLobbyNpc> It(Player.GetWorld()); It; ++It)
    {
        const double Distance = FVector::DistSquared2D(Player.GetActorLocation(), It->GetActorLocation());
        if (Distance < BestDistance && It->GetService() != ECoopRPGLobbyService::None && It->IsInRange(Player))
        {
            Closest = *It;
            BestDistance = Distance;
        }
    }
    return Closest;
}

void ACoopRPGLobbyNpc::OnRep_Service()
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    const bool bAnimal = Service == ECoopRPGLobbyService::Storage;
    const TCHAR* MeshPath = bAnimal
        ? TEXT("/Game/ThirdParty/RPGHeroSquad/Mesh/Character/SK_DogPolyart.SK_DogPolyart")
        : TEXT("/Game/ThirdParty/RPGHeroSquad/Mesh/Character/SK_RPGHeroPolyart.SK_RPGHeroPolyart");
    if (USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, MeshPath))
    {
        Mesh->SetSkeletalMeshAsset(Asset);
        const TCHAR* IdlePath = bAnimal
            ? TEXT("/Game/ThirdParty/RPGHeroSquad/Animation/AnimalHero/Anim_Idle_Battle_AnimalHero.Anim_Idle_Battle_AnimalHero")
            : TEXT("/Game/ThirdParty/RPGHeroSquad/Animation/RPGHero/Anim_Idle_RPGHero.Anim_Idle_RPGHero");
        auto* Idle = LoadObject<UAnimSequence>(nullptr, IdlePath);
        const bool bIdle = Idle && Idle->GetSkeleton() == Asset->GetSkeleton();
        if (bIdle)
        {
            Mesh->PlayAnimation(Idle, true);
        }
        UE_LOG(LogCoopRPGLobbyNpc, Log, TEXT("LobbyNpcPresentation Service=%s Mesh=%s Idle=%d"),
            *UEnum::GetValueAsString(Service), *GetNameSafe(Asset), bIdle);
        const FBoxSphereBounds Bounds = Asset->GetImportedBounds();
        const float Height = Service == ECoopRPGLobbyService::Job ? 164.0f : 139.0f;
        const float Scale = Height / FMath::Max(2.0f, static_cast<float>(2.0 * Bounds.BoxExtent.Z));
        Mesh->SetRelativeScale3D(FVector(Scale));
        Mesh->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -(Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale),
            FRotator(0.0, -90.0, 0.0));
        if (Service == ECoopRPGLobbyService::Shop)
        {
            Mesh->SetRelativeRotation(FRotator(0.0, -115.0, 0.0));
        }
    }
}
