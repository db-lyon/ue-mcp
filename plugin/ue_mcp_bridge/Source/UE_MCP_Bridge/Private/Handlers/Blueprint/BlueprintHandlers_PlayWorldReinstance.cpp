#include "BlueprintHandlers_PlayWorldReinstance.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/WeakObjectPtr.h"

// Compiling a pawn Blueprint while play runs reinstances the possessed pawn. The
// new pawn is handed the old one's OnDestroyed bindings, then repossession has the
// player state bind OnPawnPrivateDestroyed again, which trips the duplicate-binding
// ensure in ScriptDelegates.h (#1600). Unbind before the compile; on the next tick,
// when reinstancing and repossession are over, bind it once if nothing else did.
namespace
{
	const FName MCPPawnDestroyedHandlerName(TEXT("OnPawnPrivateDestroyed"));
	TArray<TWeakObjectPtr<APlayerState>> GMCPUnboundPlayerStates;
	bool GMCPPossessedPawnGuardRegistered = false;
	bool GMCPPlayerStateRestorePending = false;

	void MCPRestorePlayerStatesAfterCompile();

	void MCPUnbindPlayerStatesBeforeCompile(UBlueprint* Blueprint)
	{
		if (!Blueprint || !GEngine) return;
		UClass* PawnClass = Blueprint->GeneratedClass;
		if (!PawnClass || !PawnClass->IsChildOf(APawn::StaticClass())) return;

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || Context.WorldType != EWorldType::PIE) continue;
			for (TActorIterator<APawn> It(World, PawnClass); It; ++It)
			{
				APawn* Pawn = *It;
				APlayerState* State = Pawn ? Pawn->GetPlayerState() : nullptr;
				if (State && Pawn->OnDestroyed.Contains(State, MCPPawnDestroyedHandlerName))
				{
					Pawn->OnDestroyed.Remove(State, MCPPawnDestroyedHandlerName);
					GMCPUnboundPlayerStates.AddUnique(State);
				}
			}
		}

		if (!GMCPUnboundPlayerStates.IsEmpty() && !GMCPPlayerStateRestorePending)
		{
			GMCPPlayerStateRestorePending = true;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
			{
				MCPRestorePlayerStatesAfterCompile();
				return false;
			}));
		}
	}

	void MCPRestorePlayerStatesAfterCompile()
	{
		GMCPPlayerStateRestorePending = false;
		TArray<TWeakObjectPtr<APlayerState>> States = MoveTemp(GMCPUnboundPlayerStates);
		GMCPUnboundPlayerStates.Reset();
		for (const TWeakObjectPtr<APlayerState>& Weak : States)
		{
			APlayerState* State = Weak.Get();
			APawn* Pawn = State ? State->GetPawn() : nullptr;
			if (!IsValid(Pawn) || Pawn->OnDestroyed.Contains(State, MCPPawnDestroyedHandlerName)) continue;
			FScriptDelegate Delegate;
			Delegate.BindUFunction(State, MCPPawnDestroyedHandlerName);
			Pawn->OnDestroyed.AddUnique(Delegate);
		}
	}

	void MCPBindCompileEvents()
	{
		if (GMCPPossessedPawnGuardRegistered || !GEditor) return;
		GMCPPossessedPawnGuardRegistered = true;
		GEditor->OnBlueprintPreCompile().AddStatic(&MCPUnbindPlayerStatesBeforeCompile);
	}
}

void MCPBlueprintPlayWorld::RegisterPossessedPawnRebindGuard()
{
	if (GEditor)
	{
		MCPBindCompileEvents();
		return;
	}
#if UE_VERSION_OLDER_THAN(5, 8, 0)
	FCoreDelegates::OnPostEngineInit.AddStatic(&MCPBindCompileEvents);
#else
	FCoreDelegates::GetOnPostEngineInit().AddStatic(&MCPBindCompileEvents);
#endif
}
