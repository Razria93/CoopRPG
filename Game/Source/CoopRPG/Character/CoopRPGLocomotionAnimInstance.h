#pragma once

#include "Animation/AnimInstance.h"
#include "CoopRPGLocomotionAnimInstance.generated.h"

class UBlendSpace;

UCLASS()
class UCoopRPGLocomotionAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    void ConfigureLocomotion(UBlendSpace* BlendSpace);
    void ReadLocomotion(float& OutBlend, float& OutTime) const;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;

public:
    static const FName MeleeSlot;

private:
    friend struct FCoopRPGLocomotionAnimProxy;

    UPROPERTY(Transient)
    TObjectPtr<UBlendSpace> Locomotion;
};
