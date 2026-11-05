#pragma once

#include "CoreMinimal.h"

class UWorld;
class APlayerController;
class ULocalPlayer;

namespace MCPPIE
{
	namespace PIEWorldResolver
	{
		struct FWorldCandidate
		{
			int32 PIEInstance = INDEX_NONE;
			bool bIsRuntimeWorld = false;
			bool bIsActivePlayWorld = false;
			int32 LocalPlayerCount = 0;
		};

		struct FPlayerTarget
		{
			UWorld* World = nullptr;
			APlayerController* PlayerController = nullptr;
			ULocalPlayer* LocalPlayer = nullptr;
			int32 PIEInstance = INDEX_NONE;
			int32 LocalPlayerIndex = 0;
		};

		int32 SelectCandidateIndex(TConstArrayView<FWorldCandidate> Candidates,
			int32 RequestedPIEInstance, int32 LocalPlayerIndex);

		bool ResolvePlayer(int32 RequestedPIEInstance, int32 LocalPlayerIndex,
			FPlayerTarget& OutTarget, FString& OutError);

		bool ResolvePlayerInWorld(UWorld* World, int32 LocalPlayerIndex,
			FPlayerTarget& OutTarget, FString& OutError);
	}
}
