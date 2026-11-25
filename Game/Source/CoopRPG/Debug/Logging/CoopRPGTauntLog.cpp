#include "CoopRPGTauntLog.h"

#include "CoopRPG/Character/CoopRPGCharacter.h"
#include "CoopRPG/Character/CoopRPGEnemy.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoopRPGTaunt, Log, All);

namespace CoopRPG::Debug::TauntLog
{
    void Rejected(const AActor* Caster, const TCHAR* Reason)
    {
        const auto* Hero = ::Cast<ACoopRPGCharacter>(Caster);
        if (IsValid(Caster) && Caster->HasAuthority())
        {
            UE_LOG(LogCoopRPGTaunt, Log, TEXT("TauntRejected Slot=%d Reason=%s"), Hero ? Hero->GetDisplaySlot() : 0, Reason);
        }
    }

    void Cast(const FGuid& Round, const ACoopRPGCharacter& Caster, int32 Enemies, float Before, float After, double EndsAt)
    {
        UE_LOG(LogCoopRPGTaunt, Log, TEXT("TauntCast Round=%s Slot=%d Targets=%d SPBefore=%.1f SPAfter=%.1f EndsAt=%.3f"),
            *Round.ToString(), Caster.GetDisplaySlot(), Enemies, Before, After, EndsAt);
    }

    void Applied(const ACoopRPGEnemy& Enemy, const ACoopRPGCharacter& Caster, bool bAttackPending, double EndsAt)
    {
        UE_LOG(LogCoopRPGTaunt, Log, TEXT("TauntApplied Enemy=%s Slot=%d AttackPending=%d EndsAt=%.3f"),
            *Enemy.GetName(), Caster.GetDisplaySlot(), bAttackPending, EndsAt);
    }

    void Selected(const ACoopRPGEnemy& Enemy, const ACoopRPGCharacter& Caster)
    {
        UE_LOG(LogCoopRPGTaunt, Log, TEXT("TauntTargetSelected Enemy=%s Slot=%d"), *Enemy.GetName(), Caster.GetDisplaySlot());
    }

    void Ended(const ACoopRPGEnemy& Enemy, const TCHAR* Reason)
    {
        UE_LOG(LogCoopRPGTaunt, Log, TEXT("TauntEnded Enemy=%s Reason=%s"), *Enemy.GetName(), Reason);
    }

    void Pulse(const ACoopRPGCharacter& Caster, bool bHasNiagara)
    {
        UE_LOG(LogCoopRPGTaunt, Log, TEXT("TauntPulse Slot=%d Niagara=%d"), Caster.GetDisplaySlot(), bHasNiagara);
    }
}
