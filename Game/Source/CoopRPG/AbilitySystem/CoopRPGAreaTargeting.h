#pragma once

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"

namespace CoopRPG::AreaTargeting
{
    inline bool IsObstructed(const AActor& Caster, const AActor& Target)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CoopRPGAreaTarget), false, &Caster);
        Query.AddIgnoredActor(&Target);
        FCollisionObjectQueryParams WorldObjects;
        WorldObjects.AddObjectTypesToQuery(ECC_WorldStatic);
        WorldObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
        TArray<FHitResult> Hits;
        Caster.GetWorld()->LineTraceMultiByObjectType(Hits, Caster.GetActorLocation(), Target.GetActorLocation(), WorldObjects, Query);
        return Hits.ContainsByPredicate([](const FHitResult& Hit)
        {
            const auto* Component = Hit.GetComponent();
            return Component && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
        });
    }
}
