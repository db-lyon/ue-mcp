// Level-wide writes that used to be Python loops (#984, #941, #907, #987).
//
// The read side of this problem is level(query_components): a level-wide
// question had no editor-side answer. These are the same complaint on the
// write side. Every one of them was a loop over an editor-side selector that
// the client had to reproduce for itself:
//
//   #984  190 SpotLights tagged LichtTest, moved, retinted and shadow-toggled
//         one call at a time. batch_translate exists but wants an explicit
//         label list, and auto-numbered labels make that list a generated
//         payload rather than a selector.
//   #941  NavModifierComponent.AreaClass across dozens of placed
//         StaticMeshActors. set_component_property is one actor per call.
//   #907  230 orphaned SplineMeshComponents across 23 actors. Their names are
//         auto-generated (SplineMeshComponent_0, _1, ...) and remove_component
//         takes one name at a time, so there was no way to name them all.
//   #987  A SpotLight at every lamp post, positioned from each lamp
//         component's own bounds. place_actors_batch is StaticMesh only and
//         spawn_light takes a single position.
//
// The property writes deliberately delegate to FLevelHandlers::SetActorProperty
// and ::SetComponentProperty rather than reimplementing them. Those two carry
// years of accumulated behaviour (actor-label object refs, TArray-of-actor
// refs, instanced sub-object descent, the EditDefaultsOnly override), and a
// batch that quietly meant something narrower than the single-item action
// would be worse than no batch at all.

#include "LevelHandlers.h"

#include "Components/ActorComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HandlerQuery.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
// #985: bulk HLOD layer assignment. UHLODLayer has to be a complete type for
// AActor::SetHLODLayer and for the asset load.
#include "WorldPartition/HLOD/HLODLayer.h"

namespace
{
	// Bounds. Each of these actions replaced an unbounded loop, and an
	// unbounded batch is the same failure with fewer round trips.
	constexpr int32 MCPBatchMaxActors = 5000;
	constexpr int32 MCPBatchMaxProperties = 32;
	constexpr int32 MCPBatchMaxResultRows = 1000;
	constexpr int32 MCPBatchDefaultMaxSpawn = 500;
	constexpr int32 MCPBatchHardMaxSpawn = 5000;
	constexpr double MCPBatchMinSplineSpacing = 1.0;

	/**
	 * The selector every action in this file shares.
	 *
	 * Naming rule, and it is a rule rather than a style: a parameter that says
	 * `label` matches the EDITOR LABEL, a parameter that says `name` matches
	 * the internal object name, `Prefix` is a prefix and `Contains` is a
	 * substring. Two level actions that took similar-sounding filters and
	 * matched different things is exactly how #963 happened.
	 */
	struct FMCPBatchSelector
	{
		TArray<FString> ActorLabels;
		FString LabelPrefix;
		FString LabelContains;
		FString Tag;
		UClass* ActorClass = nullptr;
		FString ActorClassFallback;
		bool bMatchSubclasses = true;
		FString FolderPath;
		FString FolderPathPrefix;
		bool bAny = false;
	};

	TSharedPtr<FJsonValue> MCPBatchReadSelector(
		const TSharedPtr<FJsonObject>& Params,
		FMCPBatchSelector& OutSelector)
	{
		const TArray<TSharedPtr<FJsonValue>>* LabelValues = nullptr;
		if (Params->TryGetArrayField(TEXT("actorLabels"), LabelValues) && LabelValues)
		{
			OutSelector.ActorLabels = JsonArrayToStringList(LabelValues);
		}
		OutSelector.LabelPrefix = OptionalString(Params, TEXT("labelPrefix"));
		OutSelector.LabelContains = OptionalString(Params, TEXT("labelContains"));
		OutSelector.Tag = OptionalString(Params, TEXT("tag"));
		OutSelector.FolderPath = OptionalString(Params, TEXT("folderPath"));
		OutSelector.FolderPathPrefix = OptionalString(Params, TEXT("folderPathPrefix"));
		OutSelector.bMatchSubclasses = OptionalBool(Params, TEXT("matchSubclasses"), true);

		const FString ClassSpec = OptionalString(Params, TEXT("classFilter"));
		if (!ClassSpec.IsEmpty())
		{
			OutSelector.ActorClass = MCPResolveClassOfType(ClassSpec, AActor::StaticClass(), true);
			if (!OutSelector.ActorClass)
			{
				// Substring on the class name is what the older level actions
				// accept, so a call that used to work keeps working.
				OutSelector.ActorClassFallback = ClassSpec;
			}
		}

		OutSelector.bAny =
			!OutSelector.ActorLabels.IsEmpty() ||
			!OutSelector.LabelPrefix.IsEmpty() ||
			!OutSelector.LabelContains.IsEmpty() ||
			!OutSelector.Tag.IsEmpty() ||
			OutSelector.ActorClass != nullptr ||
			!OutSelector.ActorClassFallback.IsEmpty() ||
			!OutSelector.FolderPath.IsEmpty() ||
			!OutSelector.FolderPathPrefix.IsEmpty();

		return nullptr;
	}

	bool MCPBatchActorMatches(const FMCPBatchSelector& Selector, AActor* Actor, const FString& Label)
	{
		if (!Selector.ActorLabels.IsEmpty() && !Selector.ActorLabels.Contains(Label)) return false;
		if (!Selector.LabelPrefix.IsEmpty() && !Label.StartsWith(Selector.LabelPrefix, ESearchCase::CaseSensitive)) return false;
		if (!Selector.LabelContains.IsEmpty() && !Label.Contains(Selector.LabelContains, ESearchCase::IgnoreCase)) return false;
		if (!Selector.Tag.IsEmpty() && !Actor->ActorHasTag(FName(*Selector.Tag))) return false;
		if (Selector.ActorClass)
		{
			const bool bMatches = Selector.bMatchSubclasses
				? Actor->IsA(Selector.ActorClass)
				: (Actor->GetClass() == Selector.ActorClass);
			if (!bMatches) return false;
		}
		else if (!Selector.ActorClassFallback.IsEmpty() &&
			!Actor->GetClass()->GetName().Contains(Selector.ActorClassFallback, ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (!Selector.FolderPath.IsEmpty() &&
			!Actor->GetFolderPath().ToString().Equals(Selector.FolderPath, ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (!Selector.FolderPathPrefix.IsEmpty() &&
			!Actor->GetFolderPath().ToString().StartsWith(Selector.FolderPathPrefix, ESearchCase::IgnoreCase))
		{
			return false;
		}
		return true;
	}

	/** Every actor the selector picks, in a deterministic order. */
	void MCPBatchCollectActors(UWorld* World, const FMCPBatchSelector& Selector, TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			if (MCPBatchActorMatches(Selector, Actor, Actor->GetActorLabel()))
			{
				OutActors.Add(Actor);
			}
		}
		OutActors.Sort([](const AActor& A, const AActor& B)
		{
			return A.GetPathName() < B.GetPathName();
		});
	}

	/**
	 * Labels the selector asked for by exact name and did not find.
	 *
	 * A destructive or sweeping call that matched nothing must say so with the
	 * reason, not return success and a zero. That is #963's actual harm.
	 */
	TArray<FString> MCPBatchMissingLabels(const FMCPBatchSelector& Selector, const TArray<AActor*>& Matched)
	{
		TArray<FString> Missing;
		if (Selector.ActorLabels.IsEmpty())
		{
			return Missing;
		}
		TSet<FString> Found;
		for (AActor* Actor : Matched)
		{
			Found.Add(Actor->GetActorLabel());
		}
		for (const FString& Label : Selector.ActorLabels)
		{
			if (!Found.Contains(Label))
			{
				Missing.Add(Label);
			}
		}
		return Missing;
	}

	FVector MCPBatchReadVector(const TSharedPtr<FJsonObject>& Obj, const FVector& Default)
	{
		if (!Obj.IsValid()) return Default;
		FVector Value = Default;
		double Component = 0.0;
		if (Obj->TryGetNumberField(TEXT("x"), Component)) Value.X = Component;
		if (Obj->TryGetNumberField(TEXT("y"), Component)) Value.Y = Component;
		if (Obj->TryGetNumberField(TEXT("z"), Component)) Value.Z = Component;
		return Value;
	}

	FRotator MCPBatchReadRotator(const TSharedPtr<FJsonObject>& Obj, const FRotator& Default)
	{
		if (!Obj.IsValid()) return Default;
		FRotator Value = Default;
		double Component = 0.0;
		if (Obj->TryGetNumberField(TEXT("pitch"), Component)) Value.Pitch = Component;
		if (Obj->TryGetNumberField(TEXT("yaw"), Component)) Value.Yaw = Component;
		if (Obj->TryGetNumberField(TEXT("roll"), Component)) Value.Roll = Component;
		return Value;
	}

	/** Pull the { ok, error } verdict out of a delegated single-item handler. */
	void MCPBatchReadVerdict(const TSharedPtr<FJsonValue>& Response, bool& bOutOk, FString& OutError, FString& OutPrevious)
	{
		bOutOk = false;
		OutError.Reset();
		OutPrevious.Reset();
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			OutError = TEXT("handler returned no result");
			return;
		}
		const TSharedPtr<FJsonObject> Object = Response->AsObject();
		bOutOk = Object->HasField(TEXT("success")) ? Object->GetBoolField(TEXT("success")) : true;
		Object->TryGetStringField(TEXT("error"), OutError);
		Object->TryGetStringField(TEXT("previousValue"), OutPrevious);
	}

	/** Resolve one named component on an actor, matching the case-insensitive
	 *  rule set_component_property already uses for SCS components. */
	UActorComponent* MCPBatchFindComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor) return nullptr;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Component;
			}
		}
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
// batch_set_actor_properties (#984)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::BatchSetActorProperties(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	REQUIRE_EDITOR_WORLD(World);

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropertiesObject) ||
		!PropertiesObject || !(*PropertiesObject).IsValid() || (*PropertiesObject)->Values.Num() == 0)
	{
		return MCPError(TEXT("Missing 'properties' (an object of propertyName -> value; dotted paths are supported)"));
	}
	if ((*PropertiesObject)->Values.Num() > MCPBatchMaxProperties)
	{
		return MCPError(FString::Printf(
			TEXT("'properties' exceeds the maximum of %d entries"), MCPBatchMaxProperties));
	}

	FMCPBatchSelector Selector;
	if (auto Err = MCPBatchReadSelector(Params, Selector)) return Err;
	if (!Selector.bAny)
	{
		return MCPError(TEXT("Pass at least one selector: actorLabels, labelPrefix, labelContains, tag, classFilter, folderPath or folderPathPrefix"));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bForce = OptionalBool(Params, TEXT("force"), false);
	const FString TransactionLabel = OptionalString(
		Params, TEXT("transactionLabel"), TEXT("MCP batch set actor properties"));

	TArray<AActor*> Actors;
	MCPBatchCollectActors(World, Selector, Actors);
	if (Actors.Num() > MCPBatchMaxActors)
	{
		return MCPError(FString::Printf(
			TEXT("The selector matched %d actors, over the maximum of %d. Narrow it."),
			Actors.Num(), MCPBatchMaxActors));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("matched"), Actors.Num());
	MCPNoteLoadedOnlyEnumeration(World, Result);
	{
		const TArray<FString> Missing = MCPBatchMissingLabels(Selector, Actors);
		if (Missing.Num() > 0)
		{
			Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(Missing));
		}
	}
	if (Actors.IsEmpty())
	{
		const FString Needle = !Selector.LabelPrefix.IsEmpty()
			? Selector.LabelPrefix
			: Selector.LabelContains;
		if (const TSharedPtr<FJsonObject> Hint = MCPDescribeZeroActorMatch(World, Needle))
		{
			Result->SetObjectField(TEXT("zeroMatchHint"), Hint);
		}
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Updated = 0;
	int32 Failed = 0;
	int32 PropertyWrites = 0;
	int32 PropertyFailures = 0;

	// One transaction for the whole batch, so 190 lights is one undo rather
	// than 190. Skipped entirely on a dry run, which writes nothing.
	TUniquePtr<FScopedTransaction> Transaction;
	if (!bDryRun && !Actors.IsEmpty())
	{
		Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TransactionLabel));
	}

	for (AActor* Actor : Actors)
	{
		const FString Label = Actor->GetActorLabel();
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Label);
		Row->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());

		TArray<TSharedPtr<FJsonValue>> PropertyRows;
		bool bActorOk = true;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertiesObject)->Values)
		{
			TSharedPtr<FJsonObject> PropertyRow = MakeShared<FJsonObject>();
			PropertyRow->SetStringField(TEXT("propertyName"), Pair.Key);

			if (bDryRun)
			{
				// A preview has to be a real check, or it is not a preview.
				// Resolve the path against this actor's class and say whether
				// the write would land.
				FString ResolvedType;
				const TSharedPtr<FJsonValue> Current =
					MCPQuery::ReadDottedProperty(Actor, Pair.Key, ResolvedType);
				const bool bResolvable = Current.IsValid();
				PropertyRow->SetBoolField(TEXT("ok"), bResolvable);
				if (bResolvable)
				{
					PropertyRow->SetField(TEXT("currentValue"), Current);
					PropertyRow->SetStringField(TEXT("propertyType"), ResolvedType);
				}
				else
				{
					PropertyRow->SetStringField(TEXT("error"), FString::Printf(
						TEXT("Property '%s' does not resolve on %s"), *Pair.Key, *Actor->GetClass()->GetName()));
					bActorOk = false;
					++PropertyFailures;
				}
				PropertyRows.Add(MakeShared<FJsonValueObject>(PropertyRow));
				continue;
			}

			// Delegate to the single-actor writer. Reimplementing it here
			// would quietly support fewer value shapes than set_actor_property
			// does, which is the worse failure: the call succeeds and means
			// something narrower than the caller asked for.
			TSharedPtr<FJsonObject> SubParams = MakeShared<FJsonObject>();
			SubParams->SetStringField(TEXT("actorLabel"), Label);
			SubParams->SetStringField(TEXT("propertyName"), Pair.Key);
			SubParams->SetField(TEXT("value"), Pair.Value);
			if (bForce) SubParams->SetBoolField(TEXT("force"), true);

			bool bOk = false;
			FString Error;
			FString Previous;
			MCPBatchReadVerdict(FLevelHandlers::SetActorProperty(SubParams), bOk, Error, Previous);
			PropertyRow->SetBoolField(TEXT("ok"), bOk);
			if (!Previous.IsEmpty()) PropertyRow->SetStringField(TEXT("previousValue"), Previous);
			if (!bOk)
			{
				PropertyRow->SetStringField(TEXT("error"), Error);
				bActorOk = false;
				++PropertyFailures;
			}
			else
			{
				++PropertyWrites;
			}
			PropertyRows.Add(MakeShared<FJsonValueObject>(PropertyRow));
		}

		Row->SetBoolField(TEXT("ok"), bActorOk);
		Row->SetArrayField(TEXT("properties"), PropertyRows);
		if (bActorOk) ++Updated; else ++Failed;
		if (Rows.Num() < MCPBatchMaxResultRows)
		{
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	Result->SetBoolField(TEXT("success"), Failed == 0);
	if (Failed > 0)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d actors had at least one property that did not write; see results[]."),
			Failed, Actors.Num()));
	}
	Result->SetNumberField(bDryRun ? TEXT("wouldUpdate") : TEXT("updated"), Updated);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("propertyWrites"), PropertyWrites);
	Result->SetNumberField(TEXT("propertyFailures"), PropertyFailures);
	Result->SetNumberField(TEXT("returnedResults"), Rows.Num());
	Result->SetBoolField(TEXT("resultsTruncated"), Rows.Num() < Actors.Num());
	Result->SetArrayField(TEXT("results"), Rows);
	if (!bDryRun && Updated > 0)
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("saveNote"),
			TEXT("The level is left dirty and is NOT saved. Call level(save) when you are done."));
	}
	// Undoing a batch write means restoring each actor's OWN previous value,
	// and nothing on this surface takes a per-actor value list: this action
	// applies one value to everything its selector matched. A single restore
	// call would have to be a blanket write of some shared value, which is not
	// the inverse of what happened. The selectors here address actors by
	// editor label as well, which is not unique, so such a call could also
	// write onto a namesake this one never touched.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("results[].properties[].previousValue carries each actor's own value from before the write, and is present only where that value was a non-empty string: a property row WITHOUT it either held an empty value or came from a write that reported none, and the two are not distinguishable here. Replay the ones you have one at a time with level(set_actor_property), which emits its own rollback per actor. No single call restores them together."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// bulk_set_component_property (#941)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::BulkSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	REQUIRE_EDITOR_WORLD(World);

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField)
	{
		return MCPError(TEXT("Missing 'value' parameter (pass JSON null to clear an object reference)"));
	}

	FMCPBatchSelector Selector;
	if (auto Err = MCPBatchReadSelector(Params, Selector)) return Err;
	if (!Selector.bAny)
	{
		return MCPError(TEXT("Pass at least one selector: actorLabels, labelPrefix, labelContains, tag, classFilter, folderPath or folderPathPrefix"));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const FString TransactionLabel = OptionalString(
		Params, TEXT("transactionLabel"), TEXT("MCP bulk set component property"));

	TArray<AActor*> Actors;
	MCPBatchCollectActors(World, Selector, Actors);
	if (Actors.Num() > MCPBatchMaxActors)
	{
		return MCPError(FString::Printf(
			TEXT("The selector matched %d actors, over the maximum of %d. Narrow it."),
			Actors.Num(), MCPBatchMaxActors));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetNumberField(TEXT("matchedActors"), Actors.Num());
	MCPNoteLoadedOnlyEnumeration(World, Result);
	{
		const TArray<FString> Missing = MCPBatchMissingLabels(Selector, Actors);
		if (Missing.Num() > 0)
		{
			Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(Missing));
		}
	}

	TUniquePtr<FScopedTransaction> Transaction;
	if (!bDryRun && !Actors.IsEmpty())
	{
		Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TransactionLabel));
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Updated = 0;
	int32 Failed = 0;
	int32 WithoutComponent = 0;

	for (AActor* Actor : Actors)
	{
		const FString Label = Actor->GetActorLabel();
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Label);

		UActorComponent* Component = MCPBatchFindComponent(Actor, ComponentName);
		if (!Component)
		{
			// An actor that has no such component is neither a success nor an
			// error: it is a fact the caller has to be told, because a
			// selector wide enough to be useful will pick some up.
			++WithoutComponent;
			Row->SetBoolField(TEXT("ok"), false);
			Row->SetStringField(TEXT("status"), TEXT("no_such_component"));
			if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		Row->SetStringField(TEXT("componentClass"), Component->GetClass()->GetName());

		if (bDryRun)
		{
			FString ResolvedType;
			const TSharedPtr<FJsonValue> Current =
				MCPQuery::ReadDottedProperty(Component, PropertyName, ResolvedType);
			const bool bResolvable = Current.IsValid();
			Row->SetBoolField(TEXT("ok"), bResolvable);
			Row->SetStringField(TEXT("status"), bResolvable ? TEXT("would_write") : TEXT("property_not_found"));
			if (bResolvable)
			{
				Row->SetField(TEXT("currentValue"), Current);
				Row->SetStringField(TEXT("propertyType"), ResolvedType);
				++Updated;
			}
			else
			{
				++Failed;
			}
			if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		TSharedPtr<FJsonObject> SubParams = MakeShared<FJsonObject>();
		SubParams->SetStringField(TEXT("actorLabel"), Label);
		SubParams->SetStringField(TEXT("componentName"), ComponentName);
		SubParams->SetStringField(TEXT("propertyName"), PropertyName);
		SubParams->SetField(TEXT("value"), *ValueField);

		bool bOk = false;
		FString Error;
		FString Previous;
		MCPBatchReadVerdict(FLevelHandlers::SetComponentProperty(SubParams), bOk, Error, Previous);
		Row->SetBoolField(TEXT("ok"), bOk);
		Row->SetStringField(TEXT("status"), bOk ? TEXT("written") : TEXT("failed"));
		if (!Previous.IsEmpty()) Row->SetStringField(TEXT("previousValue"), Previous);
		if (!bOk)
		{
			Row->SetStringField(TEXT("error"), Error);
			++Failed;
		}
		else
		{
			++Updated;
		}
		if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	Result->SetBoolField(TEXT("success"), Failed == 0);
	if (Failed > 0)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d matched actors did not accept the write; see results[]."), Failed, Actors.Num()));
	}
	Result->SetNumberField(bDryRun ? TEXT("wouldUpdate") : TEXT("updated"), Updated);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("actorsWithoutComponent"), WithoutComponent);
	Result->SetNumberField(TEXT("returnedResults"), Rows.Num());
	Result->SetBoolField(TEXT("resultsTruncated"), Rows.Num() < Actors.Num());
	Result->SetArrayField(TEXT("results"), Rows);
	if (!bDryRun && Updated > 0)
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("saveNote"),
			TEXT("The level is left dirty and is NOT saved. Call level(save) when you are done."));
	}
	// Same structural reason as batch_set_actor_properties: the inverse is one
	// write per component with that component's own previous value, and this
	// action only applies one shared value across a label-addressed selector.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("results[].previousValue carries each component's own value from before the write, and is present only where that value was a non-empty string: a row WITHOUT it either held an empty value or came from a write that reported none, and the two are not distinguishable here. Replay the ones you have one at a time with level(set_component_property), which emits its own rollback per actor. No single call restores them together."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// remove_components_by_class (#907)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::RemoveComponentsByClass(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	REQUIRE_EDITOR_WORLD(World);

	FString ComponentClassSpec;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClassSpec)) return Err;

	UClass* ComponentClass = MCPResolveClassOfType(ComponentClassSpec, UActorComponent::StaticClass(), true);
	if (!ComponentClass)
	{
		return MCPClassNotFoundError(ComponentClassSpec, TEXT("componentClass"));
	}
	const bool bMatchComponentSubclasses = OptionalBool(Params, TEXT("matchComponentSubclasses"), true);
	const FString ComponentNameContains = OptionalString(Params, TEXT("componentNameContains"));

	FMCPBatchSelector Selector;
	if (auto Err = MCPBatchReadSelector(Params, Selector)) return Err;
	// actorClassFilter is the name #907 asks for; classFilter is what every
	// other level action calls it. Accept both rather than make the caller
	// remember which action they are in.
	const FString ActorClassFilter = OptionalString(Params, TEXT("actorClassFilter"));
	if (!ActorClassFilter.IsEmpty() && !Selector.ActorClass && Selector.ActorClassFallback.IsEmpty())
	{
		Selector.ActorClass = MCPResolveClassOfType(ActorClassFilter, AActor::StaticClass(), true);
		if (!Selector.ActorClass) Selector.ActorClassFallback = ActorClassFilter;
		Selector.bAny = true;
	}

	// dryRun DEFAULTS TO TRUE. This deletes components whose auto-generated
	// names a caller cannot enumerate, which means it is the one level action
	// where the caller most needs to see the list before it happens. Same
	// default, and same reason, as delete_exact_labeled_actors_in_levels.
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);
	const bool bSave = OptionalBool(Params, TEXT("save"), false);
	const FString TransactionLabel = OptionalString(
		Params, TEXT("transactionLabel"), TEXT("MCP remove components by class"));

	TArray<AActor*> Actors;
	if (Selector.bAny)
	{
		MCPBatchCollectActors(World, Selector, Actors);
	}
	else
	{
		// No actor selector means every loaded actor. That is a legitimate
		// request (230 orphans across 23 actors nobody had named yet), which
		// is exactly why dryRun defaults to true.
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (AActor* Actor = *It) Actors.Add(Actor);
		}
		Actors.Sort([](const AActor& A, const AActor& B) { return A.GetPathName() < B.GetPathName(); });
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetStringField(TEXT("componentClass"), ComponentClass->GetName());
	MCPNoteLoadedOnlyEnumeration(World, Result);
	{
		const TArray<FString> Missing = MCPBatchMissingLabels(Selector, Actors);
		if (Missing.Num() > 0)
		{
			Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(Missing));
		}
	}

	TUniquePtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TransactionLabel));
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 ScannedActors = 0;
	int32 MatchedComponents = 0;
	int32 RemovedComponents = 0;
	int32 SkippedNonInstance = 0;
	int32 TouchedActors = 0;

	for (AActor* Actor : Actors)
	{
		++ScannedActors;

		TArray<UActorComponent*> Targets;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component) continue;
			const bool bClassMatches = bMatchComponentSubclasses
				? Component->IsA(ComponentClass)
				: (Component->GetClass() == ComponentClass);
			if (!bClassMatches) continue;
			if (!ComponentNameContains.IsEmpty() &&
				!Component->GetName().Contains(ComponentNameContains, ESearchCase::IgnoreCase))
			{
				continue;
			}
			Targets.Add(Component);
		}
		if (Targets.IsEmpty())
		{
			continue;
		}

		// Deterministic order so a dry run and the commit that follows report
		// the same names.
		Targets.Sort([](const UActorComponent& A, const UActorComponent& B)
		{
			return A.GetName() < B.GetName();
		});

		TArray<FString> RemovedNames;
		TArray<FString> SkippedNames;
		for (UActorComponent* Component : Targets)
		{
			++MatchedComponents;
			// Only instance components can be removed. A native or SCS
			// component belongs to the class, and destroying it here would
			// come back on the next construction rerun looking like the
			// removal silently failed.
			const bool bIsInstanceComponent = Actor->GetInstanceComponents().Contains(Component);
			if (!bIsInstanceComponent)
			{
				++SkippedNonInstance;
				SkippedNames.Add(Component->GetName());
				continue;
			}
			if (bDryRun)
			{
				RemovedNames.Add(Component->GetName());
				continue;
			}
			const FString Name = Component->GetName();
			Actor->Modify();
			Component->Modify();
			Actor->RemoveInstanceComponent(Component);
			Component->DestroyComponent();
			RemovedNames.Add(Name);
			++RemovedComponents;
		}

		if (RemovedNames.IsEmpty() && SkippedNames.IsEmpty())
		{
			continue;
		}
		++TouchedActors;
		if (Rows.Num() < MCPBatchMaxResultRows)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			Row->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
			Row->SetNumberField(bDryRun ? TEXT("wouldRemove") : TEXT("removed"), RemovedNames.Num());
			Row->SetArrayField(TEXT("components"), MCPStringListToJson(RemovedNames));
			if (SkippedNames.Num() > 0)
			{
				Row->SetArrayField(TEXT("skippedNonInstanceComponents"), MCPStringListToJson(SkippedNames));
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	Result->SetNumberField(TEXT("scannedActors"), ScannedActors);
	Result->SetNumberField(TEXT("actorsAffected"), TouchedActors);
	Result->SetNumberField(TEXT("matchedComponents"), MatchedComponents);
	Result->SetNumberField(
		bDryRun ? TEXT("wouldRemoveComponents") : TEXT("removedComponents"),
		bDryRun ? MatchedComponents - SkippedNonInstance : RemovedComponents);
	Result->SetNumberField(TEXT("skippedNonInstanceComponents"), SkippedNonInstance);
	Result->SetNumberField(TEXT("returnedResults"), Rows.Num());
	Result->SetBoolField(TEXT("resultsTruncated"), Rows.Num() < TouchedActors);
	Result->SetArrayField(TEXT("results"), Rows);

	if (MatchedComponents == 0)
	{
		Result->SetStringField(TEXT("zeroMatchNote"), FString::Printf(
			TEXT("No component of class '%s' was found on any of the %d actors enumerated. Check the class name with level(query_components, componentClass:'%s', countOnly:true) before assuming there is nothing to remove."),
			*ComponentClass->GetName(), ScannedActors, *ComponentClassSpec));
	}

	if (!bDryRun && RemovedComponents > 0)
	{
		MCPSetUpdated(Result);
		if (bSave)
		{
			const bool bSaved = FEditorFileUtils::SaveCurrentLevel();
			Result->SetBoolField(TEXT("saved"), bSaved);
			if (!bSaved)
			{
				Result->SetStringField(TEXT("saveError"),
					TEXT("The removal landed but saving the level failed. The change is in memory and undoable; save from the editor or call level(save)."));
			}
		}
		else
		{
			Result->SetBoolField(TEXT("saved"), false);
			Result->SetStringField(TEXT("saveNote"),
				TEXT("The level is left dirty and is NOT saved. Pass save=true, or call level(save), to commit it."));
		}
	}
	else if (bDryRun)
	{
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("dryRunNote"),
			TEXT("dryRun defaults to TRUE for this action because it deletes components whose auto-generated names you cannot enumerate. Pass dryRun=false to commit."));
	}

	// A removal, across many actors, of components whose property state was
	// never captured. level(add_component_to_actor) takes one actor and one
	// component per call and constructs a default component, so no single call
	// is the inverse of this and even the per-actor replay is not a restore.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("results[].components names every component removed, per actor, and componentClass names the class, so level(add_component_to_actor) can put an empty one back on each. Their property values, transforms and attachments were not captured and do not come back. dryRun=true, which is the default here, is the preflight."));

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// spawn_actors_batch (#987)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::SpawnActorsBatch(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	REQUIRE_EDITOR_WORLD(World);

	FString ActorClassSpec;
	if (auto Err = RequireString(Params, TEXT("actorClass"), ActorClassSpec)) return Err;
	UClass* ActorClass = MCPResolveClassOfType(ActorClassSpec, AActor::StaticClass(), true);
	if (!ActorClass)
	{
		return MCPClassNotFoundError(ActorClassSpec, TEXT("actorClass"));
	}
	if (auto Err = MCPCheckClassUsable(ActorClassSpec, ActorClass, AActor::StaticClass()))
	{
		return Err;
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const FString LabelPrefix = OptionalString(Params, TEXT("labelPrefix"));
	const int32 MaxSpawn = FMath::Clamp(
		OptionalInt(Params, TEXT("maxSpawn"), MCPBatchDefaultMaxSpawn), 1, MCPBatchHardMaxSpawn);
	const FString TransactionLabel = OptionalString(
		Params, TEXT("transactionLabel"), TEXT("MCP spawn actors batch"));

	const TSharedPtr<FJsonObject>* SharedProperties = nullptr;
	Params->TryGetObjectField(TEXT("properties"), SharedProperties);

	// ── Build the transform list ────────────────────────────────────────────
	//
	// Three sources, and exactly one of them per call. The derived ones exist
	// because the transforms in #987 were a function of geometry already in
	// the editor: round-tripping 190 component bounds out and 190 transforms
	// back in is what made it a Python job.
	struct FPlannedSpawn
	{
		FTransform Transform;
		FString Label;
		FString Source;
		TSharedPtr<FJsonObject> Properties;
	};
	TArray<FPlannedSpawn> Planned;
	FString PlanSource;

	const TArray<TSharedPtr<FJsonValue>>* InstanceValues = nullptr;
	const TSharedPtr<FJsonObject>* FromComponents = nullptr;
	const TSharedPtr<FJsonObject>* AlongSpline = nullptr;
	const bool bHasInstances = Params->TryGetArrayField(TEXT("instances"), InstanceValues) && InstanceValues;
	const bool bHasFromComponents = Params->TryGetObjectField(TEXT("fromComponents"), FromComponents) && FromComponents;
	const bool bHasAlongSpline = Params->TryGetObjectField(TEXT("alongSpline"), AlongSpline) && AlongSpline;

	const int32 SourceCount = (bHasInstances ? 1 : 0) + (bHasFromComponents ? 1 : 0) + (bHasAlongSpline ? 1 : 0);
	if (SourceCount == 0)
	{
		return MCPError(TEXT("Pass exactly one of 'instances' (explicit transforms), 'fromComponents' (one actor per matched component's bounds) or 'alongSpline' (scatter by distance)"));
	}
	if (SourceCount > 1)
	{
		return MCPError(TEXT("Pass only ONE of 'instances', 'fromComponents' or 'alongSpline'; combining them makes the spawn count ambiguous"));
	}

	if (bHasInstances)
	{
		PlanSource = TEXT("instances");
		for (int32 Index = 0; Index < InstanceValues->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Entry = (*InstanceValues)[Index];
			if (!Entry.IsValid() || Entry->Type != EJson::Object)
			{
				return MCPError(FString::Printf(TEXT("'instances[%d]' must be an object"), Index));
			}
			const TSharedPtr<FJsonObject> Row = Entry->AsObject();
			const TSharedPtr<FJsonObject>* LocationObject = nullptr;
			const TSharedPtr<FJsonObject>* RotationObject = nullptr;
			const TSharedPtr<FJsonObject>* ScaleObject = nullptr;
			Row->TryGetObjectField(TEXT("location"), LocationObject);
			Row->TryGetObjectField(TEXT("rotation"), RotationObject);
			Row->TryGetObjectField(TEXT("scale"), ScaleObject);

			FPlannedSpawn Spawn;
			Spawn.Transform = FTransform(
				RotationObject ? MCPBatchReadRotator(*RotationObject, FRotator::ZeroRotator) : FRotator::ZeroRotator,
				LocationObject ? MCPBatchReadVector(*LocationObject, FVector::ZeroVector) : FVector::ZeroVector,
				ScaleObject ? MCPBatchReadVector(*ScaleObject, FVector::OneVector) : FVector::OneVector);
			Row->TryGetStringField(TEXT("label"), Spawn.Label);
			Spawn.Source = TEXT("instance");
			const TSharedPtr<FJsonObject>* RowProperties = nullptr;
			if (Row->TryGetObjectField(TEXT("properties"), RowProperties) && RowProperties)
			{
				Spawn.Properties = *RowProperties;
			}
			Planned.Add(MoveTemp(Spawn));
		}
	}
	else if (bHasFromComponents)
	{
		PlanSource = TEXT("fromComponents");
		const TSharedPtr<FJsonObject> Source = *FromComponents;

		const FString ComponentClassSpec = OptionalString(Source, TEXT("componentClass"));
		UClass* ComponentClass = ComponentClassSpec.IsEmpty()
			? nullptr
			: MCPResolveClassOfType(ComponentClassSpec, UActorComponent::StaticClass(), true);
		if (!ComponentClassSpec.IsEmpty() && !ComponentClass)
		{
			return MCPClassNotFoundError(ComponentClassSpec, TEXT("fromComponents.componentClass"));
		}
		const FString ComponentNameContains = OptionalString(Source, TEXT("componentNameContains"));

		FMCPBatchSelector Selector;
		if (auto Err = MCPBatchReadSelector(Source, Selector)) return Err;

		// Local by default, and this is the whole point of the mode. A
		// component's WORLD bounds are axis-aligned, so on a rotated lamp post
		// "the top of the mesh" computed from world bounds is not on the lamp
		// post. The local bounds carry the component's own orientation, and
		// transforming the offset by the component transform puts the spawn
		// where a human would point.
		const FString Space = OptionalString(Source, TEXT("space"), TEXT("local")).ToLower();
		if (Space != TEXT("local") && Space != TEXT("world"))
		{
			return MCPError(TEXT("'fromComponents.space' must be either 'local' (default) or 'world'"));
		}
		const bool bLocal = Space == TEXT("local");

		const TSharedPtr<FJsonObject>* OffsetObject = nullptr;
		Source->TryGetObjectField(TEXT("offset"), OffsetObject);
		const FVector Offset = OffsetObject ? MCPBatchReadVector(*OffsetObject, FVector::ZeroVector) : FVector::ZeroVector;

		const TSharedPtr<FJsonObject>* FractionObject = nullptr;
		Source->TryGetObjectField(TEXT("extentFraction"), FractionObject);
		const FVector ExtentFraction = FractionObject
			? MCPBatchReadVector(*FractionObject, FVector::ZeroVector)
			: FVector::ZeroVector;

		const bool bInheritRotation = OptionalBool(Source, TEXT("inheritRotation"), false);

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			if (Selector.bAny && !MCPBatchActorMatches(Selector, Actor, Actor->GetActorLabel())) continue;

			for (UActorComponent* Component : Actor->GetComponents())
			{
				USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
				if (!SceneComponent) continue;
				if (ComponentClass && !SceneComponent->IsA(ComponentClass)) continue;
				if (!ComponentNameContains.IsEmpty() &&
					!SceneComponent->GetName().Contains(ComponentNameContains, ESearchCase::IgnoreCase))
				{
					continue;
				}

				const FTransform ComponentTransform = SceneComponent->GetComponentTransform();
				FVector Location;
				if (bLocal)
				{
					const FBoxSphereBounds LocalBounds = SceneComponent->GetLocalBounds();
					const FVector LocalPoint =
						LocalBounds.Origin + LocalBounds.BoxExtent * ExtentFraction + Offset;
					Location = ComponentTransform.TransformPosition(LocalPoint);
				}
				else
				{
					const FBoxSphereBounds WorldBounds = MCPComponentWorldBounds(*SceneComponent);
					Location = WorldBounds.Origin + WorldBounds.BoxExtent * ExtentFraction + Offset;
				}

				FPlannedSpawn Spawn;
				Spawn.Transform = FTransform(
					bInheritRotation ? ComponentTransform.Rotator() : FRotator::ZeroRotator,
					Location,
					FVector::OneVector);
				Spawn.Source = FString::Printf(TEXT("%s.%s"), *Actor->GetActorLabel(), *SceneComponent->GetName());
				Planned.Add(MoveTemp(Spawn));
			}
		}
		Planned.Sort([](const FPlannedSpawn& A, const FPlannedSpawn& B) { return A.Source < B.Source; });
	}
	else
	{
		PlanSource = TEXT("alongSpline");
		const TSharedPtr<FJsonObject> Source = *AlongSpline;

		FString SplineActorLabel;
		if (auto Err = RequireStringAlt(Source, TEXT("actorLabel"), TEXT("actorPath"), SplineActorLabel)) return Err;
		// #983: the spline the batch runs along is selected the same way as
		// any other actor, and a duplicated label refuses rather than picking
		// a spline at the other end of the map to scatter along.
		TSharedPtr<FJsonValue> SplineErr;
		AActor* SplineActor = MCPResolveActor(World, Source, SplineErr);
		if (!SplineActor) return SplineErr;
		SplineActorLabel = SplineActor->GetActorLabel();
		const FString SplineComponentName = OptionalString(Source, TEXT("componentName"));
		USplineComponent* Spline = nullptr;
		for (UActorComponent* Component : SplineActor->GetComponents())
		{
			USplineComponent* Candidate = Cast<USplineComponent>(Component);
			if (!Candidate) continue;
			if (SplineComponentName.IsEmpty() ||
				Candidate->GetName().Equals(SplineComponentName, ESearchCase::IgnoreCase))
			{
				Spline = Candidate;
				break;
			}
		}
		if (!Spline)
		{
			return MCPError(FString::Printf(
				TEXT("No SplineComponent%s on actor '%s'"),
				SplineComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *SplineComponentName),
				*SplineActorLabel));
		}

		const double Spacing = OptionalNumber(Source, TEXT("spacing"), 0.0);
		if (Spacing < MCPBatchMinSplineSpacing)
		{
			return MCPError(FString::Printf(
				TEXT("'alongSpline.spacing' must be at least %.0f cm; a smaller value would spawn an unbounded number of actors"),
				MCPBatchMinSplineSpacing));
		}
		const double SplineLength = Spline->GetSplineLength();
		const double StartDistance = FMath::Clamp(
			OptionalNumber(Source, TEXT("startDistance"), 0.0), 0.0, SplineLength);
		const double EndDistance = FMath::Clamp(
			OptionalNumber(Source, TEXT("endDistance"), SplineLength), StartDistance, SplineLength);
		const bool bAlignToTangent = OptionalBool(Source, TEXT("alignToTangent"), true);

		const TSharedPtr<FJsonObject>* OffsetObject = nullptr;
		Source->TryGetObjectField(TEXT("offset"), OffsetObject);
		const FVector Offset = OffsetObject ? MCPBatchReadVector(*OffsetObject, FVector::ZeroVector) : FVector::ZeroVector;

		for (double Distance = StartDistance; Distance <= EndDistance; Distance += Spacing)
		{
			const FRotator Rotation = Spline->GetRotationAtDistanceAlongSpline(
				Distance, ESplineCoordinateSpace::World);
			FVector Location = Spline->GetLocationAtDistanceAlongSpline(
				Distance, ESplineCoordinateSpace::World);
			// The offset follows the spline's frame when aligning to it, so
			// "20cm to the left of the path" stays to the left round a corner.
			Location += bAlignToTangent ? Rotation.RotateVector(Offset) : Offset;

			FPlannedSpawn Spawn;
			Spawn.Transform = FTransform(
				bAlignToTangent ? Rotation : FRotator::ZeroRotator, Location, FVector::OneVector);
			Spawn.Source = FString::Printf(TEXT("%s@%.0f"), *Spline->GetName(), Distance);
			Planned.Add(MoveTemp(Spawn));

			if (Planned.Num() > MCPBatchHardMaxSpawn)
			{
				break;
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetStringField(TEXT("actorClass"), ActorClass->GetPathName());
	Result->SetStringField(TEXT("source"), PlanSource);
	Result->SetNumberField(TEXT("planned"), Planned.Num());

	if (Planned.Num() > MaxSpawn)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The plan produced %d spawns, over maxSpawn (%d). Narrow the selector or raise maxSpawn deliberately."),
			Planned.Num(), MaxSpawn));
		Result->SetNumberField(TEXT("maxSpawn"), MaxSpawn);
		return MCPResult(Result);
	}
	if (Planned.IsEmpty())
	{
		Result->SetNumberField(TEXT("spawned"), 0);
		Result->SetArrayField(TEXT("results"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetStringField(TEXT("zeroMatchNote"),
			TEXT("The plan produced no positions. For fromComponents, check componentClass and the actor selector with level(query_components, countOnly:true); for alongSpline, check the spline length against startDistance/endDistance."));
		return MCPResult(Result);
	}

	TUniquePtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TransactionLabel));
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Spawned = 0;
	int32 Failed = 0;
	int32 PropertyFailures = 0;

	for (int32 Index = 0; Index < Planned.Num(); ++Index)
	{
		const FPlannedSpawn& Plan = Planned[Index];
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), Index);
		Row->SetStringField(TEXT("from"), Plan.Source);
		Row->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Plan.Transform.GetLocation()));
		Row->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Plan.Transform.Rotator()));
		Row->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Plan.Transform.GetScale3D()));

		if (bDryRun)
		{
			Row->SetBoolField(TEXT("ok"), true);
			if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(ActorClass, Plan.Transform, SpawnParameters);
		if (!Actor)
		{
			++Failed;
			Row->SetBoolField(TEXT("ok"), false);
			Row->SetStringField(TEXT("error"), TEXT("SpawnActor returned null"));
			if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		++Spawned;

		FString Label = Plan.Label;
		if (Label.IsEmpty() && !LabelPrefix.IsEmpty())
		{
			Label = FString::Printf(TEXT("%s%d"), *LabelPrefix, Index);
		}
		if (!Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}
		Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Row->SetBoolField(TEXT("ok"), true);

		// Per-instance properties override the shared ones, so a caller can
		// set one field per spawn without repeating the rest.
		TArray<TSharedPtr<FJsonValue>> PropertyRows;
		auto ApplyProperties = [&](const TSharedPtr<FJsonObject>& Properties)
		{
			if (!Properties.IsValid()) return;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
			{
				TSharedPtr<FJsonObject> SubParams = MakeShared<FJsonObject>();
				SubParams->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
				SubParams->SetStringField(TEXT("propertyName"), Pair.Key);
				SubParams->SetField(TEXT("value"), Pair.Value);
				SubParams->SetBoolField(TEXT("force"), true);

				bool bOk = false;
				FString Error;
				FString Previous;
				MCPBatchReadVerdict(FLevelHandlers::SetActorProperty(SubParams), bOk, Error, Previous);
				TSharedPtr<FJsonObject> PropertyRow = MakeShared<FJsonObject>();
				PropertyRow->SetStringField(TEXT("propertyName"), Pair.Key);
				PropertyRow->SetBoolField(TEXT("ok"), bOk);
				if (!bOk)
				{
					PropertyRow->SetStringField(TEXT("error"), Error);
					++PropertyFailures;
				}
				PropertyRows.Add(MakeShared<FJsonValueObject>(PropertyRow));
			}
		};
		ApplyProperties(SharedProperties ? *SharedProperties : nullptr);
		ApplyProperties(Plan.Properties);
		if (PropertyRows.Num() > 0)
		{
			Row->SetArrayField(TEXT("properties"), PropertyRows);
		}

		if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	Result->SetBoolField(TEXT("success"), Failed == 0);
	if (Failed > 0)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d planned spawns failed; see results[]."), Failed, Planned.Num()));
	}
	Result->SetNumberField(bDryRun ? TEXT("wouldSpawn") : TEXT("spawned"), bDryRun ? Planned.Num() : Spawned);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("propertyFailures"), PropertyFailures);
	Result->SetNumberField(TEXT("returnedResults"), Rows.Num());
	Result->SetBoolField(TEXT("resultsTruncated"), Rows.Num() < Planned.Num());
	Result->SetArrayField(TEXT("results"), Rows);
	if (!bDryRun && Spawned > 0)
	{
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("saveNote"),
			TEXT("The level is left dirty and is NOT saved. Call level(save) when you are done."));
	}
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Undoing this means deleting the actors it spawned, and level(delete_actor) takes one actor path per call while level(delete_actors) takes filters rather than a path list. Deleting by the labels these actors carry would reach any namesake as well, so no inverse is named. results[].actorLabel is the list to delete one at a time."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_component_materials (#946)
//
// material(build_material) writes the ASSET's material slots. A placed actor
// usually needs the other thing: a per-slot COMPONENT override, which is what
// the details panel edits and what SetMaterial writes into OverrideMaterials.
// There was no action for that. set_actor_material does one slot, on whichever
// primitive component it happens to find first, so dressing a placed skeletal
// mesh meant a Python loop over get_num_materials().
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::SetComponentMaterials(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	REQUIRE_EDITOR_WORLD(World);

	FMCPBatchSelector Selector;
	if (auto Err = MCPBatchReadSelector(Params, Selector)) return Err;
	if (!Selector.bAny)
	{
		return MCPError(TEXT("Pass at least one selector: actorLabels, labelPrefix, labelContains, tag, classFilter, folderPath or folderPathPrefix"));
	}

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bClearOverrides = OptionalBool(Params, TEXT("clearOverrides"), false);
	const FString SingleMaterialPath = OptionalString(Params, TEXT("material"));

	const TArray<TSharedPtr<FJsonValue>>* MaterialValues = nullptr;
	const bool bHasMaterialList = Params->TryGetArrayField(TEXT("materials"), MaterialValues) && MaterialValues;

	const int32 ModeCount = (bHasMaterialList ? 1 : 0) + (SingleMaterialPath.IsEmpty() ? 0 : 1) + (bClearOverrides ? 1 : 0);
	if (ModeCount == 0)
	{
		return MCPError(TEXT("Pass 'materials' (per-slot paths), 'material' (one path applied to every slot) or clearOverrides=true"));
	}
	if (ModeCount > 1)
	{
		return MCPError(TEXT("Pass only ONE of 'materials', 'material' or clearOverrides; together they would fight over the same slots"));
	}

	// Resolve every requested material up front. A path that does not load is
	// a caller error, and finding that out after writing half the slots on
	// half the actors is not a useful place to find it out.
	TArray<UMaterialInterface*> SlotMaterials;
	if (bHasMaterialList)
	{
		for (int32 Index = 0; Index < MaterialValues->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Entry = (*MaterialValues)[Index];
			FString Path;
			const bool bIsClear = !Entry.IsValid() || Entry->Type == EJson::Null ||
				(Entry->TryGetString(Path) && Path.IsEmpty());
			if (bIsClear)
			{
				// An explicit null clears that one slot's override, so a caller
				// can reset slot 2 without touching the rest.
				SlotMaterials.Add(nullptr);
				continue;
			}
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path);
			if (!Material)
			{
				return MCPError(FString::Printf(
					TEXT("Material not found for 'materials[%d]': %s"), Index, *Path));
			}
			SlotMaterials.Add(Material);
		}
	}
	UMaterialInterface* SingleMaterial = nullptr;
	if (!SingleMaterialPath.IsEmpty())
	{
		SingleMaterial = LoadObject<UMaterialInterface>(nullptr, *SingleMaterialPath);
		if (!SingleMaterial)
		{
			return MCPError(FString::Printf(TEXT("Material not found: %s"), *SingleMaterialPath));
		}
	}

	TArray<AActor*> Actors;
	MCPBatchCollectActors(World, Selector, Actors);
	if (Actors.Num() > MCPBatchMaxActors)
	{
		return MCPError(FString::Printf(
			TEXT("The selector matched %d actors, over the maximum of %d. Narrow it."),
			Actors.Num(), MCPBatchMaxActors));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("matchedActors"), Actors.Num());
	MCPNoteLoadedOnlyEnumeration(World, Result);
	{
		const TArray<FString> Missing = MCPBatchMissingLabels(Selector, Actors);
		if (Missing.Num() > 0)
		{
			Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(Missing));
		}
	}

	TUniquePtr<FScopedTransaction> Transaction;
	if (!bDryRun && !Actors.IsEmpty())
	{
		Transaction = MakeUnique<FScopedTransaction>(FText::FromString(
			OptionalString(Params, TEXT("transactionLabel"), TEXT("MCP set component materials"))));
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Updated = 0;
	int32 WithoutComponent = 0;
	int32 SlotWrites = 0;
	int32 OutOfRange = 0;

	for (AActor* Actor : Actors)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());

		UMeshComponent* MeshComponent = nullptr;
		if (ComponentName.IsEmpty())
		{
			MeshComponent = Actor->FindComponentByClass<UMeshComponent>();
		}
		else
		{
			MeshComponent = Cast<UMeshComponent>(MCPBatchFindComponent(Actor, ComponentName));
		}
		if (!MeshComponent)
		{
			++WithoutComponent;
			Row->SetBoolField(TEXT("ok"), false);
			Row->SetStringField(TEXT("status"), TEXT("no_mesh_component"));
			if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		Row->SetStringField(TEXT("componentName"), MeshComponent->GetName());
		Row->SetStringField(TEXT("componentClass"), MeshComponent->GetClass()->GetName());

		const int32 SlotCount = MeshComponent->GetNumMaterials();
		Row->SetNumberField(TEXT("slotCount"), SlotCount);
		const TArray<FName> SlotNames = MeshComponent->GetMaterialSlotNames();

		if (bHasMaterialList && SlotMaterials.Num() > SlotCount)
		{
			// Extra entries would silently do nothing, which reads as a
			// successful assignment that did not happen.
			++OutOfRange;
			Row->SetBoolField(TEXT("ok"), false);
			Row->SetStringField(TEXT("status"), TEXT("too_many_materials"));
			Row->SetStringField(TEXT("error"), FString::Printf(
				TEXT("%d materials given but the component has %d slots"), SlotMaterials.Num(), SlotCount));
			if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> SlotRows;
		const int32 WriteCount = bHasMaterialList ? SlotMaterials.Num() : SlotCount;
		for (int32 SlotIndex = 0; SlotIndex < WriteCount; ++SlotIndex)
		{
			UMaterialInterface* Before = MeshComponent->GetMaterial(SlotIndex);
			const bool bWasOverride =
				MeshComponent->OverrideMaterials.IsValidIndex(SlotIndex) &&
				MeshComponent->OverrideMaterials[SlotIndex] != nullptr;

			UMaterialInterface* Desired = nullptr;
			if (bHasMaterialList) Desired = SlotMaterials[SlotIndex];
			else if (SingleMaterial) Desired = SingleMaterial;

			if (!bDryRun)
			{
				MeshComponent->Modify();
				// A null override is how the asset's own slot shows through
				// again, which is the inverse of what this action does and is
				// otherwise unreachable.
				MeshComponent->SetMaterial(SlotIndex, bClearOverrides ? nullptr : Desired);
				++SlotWrites;
			}

			UMaterialInterface* After = bDryRun ? Before : MeshComponent->GetMaterial(SlotIndex);
			const bool bIsOverride = bDryRun
				? bWasOverride
				: (MeshComponent->OverrideMaterials.IsValidIndex(SlotIndex) &&
				   MeshComponent->OverrideMaterials[SlotIndex] != nullptr);

			TSharedPtr<FJsonObject> SlotRow = MakeShared<FJsonObject>();
			SlotRow->SetNumberField(TEXT("slotIndex"), SlotIndex);
			SlotRow->SetStringField(TEXT("slotName"),
				SlotNames.IsValidIndex(SlotIndex) ? SlotNames[SlotIndex].ToString() : FString());
			SlotRow->SetStringField(TEXT("previousMaterialPath"), Before ? Before->GetPathName() : FString());
			SlotRow->SetStringField(TEXT("previousSource"),
				bWasOverride ? TEXT("componentOverride") : (Before ? TEXT("meshDefault") : TEXT("none")));
			SlotRow->SetStringField(TEXT("materialPath"), After ? After->GetPathName() : FString());
			// Read back rather than echoing: the difference between "the
			// component now overrides this slot" and "the asset happens to
			// have that material" is the whole distinction #946 is about.
			SlotRow->SetStringField(TEXT("source"),
				bIsOverride ? TEXT("componentOverride") : (After ? TEXT("meshDefault") : TEXT("none")));
			SlotRows.Add(MakeShared<FJsonValueObject>(SlotRow));
		}

		if (!bDryRun)
		{
			MeshComponent->MarkRenderStateDirty();
			Actor->MarkPackageDirty();
			++Updated;
		}
		Row->SetBoolField(TEXT("ok"), true);
		Row->SetStringField(TEXT("status"), bDryRun ? TEXT("would_write") : TEXT("written"));
		Row->SetArrayField(TEXT("slots"), SlotRows);
		if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	Result->SetBoolField(TEXT("success"), OutOfRange == 0);
	if (OutOfRange > 0)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d actor(s) were given more materials than their component has slots; see results[]."), OutOfRange));
	}
	Result->SetNumberField(bDryRun ? TEXT("wouldUpdate") : TEXT("updated"),
		bDryRun ? Actors.Num() - WithoutComponent : Updated);
	Result->SetNumberField(TEXT("slotWrites"), SlotWrites);
	Result->SetNumberField(TEXT("actorsWithoutMeshComponent"), WithoutComponent);
	Result->SetNumberField(TEXT("returnedResults"), Rows.Num());
	Result->SetBoolField(TEXT("resultsTruncated"), Rows.Num() < Actors.Num());
	Result->SetArrayField(TEXT("results"), Rows);
	if (!bDryRun && Updated > 0)
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("saveNote"),
			TEXT("These are COMPONENT overrides on placed actors, so the LEVEL is dirty and is NOT saved. The mesh ASSET is untouched; use material(build_material) or asset(set_property) to write the asset's own slots."));
	}
	// Restoring this is a per-actor slot array, not one shared value: each
	// component had its own previous material per slot, and a slot that showed
	// the mesh default has to be cleared rather than assigned. This action
	// takes one materials list for everything its label-addressed selector
	// matched, so no single call expresses that.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("results[].slots[] carries each slot's previousMaterialPath and its previousSource, which says whether the slot was a component override or the mesh default. Replay them per actor with an explicit actorLabels of one, passing a null for every slot whose previousSource was not componentOverride."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_actor_hlod_layer (#985)
// ---------------------------------------------------------------------------
//
// Assigning an HLODLayer override across 295 InstancedFoliageActors was a
// Python loop, for the same reason every other action in this file exists: the
// selector vocabulary lived on the client and every actor cost a round trip.
//
// This lives beside the other selector-driven batch writes rather than with the
// World Partition settings, because the selector, the bounds, the dry run and
// the missing-label reporting are all shared with them, and a second copy of
// that vocabulary would be one more thing to drift.
//
// The write goes through AActor::SetHLODLayer rather than through the reflected
// property. HLODLayer is a private UPROPERTY and reflection would reach it, but
// the engine's own setter is what the HLOD system expects to have been called,
// and a bulk write that skipped it would be the kind of edit that looks applied
// and behaves differently from one made in the details panel.
TSharedPtr<FJsonValue> FLevelHandlers::SetActorHLODLayer(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	REQUIRE_EDITOR_WORLD(World);

	// null clears the override, which is a legitimate request and has to be
	// distinguishable from "the parameter was omitted".
	if (!Params->HasField(TEXT("hlodLayer")))
	{
		return MCPError(TEXT("Missing 'hlodLayer': an HLODLayer asset path, or null to clear the per-actor override"));
	}
	FString LayerPath;
	Params->TryGetStringField(TEXT("hlodLayer"), LayerPath);
	LayerPath.TrimStartAndEndInline();

	UHLODLayer* Layer = nullptr;
	if (!LayerPath.IsEmpty())
	{
		Layer = LoadAssetByPath<UHLODLayer>(LayerPath);
		if (!Layer) return MCPAssetLoadError(LayerPath, TEXT("UHLODLayer"));
	}

	FMCPBatchSelector Selector;
	if (auto Err = MCPBatchReadSelector(Params, Selector)) return Err;
	if (!Selector.bAny)
	{
		return MCPError(TEXT("Pass at least one selector: actorLabels, labelPrefix, labelContains, tag, classFilter, folderPath or folderPathPrefix"));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bHasAutoLODField = Params->HasField(TEXT("enableAutoLODGeneration"));
	const bool bEnableAutoLOD = OptionalBool(Params, TEXT("enableAutoLODGeneration"), true);
	const FString TransactionLabel = OptionalString(
		Params, TEXT("transactionLabel"), TEXT("MCP set actor HLOD layer"));

	TArray<AActor*> Actors;
	MCPBatchCollectActors(World, Selector, Actors);
	if (Actors.Num() > MCPBatchMaxActors)
	{
		return MCPError(FString::Printf(
			TEXT("The selector matched %d actors, over the maximum of %d. Narrow it."),
			Actors.Num(), MCPBatchMaxActors));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetStringField(TEXT("hlodLayer"), Layer ? Layer->GetPathName() : FString());
	Result->SetBoolField(TEXT("clearing"), Layer == nullptr);
	Result->SetNumberField(TEXT("matched"), Actors.Num());
	MCPNoteLoadedOnlyEnumeration(World, Result);
	{
		const TArray<FString> Missing = MCPBatchMissingLabels(Selector, Actors);
		if (Missing.Num() > 0)
		{
			Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(Missing));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Updated = 0;
	int32 Unchanged = 0;
	{
		const FScopedTransaction Transaction(FText::FromString(TransactionLabel));
		for (AActor* Actor : Actors)
		{
			if (!Actor) continue;
			UHLODLayer* Previous = Actor->GetHLODLayer();
			const bool bWouldChange = Previous != Layer;

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			Row->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
			Row->SetStringField(TEXT("previousHLODLayer"), Previous ? Previous->GetPathName() : FString());

			if (!bWouldChange && !bHasAutoLODField)
			{
				// Already what was asked for. Reporting it as an update would
				// make a no-op look like work, which is what makes a rerun
				// impossible to reason about.
				++Unchanged;
				Row->SetStringField(TEXT("status"), TEXT("unchanged"));
				if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
				continue;
			}

			if (bDryRun)
			{
				Row->SetStringField(TEXT("status"), TEXT("would_write"));
				if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
				continue;
			}

			Actor->Modify();
			Actor->SetHLODLayer(Layer);
			if (bHasAutoLODField)
			{
				// The layer only matters when the actor is allowed to build
				// HLODs at all, so the two are settable in one call.
				Actor->bEnableAutoLODGeneration = bEnableAutoLOD;
			}
			Actor->PostEditChange();
			Actor->MarkPackageDirty();
			++Updated;

			UHLODLayer* After = Actor->GetHLODLayer();
			Row->SetStringField(TEXT("hlodLayer"), After ? After->GetPathName() : FString());
			Row->SetBoolField(TEXT("enableAutoLODGeneration"), Actor->bEnableAutoLODGeneration);
			Row->SetStringField(TEXT("status"), TEXT("written"));
			if (Rows.Num() < MCPBatchMaxResultRows) Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	Result->SetNumberField(bDryRun ? TEXT("wouldUpdate") : TEXT("updated"),
		bDryRun ? Actors.Num() - Unchanged : Updated);
	Result->SetNumberField(TEXT("unchanged"), Unchanged);
	Result->SetNumberField(TEXT("returnedResults"), Rows.Num());
	Result->SetBoolField(TEXT("resultsTruncated"), Rows.Num() < Actors.Num());
	Result->SetArrayField(TEXT("results"), Rows);
	if (!bDryRun && Updated > 0)
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("saveNote"),
			TEXT("On a World Partition map each actor lives in its own package, so this dirties one package per actor. Save them with level(save) or editor(save_dirty). HLOD assignment takes effect on the next HLOD build."));
	}
	// Each actor had its own previous layer, and this action writes one layer
	// across everything its label-addressed selector matched. A blanket
	// re-assignment is not the inverse of that, and on a World Partition map
	// the actors are re-homed per package, so nothing here is index-addressed
	// either.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("results[].previousHLODLayer carries each actor's own layer from before the write, an empty string meaning it had no override. Replay them one actor at a time with an explicit actorLabels of one. enableAutoLODGeneration, when it was passed, is not captured per actor and does not come back."));
	return MCPResult(Result);
}
