// level(query_components) - one editor-side component query for whole-map
// questions (#910, #943, #912).
//
// The complaint these three issues share is not a missing field. It is that
// answering a level-wide question meant shipping every candidate out to the
// client and filtering there: get_actors_by_component_class returned over a
// megabyte on a real map, and get_nanite_info answers one mesh per call. So
// the selector, the projection, the predicates, the grouping and the counts
// all run here, and only the answer crosses the wire.
//
// Three things this deliberately does NOT do:
//
//   - It never writes. No MarkPackageDirty, no SetX, no property assignment.
//     A map-wide audit that dirties 3,900 actors is worse than no audit
//     (#912). The only state it can touch is the open map, and only when the
//     caller passes levelPath: then it refuses outright if anything is
//     already dirty, and it puts the original map back afterwards.
//   - It never returns an unbounded payload. Rows are capped, groups are
//     capped, value histograms are capped, and every cap reports itself.
//   - It never reports a raw flag where the flag is not the answer. CastShadow
//     true on a hidden debug marker is not a shadow caster, and
//     str(mobility) == "EComponentMobility.MOVABLE" is why a substring test
//     for "Movable" silently matched nothing. Both are computed here.

#include "LevelHandlers.h"

#include "HandlerRegistry.h"
#include "HandlerQuery.h"
#include "HandlerUtils.h"
#include "HandlerEditorState.h"

#include "Components/ActorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace
{
	// ── Bounds ──────────────────────────────────────────────────────────────
	//
	// Every one of these exists because an unbounded version of this query is
	// what the issues are complaining about.
	constexpr int32 MCPQueryDefaultRowLimit = 200;
	constexpr int32 MCPQueryMaxRowLimit = 2000;
	/** Rows kept in memory for sorting and paging. `matched` stays exact past
	 *  this; only the retained window is capped. */
	constexpr int32 MCPQueryMaxRetainedRows = 20000;
	constexpr int32 MCPQueryMaxGroups = 200;
	constexpr int32 MCPQueryDefaultGroupSamples = 5;
	constexpr int32 MCPQueryMaxGroupSamples = 25;
	constexpr int32 MCPQueryMaxCountByPaths = 8;
	constexpr int32 MCPQueryMaxCountByValues = 64;
	constexpr int32 MCPQueryMaxProjectedProperties = 32;
	constexpr int32 MCPQueryMaxPredicates = 24;
	constexpr int32 MCPQueryMaxMaterialSlots = 64;
	constexpr int32 MCPQueryMaxDuplicateGroups = 100;

	const TCHAR* const MCPQueryWorldGridMaterial = TEXT("/Engine/EngineMaterials/WorldGridMaterial");

	// ── Clean enum names ────────────────────────────────────────────────────
	//
	// #943's reporter lost a whole result set to `str(mobility)` producing
	// "EComponentMobility.MOVABLE" and a case-sensitive test for "Movable"
	// matching none of it. These switches are what a caller compares against.

	const TCHAR* MCPQueryMobilityName(EComponentMobility::Type Mobility)
	{
		switch (Mobility)
		{
		case EComponentMobility::Static:     return TEXT("Static");
		case EComponentMobility::Stationary: return TEXT("Stationary");
		case EComponentMobility::Movable:    return TEXT("Movable");
		default:                             return TEXT("Unknown");
		}
	}

	const TCHAR* MCPQueryCollisionEnabledName(ECollisionEnabled::Type Value)
	{
		switch (Value)
		{
		case ECollisionEnabled::NoCollision:      return TEXT("NoCollision");
		case ECollisionEnabled::QueryOnly:        return TEXT("QueryOnly");
		case ECollisionEnabled::PhysicsOnly:      return TEXT("PhysicsOnly");
		case ECollisionEnabled::QueryAndPhysics:  return TEXT("QueryAndPhysics");
		case ECollisionEnabled::ProbeOnly:        return TEXT("ProbeOnly");
		case ECollisionEnabled::QueryAndProbe:    return TEXT("QueryAndProbe");
		default:                                  return TEXT("Unknown");
		}
	}

	const TCHAR* MCPQueryNavGeometryName(EHasCustomNavigableGeometry::Type Value)
	{
		switch (Value)
		{
		case EHasCustomNavigableGeometry::No:                  return TEXT("No");
		case EHasCustomNavigableGeometry::Yes:                 return TEXT("Yes");
		case EHasCustomNavigableGeometry::EvenIfNotCollidable: return TEXT("EvenIfNotCollidable");
		case EHasCustomNavigableGeometry::DontExport:          return TEXT("DontExport");
		default:                                               return TEXT("Unknown");
		}
	}

	// ── JSON shaping ────────────────────────────────────────────────────────

	TSharedPtr<FJsonObject> MCPQueryVec(const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	}

	TSharedPtr<FJsonObject> MCPQueryRot(const FRotator& R)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("pitch"), R.Pitch);
		Obj->SetNumberField(TEXT("yaw"), R.Yaw);
		Obj->SetNumberField(TEXT("roll"), R.Roll);
		return Obj;
	}

	// ── Projection ──────────────────────────────────────────────────────────

	struct FMCPQueryFields
	{
		bool bTransform = false;
		bool bBounds = false;
		bool bLocalBounds = false;
		bool bShadow = false;
		bool bNanite = false;
		bool bNavigation = false;
		bool bTick = false;
		bool bMaterials = false;
		bool bMesh = false;
		bool bDecal = false;
		bool bHealth = false;
	};

	/** Every field name this action understands, so an unknown one is an error
	 *  the caller sees rather than a silently empty projection. */
	bool MCPQueryApplyFieldName(const FString& Name, FMCPQueryFields& Fields)
	{
		if (Name == TEXT("transform"))   { Fields.bTransform = true;   return true; }
		if (Name == TEXT("bounds"))      { Fields.bBounds = true;      return true; }
		if (Name == TEXT("localBounds")) { Fields.bLocalBounds = true; return true; }
		if (Name == TEXT("shadow"))      { Fields.bShadow = true;      return true; }
		if (Name == TEXT("nanite"))      { Fields.bNanite = true;      return true; }
		if (Name == TEXT("navigation"))  { Fields.bNavigation = true;  return true; }
		if (Name == TEXT("tick"))        { Fields.bTick = true;        return true; }
		if (Name == TEXT("materials"))   { Fields.bMaterials = true;   return true; }
		if (Name == TEXT("mesh"))        { Fields.bMesh = true;        return true; }
		if (Name == TEXT("decal"))       { Fields.bDecal = true;       return true; }
		if (Name == TEXT("health"))
		{
			// health is derived from the others, so asking for it turns on
			// everything it reads rather than reporting false negatives.
			Fields.bHealth = true;
			Fields.bBounds = true;
			Fields.bTransform = true;
			Fields.bMesh = true;
			Fields.bDecal = true;
			Fields.bMaterials = true;
			return true;
		}
		return false;
	}

	/** The asset a component renders, if it renders one. */
	UObject* MCPQueryMeshAsset(UActorComponent* Component, FString& OutKind, bool& bOutIsMeshComponent)
	{
		bOutIsMeshComponent = false;
		OutKind.Reset();
		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
		{
			bOutIsMeshComponent = true;
			OutKind = TEXT("StaticMesh");
			return StaticMeshComponent->GetStaticMesh();
		}
		if (USkinnedMeshComponent* SkinnedComponent = Cast<USkinnedMeshComponent>(Component))
		{
			bOutIsMeshComponent = true;
			OutKind = TEXT("SkinnedAsset");
			return SkinnedComponent->GetSkinnedAsset();
		}
		return nullptr;
	}

	/** Read a UPROPERTY off a component by name without linking its module.
	 *  NavModifierComponent lives in NavigationSystem and its AreaClass is the
	 *  one field #943 needs; reflection reaches it on any engine version and
	 *  degrades to absent rather than to a link error. */
	TSharedPtr<FJsonValue> MCPQueryReflectProperty(UObject* Object, const TCHAR* PropertyName)
	{
		if (!Object)
		{
			return nullptr;
		}
		FProperty* Property = Object->GetClass()->FindPropertyByName(FName(PropertyName));
		if (!Property)
		{
			return nullptr;
		}
		return MCPQuery::PropertyToJson(Property, Property->ContainerPtrToValuePtr<void>(Object));
	}

	struct FMCPQueryRow
	{
		FString SortKey;
		TSharedPtr<FJsonObject> Object;
		FIntVector QuantizedLocation = FIntVector::ZeroValue;
		bool bHasTransform = false;
	};
}

// ---------------------------------------------------------------------------
// query_components
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::QueryComponents(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// ── Parameters ──────────────────────────────────────────────────────────
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	const FString LevelPath = OptionalString(Params, TEXT("levelPath"));

	const FString ComponentClassSpec = OptionalString(Params, TEXT("componentClass"));
	const FString ActorClassSpec = OptionalString(Params, TEXT("actorClass"));
	const bool bMatchSubclasses = OptionalBool(Params, TEXT("matchSubclasses"), true);
	const FString ComponentNameContains = OptionalString(Params, TEXT("componentNameContains"));
	const FString ActorLabelPrefix = OptionalString(Params, TEXT("actorLabelPrefix"));
	const FString ActorLabelContains = OptionalString(Params, TEXT("actorLabelContains"));
	const FString ActorTag = OptionalString(Params, TEXT("actorTag"));
	const FString FolderPath = OptionalString(Params, TEXT("folderPath"));
	const FString FolderPathPrefix = OptionalString(Params, TEXT("folderPathPrefix"));

	const FString WhereMode = OptionalString(Params, TEXT("whereMode"), TEXT("all")).ToLower();
	if (WhereMode != TEXT("all") && WhereMode != TEXT("any"))
	{
		return MCPError(TEXT("'whereMode' must be either 'all' or 'any'"));
	}

	const bool bCountOnly = OptionalBool(Params, TEXT("countOnly"), false);
	const bool bSuspectOnly = OptionalBool(Params, TEXT("suspectOnly"), false);
	const FString GroupBy = OptionalString(Params, TEXT("groupBy"));
	const int32 GroupSamples = FMath::Clamp(
		OptionalInt(Params, TEXT("sampleLimit"), MCPQueryDefaultGroupSamples), 0, MCPQueryMaxGroupSamples);
	const int32 Limit = FMath::Clamp(
		OptionalInt(Params, TEXT("limit"), MCPQueryDefaultRowLimit), 0, MCPQueryMaxRowLimit);
	const int32 StartIndex = FMath::Max(0, OptionalInt(Params, TEXT("startIndex"), 0));
	const double DuplicateTolerance = OptionalNumber(Params, TEXT("duplicateTransformTolerance"), 0.0);

	// Projection.
	FMCPQueryFields Fields;
	bool bAnyFieldRequested = false;
	{
		const TArray<TSharedPtr<FJsonValue>>* FieldValues = nullptr;
		if (Params->TryGetArrayField(TEXT("fields"), FieldValues) && FieldValues)
		{
			for (const FString& Name : JsonArrayToStringList(FieldValues))
			{
				if (!MCPQueryApplyFieldName(Name, Fields))
				{
					return MCPError(FString::Printf(
						TEXT("Unknown 'fields' entry '%s'. Valid: transform, bounds, localBounds, shadow, nanite, navigation, tick, materials, mesh, decal, health"),
						*Name));
				}
				bAnyFieldRequested = true;
			}
		}
	}
	if (bSuspectOnly)
	{
		MCPQueryApplyFieldName(TEXT("health"), Fields);
		bAnyFieldRequested = true;
	}
	if (!bAnyFieldRequested)
	{
		// A caller who names no fields gets identity plus transform: enough to
		// act on a row, small enough that an unfiltered query is not the
		// megabyte payload these issues are about.
		Fields.bTransform = true;
	}

	TArray<FString> ProjectedProperties;
	{
		const TArray<TSharedPtr<FJsonValue>>* PropertyValues = nullptr;
		if (Params->TryGetArrayField(TEXT("propertyNames"), PropertyValues) && PropertyValues)
		{
			ProjectedProperties = JsonArrayToStringList(PropertyValues);
		}
	}
	if (ProjectedProperties.Num() > MCPQueryMaxProjectedProperties)
	{
		return MCPError(FString::Printf(
			TEXT("'propertyNames' exceeds the maximum of %d entries"), MCPQueryMaxProjectedProperties));
	}

	// Predicates. Parsing and evaluation live in the shared query header, so
	// this action and asset(bulk_read_properties) cannot drift apart on what
	// an operator name means.
	TArray<MCPQuery::FPredicate> Predicates;
	{
		const TArray<TSharedPtr<FJsonValue>>* WhereValues = nullptr;
		Params->TryGetArrayField(TEXT("where"), WhereValues);
		FString ParseError;
		if (!MCPQuery::ParsePredicates(WhereValues, MCPQueryMaxPredicates, Predicates, ParseError))
		{
			return MCPError(ParseError);
		}
	}

	if (bSuspectOnly)
	{
		MCPQuery::FPredicate Suspect;
		Suspect.Field = TEXT("health.suspect");
		Suspect.Op = TEXT("isTrue");
		Predicates.Add(MoveTemp(Suspect));
		// suspectOnly is an AND on top of whatever else was asked for, so an
		// `any` request keeps its own semantics only among its own predicates.
		// Rather than silently changing that, refuse the ambiguous combination.
		if (WhereMode == TEXT("any") && Predicates.Num() > 1)
		{
			return MCPError(TEXT("'suspectOnly' cannot be combined with whereMode 'any'; add {field:'health.suspect', op:'isTrue'} to 'where' instead"));
		}
	}

	// Value histograms.
	TArray<FString> CountByPaths;
	{
		const TArray<TSharedPtr<FJsonValue>>* CountByValues = nullptr;
		if (Params->TryGetArrayField(TEXT("countBy"), CountByValues) && CountByValues)
		{
			CountByPaths = JsonArrayToStringList(CountByValues);
		}
	}
	if (CountByPaths.Num() > MCPQueryMaxCountByPaths)
	{
		return MCPError(FString::Printf(
			TEXT("'countBy' exceeds the maximum of %d paths"), MCPQueryMaxCountByPaths));
	}

	// A predicate on `shadow.effectiveCastShadow` with `shadow` left out of
	// `fields` would resolve to an absent field and match nothing, and a
	// confident zero is the worst answer this action can give. So a path that
	// names a field group turns that group on.
	{
		auto EnableGroupNamedBy = [&Fields](const FString& Path)
		{
			FString Head;
			FString Tail;
			MCPQueryApplyFieldName(Path.Split(TEXT("."), &Head, &Tail) ? Head : Path, Fields);
		};
		for (const MCPQuery::FPredicate& Predicate : Predicates) EnableGroupNamedBy(Predicate.Field);
		for (const FString& CountPath : CountByPaths) EnableGroupNamedBy(CountPath);
		if (!GroupBy.IsEmpty()) EnableGroupNamedBy(GroupBy);
	}

	// ── Class resolution ────────────────────────────────────────────────────
	UClass* ComponentClass = nullptr;
	FString ComponentClassFallback;
	if (!ComponentClassSpec.IsEmpty())
	{
		ComponentClass = MCPResolveClassOfType(ComponentClassSpec, UActorComponent::StaticClass(), true);
		if (!ComponentClass)
		{
			// A substring is a legitimate way to ask this question and the
			// existing get_actors_by_component_class accepts one, so keep it
			// working rather than failing a call that used to succeed.
			ComponentClassFallback = ComponentClassSpec;
		}
	}
	UClass* ActorClass = nullptr;
	FString ActorClassFallback;
	if (!ActorClassSpec.IsEmpty())
	{
		ActorClass = MCPResolveClassOfType(ActorClassSpec, AActor::StaticClass(), true);
		if (!ActorClass)
		{
			ActorClassFallback = ActorClassSpec;
		}
	}

	// ── World selection, and the map-restore contract (#912) ────────────────
	//
	// Reading another map means opening it. Opening it while the editor has
	// unsaved work either discards that work or raises a modal save prompt the
	// bridge cannot answer, so this refuses instead. And whatever happens, the
	// map that was open when the call arrived is the map that is open when it
	// returns.
	FString RestoreLevelPath;
	TArray<FString> InitialDirtyPackages;
	if (!LevelPath.IsEmpty())
	{
		if (!GEditor)
		{
			return MCPError(TEXT("GEditor is not available"));
		}
		if (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor)
		{
			return MCPError(TEXT("Stop PIE or SIE before querying another level"));
		}
		if (!WorldScope.Equals(TEXT("editor"), ESearchCase::IgnoreCase))
		{
			return MCPError(TEXT("'levelPath' queries the editor world; drop 'world' or set it to 'editor'"));
		}
		if (!MCPEditorState::IsExistingMapPackage(LevelPath))
		{
			return MCPError(FString::Printf(TEXT("Level package was not found as a .umap: %s"), *LevelPath));
		}

		MCPEditorState::CollectDirtyEditorPackageNames(InitialDirtyPackages);
		if (!InitialDirtyPackages.IsEmpty())
		{
			auto DirtyResult = MCPSuccess();
			DirtyResult->SetBoolField(TEXT("success"), false);
			DirtyResult->SetStringField(
				TEXT("error"),
				TEXT("Refusing to open another level while content or map packages are dirty"));
			DirtyResult->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(InitialDirtyPackages));
			return MCPResult(DirtyResult);
		}

		const FString CurrentLevelPath = MCPEditorState::CurrentEditorLevelPackageName();
		if (CurrentLevelPath != LevelPath)
		{
			if (!MCPEditorState::IsExistingMapPackage(CurrentLevelPath))
			{
				return MCPError(FString::Printf(
					TEXT("The currently open level cannot be restored from package path '%s', so this refuses to leave it"),
					*CurrentLevelPath));
			}
			TSharedPtr<FJsonObject> LoadParams = MakeShared<FJsonObject>();
			LoadParams->SetStringField(TEXT("levelPath"), LevelPath);
			const TSharedPtr<FJsonValue> LoadResult = FLevelHandlers::LoadLevel(LoadParams);
			if (!LoadResult.IsValid() || LoadResult->Type != EJson::Object ||
				!LoadResult->AsObject()->GetBoolField(TEXT("success")))
			{
				return MCPError(FString::Printf(TEXT("Failed to open level '%s'"), *LevelPath));
			}
			RestoreLevelPath = CurrentLevelPath;
		}
	}

	// Everything below runs against this world. RestoreLevelPath is put back
	// on every exit path from here on.
	UWorld* World = LevelPath.IsEmpty()
		? ResolveWorldFromParams(Params, *WorldScope)
		: GetEditorWorld();

	auto RestoreLevel = [&RestoreLevelPath]()
	{
		if (RestoreLevelPath.IsEmpty())
		{
			return;
		}
		TSharedPtr<FJsonObject> RestoreParams = MakeShared<FJsonObject>();
		RestoreParams->SetStringField(TEXT("levelPath"), RestoreLevelPath);
		FLevelHandlers::LoadLevel(RestoreParams);
	};

	if (!World)
	{
		RestoreLevel();
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	// ── Scan ────────────────────────────────────────────────────────────────
	int32 ScannedActors = 0;
	int32 ScannedComponents = 0;
	int32 Matched = 0;
	TArray<FMCPQueryRow> Rows;
	TMap<FString, int32> GroupCounts;
	TMap<FString, TArray<FString>> GroupSamplesByKey;
	TArray<FString> GroupOrder;
	TMap<FString, TMap<FString, int32>> CountByHistograms;
	TMap<FIntVector, int32> DuplicateCounts;
	TMap<FIntVector, TArray<FString>> DuplicateSamples;
	bool bGroupsTruncated = false;

	const double DuplicateQuantum = DuplicateTolerance > 0.0 ? DuplicateTolerance : 0.0;

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor)
		{
			continue;
		}
		++ScannedActors;

		if (ActorClass)
		{
			const bool bActorMatches = bMatchSubclasses ? Actor->IsA(ActorClass) : (Actor->GetClass() == ActorClass);
			if (!bActorMatches) continue;
		}
		else if (!ActorClassFallback.IsEmpty() &&
			!Actor->GetClass()->GetName().Contains(ActorClassFallback, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString ActorLabel = Actor->GetActorLabel();
		if (!ActorLabelPrefix.IsEmpty() && !ActorLabel.StartsWith(ActorLabelPrefix, ESearchCase::CaseSensitive)) continue;
		if (!ActorLabelContains.IsEmpty() && !ActorLabel.Contains(ActorLabelContains, ESearchCase::IgnoreCase)) continue;
		if (!ActorTag.IsEmpty() && !Actor->ActorHasTag(FName(*ActorTag))) continue;

		const FString ActorFolder = Actor->GetFolderPath().ToString();
		if (!FolderPath.IsEmpty() && !ActorFolder.Equals(FolderPath, ESearchCase::IgnoreCase)) continue;
		if (!FolderPathPrefix.IsEmpty() && !ActorFolder.StartsWith(FolderPathPrefix, ESearchCase::IgnoreCase)) continue;

		const bool bOwnerHidden = Actor->IsHidden();
		const APawn* OwnerPawn = Cast<APawn>(Actor);

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}
			++ScannedComponents;

			if (ComponentClass)
			{
				const bool bMatches = bMatchSubclasses
					? Component->IsA(ComponentClass)
					: (Component->GetClass() == ComponentClass);
				if (!bMatches) continue;
			}
			else if (!ComponentClassFallback.IsEmpty() &&
				!Component->GetClass()->GetName().Contains(ComponentClassFallback, ESearchCase::IgnoreCase))
			{
				continue;
			}

			const FString ComponentName = Component->GetName();
			if (!ComponentNameContains.IsEmpty() &&
				!ComponentName.Contains(ComponentNameContains, ESearchCase::IgnoreCase))
			{
				continue;
			}

			USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("actorLabel"), ActorLabel);
			Row->SetStringField(TEXT("actorName"), Actor->GetName());
			Row->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
			Row->SetStringField(TEXT("actorPath"), Actor->GetPathName());
			Row->SetStringField(TEXT("folderPath"), ActorFolder);
			Row->SetStringField(TEXT("componentName"), ComponentName);
			Row->SetStringField(TEXT("componentClass"), Component->GetClass()->GetName());

			FVector WorldScale = FVector::OneVector;
			bool bHasTransform = false;
			FVector WorldLocation = FVector::ZeroVector;
			if (SceneComponent)
			{
				bHasTransform = true;
				const FTransform ComponentTransform = SceneComponent->GetComponentTransform();
				WorldLocation = ComponentTransform.GetLocation();
				WorldScale = ComponentTransform.GetScale3D();
				if (Fields.bTransform)
				{
					TSharedPtr<FJsonObject> TransformObject = MakeShared<FJsonObject>();
					TransformObject->SetObjectField(TEXT("location"), MCPQueryVec(WorldLocation));
					TransformObject->SetObjectField(TEXT("rotation"), MCPQueryRot(ComponentTransform.Rotator()));
					TransformObject->SetObjectField(TEXT("scale"), MCPQueryVec(WorldScale));
					TransformObject->SetNumberField(TEXT("minAbsScale"),
						FMath::Min3(FMath::Abs(WorldScale.X), FMath::Abs(WorldScale.Y), FMath::Abs(WorldScale.Z)));
					Row->SetObjectField(TEXT("transform"), TransformObject);
				}
			}

			bool bBoundsZero = false;
			bool bBoundsInvalid = false;
			if (SceneComponent && (Fields.bBounds || Fields.bHealth))
			{
				const FBoxSphereBounds ComponentBounds = MCPComponentWorldBounds(*SceneComponent);
				const FVector Extent = ComponentBounds.BoxExtent;
				bBoundsZero = Extent.IsNearlyZero() && FMath::IsNearlyZero(ComponentBounds.SphereRadius);
				bBoundsInvalid =
					Extent.ContainsNaN() || ComponentBounds.Origin.ContainsNaN() ||
					!FMath::IsFinite(ComponentBounds.SphereRadius) ||
					Extent.X < 0.0 || Extent.Y < 0.0 || Extent.Z < 0.0;
				if (Fields.bBounds)
				{
					TSharedPtr<FJsonObject> BoundsObject = MakeShared<FJsonObject>();
					BoundsObject->SetObjectField(TEXT("origin"), MCPQueryVec(ComponentBounds.Origin));
					BoundsObject->SetObjectField(TEXT("boxExtent"), MCPQueryVec(Extent));
					BoundsObject->SetNumberField(TEXT("sphereRadius"), ComponentBounds.SphereRadius);
					BoundsObject->SetBoolField(TEXT("zero"), bBoundsZero);
					BoundsObject->SetBoolField(TEXT("invalid"), bBoundsInvalid);
					Row->SetObjectField(TEXT("bounds"), BoundsObject);
				}
			}

			if (SceneComponent && Fields.bLocalBounds)
			{
				// #914: the local, unscaled bounds. A caller comparing two
				// shapes needs the authored extent, not the extent times the
				// component scale, and the scaled one is all the existing
				// reads expose.
				const FBoxSphereBounds LocalBounds = SceneComponent->GetLocalBounds();
				TSharedPtr<FJsonObject> LocalObject = MakeShared<FJsonObject>();
				LocalObject->SetObjectField(TEXT("origin"), MCPQueryVec(LocalBounds.Origin));
				LocalObject->SetObjectField(TEXT("boxExtent"), MCPQueryVec(LocalBounds.BoxExtent));
				LocalObject->SetNumberField(TEXT("sphereRadius"), LocalBounds.SphereRadius);
				if (const UBoxComponent* BoxComponent = Cast<UBoxComponent>(Component))
				{
					// #914: UBoxComponent.BoxExtent itself, which is the
					// authored value the details panel shows and is not
					// derivable from the scaled bounds.
					LocalObject->SetObjectField(TEXT("boxExtent_unscaled"),
						MCPQueryVec(BoxComponent->GetUnscaledBoxExtent()));
				}
				Row->SetObjectField(TEXT("localBounds"), LocalObject);
			}

			// ── #910: effective shadow casting ──────────────────────────────
			if (Primitive && Fields.bShadow)
			{
				const bool bVisibleFlag = Primitive->GetVisibleFlag();
				const bool bVisible = Primitive->IsVisible();
				const bool bHiddenInGame = Primitive->bHiddenInGame;
				const bool bRenderedInGame = bVisible && !bHiddenInGame && !bOwnerHidden;
				const bool bCastShadowFlag = Primitive->CastShadow != 0;
				const bool bCastHidden = Primitive->bCastHiddenShadow != 0;
				const bool bEffective = bCastShadowFlag && (bRenderedInGame || bCastHidden);

				FString Reason;
				if (bEffective)
				{
					Reason = bRenderedInGame ? TEXT("rendered and CastShadow") : TEXT("hidden but bCastHiddenShadow");
				}
				else if (!bCastShadowFlag)
				{
					Reason = TEXT("CastShadow is false");
				}
				else if (!bVisible)
				{
					Reason = TEXT("component is not visible");
				}
				else if (bHiddenInGame)
				{
					Reason = TEXT("bHiddenInGame and not bCastHiddenShadow");
				}
				else
				{
					Reason = TEXT("owning actor is hidden and not bCastHiddenShadow");
				}

				TSharedPtr<FJsonObject> ShadowObject = MakeShared<FJsonObject>();
				ShadowObject->SetBoolField(TEXT("castShadow"), bCastShadowFlag);
				ShadowObject->SetBoolField(TEXT("castHiddenShadow"), bCastHidden);
				ShadowObject->SetBoolField(TEXT("castDynamicShadow"), Primitive->bCastDynamicShadow != 0);
				ShadowObject->SetBoolField(TEXT("castStaticShadow"), Primitive->bCastStaticShadow != 0);
				ShadowObject->SetBoolField(TEXT("visibleFlag"), bVisibleFlag);
				ShadowObject->SetBoolField(TEXT("visible"), bVisible);
				ShadowObject->SetBoolField(TEXT("hiddenInGame"), bHiddenInGame);
				ShadowObject->SetBoolField(TEXT("ownerHidden"), bOwnerHidden);
				ShadowObject->SetBoolField(TEXT("renderedInGame"), bRenderedInGame);
				ShadowObject->SetBoolField(TEXT("effectiveCastShadow"), bEffective);
				ShadowObject->SetStringField(TEXT("reason"), Reason);
				ShadowObject->SetStringField(TEXT("mobility"), MCPQueryMobilityName(Primitive->Mobility));
				Row->SetObjectField(TEXT("shadow"), ShadowObject);
			}

			// ── #910: Nanite state, without a call per mesh ─────────────────
			if (Fields.bNanite)
			{
				TSharedPtr<FJsonObject> NaniteObject = MakeShared<FJsonObject>();
				UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
				UStaticMesh* StaticMesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;
				const bool bMeshNanite = StaticMesh ? StaticMesh->IsNaniteEnabled() : false;
				const bool bDisallow = StaticMeshComponent ? StaticMeshComponent->IsDisallowNanite() : false;
				const bool bForceDisable = StaticMeshComponent ? StaticMeshComponent->IsForceDisableNanite() : false;
				NaniteObject->SetStringField(TEXT("staticMeshPath"), StaticMesh ? StaticMesh->GetPathName() : FString());
				NaniteObject->SetBoolField(TEXT("meshNaniteEnabled"), bMeshNanite);
				NaniteObject->SetBoolField(TEXT("componentDisallowNanite"), bDisallow);
				NaniteObject->SetBoolField(TEXT("componentForceDisableNanite"), bForceDisable);
				NaniteObject->SetBoolField(TEXT("effectiveNanite"), bMeshNanite && !bDisallow && !bForceDisable);
				Row->SetObjectField(TEXT("nanite"), NaniteObject);
			}

			// ── #943: navigation relevance ──────────────────────────────────
			if (Fields.bNavigation)
			{
				TSharedPtr<FJsonObject> NavObject = MakeShared<FJsonObject>();
				const bool bCanAffectNav = Component->CanEverAffectNavigation();
				NavObject->SetBoolField(TEXT("canEverAffectNavigation"), bCanAffectNav);
				NavObject->SetStringField(TEXT("mobility"),
					SceneComponent ? MCPQueryMobilityName(SceneComponent->Mobility) : TEXT("None"));

				ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;
				if (Primitive)
				{
					CollisionEnabled = Primitive->GetCollisionEnabled();
					NavObject->SetStringField(TEXT("collisionProfile"),
						Primitive->GetCollisionProfileName().ToString());
					NavObject->SetStringField(TEXT("hasCustomNavigableGeometry"),
						MCPQueryNavGeometryName(Primitive->HasCustomNavigableGeometry()));
				}
				NavObject->SetStringField(TEXT("collisionEnabled"), MCPQueryCollisionEnabledName(CollisionEnabled));

				const bool bCollides =
					CollisionEnabled == ECollisionEnabled::QueryOnly ||
					CollisionEnabled == ECollisionEnabled::QueryAndPhysics ||
					CollisionEnabled == ECollisionEnabled::QueryAndProbe;

				// NavModifierComponent lives in a module this file does not
				// link, so its AreaClass is read reflectively (#943).
				if (const TSharedPtr<FJsonValue> AreaClass = MCPQueryReflectProperty(Component, TEXT("AreaClass")))
				{
					NavObject->SetField(TEXT("navAreaClass"), AreaClass);
				}
				if (const TSharedPtr<FJsonValue> AreaClassToReplace =
					MCPQueryReflectProperty(Component, TEXT("AreaClassToReplace")))
				{
					NavObject->SetField(TEXT("navAreaClassToReplace"), AreaClassToReplace);
				}
				if (OwnerPawn)
				{
					NavObject->SetBoolField(TEXT("ownerCanAffectNavigationGeneration"),
						OwnerPawn->bCanAffectNavigationGeneration != 0);
				}

				const EHasCustomNavigableGeometry::Type CustomGeometry = Primitive
					? Primitive->HasCustomNavigableGeometry()
					: EHasCustomNavigableGeometry::No;
				// EvenIfNotCollidable is the case that makes a raw collision
				// check the wrong answer: the component exports navigation
				// geometry with no collision at all.
				const bool bCustomExports =
					CustomGeometry == EHasCustomNavigableGeometry::Yes ||
					CustomGeometry == EHasCustomNavigableGeometry::EvenIfNotCollidable;
				const bool bEffectiveNav = bCanAffectNav &&
					CustomGeometry != EHasCustomNavigableGeometry::DontExport &&
					(bCollides || bCustomExports);
				NavObject->SetBoolField(TEXT("effectiveNavRelevant"), bEffectiveNav);
				NavObject->SetStringField(TEXT("reason"),
					!bCanAffectNav ? TEXT("bCanEverAffectNavigation is false")
					: bEffectiveNav ? TEXT("navigation relevant")
					: TEXT("no query collision and no custom navigable geometry"));
				Row->SetObjectField(TEXT("navigation"), NavObject);
			}

			// ── #912: tick settings ─────────────────────────────────────────
			if (Fields.bTick)
			{
				TSharedPtr<FJsonObject> TickObject = MakeShared<FJsonObject>();
				TickObject->SetBoolField(TEXT("componentCanEverTick"), Component->PrimaryComponentTick.bCanEverTick);
				TickObject->SetBoolField(TEXT("componentTickEnabled"), Component->PrimaryComponentTick.IsTickFunctionEnabled());
				TickObject->SetNumberField(TEXT("componentTickInterval"), Component->PrimaryComponentTick.TickInterval);
				TickObject->SetBoolField(TEXT("actorCanEverTick"), Actor->PrimaryActorTick.bCanEverTick);
				TickObject->SetBoolField(TEXT("actorTickEnabled"), Actor->PrimaryActorTick.IsTickFunctionEnabled());
				TickObject->SetNumberField(TEXT("actorTickInterval"), Actor->PrimaryActorTick.TickInterval);
				Row->SetObjectField(TEXT("tick"), TickObject);
			}

			// ── #912: the rendered asset ────────────────────────────────────
			bool bIsMeshComponent = false;
			FString MeshKind;
			UObject* MeshAsset = MCPQueryMeshAsset(Component, MeshKind, bIsMeshComponent);
			const bool bMissingMesh = bIsMeshComponent && MeshAsset == nullptr;
			if (Fields.bMesh && bIsMeshComponent)
			{
				TSharedPtr<FJsonObject> MeshObject = MakeShared<FJsonObject>();
				MeshObject->SetStringField(TEXT("assetKind"), MeshKind);
				if (MeshAsset)
				{
					MeshObject->SetStringField(TEXT("assetPath"), MeshAsset->GetPathName());
				}
				else
				{
					MeshObject->SetField(TEXT("assetPath"), MakeShared<FJsonValueNull>());
				}
				MeshObject->SetBoolField(TEXT("missing"), bMissingMesh);
				// #986: how many instances an ISM/HISM holds. Without it a
				// component holding three instances and one holding three
				// hundred thousand are the same row, and the decision about
				// whether to touch it is made blind. Note this is a projected
				// value, not an aggregate: groupBy/countBy count COMPONENTS, so
				// use level(summarize_static_mesh_usage) for placement totals.
				if (const UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(Component))
				{
					MeshObject->SetNumberField(TEXT("instanceCount"), ISMC->GetInstanceCount());
					MeshObject->SetBoolField(TEXT("hierarchical"),
						ISMC->IsA<UHierarchicalInstancedStaticMeshComponent>());
				}
				Row->SetObjectField(TEXT("mesh"), MeshObject);
			}

			// ── #912: decal material ────────────────────────────────────────
			bool bMissingDecalMaterial = false;
			if (UDecalComponent* Decal = Cast<UDecalComponent>(Component))
			{
				UMaterialInterface* DecalMaterial = Decal->GetDecalMaterial();
				bMissingDecalMaterial = DecalMaterial == nullptr;
				if (Fields.bDecal)
				{
					TSharedPtr<FJsonObject> DecalObject = MakeShared<FJsonObject>();
					if (DecalMaterial)
					{
						DecalObject->SetStringField(TEXT("materialPath"), DecalMaterial->GetPathName());
					}
					else
					{
						DecalObject->SetField(TEXT("materialPath"), MakeShared<FJsonValueNull>());
					}
					DecalObject->SetBoolField(TEXT("missing"), bMissingDecalMaterial);
					Row->SetObjectField(TEXT("decal"), DecalObject);
				}
			}

			// ── #912: effective material slots ──────────────────────────────
			int32 NullMaterialSlots = 0;
			int32 WorldGridMaterialSlots = 0;
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(Component))
			{
				const int32 SlotCount = FMath::Min(MeshComponent->GetNumMaterials(), MCPQueryMaxMaterialSlots);
				const TArray<FName> SlotNames = MeshComponent->GetMaterialSlotNames();
				TArray<TSharedPtr<FJsonValue>> SlotArray;
				for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
				{
					UMaterialInterface* Effective = MeshComponent->GetMaterial(SlotIndex);
					// UMeshComponent::GetMaterial consults OverrideMaterials
					// first, so a non-null entry there is exactly what makes
					// this slot an instance override rather than the asset's
					// own assignment.
					const bool bOverride =
						MeshComponent->OverrideMaterials.IsValidIndex(SlotIndex) &&
						MeshComponent->OverrideMaterials[SlotIndex] != nullptr;
					const FString Path = Effective ? Effective->GetPathName() : FString();
					const bool bIsNull = Effective == nullptr;
					const bool bIsWorldGrid = !bIsNull && Path.StartsWith(MCPQueryWorldGridMaterial);
					if (bIsNull) ++NullMaterialSlots;
					if (bIsWorldGrid) ++WorldGridMaterialSlots;

					if (Fields.bMaterials)
					{
						TSharedPtr<FJsonObject> SlotObject = MakeShared<FJsonObject>();
						SlotObject->SetNumberField(TEXT("slotIndex"), SlotIndex);
						SlotObject->SetStringField(TEXT("slotName"),
							SlotNames.IsValidIndex(SlotIndex) ? SlotNames[SlotIndex].ToString() : FString());
						if (bIsNull)
						{
							SlotObject->SetField(TEXT("materialPath"), MakeShared<FJsonValueNull>());
						}
						else
						{
							SlotObject->SetStringField(TEXT("materialPath"), Path);
						}
						SlotObject->SetStringField(TEXT("source"),
							bOverride ? TEXT("componentOverride") : (bIsNull ? TEXT("none") : TEXT("meshDefault")));
						SlotObject->SetBoolField(TEXT("isNull"), bIsNull);
						SlotObject->SetBoolField(TEXT("isWorldGrid"), bIsWorldGrid);
						SlotArray.Add(MakeShared<FJsonValueObject>(SlotObject));
					}
				}
				if (Fields.bMaterials)
				{
					Row->SetArrayField(TEXT("materials"), SlotArray);
					Row->SetNumberField(TEXT("materialSlotCount"), MeshComponent->GetNumMaterials());
				}
			}

			// ── #912: the health roll-up ────────────────────────────────────
			const bool bZeroScale = bHasTransform && (
				FMath::IsNearlyZero(WorldScale.X) ||
				FMath::IsNearlyZero(WorldScale.Y) ||
				FMath::IsNearlyZero(WorldScale.Z));
			const bool bNegativeScale = bHasTransform &&
				(WorldScale.X < 0.0 || WorldScale.Y < 0.0 || WorldScale.Z < 0.0);
			if (Fields.bHealth)
			{
				const bool bSuspect =
					bBoundsZero || bBoundsInvalid || bZeroScale || bMissingMesh ||
					bMissingDecalMaterial || NullMaterialSlots > 0 || WorldGridMaterialSlots > 0;
				TSharedPtr<FJsonObject> HealthObject = MakeShared<FJsonObject>();
				HealthObject->SetBoolField(TEXT("boundsZero"), bBoundsZero);
				HealthObject->SetBoolField(TEXT("boundsInvalid"), bBoundsInvalid);
				HealthObject->SetBoolField(TEXT("zeroScale"), bZeroScale);
				HealthObject->SetBoolField(TEXT("negativeScale"), bNegativeScale);
				HealthObject->SetBoolField(TEXT("missingMesh"), bMissingMesh);
				HealthObject->SetBoolField(TEXT("missingDecalMaterial"), bMissingDecalMaterial);
				HealthObject->SetNumberField(TEXT("nullMaterialSlotCount"), NullMaterialSlots);
				HealthObject->SetNumberField(TEXT("worldGridMaterialSlotCount"), WorldGridMaterialSlots);
				HealthObject->SetBoolField(TEXT("suspect"), bSuspect);
				Row->SetObjectField(TEXT("health"), HealthObject);
			}

			// ── Requested UPROPERTY projection ──────────────────────────────
			if (ProjectedProperties.Num() > 0)
			{
				TSharedPtr<FJsonObject> PropsObject = MakeShared<FJsonObject>();
				for (const FString& PropertyName : ProjectedProperties)
				{
					FProperty* Property = Component->GetClass()->FindPropertyByName(FName(*PropertyName));
					if (!Property)
					{
						continue;
					}
					PropsObject->SetField(PropertyName,
						MCPQuery::PropertyToJson(Property, Property->ContainerPtrToValuePtr<void>(Component)));
				}
				Row->SetObjectField(TEXT("props"), PropsObject);
			}

			// ── Predicates, evaluated here rather than in the client ────────
			if (!MCPQuery::EvaluateAll(Predicates, Row, WhereMode == TEXT("all")))
			{
				continue;
			}

			++Matched;

			// Grouping and histograms count every match, including the ones
			// past the retention cap, so an aggregate is never a partial
			// answer dressed up as a total.
			if (!GroupBy.IsEmpty())
			{
				const FString Key = MCPQuery::ValueKey(MCPQuery::ResolvePath(Row, GroupBy));
				if (int32* Existing = GroupCounts.Find(Key))
				{
					++(*Existing);
					TArray<FString>& Samples = GroupSamplesByKey.FindOrAdd(Key);
					if (Samples.Num() < GroupSamples)
					{
						Samples.Add(FString::Printf(TEXT("%s.%s"), *ActorLabel, *ComponentName));
					}
				}
				else if (GroupCounts.Num() < MCPQueryMaxGroups)
				{
					GroupCounts.Add(Key, 1);
					GroupOrder.Add(Key);
					TArray<FString>& Samples = GroupSamplesByKey.FindOrAdd(Key);
					if (Samples.Num() < GroupSamples)
					{
						Samples.Add(FString::Printf(TEXT("%s.%s"), *ActorLabel, *ComponentName));
					}
				}
				else
				{
					bGroupsTruncated = true;
				}
			}

			for (const FString& CountPath : CountByPaths)
			{
				TMap<FString, int32>& Histogram = CountByHistograms.FindOrAdd(CountPath);
				const FString Key = MCPQuery::ValueKey(MCPQuery::ResolvePath(Row, CountPath));
				if (int32* Existing = Histogram.Find(Key))
				{
					++(*Existing);
				}
				else if (Histogram.Num() < MCPQueryMaxCountByValues)
				{
					Histogram.Add(Key, 1);
				}
			}

			FIntVector Quantized = FIntVector::ZeroValue;
			if (DuplicateQuantum > 0.0 && bHasTransform)
			{
				Quantized = FIntVector(
					FMath::RoundToInt(WorldLocation.X / DuplicateQuantum),
					FMath::RoundToInt(WorldLocation.Y / DuplicateQuantum),
					FMath::RoundToInt(WorldLocation.Z / DuplicateQuantum));
				int32& Count = DuplicateCounts.FindOrAdd(Quantized);
				++Count;
				TArray<FString>& Samples = DuplicateSamples.FindOrAdd(Quantized);
				if (Samples.Num() < FMath::Max(GroupSamples, 2))
				{
					Samples.Add(FString::Printf(TEXT("%s.%s"), *ActorLabel, *ComponentName));
				}
			}

			if (!bCountOnly && Rows.Num() < MCPQueryMaxRetainedRows)
			{
				FMCPQueryRow NewRow;
				NewRow.SortKey = FString::Printf(TEXT("%s|%s"), *Actor->GetPathName(), *ComponentName);
				NewRow.Object = Row;
				NewRow.QuantizedLocation = Quantized;
				NewRow.bHasTransform = bHasTransform;
				Rows.Add(MoveTemp(NewRow));
			}
		}
	}

	// ── Response ────────────────────────────────────────────────────────────
	const bool bRetentionTruncated = !bCountOnly && Matched > Rows.Num();

	// Deterministic order, so `startIndex` means the same thing on the next call.
	// TActorIterator order is not a contract.
	Rows.Sort([](const FMCPQueryRow& A, const FMCPQueryRow& B) { return A.SortKey < B.SortKey; });

	TArray<TSharedPtr<FJsonValue>> RowArray;
	if (!bCountOnly)
	{
		const int32 First = FMath::Min(StartIndex, Rows.Num());
		const int32 Last = FMath::Min(First + Limit, Rows.Num());
		for (int32 Index = First; Index < Last; ++Index)
		{
			FMCPQueryRow& Row = Rows[Index];
			if (DuplicateQuantum > 0.0 && Row.bHasTransform)
			{
				const int32* Count = DuplicateCounts.Find(Row.QuantizedLocation);
				const bool bDuplicate = Count && *Count > 1;
				if (Row.Object->HasTypedField<EJson::Object>(TEXT("health")))
				{
					Row.Object->GetObjectField(TEXT("health"))->SetBoolField(TEXT("duplicateTransform"), bDuplicate);
				}
				else
				{
					Row.Object->SetBoolField(TEXT("duplicateTransform"), bDuplicate);
				}
			}
			RowArray.Add(MakeShared<FJsonValueObject>(Row.Object));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), MCPEditorState::CurrentEditorLevelPackageName());
	Result->SetNumberField(TEXT("scannedActors"), ScannedActors);
	Result->SetNumberField(TEXT("scannedComponents"), ScannedComponents);
	Result->SetNumberField(TEXT("matched"), Matched);
	Result->SetNumberField(TEXT("returned"), RowArray.Num());
	Result->SetNumberField(TEXT("startIndex"), StartIndex);
	Result->SetNumberField(TEXT("limit"), Limit);
	Result->SetBoolField(TEXT("countOnly"), bCountOnly);
	Result->SetBoolField(TEXT("retentionTruncated"), bRetentionTruncated);
	if (bRetentionTruncated)
	{
		Result->SetStringField(TEXT("retentionNote"), FString::Printf(
			TEXT("Only the first %d matches were kept for paging; 'matched' is exact. Narrow the filters or use countOnly/groupBy."),
			MCPQueryMaxRetainedRows));
	}
	const int32 NextStartIndex = StartIndex + RowArray.Num();
	Result->SetBoolField(TEXT("hasMore"), !bCountOnly && NextStartIndex < Rows.Num());
	if (!bCountOnly && NextStartIndex < Rows.Num())
	{
		Result->SetNumberField(TEXT("nextStartIndex"), NextStartIndex);
	}
	if (!bCountOnly)
	{
		Result->SetArrayField(TEXT("rows"), RowArray);
	}

	if (!GroupBy.IsEmpty())
	{
		GroupOrder.Sort([&GroupCounts](const FString& A, const FString& B)
		{
			const int32 CountA = GroupCounts.FindChecked(A);
			const int32 CountB = GroupCounts.FindChecked(B);
			return CountA != CountB ? CountA > CountB : A < B;
		});
		TArray<TSharedPtr<FJsonValue>> GroupArray;
		for (const FString& Key : GroupOrder)
		{
			TSharedPtr<FJsonObject> GroupObject = MakeShared<FJsonObject>();
			GroupObject->SetStringField(TEXT("key"), Key);
			GroupObject->SetNumberField(TEXT("count"), GroupCounts.FindChecked(Key));
			if (const TArray<FString>* Samples = GroupSamplesByKey.Find(Key))
			{
				GroupObject->SetArrayField(TEXT("samples"), MCPStringListToJson(*Samples));
			}
			GroupArray.Add(MakeShared<FJsonValueObject>(GroupObject));
		}
		Result->SetStringField(TEXT("groupBy"), GroupBy);
		Result->SetArrayField(TEXT("groups"), GroupArray);
		Result->SetBoolField(TEXT("groupsTruncated"), bGroupsTruncated);
	}

	if (CountByPaths.Num() > 0)
	{
		TSharedPtr<FJsonObject> CountsObject = MakeShared<FJsonObject>();
		for (const FString& CountPath : CountByPaths)
		{
			TSharedPtr<FJsonObject> Histogram = MakeShared<FJsonObject>();
			if (const TMap<FString, int32>* Values = CountByHistograms.Find(CountPath))
			{
				for (const TPair<FString, int32>& Pair : *Values)
				{
					Histogram->SetNumberField(Pair.Key, Pair.Value);
				}
			}
			CountsObject->SetObjectField(CountPath, Histogram);
		}
		Result->SetObjectField(TEXT("counts"), CountsObject);
	}

	if (DuplicateQuantum > 0.0)
	{
		TArray<TSharedPtr<FJsonValue>> DuplicateArray;
		int32 DuplicateComponentCount = 0;
		for (const TPair<FIntVector, int32>& Pair : DuplicateCounts)
		{
			if (Pair.Value < 2)
			{
				continue;
			}
			DuplicateComponentCount += Pair.Value;
			if (DuplicateArray.Num() >= MCPQueryMaxDuplicateGroups)
			{
				continue;
			}
			TSharedPtr<FJsonObject> DuplicateObject = MakeShared<FJsonObject>();
			DuplicateObject->SetObjectField(TEXT("location"), MCPQueryVec(FVector(
				Pair.Key.X * DuplicateQuantum,
				Pair.Key.Y * DuplicateQuantum,
				Pair.Key.Z * DuplicateQuantum)));
			DuplicateObject->SetNumberField(TEXT("count"), Pair.Value);
			if (const TArray<FString>* Samples = DuplicateSamples.Find(Pair.Key))
			{
				DuplicateObject->SetArrayField(TEXT("samples"), MCPStringListToJson(*Samples));
			}
			DuplicateArray.Add(MakeShared<FJsonValueObject>(DuplicateObject));
		}
		Result->SetNumberField(TEXT("duplicateTransformTolerance"), DuplicateQuantum);
		Result->SetNumberField(TEXT("duplicateTransformComponents"), DuplicateComponentCount);
		Result->SetArrayField(TEXT("duplicateTransformGroups"), DuplicateArray);
	}

	// The read half of the #912 contract, asserted rather than assumed: if
	// anything about this call dirtied a package, say so instead of leaving a
	// map-wide audit as an unexplained unsaved-changes marker.
	TArray<FString> DirtyAfterQuery;
	MCPEditorState::CollectDirtyEditorPackageNames(DirtyAfterQuery);
	for (const FString& Already : InitialDirtyPackages)
	{
		DirtyAfterQuery.Remove(Already);
	}
	Result->SetBoolField(TEXT("dirtiedPackages"), DirtyAfterQuery.Num() > 0);
	if (DirtyAfterQuery.Num() > 0)
	{
		Result->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(DirtyAfterQuery));
	}

	if (!RestoreLevelPath.IsEmpty())
	{
		Result->SetStringField(TEXT("restoredLevelPath"), RestoreLevelPath);
	}
	RestoreLevel();

	return MCPResult(Result);
}
