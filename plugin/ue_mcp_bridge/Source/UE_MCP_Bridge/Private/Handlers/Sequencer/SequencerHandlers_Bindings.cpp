// What a Level Sequence's bindings resolve to: in the editor world, after a
// retarget (#1356), per runtime player in a PIE or game world (#1232), and the
// components of each binding's template or possessed actor (#1640).

#include "SequencerHandlers.h"
#include "SequencerHandlers_Internal.h"
#include "HandlerSequencerTracks.h"
#include "HandlerUtils.h"

#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "DefaultLevelSequenceInstanceData.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSequencePlayer.h"
#include "UniversalObjectLocatorResolveParams.h"
#include "Components/SceneComponent.h"
#include "Components/ActorComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/UObjectHash.h"
#include "HandlerQuery.h"
#include "GameFramework/Actor.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "Editor.h"

namespace UEMCP::SequencerBindings
{
	using namespace UEMCP::SequencerTracks;

	TSharedPtr<FJsonObject> DescribeBoundObject(UObject* Object)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Object->GetName());
		Obj->SetStringField(TEXT("path"), Object->GetPathName());
		Obj->SetStringField(TEXT("class"), Object->GetClass()->GetName());
		FTransform World;
		bool bHasTransform = false;
		if (AActor* Actor = Cast<AActor>(Object))
		{
			Obj->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			World = Actor->GetActorTransform();
			bHasTransform = true;
		}
		else if (USceneComponent* Comp = Cast<USceneComponent>(Object))
		{
			if (AActor* Owner = Comp->GetOwner()) Obj->SetStringField(TEXT("ownerActorLabel"), Owner->GetActorLabel());
			World = Comp->GetComponentTransform();
			bHasTransform = true;
		}
		if (bHasTransform)
		{
			Obj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(World.GetLocation()));
			Obj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(World.Rotator()));
			Obj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(World.GetScale3D()));
		}
		return Obj;
	}

	/** Objects one binding resolves to in a world. A child possessable resolves against its parent's object. */
	TArray<UObject*> LocateInWorld(ULevelSequence* Sequence, UMovieScene* MovieScene, const FGuid& Guid, UWorld* World, int32 Depth = 0)
	{
		TArray<UObject*> Result;
		if (Depth > 16) return Result;
		UObject* Context = World;
		if (const FMovieScenePossessable* P = MovieScene->FindPossessable(Guid))
		{
			if (P->GetParent().IsValid())
			{
				TArray<UObject*> Parents = LocateInWorld(Sequence, MovieScene, P->GetParent(), World, Depth + 1);
				if (Parents.Num() == 0) return Result;
				Context = Parents[0];
			}
		}
		TArray<UObject*, TInlineAllocator<1>> Found;
		LocateBound(Sequence, Guid, Context, Found);
		for (UObject* O : Found)
		{
			// A deleted actor stays resolvable while the undo buffer holds it, so it
			// counts only while its level still lists it (#1356).
			if (!IsValid(O)) continue;
			if (const AActor* Actor = Cast<AActor>(O))
			{
				const ULevel* Level = Actor->GetLevel();
				if (Actor->IsActorBeingDestroyed() || !Level || !Level->Actors.Contains(Actor)) continue;
			}
			Result.Add(O);
		}
		return Result;
	}

	FString BindingName(UMovieScene* MovieScene, const FGuid& Guid)
	{
		if (const FMovieScenePossessable* P = MovieScene->FindPossessable(Guid)) return P->GetName();
		if (const FMovieSceneSpawnable* S = MovieScene->FindSpawnable(Guid)) return S->GetName();
		return FString();
	}

	void CollectBindingGuids(UMovieScene* MovieScene, TArray<FGuid>& Out, TMap<FGuid, FString>& Types)
	{
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FGuid G = MovieScene->GetPossessable(i).GetGuid();
			Out.Add(G);
			Types.Add(G, TEXT("possessable"));
		}
		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
		{
			const FGuid G = MovieScene->GetSpawnable(i).GetGuid();
			Out.Add(G);
			Types.Add(G, TEXT("spawnable"));
		}
	}

	TArray<TSharedPtr<FJsonValue>> PathsOf(const TArray<UObject*>& Objects)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (UObject* O : Objects) Out.Add(MakeShared<FJsonValueString>(O->GetPathName()));
		return Out;
	}

	/** The component a child possessable should follow on the new actor: mapped name, then binding name, then a unique class match. */
	UActorComponent* FindChildComponent(AActor* Actor, const FMovieScenePossessable& Child, const FString& MappedName)
	{
		const FString Wanted = MappedName.IsEmpty() ? Child.GetName() : MappedName;
		if (UActorComponent* ByName = MCPFindComponentByName<UActorComponent>(Actor, Wanted)) return ByName;
		if (!MappedName.IsEmpty()) return nullptr;
#if WITH_EDITORONLY_DATA
		const UClass* ChildClass = Child.GetPossessedObjectClass();
		if (ChildClass)
		{
			UActorComponent* Only = nullptr;
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp && Comp->IsA(ChildClass))
				{
					if (Only) return nullptr;
					Only = Comp;
				}
			}
			return Only;
		}
#endif
		return nullptr;
	}

	// ─── get_sequence_binding_components (#1640) ─────────────────────────

	/** Rendering flags read when the caller names no properties: what decides how a bound primitive lights, shadows and holds out. */
	const TCHAR* const DefaultBindingComponentProperties[] = {
		TEXT("CastShadow"), TEXT("bAffectDynamicIndirectLighting"), TEXT("bAffectDistanceFieldLighting"),
		TEXT("bVisibleInRayTracing"), TEXT("bHoldout"), TEXT("LightingChannels"),
		TEXT("bRenderCustomDepth"), TEXT("CustomDepthStencilValue"), TEXT("CustomDepthStencilWriteMask"),
		TEXT("bRenderInMainPass"), TEXT("bRenderInDepthPass"), TEXT("bVisible"), TEXT("bHiddenInGame"),
	};
	constexpr int32 MaxPropertiesPerComponent = 400;

	/** Lowercase without underscores, so cast_shadow and CastShadow compare equal. */
	FString PropertyKey(const FString& Name)
	{
		return Name.Replace(TEXT("_"), TEXT("")).ToLower();
	}

	/** The C++ name of a property named in C++ or Python spelling (b_holdout, affect_dynamic_indirect_lighting), or empty. */
	FString ResolvePropertyName(const UStruct* Struct, const FString& Requested)
	{
		if (const FProperty* Exact = Struct->FindPropertyByName(FName(*Requested))) return Exact->GetName();
		const FString Wanted = PropertyKey(Requested);
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FString Key = PropertyKey(It->GetName());
			if (Key == Wanted) return It->GetName();
			if (CastField<FBoolProperty>(*It) && Key.StartsWith(TEXT("b")) && Key.Mid(1) == Wanted) return It->GetName();
		}
		return FString();
	}

	/** Read one property or dotted path off a component. The head segment may use Python spelling. */
	bool ReadComponentProperty(UObject* Component, const FString& Requested, FString& OutPath, TSharedPtr<FJsonValue>& OutValue)
	{
		int32 Split = INDEX_NONE;
		for (int32 i = 0; i < Requested.Len(); ++i)
		{
			if (Requested[i] == TEXT('.') || Requested[i] == TEXT('[')) { Split = i; break; }
		}
		const FString Head = Split == INDEX_NONE ? Requested : Requested.Left(Split);
		const FString Resolved = ResolvePropertyName(Component->GetClass(), Head);
		if (Resolved.IsEmpty()) return false;
		const FString FullPath = Split == INDEX_NONE ? Resolved : Resolved + Requested.Mid(Split);
		FString Type;
		FString ResolvedPath;
		TSharedPtr<FJsonValue> Value = MCPQuery::ReadDottedProperty(Component, FullPath, Type, &ResolvedPath);
		if (!Value.IsValid()) return false;
		OutPath = ResolvedPath.IsEmpty() ? FullPath : ResolvedPath;
		OutValue = Value;
		return true;
	}

	/** True when the class or one of its parents is named Filter, with or without the U prefix. */
	bool ClassMatches(const UClass* Class, const FString& Filter)
	{
		for (const UClass* C = Class; C; C = C->GetSuperClass())
		{
			const FString Name = C->GetName();
			if (Name.Equals(Filter, ESearchCase::IgnoreCase) || (TEXT("U") + Name).Equals(Filter, ESearchCase::IgnoreCase)) return true;
		}
		return false;
	}

	struct FBindingComponent
	{
		UActorComponent* Component = nullptr;
		FString Name;
		FString Source;
	};

	/**
	 * The components a bound object carries. A component binds as itself. An actor
	 * contributes its own components, and for a Blueprint class the construction
	 * script templates it holds no instance of, since a template only carries the
	 * components that existed when it was copied.
	 */
	void CollectBindingComponents(UObject* Root, TArray<FBindingComponent>& Out)
	{
		if (UActorComponent* AsComponent = Cast<UActorComponent>(Root))
		{
			Out.Add({ AsComponent, AsComponent->GetName(), TEXT("bound") });
			return;
		}
		AActor* Actor = Cast<AActor>(Root);
		if (!Actor) return;

		TSet<UObject*> Seen;
		TSet<FName> Names;
		auto Add = [&](UActorComponent* Comp, const FName Name, const TCHAR* Source)
		{
			if (!Comp || Seen.Contains(Comp) || Names.Contains(Name)) return;
			Seen.Add(Comp);
			Names.Add(Name);
			Out.Add({ Comp, Name.ToString(), Source });
		};
		for (UActorComponent* Comp : Actor->GetComponents()) if (Comp) Add(Comp, Comp->GetFName(), TEXT("instance"));
		TArray<UObject*> Inner;
		GetObjectsWithOuter(Actor, Inner, false);
		for (UObject* Obj : Inner)
		{
			if (UActorComponent* Comp = Cast<UActorComponent>(Obj)) Add(Comp, Comp->GetFName(), TEXT("instance"));
		}

		UBlueprintGeneratedClass* ActualClass = Cast<UBlueprintGeneratedClass>(Actor->GetClass());
		for (UClass* C = Actor->GetClass(); C && ActualClass; C = C->GetSuperClass())
		{
			UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C);
			if (!BPGC || !BPGC->SimpleConstructionScript) continue;
			for (USCS_Node* Node : BPGC->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node) continue;
				Add(Node->GetActualComponentTemplate(ActualClass), Node->GetVariableName(), TEXT("blueprintClassTemplate"));
			}
		}
	}

	/** One component with the requested property values. Names it does not have go to missingProperties when they were asked for. */
	TSharedPtr<FJsonObject> DescribeBindingComponent(const FBindingComponent& Entry, const TArray<FString>& Requested, bool bAll, bool bReportMissing)
	{
		UActorComponent* Comp = Entry.Component;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Entry.Name);
		Obj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		Obj->SetStringField(TEXT("path"), Comp->GetPathName());
		Obj->SetStringField(TEXT("source"), Entry.Source);
		if (const USceneComponent* Scene = Cast<USceneComponent>(Comp))
		{
			if (const USceneComponent* Parent = Scene->GetAttachParent()) Obj->SetStringField(TEXT("attachParent"), Parent->GetName());
		}

		TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Missing;
		bool bTruncated = false;
		if (bAll)
		{
			int32 Count = 0;
			for (TFieldIterator<FProperty> It(Comp->GetClass()); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Deprecated)) continue;
				if (Count >= MaxPropertiesPerComponent) { bTruncated = true; break; }
				Values->SetField(Prop->GetName(), MCPQuery::PropertyToJson(Prop, Prop->ContainerPtrToValuePtr<void>(Comp)));
				++Count;
			}
		}
		for (const FString& Name : Requested)
		{
			FString Path;
			TSharedPtr<FJsonValue> Value;
			if (ReadComponentProperty(Comp, Name, Path, Value)) Values->SetField(Path, Value);
			else if (bReportMissing) Missing.Add(MakeShared<FJsonValueString>(Name));
		}
		Obj->SetObjectField(TEXT("properties"), Values);
		if (Missing.Num() > 0) Obj->SetArrayField(TEXT("missingProperties"), Missing);
		if (bTruncated) Obj->SetBoolField(TEXT("propertiesTruncated"), true);
		return Obj;
	}
}

// resolve_sequence_bindings (#1356): what every binding resolves to in the
// editor world right now. A possessable that resolves to nothing is stale.
TSharedPtr<FJsonValue> FSequencerHandlers::ResolveBindings(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::SequencerTracks;
	using namespace UEMCP::SequencerBindings;

	FString Path;
	const bool bHasPath = TryGetStringParam(Params, TEXT("sequencePath"), Path);
	const FString GuidText = OptionalString(Params, TEXT("bindingGuid"));
	const FString NameText = OptionalString(Params, TEXT("bindingName"));

	if (auto PieErr = MCPRefuseDuringPlayInEditor(TEXT("resolve_sequence_bindings"))) return PieErr;

	TSharedPtr<FJsonValue> LoadErr;
	ULevelSequence* Sequence = LoadSequence(Path, bHasPath, LoadErr);
	if (!Sequence) return LoadErr;
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));
	REQUIRE_EDITOR_WORLD(World);

	TArray<FGuid> Guids;
	TMap<FGuid, FString> Types;
	CollectBindingGuids(MovieScene, Guids, Types);
	if (!GuidText.IsEmpty() || !NameText.IsEmpty())
	{
		FGuid Only;
		FString Err;
		if (!FindBindingByNameOrGuid(MovieScene, GuidText, NameText, Only, Err)) return MCPError(Err);
		Guids = { Only };
	}

	int32 Stale = 0;
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FGuid& Guid : Guids)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), BindingName(MovieScene, Guid));
		Obj->SetStringField(TEXT("guid"), Guid.ToString());
		const FString Type = Types.FindRef(Guid);
		Obj->SetStringField(TEXT("type"), Type);
		if (const FMovieScenePossessable* P = MovieScene->FindPossessable(Guid))
		{
			if (P->GetParent().IsValid()) Obj->SetStringField(TEXT("parentGuid"), P->GetParent().ToString());
#if WITH_EDITORONLY_DATA
			if (const UClass* Cls = P->GetPossessedObjectClass()) Obj->SetStringField(TEXT("possessedClass"), Cls->GetName());
#endif
		}
		TArray<TSharedPtr<FJsonValue>> Bound;
		const TArray<UObject*> Objects = Type == TEXT("possessable") ? LocateInWorld(Sequence, MovieScene, Guid, World) : TArray<UObject*>();
		for (UObject* O : Objects) Bound.Add(MakeShared<FJsonValueObject>(DescribeBoundObject(O)));
		Obj->SetArrayField(TEXT("boundObjects"), Bound);
		const bool bStale = Type == TEXT("possessable") && Objects.Num() == 0;
		Obj->SetBoolField(TEXT("resolved"), Objects.Num() > 0);
		Obj->SetBoolField(TEXT("stale"), bStale);
		if (Type == TEXT("spawnable"))
		{
			Obj->SetStringField(TEXT("note"), TEXT("Spawnables have no object until the sequence plays; get_sequence_runtime_bindings reports what a player spawned."));
		}
		Stale += bStale ? 1 : 0;
		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("sequencePath"), Sequence->GetPathName());
	Result->SetStringField(TEXT("world"), World->GetPathName());
	Result->SetArrayField(TEXT("bindings"), Out);
	Result->SetNumberField(TEXT("bindingCount"), Out.Num());
	Result->SetNumberField(TEXT("staleCount"), Stale);
	if (Stale > 0)
	{
		Result->SetStringField(TEXT("hint"), TEXT("A stale possessable points at an actor this world no longer holds. editor(rebind_possessable) retargets it to a current actor and keeps its tracks."));
	}
	return MCPResult(Result);
}

// rebind_possessable (#1356): point an existing possessable, and its child
// component possessables, at a current actor. The binding guid is kept, so its
// tracks and every reference to it (camera cuts, attach sections) stay valid.
TSharedPtr<FJsonValue> FSequencerHandlers::RebindPossessable(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::SequencerTracks;
	using namespace UEMCP::SequencerBindings;

	FString Path;
	const bool bHasPath = TryGetStringParam(Params, TEXT("sequencePath"), Path);
	const FString GuidText = OptionalString(Params, TEXT("bindingGuid"));
	const FString NameText = OptionalString(Params, TEXT("bindingName"));
	const FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));
	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));
	const bool bChildren = OptionalBool(Params, TEXT("includeChildren"), true);
	const bool bRename = OptionalBool(Params, TEXT("renameBinding"));
	const TSharedPtr<FJsonObject>* ComponentMap = nullptr;
	TryGetObjectParam(Params, TEXT("componentMap"), ComponentMap);

	if (auto PieErr = MCPRefuseDuringPlayInEditor(TEXT("rebind_possessable"))) return PieErr;

	TSharedPtr<FJsonValue> LoadErr;
	ULevelSequence* Sequence = LoadSequence(Path, bHasPath, LoadErr);
	if (!Sequence) return LoadErr;
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));
	REQUIRE_EDITOR_WORLD(World);

	if (GuidText.IsEmpty() && NameText.IsEmpty())
	{
		return MCPError(TEXT("Pass bindingGuid or bindingName to choose the possessable to retarget"));
	}
	FGuid Guid;
	FString Err;
	if (!FindBindingByNameOrGuid(MovieScene, GuidText, NameText, Guid, Err)) return MCPError(Err);
	FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid);
	if (!Possessable)
	{
		return MCPError(FString::Printf(TEXT("Binding %s is a spawnable; only possessables bind to level actors"), *Guid.ToString()));
	}
	if (Possessable->GetParent().IsValid())
	{
		return MCPError(TEXT("That binding is a child (component) possessable. Rebind its parent actor binding; children follow it."));
	}

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	if (!Sequence->CanPossessObject(*Actor, World))
	{
		return MCPError(FString::Printf(TEXT("The sequence cannot possess '%s'"), *Actor->GetActorLabel()));
	}

	// Before: what the binding pointed at, for the report and the rollback.
	const TArray<UObject*> BeforeObjects = LocateInWorld(Sequence, MovieScene, Guid, World);
	const FString PreviousName = Possessable->GetName();

	Sequence->Modify();
	MovieScene->Modify();

	Sequence->UnbindPossessableObjects(Guid);
	Sequence->BindPossessableObject(Guid, *Actor, World);
#if WITH_EDITORONLY_DATA
	Possessable->SetPossessedObjectClass(Actor->GetClass());
#endif
	if (bRename)
	{
		const FString NewName = Actor->GetActorLabel();
		Possessable->SetName(NewName);
#if !UE_MCP_HAS_5_7_API
		if (FMovieSceneBinding* Binding = MovieScene->FindBinding(Guid)) Binding->SetName(NewName);
#endif
#if WITH_EDITORONLY_DATA
		MovieScene->SetObjectDisplayName(Guid, FText::FromString(NewName));
#endif
	}

	// Children: each component possessable follows its component on the new actor.
	TArray<TSharedPtr<FJsonValue>> ChildrenJson;
	int32 ChildFailures = 0;
	if (bChildren)
	{
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			FMovieScenePossessable& Child = MovieScene->GetPossessable(i);
			if (Child.GetParent() != Guid) continue;
			const FGuid ChildGuid = Child.GetGuid();
			FString Mapped;
			if (ComponentMap && ComponentMap->IsValid())
			{
				if (!(*ComponentMap)->TryGetStringField(Child.GetName(), Mapped)) (*ComponentMap)->TryGetStringField(ChildGuid.ToString(), Mapped);
			}
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("name"), Child.GetName());
			C->SetStringField(TEXT("guid"), ChildGuid.ToString());
			UActorComponent* Comp = FindChildComponent(Actor, Child, Mapped);
			if (!Comp)
			{
				C->SetBoolField(TEXT("rebound"), false);
				C->SetStringField(TEXT("error"), FString::Printf(TEXT("No component on '%s' matches '%s'. Name it in componentMap {\"%s\": \"<component name>\"}."),
					*Actor->GetActorLabel(), *(Mapped.IsEmpty() ? Child.GetName() : Mapped), *Child.GetName()));
				++ChildFailures;
				ChildrenJson.Add(MakeShared<FJsonValueObject>(C));
				continue;
			}
			Sequence->UnbindPossessableObjects(ChildGuid);
			Sequence->BindPossessableObject(ChildGuid, *Comp, Actor);
#if WITH_EDITORONLY_DATA
			Child.SetPossessedObjectClass(Comp->GetClass());
#endif
			const TArray<UObject*> Located = LocateInWorld(Sequence, MovieScene, ChildGuid, World);
			const bool bVerified = Located.Contains(Comp);
			C->SetStringField(TEXT("component"), Comp->GetName());
			C->SetBoolField(TEXT("rebound"), true);
			C->SetBoolField(TEXT("verified"), bVerified);
			C->SetArrayField(TEXT("resolvesTo"), PathsOf(Located));
			if (!bVerified) ++ChildFailures;
			ChildrenJson.Add(MakeShared<FJsonValueObject>(C));
		}
	}

	Sequence->GetOutermost()->MarkPackageDirty();

	// Verification: the binding must now resolve to exactly the requested actor.
	const TArray<UObject*> AfterObjects = LocateInWorld(Sequence, MovieScene, Guid, World);
	const bool bVerified = AfterObjects.Num() == 1 && AfterObjects[0] == Actor;
	int32 TrackCount = 0;
	if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(Guid)) TrackCount = Binding->GetTracks().Num();

	if (!bVerified)
	{
		TSharedPtr<FJsonObject> ErrObj = MCPErrorObject(FString::Printf(
			TEXT("Rebound binding %s to '%s', but it resolves to %d object(s) instead of that actor"),
			*Guid.ToString(), *Actor->GetActorLabel(), AfterObjects.Num()));
		ErrObj->SetArrayField(TEXT("resolvesTo"), PathsOf(AfterObjects));
		return MCPResult(ErrObj);
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("sequencePath"), Sequence->GetPathName());
	Result->SetStringField(TEXT("bindingGuid"), Guid.ToString());
	Result->SetStringField(TEXT("bindingName"), Possessable->GetName());
	Result->SetStringField(TEXT("previousName"), PreviousName);
	Result->SetArrayField(TEXT("previouslyResolvedTo"), PathsOf(BeforeObjects));
	Result->SetBoolField(TEXT("wasStale"), BeforeObjects.Num() == 0);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetArrayField(TEXT("resolvesTo"), PathsOf(AfterObjects));
	Result->SetBoolField(TEXT("verified"), true);
	Result->SetNumberField(TEXT("trackCount"), TrackCount);
	Result->SetArrayField(TEXT("children"), ChildrenJson);
	Result->SetBoolField(TEXT("childrenVerified"), ChildFailures == 0);
	Result->SetBoolField(TEXT("unchanged"), BeforeObjects.Num() == 1 && BeforeObjects[0] == Actor && !bRename);
	if (ChildFailures > 0)
	{
		Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("%d child possessable(s) were not rebound or did not verify; see children"), ChildFailures));
	}
	Result->SetStringField(TEXT("saveNote"), TEXT("The sequence is marked dirty, not saved. asset(save) writes it."));

	AActor* PreviousActor = BeforeObjects.Num() == 1 ? Cast<AActor>(BeforeObjects[0]) : nullptr;
	if (PreviousActor && PreviousActor != Actor)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("sequencePath"), Sequence->GetPathName());
		Payload->SetStringField(TEXT("bindingGuid"), Guid.ToString());
		Payload->SetStringField(TEXT("actorPath"), PreviousActor->GetPathName());
		Payload->SetBoolField(TEXT("includeChildren"), bChildren);
		MCPSetRollback(Result, TEXT("rebind_possessable"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), bRename);
		if (bRename) Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback rebinds the previous actor but keeps the new binding name."));
	}
	else if (!PreviousActor)
	{
		MCPSetNoRollback(Result, TEXT("The binding resolved to no single actor before this call (it was stale), so there is nothing to rebind back to."));
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The binding already resolved to this actor, so there is no earlier target to restore."));
	}
	return MCPResult(Result);
}

// get_sequence_runtime_bindings (#1232): every LevelSequenceActor in a world,
// its player state and instance-data origin, and the objects each binding of
// that player resolves to. This is what tells an actor spawned by a player
// apart from the duplicates an open Sequencer editor spawns.
TSharedPtr<FJsonValue> FSequencerHandlers::GetRuntimeBindings(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::SequencerTracks;
	using namespace UEMCP::SequencerBindings;

	const FString Scope = OptionalString(Params, TEXT("world"), TEXT("auto"));
	UWorld* World = ResolveWorldFromParams(Params, TEXT("auto"));
	const FString SequenceFilter = OptionalString(Params, TEXT("sequencePath"));
	const FString ActorFilter = OptionalString(Params, TEXT("actorLabel"));

	if (!World)
	{
		return MCPError(FString::Printf(TEXT("No %s world is available. Start PIE, or pass world 'editor'."), *Scope));
	}

	TArray<TSharedPtr<FJsonValue>> ActorsJson;
	for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
	{
		ALevelSequenceActor* SeqActor = *It;
		if (!SeqActor) continue;
		if (!ActorFilter.IsEmpty() && !SeqActor->GetActorLabel().Equals(ActorFilter, ESearchCase::IgnoreCase) && !SeqActor->GetName().Equals(ActorFilter, ESearchCase::IgnoreCase)) continue;
		ULevelSequence* Sequence = SeqActor->GetSequence();
		if (!SequenceFilter.IsEmpty())
		{
			if (!Sequence) continue;
			const FString SeqPath = Sequence->GetPathName();
			if (!SeqPath.Equals(SequenceFilter, ESearchCase::IgnoreCase) && !SeqPath.StartsWith(SequenceFilter + TEXT("."), ESearchCase::IgnoreCase)
				&& !Sequence->GetName().Equals(SequenceFilter, ESearchCase::IgnoreCase)) continue;
		}

		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("actorLabel"), SeqActor->GetActorLabel());
		A->SetStringField(TEXT("actorPath"), SeqActor->GetPathName());
		A->SetStringField(TEXT("sequencePath"), Sequence ? Sequence->GetPathName() : FString());
		A->SetBoolField(TEXT("overrideInstanceData"), SeqActor->bOverrideInstanceData != 0);
		if (UDefaultLevelSequenceInstanceData* Instance = Cast<UDefaultLevelSequenceInstanceData>(SeqActor->DefaultInstanceData))
		{
			TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
			I->SetObjectField(TEXT("transformOrigin"), MCPTransformToJsonObject(Instance->TransformOrigin));
			I->SetStringField(TEXT("transformOriginActor"), Instance->TransformOriginActor ? Instance->TransformOriginActor->GetPathName() : FString());
			A->SetObjectField(TEXT("instanceData"), I);
		}

		ULevelSequencePlayer* Player = SeqActor->GetSequencePlayer();
		A->SetBoolField(TEXT("hasPlayer"), Player != nullptr);
		if (Player && Sequence)
		{
			A->SetBoolField(TEXT("isPlaying"), Player->IsPlaying());
			A->SetBoolField(TEXT("isPaused"), Player->IsPaused());
			const FQualifiedFrameTime Now = Player->GetCurrentTime();
			TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetNumberField(TEXT("frame"), Now.Time.AsDecimal());
			T->SetNumberField(TEXT("seconds"), Now.AsSeconds());
			UEMCP::SequencerInfo::SetFrameRateFields(*T, Now.Rate);
			A->SetObjectField(TEXT("currentTime"), T);

			UMovieScene* MovieScene = Sequence->GetMovieScene();
			TArray<FGuid> Guids;
			TMap<FGuid, FString> Types;
			if (MovieScene) CollectBindingGuids(MovieScene, Guids, Types);
			TArray<TSharedPtr<FJsonValue>> BindingsJson;
			for (const FGuid& Guid : Guids)
			{
				TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
				B->SetStringField(TEXT("name"), BindingName(MovieScene, Guid));
				B->SetStringField(TEXT("guid"), Guid.ToString());
				B->SetStringField(TEXT("type"), Types.FindRef(Guid));
				if (const FMovieScenePossessable* P = MovieScene->FindPossessable(Guid))
				{
					if (P->GetParent().IsValid()) B->SetStringField(TEXT("parentGuid"), P->GetParent().ToString());
				}
				TArray<TSharedPtr<FJsonValue>> Bound;
				for (UObject* O : Player->GetBoundObjects(FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(Guid))))
				{
					if (O) Bound.Add(MakeShared<FJsonValueObject>(DescribeBoundObject(O)));
				}
				B->SetArrayField(TEXT("boundObjects"), Bound);
				BindingsJson.Add(MakeShared<FJsonValueObject>(B));
			}
			A->SetArrayField(TEXT("bindings"), BindingsJson);
		}
		ActorsJson.Add(MakeShared<FJsonValueObject>(A));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), World->GetPathName());
	Result->SetStringField(TEXT("worldType"), World->IsPlayInEditor() ? TEXT("pie") : (World->IsGameWorld() ? TEXT("game") : TEXT("editor")));
	Result->SetArrayField(TEXT("sequenceActors"), ActorsJson);
	Result->SetNumberField(TEXT("sequenceActorCount"), ActorsJson.Num());
	return MCPResult(Result);
}

// get_sequence_binding_components (#1640): every binding's spawnable template,
// or the actor or component a possessable resolves to in the editor world, with
// its components and the requested property values. Nothing is opened or
// evaluated, so the values are what the sequence will start from.
TSharedPtr<FJsonValue> FSequencerHandlers::GetBindingComponents(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::SequencerTracks;
	using namespace UEMCP::SequencerBindings;

	FString Path;
	const bool bHasPath = TryGetStringParam(Params, TEXT("sequencePath"), Path);
	const FString GuidText = OptionalString(Params, TEXT("bindingGuid"));
	const FString NameText = OptionalString(Params, TEXT("bindingName"));
	const FString ClassFilter = OptionalString(Params, TEXT("componentClass"));
	const TArray<TSharedPtr<FJsonValue>>* PropertiesJson = nullptr;
	TryGetArrayParam(Params, TEXT("propertyNames"), PropertiesJson);

	if (auto PieErr = MCPRefuseDuringPlayInEditor(TEXT("get_sequence_binding_components"))) return PieErr;

	TSharedPtr<FJsonValue> LoadErr;
	ULevelSequence* Sequence = LoadSequence(Path, bHasPath, LoadErr);
	if (!Sequence) return LoadErr;
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));
	UWorld* World = GetEditorWorld();

	TArray<FString> Requested;
	bool bAll = false;
	const bool bExplicit = PropertiesJson && PropertiesJson->Num() > 0;
	if (bExplicit)
	{
		for (const TSharedPtr<FJsonValue>& Value : *PropertiesJson)
		{
			FString Name;
			if (!Value.IsValid() || !Value->TryGetString(Name) || Name.TrimStartAndEnd().IsEmpty())
			{
				return MCPError(TEXT("propertyNames must be an array of property names or dotted paths, or [\"*\"] for every editable property"));
			}
			Name.TrimStartAndEndInline();
			if (Name == TEXT("*")) bAll = true;
			else Requested.AddUnique(Name);
		}
	}
	else
	{
		for (const TCHAR* Name : DefaultBindingComponentProperties) Requested.Add(Name);
	}

	TArray<FGuid> Guids;
	TMap<FGuid, FString> Types;
	CollectBindingGuids(MovieScene, Guids, Types);
	if (!GuidText.IsEmpty() || !NameText.IsEmpty())
	{
		FGuid Only;
		FString Err;
		if (!FindBindingByNameOrGuid(MovieScene, GuidText, NameText, Only, Err)) return MCPError(Err);
		Guids = { Only };
	}

	int32 ComponentTotal = 0;
	int32 Unresolved = 0;
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FGuid& Guid : Guids)
	{
		TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetStringField(TEXT("name"), BindingName(MovieScene, Guid));
		B->SetStringField(TEXT("guid"), Guid.ToString());
		FString Type = Types.FindRef(Guid);
		const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid);
		if (Possessable && Possessable->GetParent().IsValid()) B->SetStringField(TEXT("parentGuid"), Possessable->GetParent().ToString());

		// Where the components come from: a template for a spawnable, the editor
		// world for a possessable.
		TArray<UObject*> Roots;
		FString Source;
		if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Guid))
		{
			if (UObject* Template = Spawnable->GetObjectTemplate())
			{
				Roots.Add(Template);
				Source = TEXT("spawnableTemplate");
			}
		}
#if UE_MCP_HAS_5_5_API
		if (Roots.Num() == 0)
		{
			const FMovieSceneBindingReferences* Refs = Sequence->GetBindingReferences();
			if (const UMovieSceneCustomBinding* Custom = Refs ? Refs->GetCustomBinding(Guid, 0) : nullptr)
			{
				B->SetStringField(TEXT("customBinding"), Custom->GetClass()->GetName());
				if (const FObjectProperty* Prop = CastField<FObjectProperty>(Custom->GetClass()->FindPropertyByName(TEXT("ActorTemplate"))))
				{
					if (UObject* Template = Prop->GetObjectPropertyValue_InContainer(Custom))
					{
						Roots.Add(Template);
						Source = TEXT("customBindingTemplate");
						Type = TEXT("spawnable");
					}
				}
			}
		}
#endif
		if (Roots.Num() == 0 && Possessable && World)
		{
			Roots = LocateInWorld(Sequence, MovieScene, Guid, World);
			if (Roots.Num() > 0) Source = TEXT("editorWorld");
		}
		B->SetStringField(TEXT("type"), Type);

		if (Roots.Num() == 0)
		{
			B->SetBoolField(TEXT("resolved"), false);
			B->SetArrayField(TEXT("components"), TArray<TSharedPtr<FJsonValue>>());
			B->SetStringField(TEXT("note"), Possessable
				? TEXT("This possessable resolves to nothing in the editor world: its actor was deleted or re-created (resolve_sequence_bindings, rebind_possessable), or sits in a level that is not loaded.")
				: TEXT("This spawnable has no object template."));
			++Unresolved;
			Out.Add(MakeShared<FJsonValueObject>(B));
			continue;
		}

		B->SetBoolField(TEXT("resolved"), true);
		B->SetStringField(TEXT("source"), Source);
		TArray<TSharedPtr<FJsonValue>> ObjectsJson;
		TArray<TSharedPtr<FJsonValue>> ComponentsJson;
		for (UObject* Root : Roots)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), Root->GetPathName());
			O->SetStringField(TEXT("class"), Root->GetClass()->GetName());
			if (const AActor* Actor = Cast<AActor>(Root)) O->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			ObjectsJson.Add(MakeShared<FJsonValueObject>(O));

			TArray<FBindingComponent> Components;
			CollectBindingComponents(Root, Components);
			for (const FBindingComponent& Entry : Components)
			{
				if (!ClassFilter.IsEmpty() && !ClassMatches(Entry.Component->GetClass(), ClassFilter)) continue;
				ComponentsJson.Add(MakeShared<FJsonValueObject>(DescribeBindingComponent(Entry, Requested, bAll, bExplicit)));
			}
		}
		B->SetArrayField(TEXT("objects"), ObjectsJson);
		B->SetArrayField(TEXT("components"), ComponentsJson);
		B->SetNumberField(TEXT("componentCount"), ComponentsJson.Num());
		ComponentTotal += ComponentsJson.Num();
		Out.Add(MakeShared<FJsonValueObject>(B));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("sequencePath"), Sequence->GetPathName());
	if (World) Result->SetStringField(TEXT("world"), World->GetPathName());
	TArray<TSharedPtr<FJsonValue>> RequestedJson;
	if (bAll) RequestedJson.Add(MakeShared<FJsonValueString>(TEXT("*")));
	for (const FString& Name : Requested) RequestedJson.Add(MakeShared<FJsonValueString>(Name));
	Result->SetArrayField(TEXT("propertyNames"), RequestedJson);
	Result->SetBoolField(TEXT("defaultProperties"), !bExplicit);
	if (!ClassFilter.IsEmpty()) Result->SetStringField(TEXT("componentClass"), ClassFilter);
	Result->SetArrayField(TEXT("bindings"), Out);
	Result->SetNumberField(TEXT("bindingCount"), Out.Num());
	Result->SetNumberField(TEXT("componentCount"), ComponentTotal);
	Result->SetNumberField(TEXT("unresolvedCount"), Unresolved);
	return MCPResult(Result);
}
