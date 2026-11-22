#pragma once

#include "CoreMinimal.h"

class ACoopRPGCharacter;
class ACoopRPGGameState;
class UCanvas;

namespace CoopRPG::ExpeditionUI
{
    enum class EAttackHudState : uint8 { Waiting, Unarmed, Down, LowStamina, Cooling, Ready, Unassigned };

    struct FAttackHudStatus
    {
        EAttackHudState State = EAttackHudState::Waiting;
        float Remaining = 0.0f;
        float Duration = 0.0f;

        int32 GetRemainingTenths() const { return FMath::CeilToInt(Remaining * 10.0f); }
        float GetElapsedFraction() const { return Duration > 0.0f ? FMath::Clamp(1.0f - Remaining / Duration, 0.0f, 1.0f) : 0.0f; }
    };

    FAttackHudStatus ReadAttackHud(const ACoopRPGGameState& State, const ACoopRPGCharacter& Hero, bool bCombatStage, bool bSlash = false);
    const TCHAR* AttackHudLabel(EAttackHudState State);
    const TCHAR* AttackHudLogState(EAttackHudState State);
    void DrawCooldownSweep(UCanvas& Canvas, FVector2D Position, double Size, float ElapsedFraction);
}
