#include "Handlers/PIE/PIEWorldResolver.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace MCPPIE
{
	namespace PIEWorldResolver
	{
		namespace
		{
			bool IsCandidateEligible(const FWorldCandidate& Candidate, int32 LocalPlayerIndex)
			{
				return Candidate.bIsRuntimeWorld
					&& LocalPlayerIndex >= 0
					&& Candidate.LocalPlayerCount > LocalPlayerIndex;
			}

			int32 GetLocalPlayerCount(UWorld* World)
			{
				if (!World)
				{
					return 0;
				}

				UGameInstance* GameInstance = World->GetGameInstance();
				if (!GameInstance)
				{
					return 0;
				}

				int32 Count = 0;
				for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
				{
					if (!LocalPlayer)
					{
						break;
					}
					++Count;
				}
				return Count;
			}
		}

		int32 SelectCandidateIndex(TConstArrayView<FWorldCandidate> Candidates,
			int32 RequestedPIEInstance, int32 LocalPlayerIndex)
		{
			if (RequestedPIEInstance >= 0)
			{
				for (int32 Index = 0; Index < Candidates.Num(); ++Index)
				{
					const FWorldCandidate& Candidate = Candidates[Index];
					if (Candidate.PIEInstance == RequestedPIEInstance)
					{
						return IsCandidateEligible(Candidate, LocalPlayerIndex) ? Index : INDEX_NONE;
					}
				}

				return INDEX_NONE;
			}

			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				const FWorldCandidate& Candidate = Candidates[Index];
				if (IsCandidateEligible(Candidate, LocalPlayerIndex) && Candidate.bIsActivePlayWorld)
				{
					return Index;
				}
			}

			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				if (IsCandidateEligible(Candidates[Index], LocalPlayerIndex))
				{
					return Index;
				}
			}

			return INDEX_NONE;
		}

		bool ResolvePlayerInWorld(UWorld* World, int32 LocalPlayerIndex,
			FPlayerTarget& OutTarget, FString& OutError)
		{
			OutTarget = FPlayerTarget();
			OutTarget.LocalPlayerIndex = LocalPlayerIndex;

			if (!World)
			{
				OutError = TEXT("PIE world is unavailable");
				return false;
			}

			if (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game)
			{
				OutError = FString::Printf(TEXT("World '%s' is not a PIE/Game world"), *World->GetPathName());
				return false;
			}

			if (LocalPlayerIndex < 0)
			{
				OutError = FString::Printf(TEXT("Local player index %d is invalid"), LocalPlayerIndex);
				return false;
			}

			UGameInstance* GameInstance = World->GetGameInstance();
			if (!GameInstance)
			{
				OutError = FString::Printf(TEXT("PIE world '%s' has no game instance"), *World->GetPathName());
				return false;
			}

			const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
			if (LocalPlayerIndex >= LocalPlayers.Num() || !LocalPlayers[LocalPlayerIndex])
			{
				OutError = FString::Printf(TEXT("PIE world '%s' has no local player %d"),
					*World->GetPathName(), LocalPlayerIndex);
				return false;
			}

			ULocalPlayer* LocalPlayer = LocalPlayers[LocalPlayerIndex];
			APlayerController* PlayerController = LocalPlayer->PlayerController;
			if (!PlayerController || PlayerController->GetWorld() != World)
			{
				OutError = FString::Printf(TEXT("PIE world '%s' local player %d has no player controller"),
					*World->GetPathName(), LocalPlayerIndex);
				return false;
			}

			OutTarget.World = World;
			OutTarget.PlayerController = PlayerController;
			OutTarget.LocalPlayer = LocalPlayer;
			return true;
		}

		bool ResolvePlayer(int32 RequestedPIEInstance, int32 LocalPlayerIndex,
			FPlayerTarget& OutTarget, FString& OutError)
		{
			OutTarget = FPlayerTarget();
			OutTarget.LocalPlayerIndex = LocalPlayerIndex;

			if (!GEngine)
			{
				OutError = TEXT("GEngine is unavailable");
				return false;
			}

			TArray<FWorldCandidate> Candidates;
			TArray<UWorld*> Worlds;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				const bool bIsRuntimeWorld = Context.WorldType == EWorldType::PIE
					|| Context.WorldType == EWorldType::Game;

				FWorldCandidate Candidate;
				Candidate.PIEInstance = Context.PIEInstance;
				Candidate.bIsRuntimeWorld = bIsRuntimeWorld;
				Candidate.bIsActivePlayWorld = bIsRuntimeWorld && GEditor && GEditor->PlayWorld == World;
				Candidate.LocalPlayerCount = bIsRuntimeWorld ? GetLocalPlayerCount(World) : 0;
				Candidates.Add(Candidate);
				Worlds.Add(World);
			}

			const int32 CandidateIndex = SelectCandidateIndex(Candidates, RequestedPIEInstance, LocalPlayerIndex);
			if (CandidateIndex == INDEX_NONE)
			{
				if (RequestedPIEInstance >= 0)
				{
					OutError = FString::Printf(TEXT("PIE instance %d is unavailable or has no local player %d"),
						RequestedPIEInstance, LocalPlayerIndex);
				}
				else
				{
					OutError = FString::Printf(TEXT("No eligible PIE/Game world has local player %d"), LocalPlayerIndex);
				}
				return false;
			}

			if (!ResolvePlayerInWorld(Worlds[CandidateIndex], LocalPlayerIndex, OutTarget, OutError))
			{
				return false;
			}

			OutTarget.PIEInstance = Candidates[CandidateIndex].PIEInstance;
			return true;
		}
	}
}
