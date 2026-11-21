#pragma once

#include "Engine/World.h"

namespace CoopRPG::Expedition
{
    inline constexpr TCHAR LobbyMap[] = TEXT("L_ExpeditionLobby");
    inline constexpr TCHAR SkirmishMap[] = TEXT("L_ExpeditionSkirmish");
    inline constexpr TCHAR SkirmishPackage[] = TEXT("/Game/Maps/L_ExpeditionSkirmish");

    inline constexpr TCHAR PuzzleMap[] = TEXT("L_ExpeditionPuzzle");
    inline constexpr TCHAR PuzzlePackage[] = TEXT("/Game/Maps/L_ExpeditionPuzzle");
    inline constexpr double PuzzlePlateRadius = 90.0;

    inline constexpr TCHAR BossMap[] = TEXT("L_ExpeditionBoss");
    inline constexpr TCHAR BossPackage[] = TEXT("/Game/Maps/L_ExpeditionBoss");

    inline bool IsBoss(const UWorld* World)
    {
        return World && UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) == BossPackage;
    }

    inline bool IsSkirmish(const UWorld* World)
    {
        return World && UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) == SkirmishPackage;
    }
}
