#pragma once

#include "Camera/CameraShakeBase.h"
#include "CoopRPGImpactCameraShake.generated.h"

UCLASS()
class UCoopRPGImpactCameraShakePattern final : public UCameraShakePattern
{
    GENERATED_BODY()

private:
    virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
    virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
    virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,
        FCameraShakePatternUpdateResult& OutResult) override;
    virtual bool IsFinishedImpl() const override;

private:
    float Elapsed = 0.0f;
};

UCLASS()
class UCoopRPGImpactCameraShake final : public UCameraShakeBase
{
    GENERATED_BODY()

public:
    explicit UCoopRPGImpactCameraShake(const FObjectInitializer& ObjectInitializer);
};
