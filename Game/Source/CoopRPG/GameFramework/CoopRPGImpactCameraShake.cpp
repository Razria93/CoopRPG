#include "CoopRPGImpactCameraShake.h"

#include "Math/UnrealMathUtility.h"
#include "Math/Vector.h"

namespace
{
    constexpr float ImpactDurationSeconds = 0.18f;
    constexpr float SideAmplitudeCm = 8.0f;
}

void UCoopRPGImpactCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
    OutInfo.Duration = ImpactDurationSeconds;
}

void UCoopRPGImpactCameraShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
    Elapsed = 0.0f;
}

void UCoopRPGImpactCameraShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,
    FCameraShakePatternUpdateResult& OutResult)
{
    Elapsed += Params.DeltaTime;
    const float Progress = FMath::Clamp(Elapsed / ImpactDurationSeconds, 0.0f, 1.0f);
    const float Offset = SideAmplitudeCm * FMath::Sin(2.0f * PI * Progress) * FMath::Square(1.0f - Progress);
    OutResult.Location = FVector(0.0, Offset, 0.25f * Offset);
}

bool UCoopRPGImpactCameraShakePattern::IsFinishedImpl() const
{
    return Elapsed >= ImpactDurationSeconds;
}

UCoopRPGImpactCameraShake::UCoopRPGImpactCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bSingleInstance = true;
    SetRootShakePattern(CreateDefaultSubobject<UCoopRPGImpactCameraShakePattern>(TEXT("ImpactPattern")));
}
