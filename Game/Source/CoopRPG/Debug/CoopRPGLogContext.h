#pragma once

#include "CoreMinimal.h"

namespace CoopRPG::Debug
{
    FString DescribeLogContext(const UObject* WorldContext, const TCHAR* Function);
    FString DescribePlayerRequestContext(const AActor* Actor, const TCHAR* Function);
}
