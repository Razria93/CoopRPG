#include "CoopRPGHeroAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "GameFramework/Pawn.h"

const FName UCoopRPGHeroAnimInstance::MeleeSlot(TEXT("DefaultSlot"));

struct FCoopRPGHeroAnimProxy final : FAnimInstanceProxy
{
    explicit FCoopRPGHeroAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        Slot.SlotName = UCoopRPGHeroAnimInstance::MeleeSlot;
        Slot.bAlwaysUpdateSourcePose = true;
        Slot.Source.SetLinkNode(&Movement);
    }

    virtual FAnimNode_Base* GetCustomRootNode() override { return &Slot; }

    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        const auto* Hero = CastChecked<UCoopRPGHeroAnimInstance>(Instance);
        Movement.SetBlendSpace(Hero->Locomotion);
        const APawn* Pawn = Hero->TryGetPawnOwner();
        Movement.SetPosition(FVector(Pawn ? Pawn->GetVelocity().Size2D() : 0.0, 0.0, 0.0));
    }

    FAnimNode_BlendSpacePlayer_Standalone Movement;
    FAnimNode_Slot Slot;
};

void UCoopRPGHeroAnimInstance::ConfigureLocomotion(UBlendSpace* BlendSpace)
{
    Locomotion = BlendSpace;
}

void UCoopRPGHeroAnimInstance::ReadLocomotion(float& OutBlend, float& OutTime) const
{
    const auto& Proxy = GetProxyOnGameThread<FCoopRPGHeroAnimProxy>();
    OutBlend = Proxy.Movement.GetFilteredPosition().X;
    OutTime = Proxy.Movement.GetCurrentAssetTime();
}

FAnimInstanceProxy* UCoopRPGHeroAnimInstance::CreateAnimInstanceProxy()
{
    return new FCoopRPGHeroAnimProxy(this);
}

void UCoopRPGHeroAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
    delete Proxy;
}
