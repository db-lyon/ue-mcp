#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// UWorld::SpawnActor skips the step the editor's own placement path runs on a
// new actor: invalidating its lighting cache. For a stationary light that is
// what assigns a preview shadow map channel, so a light spawned without it
// does not light the scene until something moves it (#1548). Run this on
// every actor a handler spawns into an editor world, after its properties are
// applied, so the channel is assigned for the light's final settings.
namespace MCPSpawnFinalize
{
	inline void FinishEditorSpawn(AActor* Actor)
	{
#if WITH_EDITOR
		if (!GIsEditor || !Actor) return;
		const UWorld* World = Actor->GetWorld();
		if (!World || World->IsGameWorld()) return;
		Actor->InvalidateLightingCache();
#endif
	}
}
