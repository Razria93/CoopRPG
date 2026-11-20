#pragma once

#include "Animation/AnimInstance.h"
#include "CoopRPGHeroAnimInstance.generated.h"

class UBlendSpace;

UCLASS()
class UCoopRPGHeroAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    static const FName MeleeSlot;

    void ConfigureLocomotion(UBlendSpace* BlendSpace);
    void ReadLocomotion(float& OutBlend, float& OutTime) const;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;

private:
    friend struct FCoopRPGHeroAnimProxy;

    UPROPERTY(Transient)
    TObjectPtr<UBlendSpace> Locomotion;
};
