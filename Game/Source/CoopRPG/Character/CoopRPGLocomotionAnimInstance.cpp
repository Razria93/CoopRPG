#include "CoopRPGLocomotionAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "GameFramework/Pawn.h"

const FName UCoopRPGLocomotionAnimInstance::MeleeSlot(TEXT("DefaultSlot"));

struct FCoopRPGLocomotionAnimProxy final : FAnimInstanceProxy
{
    explicit FCoopRPGLocomotionAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        Slot.SlotName = UCoopRPGLocomotionAnimInstance::MeleeSlot;
        Slot.bAlwaysUpdateSourcePose = true;
        Slot.Source.SetLinkNode(&Movement);
    }

    virtual FAnimNode_Base* GetCustomRootNode() override { return &Slot; }

    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        const auto* Locomotion = CastChecked<UCoopRPGLocomotionAnimInstance>(Instance);
        Movement.SetBlendSpace(Locomotion->Locomotion);
        const APawn* Pawn = Locomotion->TryGetPawnOwner();
        Movement.SetPosition(FVector(Pawn ? Pawn->GetVelocity().Size2D() : 0.0, 0.0, 0.0));
    }

    FAnimNode_BlendSpacePlayer_Standalone Movement;
    FAnimNode_Slot Slot;
};

void UCoopRPGLocomotionAnimInstance::ConfigureLocomotion(UBlendSpace* BlendSpace)
{
    Locomotion = BlendSpace;
}

void UCoopRPGLocomotionAnimInstance::ReadLocomotion(float& OutBlend, float& OutTime) const
{
    const auto& Proxy = GetProxyOnGameThread<FCoopRPGLocomotionAnimProxy>();
    OutBlend = Proxy.Movement.GetFilteredPosition().X;
    OutTime = Proxy.Movement.GetCurrentAssetTime();
}

FAnimInstanceProxy* UCoopRPGLocomotionAnimInstance::CreateAnimInstanceProxy()
{
    return new FCoopRPGLocomotionAnimProxy(this);
}

void UCoopRPGLocomotionAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
    delete Proxy;
}
