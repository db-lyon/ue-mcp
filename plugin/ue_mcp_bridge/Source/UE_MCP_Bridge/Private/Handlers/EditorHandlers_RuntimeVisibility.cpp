// Bounded visibility control for live PIE actors and scene components.
//
// Raw reflected property writes do not call the semantic visibility setters,
// and invoking one function at a time cannot safely preflight or roll back a
// filtered batch. These handlers resolve the complete target set first, cap it,
// capture exact prior state, and never save or dirty editor packages.

#include "EditorHandlers.h"

#include "HandlerUtils.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/Guid.h"

namespace
{
	constexpr int32 MCPRuntimeVisibilityDefaultMaxTargets = 64;
	constexpr int32 MCPRuntimeVisibilityAbsoluteMaxTargets = 256;
	constexpr int32 MCPRuntimeVisibilityMaxExplicitActors = 64;
	constexpr int32 MCPRuntimeVisibilityMaxComponentFilters = 64;
	constexpr int32 MCPRuntimeVisibilityMaxRollbackSnapshots = 64;

	struct FMCPRuntimeVisibilityTarget
	{
		AActor* Actor = nullptr;
		USceneComponent* Component = nullptr;
		bool bDirectComponentMatch = false;
		bool bPreviousActorHidden = false;
		bool bPreviousVisible = true;
		bool bPreviousComponentHidden = false;
	};

	struct FMCPRuntimeVisibilityRestoreTarget
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<USceneComponent> Component;
		bool bActorHidden = false;
		bool bVisible = true;
		bool bComponentHidden = false;
	};

	struct FMCPRuntimeVisibilityRollbackSnapshot
	{
		TWeakObjectPtr<UWorld> World;
		int32 PIEInstance = INDEX_NONE;
		uint64 Sequence = 0;
		TArray<FMCPRuntimeVisibilityRestoreTarget> Targets;
	};

	TMap<FString, FMCPRuntimeVisibilityRollbackSnapshot> GMCPRuntimeVisibilityRollbacks;
	uint64 GMCPRuntimeVisibilityRollbackSequence = 0;

	bool ReadMCPRuntimeVisibilityStringArray(
		const TSharedPtr<FJsonObject>& Params,
		const TCHAR* Field,
		int32 MaxValues,
		TArray<FString>& OutValues,
		FString& OutError)
	{
		if (!Params->HasField(Field)) return true;

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(Field, Values) || !Values)
		{
			OutError = FString::Printf(TEXT("'%s' must be an array of strings"), Field);
			return false;
		}
		if (Values->Num() == 0)
		{
			OutError = FString::Printf(TEXT("'%s' must not be empty"), Field);
			return false;
		}
		if (Values->Num() > MaxValues)
		{
			OutError = FString::Printf(TEXT("'%s' accepts at most %d values"), Field, MaxValues);
			return false;
		}

		TSet<FString> Seen;
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Text;
			if (!Value.IsValid() || !Value->TryGetString(Text) || Text.TrimStartAndEnd().IsEmpty())
			{
				OutError = FString::Printf(TEXT("'%s' contains a non-string or empty value"), Field);
				return false;
			}
			Text = Text.TrimStartAndEnd();
			const FString Key = Text.ToLower();
			if (!Seen.Contains(Key))
			{
				Seen.Add(Key);
				OutValues.Add(Text);
			}
		}
		return true;
	}

	bool MCPRuntimeVisibilityClassMatches(UClass* Candidate, UClass* Requested, bool bMatchSubclasses)
	{
		return Candidate && Requested &&
			(bMatchSubclasses ? Candidate->IsChildOf(Requested) : Candidate == Requested);
	}

	bool MCPRuntimeVisibilityMatchesAnyClass(
		UClass* Candidate,
		const TArray<UClass*>& Requested,
		bool bMatchSubclasses)
	{
		if (Requested.Num() == 0) return true;
		for (UClass* RequestedClass : Requested)
		{
			if (MCPRuntimeVisibilityClassMatches(Candidate, RequestedClass, bMatchSubclasses)) return true;
		}
		return false;
	}

	bool MCPRuntimeVisibilityMatchesComponent(
		USceneComponent* Component,
		const TArray<FString>& ComponentNames,
		const TArray<UClass*>& ComponentClasses,
		bool bMatchSubclasses)
	{
		if (!Component) return false;
		if (ComponentNames.Num() == 0 && ComponentClasses.Num() == 0) return true;

		for (const FString& Name : ComponentNames)
		{
			if (Component->GetName().Equals(Name, ESearchCase::IgnoreCase)) return true;
		}
		return ComponentClasses.Num() > 0 &&
			MCPRuntimeVisibilityMatchesAnyClass(Component->GetClass(), ComponentClasses, bMatchSubclasses);
	}

	bool MCPRuntimeVisibilityTryAddComponent(
		TMap<USceneComponent*, bool>& ComponentTargets,
		USceneComponent* Component,
		bool bDirectMatch,
		int32 ActorTargetCount,
		int32 MaxTargets)
	{
		if (!Component) return true;
		bool* Existing = ComponentTargets.Find(Component);
		if (Existing)
		{
			*Existing = *Existing || bDirectMatch;
			return true;
		}
		if (ActorTargetCount + ComponentTargets.Num() >= MaxTargets) return false;
		ComponentTargets.Add(Component, bDirectMatch);
		return true;
	}

	bool MCPRuntimeVisibilityAddComponentTree(
		TMap<USceneComponent*, bool>& ComponentTargets,
		USceneComponent* Root,
		int32 ActorTargetCount,
		int32 MaxTargets)
	{
		TArray<USceneComponent*> Pending;
		Pending.Add(Root);
		while (Pending.Num() > 0)
		{
			USceneComponent* Parent = Pending.Pop(EAllowShrinking::No);
			for (USceneComponent* Child : Parent->GetAttachChildren())
			{
				if (!Child || ComponentTargets.Contains(Child)) continue;
				if (!MCPRuntimeVisibilityTryAddComponent(
					ComponentTargets, Child, false, ActorTargetCount, MaxTargets))
				{
					return false;
				}
				Pending.Add(Child);
			}
		}
		return true;
	}

	void MCPRuntimeVisibilityRemoveExpiredRollbacks()
	{
		for (auto It = GMCPRuntimeVisibilityRollbacks.CreateIterator(); It; ++It)
		{
			if (!It.Value().World.IsValid()) It.RemoveCurrent();
		}
	}

	void MCPRuntimeVisibilityMakeRollbackRoom()
	{
		MCPRuntimeVisibilityRemoveExpiredRollbacks();
		while (GMCPRuntimeVisibilityRollbacks.Num() >= MCPRuntimeVisibilityMaxRollbackSnapshots)
		{
			FString OldestToken;
			uint64 OldestSequence = MAX_uint64;
			for (const TPair<FString, FMCPRuntimeVisibilityRollbackSnapshot>& Pair :
				GMCPRuntimeVisibilityRollbacks)
			{
				if (Pair.Value.Sequence < OldestSequence)
				{
					OldestSequence = Pair.Value.Sequence;
					OldestToken = Pair.Key;
				}
			}
			if (OldestToken.IsEmpty()) break;
			GMCPRuntimeVisibilityRollbacks.Remove(OldestToken);
		}
	}

	FString MCPRuntimeVisibilityStoreRollback(
		UWorld* World,
		int32 PIEInstance,
		const TArray<FMCPRuntimeVisibilityRestoreTarget>& Targets)
	{
		MCPRuntimeVisibilityMakeRollbackRoom();
		const FString Token = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		FMCPRuntimeVisibilityRollbackSnapshot& Snapshot =
			GMCPRuntimeVisibilityRollbacks.Add(Token);
		Snapshot.World = World;
		Snapshot.PIEInstance = PIEInstance;
		Snapshot.Sequence = ++GMCPRuntimeVisibilityRollbackSequence;
		Snapshot.Targets = Targets;
		return Token;
	}

	TSharedPtr<FJsonObject> MCPRuntimeVisibilityActorResult(
		const FMCPRuntimeVisibilityTarget& Target,
		bool bHidden)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("kind"), TEXT("actor"));
		Entry->SetStringField(TEXT("actorLabel"), Target.Actor->GetActorLabel());
		Entry->SetStringField(TEXT("actorPath"), Target.Actor->GetPathName());
		Entry->SetStringField(TEXT("actorClass"), Target.Actor->GetClass()->GetPathName());
		Entry->SetBoolField(TEXT("previousHidden"), Target.bPreviousActorHidden);
		Entry->SetBoolField(TEXT("desiredHidden"), bHidden);
		Entry->SetBoolField(TEXT("changed"), Target.bPreviousActorHidden != bHidden);
		return Entry;
	}

	TSharedPtr<FJsonObject> MCPRuntimeVisibilityComponentResult(
		const FMCPRuntimeVisibilityTarget& Target,
		bool bHidden)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("kind"), TEXT("component"));
		Entry->SetStringField(TEXT("actorLabel"), Target.Actor->GetActorLabel());
		Entry->SetStringField(TEXT("actorPath"), Target.Actor->GetPathName());
		Entry->SetStringField(TEXT("componentName"), Target.Component->GetName());
		Entry->SetStringField(TEXT("componentPath"), Target.Component->GetPathName());
		Entry->SetStringField(TEXT("componentClass"), Target.Component->GetClass()->GetPathName());
		Entry->SetBoolField(TEXT("directMatch"), Target.bDirectComponentMatch);
		Entry->SetBoolField(TEXT("previousVisible"), Target.bPreviousVisible);
		Entry->SetBoolField(TEXT("previousHiddenInGame"), Target.bPreviousComponentHidden);
		Entry->SetBoolField(TEXT("desiredVisible"), !bHidden);
		Entry->SetBoolField(TEXT("desiredHiddenInGame"), bHidden);
		Entry->SetBoolField(TEXT("changed"),
			Target.bPreviousVisible != !bHidden || Target.bPreviousComponentHidden != bHidden);
		return Entry;
	}

	FMCPRuntimeVisibilityRestoreTarget MCPRuntimeVisibilityRestoreActor(
		const FMCPRuntimeVisibilityTarget& Target)
	{
		FMCPRuntimeVisibilityRestoreTarget Entry;
		Entry.Actor = Target.Actor;
		Entry.bActorHidden = Target.bPreviousActorHidden;
		return Entry;
	}

	FMCPRuntimeVisibilityRestoreTarget MCPRuntimeVisibilityRestoreComponent(
		const FMCPRuntimeVisibilityTarget& Target)
	{
		FMCPRuntimeVisibilityRestoreTarget Entry;
		Entry.Component = Target.Component;
		Entry.bVisible = Target.bPreviousVisible;
		Entry.bComponentHidden = Target.bPreviousComponentHidden;
		return Entry;
	}

	TSharedPtr<FJsonValue> MCPRuntimeVisibilityRequirePIEWorld(
		const TSharedPtr<FJsonObject>& Params,
		UWorld*& OutWorld,
		const FWorldContext*& OutContext)
	{
		FString WorldScope = TEXT("pie");
		if (Params->HasField(TEXT("world")) &&
			(!Params->TryGetStringField(TEXT("world"), WorldScope) || WorldScope.IsEmpty()))
		{
			return MCPError(TEXT("'world' must be the string 'pie'"));
		}
		WorldScope = WorldScope.ToLower();
		if (WorldScope != TEXT("pie"))
		{
			return MCPError(TEXT("Runtime visibility is PIE-only; 'world' must be 'pie'"));
		}

		double RawPIEInstance = INDEX_NONE;
		const bool bHasPIEInstance = Params->HasField(TEXT("pieInstance"));
		if (bHasPIEInstance && !Params->TryGetNumberField(TEXT("pieInstance"), RawPIEInstance))
		{
			return MCPError(TEXT("'pieInstance' must be an integer"));
		}
		const int32 PIEInstance = bHasPIEInstance ? FMath::RoundToInt(RawPIEInstance) : INDEX_NONE;
		if (bHasPIEInstance && !FMath::IsNearlyEqual(RawPIEInstance, PIEInstance))
		{
			return MCPError(TEXT("'pieInstance' must be an integer"));
		}

		OutWorld = nullptr;
		OutContext = nullptr;
		int32 AvailablePIEWorlds = 0;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) continue;
				++AvailablePIEWorlds;
				if (bHasPIEInstance && Context.PIEInstance != PIEInstance) continue;
				if (!OutWorld)
				{
					OutWorld = Context.World();
					OutContext = &Context;
				}
			}
		}
		if (!bHasPIEInstance && AvailablePIEWorlds > 1)
		{
			OutWorld = nullptr;
			OutContext = nullptr;
			return MCPError(TEXT("Multiple PIE worlds are active; provide an explicit integer 'pieInstance'"));
		}
		if (!OutWorld || !OutContext)
		{
			return MCPError(TEXT("PIE world not available (or no such pieInstance). See editor(list_pie_instances)."));
		}
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FEditorHandlers::SetRuntimeVisibility(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	if (!Params.IsValid()) return MCPError(TEXT("Missing params"));

	bool bHidden = false;
	if (!Params->TryGetBoolField(TEXT("hidden"), bHidden))
	{
		return MCPError(TEXT("Missing required boolean 'hidden'"));
	}
	if (Params->HasField(TEXT("all")))
	{
		return MCPError(TEXT("'all' is not supported; use explicit actorLabels/actorPaths or actorClass"));
	}

	const int32 MaxTargets = OptionalInt(
		Params, TEXT("maxTargets"), MCPRuntimeVisibilityDefaultMaxTargets);
	if (MaxTargets < 1 || MaxTargets > MCPRuntimeVisibilityAbsoluteMaxTargets)
	{
		return MCPError(FString::Printf(
			TEXT("'maxTargets' must be between 1 and %d"),
			MCPRuntimeVisibilityAbsoluteMaxTargets));
	}

	TArray<FString> ActorLabels;
	TArray<FString> ActorPaths;
	TArray<FString> ComponentNames;
	TArray<FString> ComponentClasses;
	FString ParseError;
	if (!ReadMCPRuntimeVisibilityStringArray(
		Params, TEXT("actorLabels"), MCPRuntimeVisibilityMaxExplicitActors, ActorLabels, ParseError) ||
		!ReadMCPRuntimeVisibilityStringArray(
		Params, TEXT("actorPaths"), MCPRuntimeVisibilityMaxExplicitActors, ActorPaths, ParseError) ||
		!ReadMCPRuntimeVisibilityStringArray(
		Params, TEXT("componentNames"), MCPRuntimeVisibilityMaxComponentFilters, ComponentNames, ParseError) ||
		!ReadMCPRuntimeVisibilityStringArray(
		Params, TEXT("componentClasses"), MCPRuntimeVisibilityMaxComponentFilters, ComponentClasses, ParseError))
	{
		return MCPError(ParseError);
	}

	FString ActorClass;
	const bool bHasActorClass = Params->HasField(TEXT("actorClass"));
	if (bHasActorClass &&
		(!Params->TryGetStringField(TEXT("actorClass"), ActorClass) || ActorClass.TrimStartAndEnd().IsEmpty()))
	{
		return MCPError(TEXT("'actorClass' must be a non-empty string"));
	}
	ActorClass = ActorClass.TrimStartAndEnd();

	const int32 SelectorCount =
		(Params->HasField(TEXT("actorLabels")) ? 1 : 0) +
		(Params->HasField(TEXT("actorPaths")) ? 1 : 0) +
		(bHasActorClass ? 1 : 0);
	if (SelectorCount != 1)
	{
		return MCPError(TEXT("Provide exactly one actor selector: actorLabels, actorPaths, or actorClass"));
	}

	const bool bHasComponentFilters = ComponentNames.Num() > 0 || ComponentClasses.Num() > 0;
	const bool bAffectActor = OptionalBool(Params, TEXT("affectActor"), !bHasComponentFilters);
	const bool bAffectComponents = OptionalBool(Params, TEXT("affectComponents"), bHasComponentFilters);
	if (!bAffectActor && !bAffectComponents)
	{
		return MCPError(TEXT("At least one of 'affectActor' or 'affectComponents' must be true"));
	}
	if (bHasComponentFilters && !bAffectComponents)
	{
		return MCPError(TEXT("componentNames/componentClasses require 'affectComponents'=true"));
	}

	UClass* ResolvedActorClass = nullptr;
	if (bHasActorClass)
	{
		ResolvedActorClass = MCPResolveClass(ActorClass);
		if (TSharedPtr<FJsonValue> Error = MCPCheckClassUsable(
			ActorClass, ResolvedActorClass, AActor::StaticClass(), false))
		{
			return Error;
		}
	}

	TArray<UClass*> ResolvedComponentClasses;
	for (const FString& ComponentClass : ComponentClasses)
	{
		UClass* ResolvedClass = MCPResolveClass(ComponentClass);
		if (!ResolvedClass) return MCPClassNotFoundError(ComponentClass, TEXT("componentClasses"));
		if (!ResolvedClass->IsChildOf(USceneComponent::StaticClass()))
		{
			return MCPClassUnusableError(
				ComponentClass,
				ResolvedClass,
				TEXT("wrong_base"),
				TEXT("it does not derive from SceneComponent"));
		}
		ResolvedComponentClasses.AddUnique(ResolvedClass);
	}

	UWorld* World = nullptr;
	const FWorldContext* WorldContext = nullptr;
	if (TSharedPtr<FJsonValue> Error = MCPRuntimeVisibilityRequirePIEWorld(
		Params, World, WorldContext)) return Error;

	const bool bMatchSubclasses = OptionalBool(Params, TEXT("matchSubclasses"), true);
	TArray<AActor*> SelectedActors;
	if (bHasActorClass)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (MCPRuntimeVisibilityClassMatches(It->GetClass(), ResolvedActorClass, bMatchSubclasses))
			{
				SelectedActors.Add(*It);
				if (SelectedActors.Num() > MaxTargets)
				{
					return MCPError(FString::Printf(
						TEXT("Actor selector matched more than maxTargets=%d actors; narrow the class or raise the bound deliberately"),
						MaxTargets));
				}
			}
		}
	}
	else
	{
		const bool bByPath = ActorPaths.Num() > 0;
		const TArray<FString>& Requested = bByPath ? ActorPaths : ActorLabels;
		for (const FString& Selector : Requested)
		{
			TArray<AActor*> Matches;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				const bool bMatches = bByPath
					? It->GetPathName().Equals(Selector, ESearchCase::IgnoreCase)
					: It->GetActorLabel().Equals(Selector, ESearchCase::IgnoreCase);
				if (bMatches) Matches.Add(*It);
			}
			if (Matches.Num() == 0)
			{
				return MCPError(FString::Printf(TEXT("Actor selector matched nothing: %s"), *Selector));
			}
			if (Matches.Num() > 1)
			{
				return MCPError(FString::Printf(
					TEXT("Actor selector is ambiguous (%d matches): %s. Use actorPaths instead."),
					Matches.Num(), *Selector));
			}
			SelectedActors.AddUnique(Matches[0]);
		}
	}

	SelectedActors.Sort([](const AActor& Left, const AActor& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});

	TMap<USceneComponent*, bool> ComponentTargets;
	if (bAffectComponents)
	{
		const int32 ActorTargetCount = bAffectActor ? SelectedActors.Num() : 0;
		const bool bPropagateToChildren = OptionalBool(Params, TEXT("propagateToChildren"), true);
		for (AActor* Actor : SelectedActors)
		{
			for (UActorComponent* OwnedComponent : Actor->GetComponents())
			{
				USceneComponent* Component = Cast<USceneComponent>(OwnedComponent);
				if (!MCPRuntimeVisibilityMatchesComponent(
					Component, ComponentNames, ResolvedComponentClasses, bMatchSubclasses))
				{
					continue;
				}

				if (!MCPRuntimeVisibilityTryAddComponent(
					ComponentTargets, Component, true, ActorTargetCount, MaxTargets))
				{
					return MCPError(FString::Printf(
						TEXT("Visibility request exceeds maxTargets=%d while expanding components"), MaxTargets));
				}
				if (bPropagateToChildren)
				{
					if (!MCPRuntimeVisibilityAddComponentTree(
						ComponentTargets, Component, ActorTargetCount, MaxTargets))
					{
						return MCPError(FString::Printf(
							TEXT("Visibility request exceeds maxTargets=%d while expanding component descendants"),
							MaxTargets));
					}
				}
			}
		}
	}

	const int32 TargetCount = (bAffectActor ? SelectedActors.Num() : 0) + ComponentTargets.Num();
	if (TargetCount > MaxTargets)
	{
		return MCPError(FString::Printf(
			TEXT("Visibility request resolves to %d targets, above maxTargets=%d. Narrow selectors or raise the bound deliberately."),
			TargetCount, MaxTargets));
	}

	TArray<FMCPRuntimeVisibilityTarget> Targets;
	Targets.Reserve(TargetCount);
	if (bAffectActor)
	{
		for (AActor* Actor : SelectedActors)
		{
			FMCPRuntimeVisibilityTarget& Target = Targets.AddDefaulted_GetRef();
			Target.Actor = Actor;
			Target.bPreviousActorHidden = Actor->IsHidden();
		}
	}

	TArray<USceneComponent*> SortedComponents;
	ComponentTargets.GetKeys(SortedComponents);
	SortedComponents.Sort([](const USceneComponent& Left, const USceneComponent& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	for (USceneComponent* Component : SortedComponents)
	{
		FMCPRuntimeVisibilityTarget& Target = Targets.AddDefaulted_GetRef();
		Target.Actor = Component->GetOwner();
		Target.Component = Component;
		Target.bDirectComponentMatch = ComponentTargets.FindRef(Component);
		Target.bPreviousVisible = Component->GetVisibleFlag();
		Target.bPreviousComponentHidden = Component->bHiddenInGame != 0;
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);
	int32 Changed = 0;
	TArray<TSharedPtr<FJsonValue>> Affected;
	TArray<FMCPRuntimeVisibilityRestoreTarget> RestoreTargets;
	for (const FMCPRuntimeVisibilityTarget& Target : Targets)
	{
		const bool bTargetChanged = Target.Component
			? Target.bPreviousVisible != !bHidden || Target.bPreviousComponentHidden != bHidden
			: Target.bPreviousActorHidden != bHidden;
		if (bTargetChanged) ++Changed;

		if (Target.Component)
		{
			Affected.Add(MakeShared<FJsonValueObject>(MCPRuntimeVisibilityComponentResult(Target, bHidden)));
			if (bTargetChanged)
			{
				RestoreTargets.Add(MCPRuntimeVisibilityRestoreComponent(Target));
				if (!bDryRun)
				{
					// Descendants are captured as independent targets, so apply without
					// recursive propagation and preserve an exact rollback snapshot.
					Target.Component->SetVisibility(!bHidden, false);
					Target.Component->SetHiddenInGame(bHidden, false);
				}
			}
		}
		else
		{
			Affected.Add(MakeShared<FJsonValueObject>(MCPRuntimeVisibilityActorResult(Target, bHidden)));
			if (bTargetChanged)
			{
				RestoreTargets.Add(MCPRuntimeVisibilityRestoreActor(Target));
				if (!bDryRun) Target.Actor->SetActorHiddenInGame(bHidden);
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("hidden"), bHidden);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("mutationPerformed"), !bDryRun && Changed > 0);
	Result->SetNumberField(TEXT("matchedActors"), SelectedActors.Num());
	Result->SetNumberField(TEXT("targetCount"), Targets.Num());
	Result->SetNumberField(TEXT("changed"), Changed);
	Result->SetNumberField(TEXT("alreadyDesired"), Targets.Num() - Changed);
	Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	Result->SetNumberField(TEXT("pieInstance"), WorldContext->PIEInstance);
	Result->SetStringField(TEXT("netMode"), DescribePIENetMode(World));
	if (bAffectActor)
	{
		Result->SetStringField(
			TEXT("actorVisibilityNote"),
			TEXT("Actor hidden state may be authority-owned or replicated; select the intended PIE instance."));
	}
	Result->SetArrayField(TEXT("targets"), Affected);

	if (!bDryRun && RestoreTargets.Num() > 0)
	{
		TSharedPtr<FJsonObject> RestorePayload = MakeShared<FJsonObject>();
		RestorePayload->SetStringField(TEXT("world"), TEXT("pie"));
		RestorePayload->SetNumberField(TEXT("pieInstance"), WorldContext->PIEInstance);
		RestorePayload->SetStringField(
			TEXT("rollbackToken"),
			MCPRuntimeVisibilityStoreRollback(World, WorldContext->PIEInstance, RestoreTargets));
		MCPSetRollback(Result, TEXT("restore_runtime_visibility"), RestorePayload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::RestoreRuntimeVisibility(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	if (!Params.IsValid()) return MCPError(TEXT("Missing params"));

	FString RollbackToken;
	if (!Params->TryGetStringField(TEXT("rollbackToken"), RollbackToken) ||
		RollbackToken.TrimStartAndEnd().IsEmpty())
	{
		return MCPError(TEXT("Restore requires non-empty 'rollbackToken' from the set response"));
	}
	RollbackToken = RollbackToken.TrimStartAndEnd();
	MCPRuntimeVisibilityRemoveExpiredRollbacks();
	FMCPRuntimeVisibilityRollbackSnapshot* Snapshot =
		GMCPRuntimeVisibilityRollbacks.Find(RollbackToken);
	if (!Snapshot)
	{
		return MCPError(TEXT("Rollback token was not found or has expired"));
	}

	UWorld* World = Snapshot->World.Get();
	const FWorldContext* WorldContext = nullptr;
	if (GEngine && World)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World() == World &&
				Context.PIEInstance == Snapshot->PIEInstance)
			{
				WorldContext = &Context;
				break;
	}
	}
	}
	if (!World || !WorldContext)
	{
		GMCPRuntimeVisibilityRollbacks.Remove(RollbackToken);
		return MCPError(TEXT("Rollback token belongs to an expired PIE session"));
	}

	if (Params->HasField(TEXT("world")))
	{
		FString WorldScope;
		if (!Params->TryGetStringField(TEXT("world"), WorldScope) ||
			!WorldScope.Equals(TEXT("pie"), ESearchCase::IgnoreCase))
		{
			return MCPError(TEXT("Runtime visibility restore is PIE-only; 'world' must be 'pie'"));
		}
	}
	if (Params->HasField(TEXT("pieInstance")))
	{
		double RawPIEInstance = 0.0;
		if (!Params->TryGetNumberField(TEXT("pieInstance"), RawPIEInstance) ||
			!FMath::IsNearlyEqual(RawPIEInstance, FMath::RoundToInt(RawPIEInstance)))
		{
			return MCPError(TEXT("'pieInstance' must be an integer"));
		}
		if (FMath::RoundToInt(RawPIEInstance) != Snapshot->PIEInstance)
		{
			return MCPError(TEXT("'pieInstance' does not match the rollback token's PIE session"));
		}
	}

	for (const FMCPRuntimeVisibilityRestoreTarget& Target : Snapshot->Targets)
	{
		if (Target.Component.IsValid())
		{
			if (Target.Component->GetWorld() != World)
			{
				GMCPRuntimeVisibilityRollbacks.Remove(RollbackToken);
				return MCPError(TEXT("Rollback component no longer belongs to the original PIE session"));
			}
		}
		else if (Target.Actor.IsValid())
		{
			if (Target.Actor->GetWorld() != World)
			{
				GMCPRuntimeVisibilityRollbacks.Remove(RollbackToken);
				return MCPError(TEXT("Rollback actor no longer belongs to the original PIE session"));
			}
		}
		else
		{
			GMCPRuntimeVisibilityRollbacks.Remove(RollbackToken);
			return MCPError(TEXT("Rollback target was destroyed; no state was restored"));
		}
	}

	int32 Changed = 0;
	for (const FMCPRuntimeVisibilityRestoreTarget& Target : Snapshot->Targets)
	{
		if (USceneComponent* Component = Target.Component.Get())
		{
			if (Component->GetVisibleFlag() != Target.bVisible ||
				(Component->bHiddenInGame != 0) != Target.bComponentHidden)
			{
				++Changed;
				Component->SetVisibility(Target.bVisible, false);
				Component->SetHiddenInGame(Target.bComponentHidden, false);
			}
		}
		else if (AActor* Actor = Target.Actor.Get(); Actor && Actor->IsHidden() != Target.bActorHidden)
		{
			++Changed;
			Actor->SetActorHiddenInGame(Target.bActorHidden);
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("restored"), true);
	Result->SetStringField(TEXT("rollbackToken"), RollbackToken);
	Result->SetNumberField(TEXT("targetCount"), Snapshot->Targets.Num());
	Result->SetNumberField(TEXT("changed"), Changed);
	Result->SetNumberField(TEXT("alreadyRestored"), Snapshot->Targets.Num() - Changed);
	Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	Result->SetNumberField(TEXT("pieInstance"), WorldContext->PIEInstance);
	Result->SetStringField(TEXT("netMode"), DescribePIENetMode(World));
	Result->SetBoolField(TEXT("persisted"), false);
	return MCPResult(Result);
}
