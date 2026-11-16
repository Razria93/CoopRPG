#include "CoopRPGLogContext.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "UObject/Package.h"

namespace
{
    const TCHAR* GetLocalRoleLabel(ENetRole Role)
    {
        switch (Role)
        {
        case ROLE_Authority: return TEXT("Authority");
        case ROLE_AutonomousProxy: return TEXT("AutonomousProxy");
        case ROLE_SimulatedProxy: return TEXT("SimulatedProxy");
        case ROLE_None: return TEXT("None");
        default: return TEXT("Unknown");
        }
    }
}

namespace CoopRPG::Debug
{
    FString DescribeLogContext(const UObject* WorldContext, const TCHAR* Function)
    {
        const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
        const TCHAR* NetMode = TEXT("Unknown");
        int32 PIEInstance = INDEX_NONE;
        if (World)
        {
            switch (World->GetNetMode())
            {
            case NM_Standalone: NetMode = TEXT("Standalone"); break;
            case NM_DedicatedServer: NetMode = TEXT("DedicatedServer"); break;
            case NM_ListenServer: NetMode = TEXT("ListenServer"); break;
            case NM_Client: NetMode = TEXT("Client"); break;
            default: break;
            }
            PIEInstance = World->GetOutermost()->GetPIEInstanceID();
        }

        FString Context = FString::Printf(TEXT("NetMode=%s PIE=%d Func=%s"), NetMode, PIEInstance, Function);
        const auto* Actor = Cast<AActor>(WorldContext);
        if (Actor)
        {
            Context += FString::Printf(TEXT(" LocalRole=%s"), GetLocalRoleLabel(Actor->GetLocalRole()));
        }
        if (const auto* Pawn = Cast<APawn>(Actor); Pawn && Pawn->GetNetMode() == NM_Client && Pawn->IsLocallyControlled())
        {
            if (const APlayerState* PlayerState = Pawn->GetPlayerState(); PlayerState && PlayerState->GetPlayerId() > 0)
            {
                Context += FString::Printf(TEXT(" LocalPlayerId=%d"), PlayerState->GetPlayerId());
            }
        }
        return Context;
    }
}
