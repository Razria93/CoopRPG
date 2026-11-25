#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoopRPGLobbyNpc.generated.h"

class USkeletalMeshComponent;

UENUM()
enum class ECoopRPGLobbyService : uint8
{
    None,
    Storage,
    Shop,
    Job
};

UCLASS()
class ACoopRPGLobbyNpc : public AActor
{
    GENERATED_BODY()

public:
    ACoopRPGLobbyNpc();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void InitializeService(ECoopRPGLobbyService InService);
    ECoopRPGLobbyService GetService() const { return Service; }
    const TCHAR* GetServiceLabel() const;
    bool IsInRange(const APawn& Player) const;
    static ACoopRPGLobbyNpc* FindClosest(const APawn& Player);

private:
    UFUNCTION()
    void OnRep_Service();

    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> Mesh;

    UPROPERTY(ReplicatedUsing = OnRep_Service)
    ECoopRPGLobbyService Service = ECoopRPGLobbyService::None;
};
