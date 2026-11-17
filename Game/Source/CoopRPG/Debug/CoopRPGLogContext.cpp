#include "CoopRPGLogContext.h"

#include "Engine/World.h"
#include "UObject/Package.h"

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

        return FString::Printf(TEXT("NetMode=%s PIE=%d Func=%s"), NetMode, PIEInstance, Function);
    }
}
