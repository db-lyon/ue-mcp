#pragma once

// F7 Runtime (plans/domain-design.md 3.3): the world a runtime handler acts
// on, resolved by one rule from `world` and `pieInstance`, the refusal when
// it is not there, the actor and component inside it, and the world echo a
// rollback payload carries so its inverse replays against the same world.
//
// The rule: world=pie|game takes a live PIE or Game world only; world=auto
// takes a live world when one runs and the editor world otherwise;
// world=editor takes the editor world. A pieInstance names one live world,
// and a pieInstance no running world has is a refusal naming it, never a
// quiet answer from a different world (#778, defect 9).
//
//   const FMCPRuntimeWorld Live = FMCPRuntimeWorld::Resolve(Params, TEXT("pie"));
//   if (auto Err = Live.Require(TEXT("PIE is not running."))) return Err;
//   AActor* Actor = Live.Actor(Params, Err);
//   ...
//   Live.Echo(RollbackPayload);

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HandlerUtils.h"

struct FMCPRuntimeWorld
{
	UWorld* World = nullptr;
	/** The world param as read, or the handler's default. */
	FString Scope;
	/** The pieInstance the caller asked for, or INDEX_NONE. */
	int32 Requested = INDEX_NONE;
	/** The resolved world's PIE instance, or INDEX_NONE for the editor world. */
	int32 PIEInstance = INDEX_NONE;

	bool IsLive() const
	{
		return World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game);
	}

	/** Resolve from the call's own world and pieInstance params. */
	static FMCPRuntimeWorld Resolve(const TSharedPtr<FJsonObject>& Params, const TCHAR* DefaultScope)
	{
		FMCPRuntimeWorld Out;
		Out.Scope = OptionalString(Params, TEXT("world"), DefaultScope);
		double Raw = 0.0;
		if (TryGetNumberParam(Params, TEXT("pieInstance"), Raw))
		{
			Out.Requested = FMath::RoundToInt(Raw);
		}

		const bool bLiveOnly = Out.Scope.Equals(TEXT("pie"), ESearchCase::IgnoreCase) || Out.Scope.Equals(TEXT("game"), ESearchCase::IgnoreCase);
		const bool bAuto = Out.Scope.Equals(TEXT("auto"), ESearchCase::IgnoreCase);
		if (bLiveOnly || bAuto)
		{
			if (const FWorldContext* Context = LiveContext(Out.Requested))
			{
				Out.World = Context->World();
				Out.PIEInstance = Context->PIEInstance;
				return Out;
			}
			// An instance the caller named and no running world has is not answered from the editor world.
			if (bLiveOnly || Out.Requested != INDEX_NONE) return Out;
		}
		Out.World = GetEditorWorld();
		return Out;
	}

	/** Resolve a live world only: the scope may widen to the editor world, the answer never does. */
	static FMCPRuntimeWorld ResolveLive(const TSharedPtr<FJsonObject>& Params, const TCHAR* DefaultScope = TEXT("pie"))
	{
		FMCPRuntimeWorld Out = Resolve(Params, DefaultScope);
		if (!Out.IsLive()) Out.World = nullptr;
		return Out;
	}

	/** Why no world answered: the pieInstance when the caller named one, else Default. */
	FString Missing(const FString& Default) const
	{
		if (Requested != INDEX_NONE)
		{
			return FString::Printf(
				TEXT("No live PIE world has pieInstance %d. editor(list_pie_instances) lists the running ones."), Requested);
		}
		return Default;
	}

	/** The refusal when no world answered, or nullptr. */
	TSharedPtr<FJsonValue> Require(const FString& Default) const
	{
		return World ? nullptr : MCPError(Missing(Default));
	}

	/** The actor the call's actorPath or actorLabel names in this world. */
	AActor* Actor(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonValue>& OutError, FMCPActorSelector Selector = FMCPActorSelector()) const
	{
		Selector.WorldLabel = IsLive() ? TEXT("PIE") : TEXT("editor");
		return MCPResolveActor(World, Params, OutError, Selector);
	}

	/** Write world and, for a live world, its pieInstance onto an inverse's payload. */
	void Echo(const TSharedPtr<FJsonObject>& Payload) const
	{
		if (!Payload.IsValid()) return;
		Payload->SetStringField(TEXT("world"), Scope);
		if (IsLive() && PIEInstance != INDEX_NONE) Payload->SetNumberField(TEXT("pieInstance"), PIEInstance);
	}

	/** The running PIE or Game context with Instance, or the first one for INDEX_NONE. */
	static const FWorldContext* LiveContext(int32 Instance)
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game) continue;
			if (Instance != INDEX_NONE && Context.PIEInstance != Instance) continue;
			if (Context.World()) return &Context;
		}
		return nullptr;
	}
};

namespace MCPRuntime
{
	/**
	 * The component of type T on Actor that Name names, across everything
	 * GetComponents returns (SCS components on placed Blueprint instances too,
	 * #539): an exact instance or class name in any case, then an instance or
	 * class name prefix (StaticMeshComponent finds StaticMeshComponent0), then
	 * an instance-name substring (_GEN_VARIABLE suffixes). An empty Name takes
	 * the root when it is a T, else the first T. For a mutation that must never
	 * land on a sibling, use MCPFindComponentByName's exact rule instead.
	 */
	template <typename T = UActorComponent>
	T* FindComponent(AActor* Actor, const FString& Name)
	{
		if (!Actor) return nullptr;
		TArray<T*> Components;
		Actor->GetComponents<T>(Components);
		if (Name.IsEmpty())
		{
			if (T* Root = Cast<T>(Actor->GetRootComponent())) return Root;
			return Components.Num() > 0 ? Components[0] : nullptr;
		}
		for (T* Component : Components)
		{
			if (Component->GetName().Equals(Name, ESearchCase::IgnoreCase)
				|| Component->GetClass()->GetName().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Component;
			}
		}
		for (T* Component : Components)
		{
			if (Component->GetName().StartsWith(Name, ESearchCase::IgnoreCase)
				|| Component->GetClass()->GetName().StartsWith(Name, ESearchCase::IgnoreCase))
			{
				return Component;
			}
		}
		for (T* Component : Components)
		{
			if (Component->GetName().Contains(Name, ESearchCase::IgnoreCase)) return Component;
		}
		return nullptr;
	}
}
