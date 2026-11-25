#pragma once

#include "CoreMinimal.h"

class AActor;
class ACoopRPGCharacter;
class ACoopRPGEnemy;

namespace CoopRPG::Debug::TauntLog
{
    void Rejected(const AActor* Caster, const TCHAR* Reason);
    void Cast(const FGuid& Round, const ACoopRPGCharacter& Caster, int32 Enemies, float Before, float After, double EndsAt);
    void Applied(const ACoopRPGEnemy& Enemy, const ACoopRPGCharacter& Caster, bool bAttackPending, double EndsAt);
    void Selected(const ACoopRPGEnemy& Enemy, const ACoopRPGCharacter& Caster);
    void Ended(const ACoopRPGEnemy& Enemy, const TCHAR* Reason);
    void Pulse(const ACoopRPGCharacter& Caster, bool bHasNiagara);
}
