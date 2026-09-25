// Transient verification actors (#956).
//
// Verifying anything that only exists on a live actor (a GAS attribute set, a
// component's runtime state) needs a subject, and there was no way to make one
// without leaving it behind. Spawning a normal actor to check something and
// then forgetting to delete it is #966's exact shape: a capture action left a
// SceneCapture2D in the map and it got committed.
//
// So these actors are transient by construction rather than by discipline:
//
//   RF_Transient means the package serializer skips them, so a save cannot
//   write one into the map even if the caller never destroys it.
//   bTemporaryEditorActor marks the actor as an editor preview actor, which
//   keeps it out of the level's persistent actor list.
//   bCreateActorPackage is off, so a World Partition map does not mint an
//   external actor package for it, which is the artefact that would actually
//   reach source control.
//   The label is set with bMarkDirty=false, because SetActorLabel dirties the
//   map by default and a verification spawn that dirties the map has already
//   failed at its one job.
//
// Every one of them also carries a marker tag, so they can be listed and so
// destroy refuses anything it did not create.

#include "LevelHandlers.h"

#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HandlerEditorState.h"
#include "HandlerJsonProperty.h"
#include "HandlerPagination.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "JsonSerializer.h"

namespace
{
	/** The marker every actor spawned by this action carries. Listing and
	 *  destruction both key on it, so nothing else can be destroyed by
	 *  accident and nothing spawned here can be missed. */
	const TCHAR* const MCPTransientActorTag = TEXT("UEMCP_TransientVerification");

	constexpr int32 MCPTransientMaxListed = 500;

	TSharedPtr<FJsonObject> MCPDescribeTransientActor(AActor* Actor)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Object->SetStringField(TEXT("actorName"), Actor->GetName());
		Object->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Object->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetPathName());
		Object->SetBoolField(TEXT("transient"), Actor->HasAnyFlags(RF_Transient));
		Object->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Actor->GetActorLocation()));
		return Object;
	}
}

// ---------------------------------------------------------------------------
// spawn_transient_actor (#956)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::SpawnTransientActor(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

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

	const FVector Location = OptionalVec3(Params, TEXT("location"), FVector::ZeroVector);
	const FRotator Rotation = OptionalRotator(Params, TEXT("rotation"), FRotator::ZeroRotator);
	const FVector Scale = OptionalVec3(Params, TEXT("scale"), FVector::OneVector);
	const FString Label = OptionalString(Params, TEXT("label"));
	const bool bHideFromOutliner = OptionalBool(Params, TEXT("hideFromOutliner"), false);

	// How far to take the actor towards a running state. The editor world has
	// not begun play, so a spawned actor gets neither InitializeComponent nor
	// BeginPlay, and anything that does its setup there (a GAS ability system
	// component's default subobject scan, for instance) is not ready to be
	// read. Each level is opt-in because each does more to the editor world.
	const FString Initialize = OptionalString(Params, TEXT("initialize"), TEXT("construction")).ToLower();
	if (Initialize != TEXT("none") && Initialize != TEXT("construction") && Initialize != TEXT("beginplay"))
	{
		return MCPError(TEXT("'initialize' must be 'none', 'construction' (default) or 'beginPlay'"));
	}

	TArray<FString> DirtyBefore;
	MCPEditorState::CollectDirtyEditorPackageNames(DirtyBefore);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// The transiency contract, stated three ways so no single engine path can
	// quietly persist this actor.
	SpawnParameters.ObjectFlags |= RF_Transient;
#if WITH_EDITOR
	SpawnParameters.bTemporaryEditorActor = true;
	SpawnParameters.bHideFromSceneOutliner = bHideFromOutliner;
	SpawnParameters.bCreateActorPackage = false;
#endif

	AActor* Actor = World->SpawnActor<AActor>(
		ActorClass, FTransform(Rotation, Location, Scale), SpawnParameters);
	if (!Actor)
	{
		return MCPError(FString::Printf(
			TEXT("Failed to spawn a transient %s"), *ActorClass->GetName()));
	}

	// Belt and braces: SpawnActor does not always propagate ObjectFlags to
	// every path, and the transiency claim in the response has to be true.
	Actor->SetFlags(RF_Transient);
	Actor->Tags.AddUnique(FName(MCPTransientActorTag));
	if (!Label.IsEmpty())
	{
		// bMarkDirty=false. The default marks the map package dirty, and a
		// verification spawn that dirties the map has failed at its one job.
		Actor->SetActorLabel(Label, /*bMarkDirty*/ false);
	}

	// #1103: properties go through set_actor_property, the same path
	// spawn_actors_batch uses, before construction and BeginPlay so both see
	// them. Every entry is reported with the value read back, and a failed one
	// fails the call rather than reporting a configured actor that is not.
	TArray<TSharedPtr<FJsonValue>> PropertyRows;
	int32 PropertyFailures = 0;
	const TSharedPtr<FJsonObject>* Properties = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), Properties) && Properties && Properties->IsValid())
	{
		for (const auto& Entry : (*Properties)->Values)
		{
			TSharedPtr<FJsonObject> SubParams = MakeShared<FJsonObject>();
			SubParams->SetStringField(TEXT("actorPath"), Actor->GetPathName());
			SubParams->SetStringField(TEXT("propertyName"), Entry.Key);
			SubParams->SetField(TEXT("value"), Entry.Value);
			SubParams->SetBoolField(TEXT("force"), true);
			if (World->IsGameWorld()) SubParams->SetStringField(TEXT("world"), TEXT("pie"));
			if (Params->HasField(TEXT("pieInstance"))) SubParams->SetField(TEXT("pieInstance"), Params->TryGetField(TEXT("pieInstance")));

			const TSharedPtr<FJsonValue> Response = FLevelHandlers::SetActorProperty(SubParams);
			const TSharedPtr<FJsonObject> ResponseObject =
				Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
			bool bOk = false;
			FString Error = TEXT("set_actor_property returned no result");
			if (ResponseObject.IsValid())
			{
				bOk = !ResponseObject->HasField(TEXT("success")) || ResponseObject->GetBoolField(TEXT("success"));
				Error.Reset();
				ResponseObject->TryGetStringField(TEXT("error"), Error);
			}

			TSharedPtr<FJsonObject> PropertyRow = MakeShared<FJsonObject>();
			PropertyRow->SetStringField(TEXT("propertyName"), Entry.Key);
			PropertyRow->SetBoolField(TEXT("ok"), bOk);
			if (!bOk)
			{
				PropertyRow->SetStringField(TEXT("error"), Error);
				++PropertyFailures;
			}
			PropertyRows.Add(MakeShared<FJsonValueObject>(PropertyRow));
		}
	}

	FString InitializeNote;
#if WITH_EDITOR
	if (Initialize == TEXT("construction"))
	{
		Actor->RerunConstructionScripts();
		InitializeNote = TEXT("Construction scripts ran. Components exist and their editable defaults are applied, but InitializeComponent and BeginPlay have NOT run, because the editor world has not begun play.");
	}
	else if (Initialize == TEXT("beginplay"))
	{
		Actor->RerunConstructionScripts();
		Actor->RegisterAllComponents();
		Actor->DispatchBeginPlay();
		InitializeNote = TEXT("Components were registered and the begin-play cycle was dispatched on this actor, so InitializeComponent and BeginPlay ran. This is what a component that does its setup in BeginPlay needs before it can be read. It runs on this actor only, in a world that has not begun play, so anything depending on other actors or on world subsystems may still be absent.");
	}
	else
	{
		InitializeNote = TEXT("initialize='none': the actor exists with its default subobjects and nothing else has been run on it.");
	}
#else
	InitializeNote = TEXT("Non-editor build: no construction or begin-play cycle was run.");
#endif

	// Read back after initialization, so the report shows what the actor holds
	// now rather than what the write returned.
	for (const TSharedPtr<FJsonValue>& RowValue : PropertyRows)
	{
		const TSharedPtr<FJsonObject> PropertyRow = RowValue->AsObject();
		if (!PropertyRow.IsValid() || !PropertyRow->GetBoolField(TEXT("ok"))) continue;
		FProperty* Prop = nullptr;
		void* ValueAddr = nullptr;
		UObject* LeafOwner = nullptr;
		FString ReadError;
		if (MCPJsonProperty::ResolveDottedPath(
				Actor, PropertyRow->GetStringField(TEXT("propertyName")), Prop, ValueAddr, LeafOwner, ReadError))
		{
			PropertyRow->SetField(TEXT("value"), FMCPJsonSerializer::SerializeValue(ValueAddr, Prop));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Components;
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (!Component) continue;
		TSharedPtr<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
		ComponentObject->SetStringField(TEXT("name"), Component->GetName());
		ComponentObject->SetStringField(TEXT("class"), Component->GetClass()->GetName());
		ComponentObject->SetBoolField(TEXT("registered"), Component->IsRegistered());
		// The read side of #956 needs this: a component whose setup happens in
		// InitializeComponent is not ready until this is true.
		ComponentObject->SetBoolField(TEXT("initialized"), Component->HasBeenInitialized());
		Components.Add(MakeShared<FJsonValueObject>(ComponentObject));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetObjectField(TEXT("actor"), MCPDescribeTransientActor(Actor));
	Result->SetStringField(TEXT("initialize"), Initialize);
	Result->SetStringField(TEXT("initializeNote"), InitializeNote);
	Result->SetArrayField(TEXT("components"), Components);
	if (PropertyRows.Num() > 0)
	{
		Result->SetArrayField(TEXT("properties"), PropertyRows);
		Result->SetNumberField(TEXT("propertyFailures"), PropertyFailures);
	}
	if (PropertyFailures > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The actor was spawned but %d of %d properties did not apply; see properties[]. It is still in the world, so destroy it or fix the values."),
			PropertyFailures, PropertyRows.Num()));
	}
	Result->SetStringField(TEXT("cleanupNote"), FString::Printf(
		TEXT("Destroy this with level(destroy_transient_actor, actorPath:'%s') when you are done. It is RF_Transient and a save cannot write it into the map, but it stays in the open world until it is destroyed or the map is reloaded."),
		*Actor->GetPathName()));

	// The #966-shaped guarantee, asserted rather than promised: if spawning
	// this dirtied anything, the caller is told which package.
	TArray<FString> DirtyAfter;
	MCPEditorState::CollectDirtyEditorPackageNames(DirtyAfter);
	for (const FString& Already : DirtyBefore)
	{
		DirtyAfter.Remove(Already);
	}
	Result->SetBoolField(TEXT("dirtiedPackages"), DirtyAfter.Num() > 0);
	if (DirtyAfter.Num() > 0)
	{
		Result->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(DirtyAfter));
		Result->SetStringField(TEXT("dirtyWarning"),
			TEXT("This spawn dirtied a package, which it is not supposed to do. Destroy the actor and do not save until you have checked what changed."));
	}

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	MCPSetRollback(Result, TEXT("destroy_transient_actor"), Rollback);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// destroy_transient_actor (#956)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::DestroyTransientActor(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));
	const FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));
	const bool bAll = OptionalBool(Params, TEXT("all"), false);
	if (ActorPath.IsEmpty() && ActorLabel.IsEmpty() && !bAll)
	{
		return MCPError(TEXT("Pass 'actorPath', 'actorLabel', or all=true to destroy every transient verification actor in this world"));
	}

	const FName MarkerTag(MCPTransientActorTag);
	TArray<AActor*> Targets;
	TArray<FString> RefusedNonTransient;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		bool bSelected = bAll;
		if (!bSelected && !ActorPath.IsEmpty()) bSelected = Actor->GetPathName() == ActorPath;
		if (!bSelected && !ActorLabel.IsEmpty()) bSelected = Actor->GetActorLabel() == ActorLabel;
		if (!bSelected) continue;

		// The guardrail. This action destroys things, and a label or a path is
		// easy to get wrong, so it will only destroy an actor it created. A
		// normal placed actor selected by mistake is refused by name rather
		// than deleted.
		if (!Actor->Tags.Contains(MarkerTag) || !Actor->HasAnyFlags(RF_Transient))
		{
			if (!bAll)
			{
				RefusedNonTransient.Add(FString::Printf(
					TEXT("%s (%s)"), *Actor->GetActorLabel(), *Actor->GetClass()->GetName()));
			}
			continue;
		}
		Targets.Add(Actor);
	}

	auto Result = MCPSuccess();
	if (!RefusedNonTransient.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"),
			TEXT("Refusing to destroy an actor this action did not create. Only actors spawned by level(spawn_transient_actor), which carry the UEMCP_TransientVerification tag and RF_Transient, can be destroyed here. Use level(delete_actor) or level(delete_actors) for a real level actor."));
		Result->SetArrayField(TEXT("refused"), MCPStringListToJson(RefusedNonTransient));
		return MCPResult(Result);
	}

	// What the inverse needs, read while the actor still exists. A destroyed
	// actor cannot be asked what class it was or where it stood.
	struct FDestroyedTransient
	{
		FString ClassPath;
		FString Label;
		FTransform Transform;
	};
	TArray<FDestroyedTransient> Restorable;

	TArray<FString> Destroyed;
	TArray<FString> Failed;
	for (AActor* Actor : Targets)
	{
		const FString Description = FString::Printf(
			TEXT("%s (%s)"), *Actor->GetActorLabel(), *Actor->GetClass()->GetName());
		FDestroyedTransient Snapshot;
		Snapshot.ClassPath = Actor->GetClass()->GetPathName();
		Snapshot.Label = Actor->GetActorLabel();
		Snapshot.Transform = Actor->GetActorTransform();
		if (World->DestroyActor(Actor))
		{
			Destroyed.Add(Description);
			Restorable.Add(MoveTemp(Snapshot));
		}
		else
		{
			Failed.Add(Description);
		}
	}

	Result->SetBoolField(TEXT("success"), Failed.IsEmpty());
	if (!Failed.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d transient actor(s) refused to be destroyed; see failed."), Failed.Num()));
	}
	Result->SetNumberField(TEXT("matched"), Targets.Num());
	Result->SetNumberField(TEXT("destroyed"), Destroyed.Num());
	// A replay of the same call finds nothing left to destroy, which is a
	// success that changed nothing. Saying so is what lets a retried step tell
	// the two apart.
	Result->SetBoolField(TEXT("alreadyDeleted"), Targets.IsEmpty());
	Result->SetArrayField(TEXT("destroyedActors"), MCPStringListToJson(Destroyed));
	Result->SetArrayField(TEXT("failed"), MCPStringListToJson(Failed));
	// The spawn_transient_actor call that brings one of them back, in that
	// action's own parameter names so it can be replayed unedited.
	auto MakeSpawnPayload = [&WorldScope](const FDestroyedTransient& Entry)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("world"), WorldScope);
		Payload->SetStringField(TEXT("actorClass"), Entry.ClassPath);
		if (!Entry.Label.IsEmpty()) Payload->SetStringField(TEXT("label"), Entry.Label);
		Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Entry.Transform.GetLocation()));
		Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Entry.Transform.Rotator()));
		Payload->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Entry.Transform.GetScale3D()));
		return Payload;
	};

	if (Restorable.Num() == 1)
	{
		MCPSetRollback(Result, TEXT("spawn_transient_actor"), MakeSpawnPayload(Restorable[0]));
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The inverse spawns a fresh verification actor of the same class at the same transform. It is a new actor with a new object path, so anything written onto the destroyed one after it was spawned is not restored."));
	}
	else if (Restorable.Num() > 1)
	{
		// One rollback record is one call, so a batch destroy has no single
		// inverse. The per-actor calls are handed over instead of described.
		TArray<TSharedPtr<FJsonValue>> RestoreCalls;
		for (const FDestroyedTransient& Entry : Restorable)
		{
			RestoreCalls.Add(MakeShared<FJsonValueObject>(MakeSpawnPayload(Entry)));
		}
		Result->SetArrayField(TEXT("restorable"), RestoreCalls);
		MCPSetNoRollback(Result, FString::Printf(
			TEXT("%d transient verification actors were destroyed in one call, and level(spawn_transient_actor) makes one actor at a time, so no single call undoes the batch. ")
			TEXT("Each entry of 'restorable' is a ready spawn_transient_actor payload for one of them."),
			Restorable.Num()));
	}
	if (Targets.IsEmpty())
	{
		Result->SetStringField(TEXT("zeroMatchNote"),
			TEXT("Nothing matched. A transient verification actor does not survive a map reload or an editor restart, so it may simply be gone already. level(list_transient_actors) shows what is still there."));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// list_transient_actors (#956)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::ListTransientActors(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	// T3: paged. This stopped at MCPTransientMaxListed and reported `truncated`
	// with no way to reach the rest, which on a run that spawned more than that
	// left the caller unable to see what it still had to clean up.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_transient_actors|world=%s"), *WorldScope),
			/*DefaultLimit*/ MCPTransientMaxListed, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	const FName MarkerTag(MCPTransientActorTag);
	TArray<MCPPagination::FPageRow> Rows;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !Actor->Tags.Contains(MarkerTag)) continue;
		// The actor path is the anchor: verification actors are spawned with a
		// shared label prefix, so the label does not name one of them.
		Rows.Add({ Actor->GetPathName(), MakeShared<FJsonValueObject>(MCPDescribeTransientActor(Actor)) });
	}

	// TActorIterator order is not a contract, and this list changes shape by
	// construction as verification actors are spawned and destroyed, so it is
	// sorted before paging.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("worldName"), World->GetName());
	MCPPagination::EmitPage(Page, Rows, TEXT("actors"), Result);
	Result->SetNumberField(TEXT("returned"), Result->GetIntegerField(TEXT("count")));
	Result->SetStringField(TEXT("note"),
		TEXT("These are RF_Transient verification actors spawned by level(spawn_transient_actor). A save cannot write them into the map, and they do not survive a map reload, but they are in the open world until destroyed."));
	return MCPResult(Result);
}
