#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "VolumeHelpers_Internal.h"
#include "EditorScriptingUtilities/Public/EditorLevelLibrary.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Components/InstancedStaticMeshComponent.h"
// #986: get_component_tree distinguishes HISM from plain ISM, because per
// instance culling and LOD change what an edit to one costs.
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Components/StaticMeshComponent.h"
#include "Exporters/Exporter.h"
#include "Exporters/FbxExportOption.h"
#include "AssetExportTask.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ReferenceSkeleton.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimInstance.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonSerializer.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/RectLight.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/BrushBuilder.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Components/BrushComponent.h"
#include "GameFramework/Volume.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "Engine/BlockingVolume.h"
#include "Engine/TriggerVolume.h"
#include "Engine/PostProcessVolume.h"
#include "Sound/AudioVolume.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "GameFramework/PainCausingVolume.h"
#include "Selection.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "LevelEditorSubsystem.h"
#include "EditorLevelUtils.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "HandlerJsonProperty.h"
#include "Engine/Blueprint.h"
#include "Engine/LevelScriptBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

void FLevelHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("get_world_outliner"), &GetOutliner);
	// #717: query/set per-actor editor-only visibility (temporarily hidden).
	Registry.RegisterHandler(TEXT("set_editor_visibility"), &SetEditorVisibility);
	Registry.RegisterHandler(TEXT("place_actor"), &PlaceActor);
	Registry.RegisterHandler(TEXT("delete_actor"), &DeleteActor);
	Registry.RegisterHandler(TEXT("get_actor_details"), &GetActorDetails);
	Registry.RegisterHandler(TEXT("get_component_tree"), &GetComponentTree);
	Registry.RegisterHandler(TEXT("get_relative_transform"), &GetRelativeTransform);
	Registry.RegisterHandler(TEXT("get_current_level"), &GetCurrentLevel);
	// #964: LevelHandlers_Save.cpp. Saves through the same package path
	// editor(save_dirty) uses, so the two cannot disagree about one package,
	// and reports the package, the file and the engine's own reason on failure.
	Registry.RegisterHandler(TEXT("save_level"), &SaveLevel);
	Registry.RegisterHandler(TEXT("list_levels"), &ListLevels);
	Registry.RegisterHandler(TEXT("get_selected_actors"), &GetSelectedActors);
	Registry.RegisterHandler(TEXT("list_volumes"), &ListVolumes);
	Registry.RegisterHandler(TEXT("move_actor"), &MoveActor);
	Registry.RegisterHandler(TEXT("aim_actor_at"), &AimActorAt);
	Registry.RegisterHandler(TEXT("nav_project_point"), &NavProjectPoint);
	Registry.RegisterHandler(TEXT("select_actors"), &SelectActors);
	Registry.RegisterHandler(TEXT("spawn_light"), &SpawnLight);
	Registry.RegisterHandler(TEXT("set_light_properties"), &SetLightProperties);
	Registry.RegisterHandler(TEXT("spawn_volume"), &SpawnVolume);
	Registry.RegisterHandler(TEXT("add_component_to_actor"), &AddComponentToActor);
	Registry.RegisterHandler(TEXT("remove_component_from_actor"), &RemoveComponentFromActor);
	Registry.RegisterHandler(TEXT("load_level"), &LoadLevel);
	Registry.RegisterHandler(TEXT("clear_level_script"), &ClearLevelScript);
	Registry.RegisterHandler(TEXT("set_component_property"), &SetComponentProperty);
	Registry.RegisterHandler(TEXT("nudge_component"), &NudgeComponent);
	Registry.RegisterHandler(TEXT("get_component_details"), &GetComponentDetails);
	Registry.RegisterHandler(TEXT("set_actor_material"), &SetActorMaterial);
	Registry.RegisterHandler(TEXT("set_volume_properties"), &SetVolumeProperties);
	Registry.RegisterHandler(TEXT("get_world_settings"), &GetWorldSettings);
	Registry.RegisterHandler(TEXT("set_world_settings"), &SetWorldSettings);
	Registry.RegisterHandler(TEXT("set_fog_properties"), &SetFogProperties);
	Registry.RegisterHandler(TEXT("get_actors_by_class"), &GetActorsByClass);
	Registry.RegisterHandler(TEXT("get_actors_by_component_class"), &GetActorsByComponentClass);
	// Same budget as query_components, and for the same reason: this is a full
	// TActorIterator pass on the game thread. It additionally sorts every actor
	// by path name and sorts each actor's components, so it is the heavier of
	// the two whole-map scans and had no business inheriting the 30 second
	// default. Mirrored in src/bridge-timeouts.ts, which a parity test checks.
	Registry.RegisterHandlerWithTimeout(TEXT("summarize_static_mesh_usage"), &SummarizeStaticMeshUsage, 300.0f);
	Registry.RegisterHandler(TEXT("count_actors_by_class"), &CountActorsByClass);
	Registry.RegisterHandler(TEXT("get_runtime_virtual_texture_summary"), &GetRVTSummary);
	Registry.RegisterHandler(TEXT("set_water_body_property"), &SetWaterBodyProperty);
	Registry.RegisterHandler(TEXT("get_actor_bounds"), &GetActorBounds);
	Registry.RegisterHandler(TEXT("resolve_actor"), &ResolveActor);
	Registry.RegisterHandler(TEXT("set_actor_property"), &SetActorProperty);
	Registry.RegisterHandler(TEXT("line_trace"), &LineTrace);
	Registry.RegisterHandler(TEXT("bulk_line_trace"), &BulkLineTrace);
	// #453: per-actor motion snapshot for telemetry probes. Reads location,
	// rotation, velocity, angular velocity, scale, and ground state in one
	// call. Caller is expected to invoke at the desired sample interval.
	Registry.RegisterHandler(TEXT("read_actor_motion"), &ReadActorMotion);
	// #434: bulk-add transforms to a HISMC / ISMC component (Python crashes).
	Registry.RegisterHandler(TEXT("add_hismc_instances"), &AddHismcInstances);
	Registry.RegisterHandler(TEXT("add_ismc_instances"), &AddHismcInstances);
	Registry.RegisterHandler(TEXT("add_instances"), &AddHismcInstances);
	// #697: read/update/remove existing instances on an ISMC/HISMC.
	Registry.RegisterHandler(TEXT("get_instance_transforms"), &GetInstanceTransforms);
	Registry.RegisterHandler(TEXT("update_instance_transform"), &UpdateInstanceTransform);
	Registry.RegisterHandler(TEXT("remove_instance"), &RemoveInstance);
	// Native bridge method only in this plugin-scoped change. It appears in
	// get_bridge_capabilities.actions and can be called directly over JSON-RPC
	// (or a UeMcpTask bridge.call). A first-class category action also requires a
	// server schema wrapper, which intentionally lives outside this plugin.
	Registry.RegisterHandlerWithTimeout(TEXT("snap_instances_to_surface"), &SnapInstancesToSurface, 300.0f);
	// #696: enable + force-build Nanite on a static mesh.
	Registry.RegisterHandler(TEXT("set_nanite_settings"), &SetNaniteSettings);
	Registry.RegisterHandler(TEXT("get_nanite_info"), &GetNaniteInfo);
	// #679/#677: spawn a SkeletalMeshActor for visual/deform verification.
	Registry.RegisterHandler(TEXT("spawn_skeletal_mesh_actor"), &SpawnSkeletalMeshActor);
	Registry.RegisterHandler(TEXT("place_skeletal_actor"), &SpawnSkeletalMeshActor);
	// #666: add a material blendable to a PostProcessVolume.
	Registry.RegisterHandler(TEXT("add_post_process_blendable"), &AddPostProcessBlendable);
	// #950: LevelHandlers_PostProcess.cpp. The value half and the bOverride_ half
	// of FPostProcessSettings, written together.
	Registry.RegisterHandler(TEXT("set_post_process_settings"), &SetPostProcessSettings);
	Registry.RegisterHandler(TEXT("get_post_process_settings"), &GetPostProcessSettings);
	Registry.RegisterHandler(TEXT("set_fixed_exposure"), &SetFixedExposure);
	// #637: export a selected actor's mesh to FBX + metadata sidecar.
	Registry.RegisterHandler(TEXT("export_actor_fbx"), &ExportActorFbx);
	Registry.RegisterHandler(TEXT("snap_actor_to_floor"), &SnapActorToFloor);
	Registry.RegisterHandler(TEXT("delete_actors"), &DeleteActors);
	Registry.RegisterHandlerWithTimeout(TEXT("delete_exact_labeled_actors_in_levels"), &DeleteExactLabeledActorsInLevels, 300.0f);
	Registry.RegisterHandler(TEXT("set_actor_folder_path"), &SetActorFolderPath);
	Registry.RegisterHandler(TEXT("list_actor_descs"), &ListActorDescs);
	Registry.RegisterHandlerWithTimeout(TEXT("load_actor_descs"), &LoadActorDescs, 300.0f);
	// #985: LevelHandlers_WorldPartitionSettings.cpp. The streaming knobs and
	// the runtime cell transformer stack live with the other World Partition
	// actions rather than in a category of their own.
	Registry.RegisterHandler(TEXT("get_world_partition_settings"), &GetWorldPartitionSettings);
	Registry.RegisterHandler(TEXT("set_world_partition_settings"), &SetWorldPartitionSettings);
	Registry.RegisterHandler(TEXT("add_runtime_cell_transformer"), &AddRuntimeCellTransformer);
	// #985: bulk HLOD layer assignment. A whole-map selector, so it takes the
	// same 300 second budget as the other batch writes. Mirrored in
	// src/bridge-timeouts.ts, which a parity test checks.
	Registry.RegisterHandlerWithTimeout(TEXT("set_actor_hlod_layer"), &SetActorHLODLayer, 300.0f);
	Registry.RegisterHandler(TEXT("add_actor_tag"), &AddActorTag);
	Registry.RegisterHandler(TEXT("remove_actor_tag"), &RemoveActorTag);
	Registry.RegisterHandler(TEXT("set_actor_tags"), &SetActorTags);
	Registry.RegisterHandler(TEXT("list_actor_tags"), &ListActorTags);
	Registry.RegisterHandler(TEXT("attach_actor"), &AttachActor);
	Registry.RegisterHandler(TEXT("detach_actor"), &DetachActor);
	Registry.RegisterHandler(TEXT("attach_component"), &AttachComponent);
	Registry.RegisterHandler(TEXT("detach_component"), &DetachComponent);
	Registry.RegisterHandler(TEXT("set_actor_mobility"), &SetActorMobility);
	Registry.RegisterHandler(TEXT("get_current_edit_level"), &GetCurrentEditLevel);
	Registry.RegisterHandler(TEXT("set_current_edit_level"), &SetCurrentEditLevel);
	Registry.RegisterHandler(TEXT("list_streaming_sublevels"), &ListStreamingSublevels);
	Registry.RegisterHandler(TEXT("add_streaming_sublevel"), &AddStreamingSublevel);
	Registry.RegisterHandler(TEXT("remove_streaming_sublevel"), &RemoveStreamingSublevel);
	Registry.RegisterHandler(TEXT("set_streaming_sublevel_properties"), &SetStreamingSublevelProperties);
	Registry.RegisterHandler(TEXT("spawn_grid"), &SpawnGrid);
	Registry.RegisterHandler(TEXT("batch_translate"), &BatchTranslate);
	Registry.RegisterHandler(TEXT("place_actors_batch"), &PlaceActorsBatch);
	// #910/#943/#912: the general editor-side component query. A whole-map
	// scan with a projection can take a while on a 4,000 actor level, so it
	// gets its own timeout rather than the 30 second default.
	Registry.RegisterHandlerWithTimeout(TEXT("query_components"), &QueryComponents, 300.0f);
	// #984/#941/#907/#987: level-wide writes driven by an editor-side selector.
	// Each can touch thousands of actors, so each gets its own timeout.
	Registry.RegisterHandlerWithTimeout(TEXT("batch_set_actor_properties"), &BatchSetActorProperties, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("bulk_set_component_property"), &BulkSetComponentProperty, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("remove_components_by_class"), &RemoveComponentsByClass, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("spawn_actors_batch"), &SpawnActorsBatch, 300.0f);
	// #944/#915/#914: refresh state the editor is caching, and read the bounds
	// a caller needs to check the result.
	Registry.RegisterHandlerWithTimeout(TEXT("rerun_construction_scripts"), &RerunConstruction, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("recreate_physics_state"), &RecreatePhysicsState, 300.0f);
	Registry.RegisterHandler(TEXT("test_component_overlap"), &TestComponentOverlap);
	// #911: BSP to StaticMesh. Generating meshes for hundreds of brushes takes
	// far longer than the default handler timeout.
	Registry.RegisterHandlerWithTimeout(TEXT("convert_brushes_to_static_mesh"), &ConvertBrushesToStaticMesh, 600.0f);
	// #946: component-level material overrides on placed actors.
	Registry.RegisterHandlerWithTimeout(TEXT("set_component_materials"), &SetComponentMaterials, 300.0f);
	// #956: a transient verification subject, and the two actions that keep it
	// from being left behind.
	Registry.RegisterHandler(TEXT("spawn_transient_actor"), &SpawnTransientActor);
	Registry.RegisterHandler(TEXT("destroy_transient_actor"), &DestroyTransientActor);
	Registry.RegisterHandler(TEXT("list_transient_actors"), &ListTransientActors);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetOutliner(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));

	FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	FString NameFilter = OptionalString(Params, TEXT("nameFilter"));
	// #911: classFilter has always been a case-sensitive substring on the class
	// name, which cannot express "only this exact class". Combined with the
	// folder filters below, that is what forced a get_actor_details round trip
	// per entry to narrow a folder to one class.
	const bool bExactClass = OptionalBool(Params, TEXT("exactClass"), false);
	const FString FolderPathFilter = OptionalString(Params, TEXT("folderPath"));
	const FString FolderPathPrefixFilter = OptionalString(Params, TEXT("folderPathPrefix"));
	bool bIncludeStreaming = OptionalBool(Params, TEXT("includeStreaming"), false);

	// #717: optional tri-state filter on editor-only visibility. When present,
	// only actors whose IsTemporarilyHiddenInEditor() matches are returned. The
	// per-actor editorHidden flag is always reported so callers can find lights
	// that are hidden in the viewport but still render in game.
	bool bEditorHiddenFilterValue = false;
	const bool bHasEditorHiddenFilter = Params->TryGetBoolField(TEXT("editorHidden"), bEditorHiddenFilterValue);

	// T3: paged. The default of 50 kept this snappy on World Partition levels
	// and told nobody it had cut anything, so an agent reading a 900-actor map
	// acted on the first 50 as if they were the level. The default stays 50;
	// what changes is that the rest is now reachable and the response says it
	// is there.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(
				TEXT("get_world_outliner|world=%s|classFilter=%s|exactClass=%d|nameFilter=%s|folderPath=%s|folderPathPrefix=%s|editorHidden=%d|includeStreaming=%d"),
				*WorldScope, *ClassFilter, bExactClass ? 1 : 0, *NameFilter,
				*FolderPathFilter, *FolderPathPrefixFilter,
				bHasEditorHiddenFilter ? (bEditorHiddenFilterValue ? 1 : 0) : -1,
				bIncludeStreaming ? 1 : 0),
			/*DefaultLimit*/ 50, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	TArray<MCPPagination::FPageRow> Rows;
	int32 TotalCount = 0;
	int32 StreamingSkipped = 0;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor) continue;
		TotalCount++;

		FString ActorClass = Actor->GetClass()->GetName();
		FString ActorName = Actor->GetName();

		// World Partition spawns large numbers of LandscapeStreamingProxy and
		// WorldPartitionHLOD actors whose component graphs are expensive to
		// walk. Skip by default; callers can opt in via includeStreaming=true.
		if (!bIncludeStreaming &&
			(ActorClass == TEXT("LandscapeStreamingProxy") ||
			 ActorClass == TEXT("WorldPartitionHLOD")))
		{
			StreamingSkipped++;
			continue;
		}

		FString ActorLabel = Actor->GetActorLabel();

		if (!ClassFilter.IsEmpty())
		{
			const bool bClassMatches = bExactClass
				? ActorClass.Equals(ClassFilter, ESearchCase::IgnoreCase)
				: ActorClass.Contains(ClassFilter);
			if (!bClassMatches)
			{
				continue;
			}
		}
		if (!NameFilter.IsEmpty() && !ActorName.Contains(NameFilter) && !ActorLabel.Contains(NameFilter))
		{
			continue;
		}
		if (!FolderPathFilter.IsEmpty() || !FolderPathPrefixFilter.IsEmpty())
		{
			const FString Folder = Actor->GetFolderPath().ToString();
			if (!FolderPathFilter.IsEmpty() && !Folder.Equals(FolderPathFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			// A folder prefix matches the folder itself and everything nested
			// under it, so "Gameplay" does not also match "GameplayOld".
			if (!FolderPathPrefixFilter.IsEmpty() &&
				!Folder.Equals(FolderPathPrefixFilter, ESearchCase::IgnoreCase) &&
				!Folder.StartsWith(FolderPathPrefixFilter + TEXT("/"), ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

#if WITH_EDITOR
		const bool bEditorHidden = Actor->IsTemporarilyHiddenInEditor();
#else
		const bool bEditorHidden = false;
#endif
		if (bHasEditorHiddenFilter && bEditorHidden != bEditorHiddenFilterValue)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), ActorName);
		ActorObj->SetStringField(TEXT("label"), ActorLabel);
		ActorObj->SetStringField(TEXT("class"), ActorClass);
		ActorObj->SetStringField(TEXT("path"), Actor->GetPathName());
		// #767: the outliner folder is what an agent sees in the editor tree,
		// so report it alongside the label rather than only being able to set it.
		ActorObj->SetStringField(TEXT("folderPath"), Actor->GetFolderPath().ToString());
		ActorObj->SetBoolField(TEXT("editorHidden"), bEditorHidden);

		FVector Location = Actor->GetActorLocation();
		TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
		LocationObj->SetNumberField(TEXT("x"), Location.X);
		LocationObj->SetNumberField(TEXT("y"), Location.Y);
		LocationObj->SetNumberField(TEXT("z"), Location.Z);
		ActorObj->SetObjectField(TEXT("location"), LocationObj);

		FRotator Rotation = Actor->GetActorRotation();
		TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
		RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
		RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
		RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
		ActorObj->SetObjectField(TEXT("rotation"), RotationObj);

		// Include child components
		TArray<TSharedPtr<FJsonValue>> ComponentsArray;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (!Comp) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Comp->GetName());
			CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
		ActorObj->SetArrayField(TEXT("components"), ComponentsArray);

		// The actor PATH is the anchor, not the label or the internal name: two
		// actors in a level can carry the same label, and a page boundary has to
		// name exactly one of them.
		Rows.Add({ Actor->GetPathName(), MakeShared<FJsonValueObject>(ActorObj) });
	}

	// TActorIterator walks the level's actor arrays, whose order is not a
	// contract and which a spawn or a delete reshuffles, so the rows are sorted
	// before paging. Without it the same page can come back in a different
	// order between two calls and the anchor would report a change that is only
	// the iteration reshuffling.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetNumberField(TEXT("totalActors"), TotalCount);
	Result->SetNumberField(TEXT("streamingSkipped"), StreamingSkipped);
	MCPPagination::EmitPage(Page, Rows, TEXT("actors"), Result);
	Result->SetNumberField(TEXT("returnedActors"), Result->GetIntegerField(TEXT("count")));

	return MCPResult(Result);
}

// #717: bulk set editor-only visibility (temporarily hidden in editor). Targets
// either an explicit actorLabels list or every actor (all=true). Editor-hidden
// actors still render in game, so unhiding them is a common cleanup step.
TSharedPtr<FJsonValue> FLevelHandlers::SetEditorVisibility(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	bool bHidden = false;
	if (!Params->TryGetBoolField(TEXT("hidden"), bHidden))
	{
		return MCPError(TEXT("Missing 'hidden' parameter (true = hide in editor, false = show)"));
	}

	const bool bAll = OptionalBool(Params, TEXT("all"), false);

	TSet<FString> TargetLabels;
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("actorLabels"), LabelsArr) && LabelsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *LabelsArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) TargetLabels.Add(S);
		}
	}

	if (!bAll && TargetLabels.Num() == 0)
	{
		return MCPError(TEXT("Provide 'actorLabels' (array) or 'all'=true"));
	}

	int32 Changed = 0;
	int32 Matched = 0;
	TArray<TSharedPtr<FJsonValue>> Affected;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor) continue;
		const FString Label = Actor->GetActorLabel();
		if (!bAll && !TargetLabels.Contains(Label)) continue;
		Matched++;
#if WITH_EDITOR
		if (Actor->IsTemporarilyHiddenInEditor() != bHidden)
		{
			Actor->SetIsTemporarilyHiddenInEditor(bHidden);
			Changed++;
			Affected.Add(MakeShared<FJsonValueString>(Label));
		}
#endif
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("hidden"), bHidden);
	Result->SetNumberField(TEXT("matched"), Matched);
	Result->SetNumberField(TEXT("changed"), Changed);
	Result->SetBoolField(TEXT("unchanged"), Changed == 0);
	Result->SetArrayField(TEXT("affected"), Affected);

	// The inverse flips back only the actors whose flag this call actually
	// moved, which is what `affected` holds.
	if (Changed > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("actorLabels"), Affected);
		Payload->SetBoolField(TEXT("hidden"), !bHidden);
		MCPSetRollback(Result, TEXT("set_editor_visibility"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The editor label is the only selector this action takes, and labels are not unique. Where several actors share a label, the inverse also flips the ones this call found already in the requested state."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::PlaceActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorClass;
	if (auto Err = RequireString(Params, TEXT("actorClass"), ActorClass)) return Err;

	// #585: respect world:pie so the actor spawns into the running PIE world
	// instead of silently landing in the editor world.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString Label = OptionalString(Params, TEXT("label"));

	if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("Actor")))
	{
		return Existing;
	}

	UClass* Class = FindClassByShortName(ActorClass);
	if (!Class)
	{
		Class = LoadObject<UClass>(nullptr, *ActorClass);
	}
	if (!Class)
	{
		return MCPError(FString::Printf(TEXT("Actor class not found: %s"), *ActorClass));
	}

	const FVector Location = OptionalVec3(Params, TEXT("location"));
	const FRotator Rotation = OptionalRotator(Params, TEXT("rotation"));

	FTransform SpawnTransform(Rotation, Location);
	AActor* NewActor = World->SpawnActor<AActor>(Class, SpawnTransform);
	if (!NewActor)
	{
		return MCPError(TEXT("Failed to spawn actor"));
	}

	if (!Label.IsEmpty())
	{
		NewActor->SetActorLabel(Label);
	}

	if (Params->HasField(TEXT("scale")))
	{
		NewActor->SetActorScale3D(OptionalVec3(Params, TEXT("scale"), FVector::OneVector));
	}

	// Static mesh shorthand
	FString StaticMeshPath = OptionalString(Params, TEXT("staticMesh"));
	if (!StaticMeshPath.IsEmpty())
	{
		AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(NewActor);
		if (MeshActor && MeshActor->GetStaticMeshComponent())
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);
			if (Mesh)
			{
				MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			}
		}
	}

	// Material shorthand
	FString MaterialPath = OptionalString(Params, TEXT("material"));
	if (!MaterialPath.IsEmpty())
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		if (Material)
		{
			UPrimitiveComponent* PrimComp = NewActor->FindComponentByClass<UPrimitiveComponent>();
			if (PrimComp)
			{
				PrimComp->SetMaterial(0, Material);
			}
		}
	}

	const FString FinalLabel = NewActor->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorClass"), ActorClass);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DeleteActor(const TSharedPtr<FJsonObject>& Params)
{
	FString Selector;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), Selector)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// #983: a duplicate label is refused rather than deleted at random. The
	// miss stays idempotent, but "already deleted" would be a lie when three
	// actors carry the label and all three are still there.
	TSharedPtr<FJsonValue> ActorErr;
	AActor* ActorToDelete = MCPResolveActor(World, Params, ActorErr);
	if (!ActorToDelete && MCPIsAmbiguousActorError(ActorErr)) return ActorErr;

	// Idempotent: deleting a non-existent actor is a no-op, not an error.
	if (!ActorToDelete)
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("actorLabel"), Selector);
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Result);
	}

	const FString ActorLabel = ActorToDelete->GetActorLabel();

	// Snapshot before the actor goes away. A delete inverts to a spawn only as
	// far as what was captured first, so capture the three things place_actor
	// can put back and say plainly that the rest is gone.
	const FString DeletedClassPath = ActorToDelete->GetClass()->GetPathName();
	const FTransform DeletedTransform = ActorToDelete->GetActorTransform();

	// place_actor checks the LABEL before it spawns and skips on a hit, so a
	// respawn that names a label some other actor still carries would return
	// existed:true and create nothing while reporting success. That is exactly
	// the case a path-addressed delete produces: this handler refuses an
	// ambiguous label, so reaching a namesake at all means the caller passed
	// actorPath. Count the namesakes that will remain after the destroy.
	TArray<AActor*> LabelMatches;
	MCPCollectActorsByToken(World, ActorLabel, EMCPActorMatch::Label, LabelMatches);
	const bool bLabelSurvivesElsewhere = LabelMatches.Num() > 1;

	World->DestroyActor(ActorToDelete);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("deleted"), true);

	Result->SetBoolField(TEXT("labelSurvivesElsewhere"), bLabelSurvivesElsewhere);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorClass"), DeletedClassPath);
	Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(DeletedTransform.GetLocation()));
	Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(DeletedTransform.Rotator()));
	Payload->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(DeletedTransform.GetScale3D()));

	FString LossNote =
		TEXT("place_actor respawns the same class at the same transform. It does not restore the actor's property overrides, its instance components, its tags, its folder path, its attachments or its streaming state, and the respawned actor is a new object with a new path, so anything that referenced the old one by path stays broken.");
	if (bLabelSurvivesElsewhere)
	{
		// Naming the label here would make place_actor find the surviving
		// namesake, answer existed:true and spawn NOTHING while reporting
		// success. Spawning under an engine-assigned label is a visible,
		// truthful outcome; a silent no-op is not.
		LossNote += TEXT(" The label is deliberately NOT restored: another actor already carries it, and place_actor skips when a label is taken, so passing it would spawn nothing and still report success. The respawned actor gets an engine-assigned label and has to be renamed by hand.");
	}
	else
	{
		// Unique at delete time. onConflict=error so that if something claims
		// the label before the replay, the rollback FAILS visibly instead of
		// returning existed:true and creating nothing.
		Payload->SetStringField(TEXT("label"), ActorLabel);
		Payload->SetStringField(TEXT("onConflict"), TEXT("error"));
		LossNote += TEXT(" The label was unique when this ran, so the inverse restores it and passes onConflict=error: if another actor has taken the label by the time the inverse runs, it fails loudly rather than skipping the spawn and reporting success.");
	}
	LossNote += TEXT(" On a World Partition map the respawn also lands in whichever cell the transform falls in, which need not be the package the original lived in.");

	MCPSetRollback(Result, TEXT("place_actor"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), LossNote);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetActorDetails(const TSharedPtr<FJsonObject>& Params)
{
	FString Selector;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), Selector)) return Err;

	// World selection: "editor" (default) or "pie" (#111)
	// #778: this hand-rolled loop took the FIRST PIE context, i.e. the server,
	// so pieInstance could not select a client. Use the shared resolver.
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = nullptr;
	if (WorldScope.Equals(TEXT("pie"), ESearchCase::IgnoreCase) || WorldScope.Equals(TEXT("game"), ESearchCase::IgnoreCase))
	{
		World = ResolveWorldFromParams(Params, *WorldScope);
		if (!World) return MCPError(TEXT("No PIE/Game world active (or no such pieInstance). See editor(list_pie_instances)."));
	}
	else
	{
		World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World) return MCPError(TEXT("No editor world available"));
	}

	// LabelOrName, not label alone: a caller often has an internal name rather
	// than a label, because a PIE-spawned actor has no label worth guessing.
	// The label pass is still exhausted first and the name pass refuses on
	// ambiguity, so accepting the name cannot reintroduce a silent pick (#983).
	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelOrName;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("name"), Actor->GetName());
	Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Result->SetStringField(TEXT("path"), Actor->GetPathName());
	// #983: the same value under the name the selector uses, so the round trip
	// back into any actor-targeting action is a copy of one field.
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("folderPath"), Actor->GetFolderPath().ToString());

	FVector Location = Actor->GetActorLocation();
	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Result->SetObjectField(TEXT("location"), LocationObj);

	FRotator Rot = Actor->GetActorRotation();
	TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
	RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
	Result->SetObjectField(TEXT("rotation"), RotObj);

	FVector Scale = Actor->GetActorScale3D();
	TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	Result->SetObjectField(TEXT("scale"), ScaleObj);

	if (AActor* Parent = Actor->GetAttachParentActor())
	{
		Result->SetStringField(TEXT("attachParent"), Parent->GetActorLabel());
	}

	// Components (always on) - name + class
	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	TArray<TSharedPtr<FJsonValue>> CompArr;
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), Comp->GetName());
		C->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		CompArr.Add(MakeShared<FJsonValueObject>(C));
	}
	Result->SetArrayField(TEXT("components"), CompArr);

	// #125: optional includeProperties=true dumps UPROPERTY name/type/value
	if (OptionalBool(Params, TEXT("includeProperties")))
	{
		FString PropFilter = OptionalString(Params, TEXT("propertyName"));
		TArray<TSharedPtr<FJsonValue>> PropsArr;
		for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;
			if (!PropFilter.IsEmpty() && Prop->GetName() != PropFilter) continue;

			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Prop->GetName());
			P->SetStringField(TEXT("type"), Prop->GetCPPType());

			// #927: a UPROPERTY declared as a C-style fixed array, `int32 Foo[3]`,
			// is ONE FProperty with ArrayDim == 3, not three properties. Exporting
			// it without an index writes element 0 and stops, and the value then
			// reads as an ordinary scalar with the remaining elements invisible.
			//
			// This is a general serialization bug, not a navmesh one. It was
			// noticed on RecastNavMesh's NavMeshResolutionParams, a three-element
			// lint-prose-allow: tier  RecastNavMesh's own name for its three generation tiers
			// fixed array holding the Low, Default and High generation tiers, and
			// lint-prose-allow: tier  RecastNavMesh's own name for its three generation tiers
			// reporting only the Low tier as if it were the whole property sent a
			// user tuning cell sizes against numbers Recast was not using. Any
			// fixed array on any class had the same problem.
			//
			// MCPExportPropertyValue returns a JSON array of one string per
			// element when ArrayDim > 1 and a plain string otherwise, so the two
			// cases stay distinguishable rather than being conflated.
			P->SetField(TEXT("value"), MCPExportPropertyValue(Prop, Actor));
			if (MCPPropertyIsFixedArray(Prop))
			{
				P->SetNumberField(TEXT("arrayDim"), Prop->ArrayDim);
			}
			PropsArr.Add(MakeShared<FJsonValueObject>(P));
		}
		Result->SetArrayField(TEXT("properties"), PropsArr);
		Result->SetNumberField(TEXT("propertyCount"), PropsArr.Num());
	}

	return MCPResult(Result);
}

// #240/#241/#302/#320/#370/#353: deep component-tree introspection.
//
// Single call returns the actor's component list with all the inspection
// data that previously required either a tower of blueprint.get_component_property
// calls or a fall-back to execute_python with subclass-specific accessors.
// Covers:
//   - attach topology (parent + socket)
//   - relative + world transforms
//   - mobility + visibility
//   - collision profile + enabled state for PrimitiveComponents
//   - mesh path + override materials for StaticMesh / SkeletalMesh / SplineMesh
//   - bounds (origin + extent) for PrimitiveComponents
//   - tags
//   - reflected UPROPERTY name/type/value when includeProperties=true
TSharedPtr<FJsonValue> FLevelHandlers::GetComponentTree(const TSharedPtr<FJsonObject>& Params)
{
	FString Selector;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), Selector)) return Err;

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World '%s' not available"), *WorldScope));
	}

	// LabelOrName, not label alone: a caller often has an internal name rather
	// than a label, because a PIE-spawned actor has no label worth guessing.
	// The label pass is still exhausted first and the name pass refuses on
	// ambiguity, so accepting the name cannot reintroduce a silent pick (#983).
	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelOrName;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;

	const bool bIncludeProperties = OptionalBool(Params, TEXT("includeProperties"));
	const FString PropertyFilter = OptionalString(Params, TEXT("componentClass"));

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	TArray<TSharedPtr<FJsonValue>> CompArr;
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;
		if (!PropertyFilter.IsEmpty() && !Comp->GetClass()->GetName().Contains(PropertyFilter, ESearchCase::IgnoreCase)) continue;

		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), Comp->GetName());
		C->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		C->SetBoolField(TEXT("isEditorOnly"), Comp->IsEditorOnly());

		// Tags array
		TArray<TSharedPtr<FJsonValue>> TagArr;
		for (FName Tag : Comp->ComponentTags) { TagArr.Add(MakeShared<FJsonValueString>(Tag.ToString())); }
		C->SetArrayField(TEXT("tags"), TagArr);

		if (USceneComponent* SC = Cast<USceneComponent>(Comp))
		{
			// Attach topology
			if (USceneComponent* AttachParent = SC->GetAttachParent())
			{
				C->SetStringField(TEXT("attachParent"), AttachParent->GetName());
			}
			const FName SocketName = SC->GetAttachSocketName();
			if (SocketName != NAME_None)
			{
				C->SetStringField(TEXT("attachSocket"), SocketName.ToString());
			}

			// Visibility + mobility
			C->SetBoolField(TEXT("bVisible"), SC->IsVisible());
			switch (SC->Mobility)
			{
			case EComponentMobility::Static:     C->SetStringField(TEXT("mobility"), TEXT("Static")); break;
			case EComponentMobility::Stationary: C->SetStringField(TEXT("mobility"), TEXT("Stationary")); break;
			case EComponentMobility::Movable:    C->SetStringField(TEXT("mobility"), TEXT("Movable")); break;
			default: break;
			}

			// Relative transform
			const FVector RelLoc = SC->GetRelativeLocation();
			const FRotator RelRot = SC->GetRelativeRotation();
			const FVector RelScale = SC->GetRelativeScale3D();
			auto MakeVec = [](const FVector& V) {
				auto O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("x"), V.X);
				O->SetNumberField(TEXT("y"), V.Y);
				O->SetNumberField(TEXT("z"), V.Z);
				return O;
			};
			auto MakeRot = [](const FRotator& R) {
				auto O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("pitch"), R.Pitch);
				O->SetNumberField(TEXT("yaw"), R.Yaw);
				O->SetNumberField(TEXT("roll"), R.Roll);
				return O;
			};
			C->SetObjectField(TEXT("relativeLocation"), MakeVec(RelLoc));
			C->SetObjectField(TEXT("relativeRotation"), MakeRot(RelRot));
			C->SetObjectField(TEXT("relativeScale"), MakeVec(RelScale));

			// World transform
			C->SetObjectField(TEXT("worldLocation"), MakeVec(SC->GetComponentLocation()));
			C->SetObjectField(TEXT("worldRotation"), MakeRot(SC->GetComponentRotation()));
			C->SetObjectField(TEXT("worldScale"), MakeVec(SC->GetComponentScale()));

			if (UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(SC))
			{
				// Collision profile
				const FName CollisionProfile = PC->GetCollisionProfileName();
				C->SetStringField(TEXT("collisionProfile"), CollisionProfile.ToString());
				switch (PC->GetCollisionEnabled())
				{
				case ECollisionEnabled::NoCollision:        C->SetStringField(TEXT("collisionEnabled"), TEXT("NoCollision")); break;
				case ECollisionEnabled::QueryOnly:          C->SetStringField(TEXT("collisionEnabled"), TEXT("QueryOnly")); break;
				case ECollisionEnabled::PhysicsOnly:        C->SetStringField(TEXT("collisionEnabled"), TEXT("PhysicsOnly")); break;
				case ECollisionEnabled::QueryAndPhysics:    C->SetStringField(TEXT("collisionEnabled"), TEXT("QueryAndPhysics")); break;
				case ECollisionEnabled::ProbeOnly:          C->SetStringField(TEXT("collisionEnabled"), TEXT("ProbeOnly")); break;
				case ECollisionEnabled::QueryAndProbe:      C->SetStringField(TEXT("collisionEnabled"), TEXT("QueryAndProbe")); break;
				default: break;
				}
				C->SetBoolField(TEXT("castShadow"), PC->CastShadow);

				// Bounds
				const FBoxSphereBounds Bounds = PC->Bounds;
				C->SetObjectField(TEXT("boundsOrigin"), MakeVec(Bounds.Origin));
				C->SetObjectField(TEXT("boundsBoxExtent"), MakeVec(Bounds.BoxExtent));
				C->SetNumberField(TEXT("boundsSphereRadius"), Bounds.SphereRadius);

				// Material slots + meshes (mesh-component subclasses)
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(PC))
				{
					if (UStaticMesh* Mesh = SMC->GetStaticMesh())
					{
						C->SetStringField(TEXT("staticMesh"), Mesh->GetPathName());
					}
					TArray<TSharedPtr<FJsonValue>> Mats;
					const int32 NumMats = SMC->GetNumMaterials();
					for (int32 i = 0; i < NumMats; i++)
					{
						UMaterialInterface* Mat = SMC->GetMaterial(i);
						Mats.Add(MakeShared<FJsonValueString>(Mat ? Mat->GetPathName() : TEXT("")));
					}
					C->SetArrayField(TEXT("materials"), Mats);

					// #986: an ISM/HISM reported its class and its mesh and not
					// how many instances it holds, which is the one number that
					// decides whether to touch it at all. A component with three
					// instances and one with three hundred thousand looked
					// identical here, so the decision was made blind or cost a
					// separate get_instance_transforms dump of every transform.
					if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC))
					{
						C->SetNumberField(TEXT("instanceCount"), ISMC->GetInstanceCount());
						TSharedPtr<FJsonObject> Instanced = MakeShared<FJsonObject>();
						// HISM culls and LODs per instance and ISM does not, so
						// the distinction changes what an edit costs.
						Instanced->SetBoolField(TEXT("hierarchical"),
							ISMC->IsA<UHierarchicalInstancedStaticMeshComponent>());
						Instanced->SetNumberField(TEXT("numCustomDataFloats"), ISMC->NumCustomDataFloats);
						C->SetObjectField(TEXT("instanced"), Instanced);
					}
				}
				else if (USkeletalMeshComponent* SKMC = Cast<USkeletalMeshComponent>(PC))
				{
					if (USkeletalMesh* Mesh = SKMC->GetSkeletalMeshAsset())
					{
						C->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
					}
					TArray<TSharedPtr<FJsonValue>> Mats;
					const int32 NumMats = SKMC->GetNumMaterials();
					for (int32 i = 0; i < NumMats; i++)
					{
						UMaterialInterface* Mat = SKMC->GetMaterial(i);
						Mats.Add(MakeShared<FJsonValueString>(Mat ? Mat->GetPathName() : TEXT("")));
					}
					C->SetArrayField(TEXT("materials"), Mats);
					if (USkeleton* Skel = SKMC->GetSkeletalMeshAsset() ? SKMC->GetSkeletalMeshAsset()->GetSkeleton() : nullptr)
					{
						C->SetStringField(TEXT("skeleton"), Skel->GetPathName());
					}
				}
			}
		}

		// #581: dynamically-spawned FX components' runtime state, so visual
		// verification doesn't need Python. NiagaraComponent: asset/active/visible;
		// AudioComponent: sound/playing. Works in editor or PIE (world scope).
		if (UNiagaraComponent* Niagara = Cast<UNiagaraComponent>(Comp))
		{
			TSharedPtr<FJsonObject> Fx = MakeShared<FJsonObject>();
			if (UNiagaraSystem* Sys = Niagara->GetAsset()) Fx->SetStringField(TEXT("asset"), Sys->GetPathName());
			Fx->SetBoolField(TEXT("active"), Niagara->IsActive());
			Fx->SetBoolField(TEXT("visible"), Niagara->IsVisible());
			C->SetObjectField(TEXT("niagara"), Fx);
		}
		else if (UAudioComponent* Audio = Cast<UAudioComponent>(Comp))
		{
			TSharedPtr<FJsonObject> Fx = MakeShared<FJsonObject>();
			if (USoundBase* Snd = Audio->GetSound()) Fx->SetStringField(TEXT("sound"), Snd->GetPathName());
			Fx->SetBoolField(TEXT("playing"), Audio->IsPlaying());
			C->SetObjectField(TEXT("audio"), Fx);
		}

		if (bIncludeProperties)
		{
			TArray<TSharedPtr<FJsonValue>> Props;
			for (TFieldIterator<FProperty> PIt(Comp->GetClass()); PIt; ++PIt)
			{
				FProperty* Prop = *PIt;
				if (!Prop) continue;
				// Skip uneditable / hidden flagged fields to keep the payload focused
				// on values an agent would actually inspect.
				if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_DisableEditOnInstance)) continue;
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"), Prop->GetName());
				P->SetStringField(TEXT("type"), Prop->GetCPPType());
				// #927: a fixed array is one FProperty with ArrayDim > 1, and
				// exporting it without an index reports element 0 as though it
				// were the whole value. Same helper as the actor dump, so the
				// two cannot drift.
				P->SetField(TEXT("value"), MCPExportPropertyValue(Prop, Comp));
				if (MCPPropertyIsFixedArray(Prop))
				{
					P->SetNumberField(TEXT("arrayDim"), Prop->ArrayDim);
				}
				Props.Add(MakeShared<FJsonValueObject>(P));
			}
			C->SetArrayField(TEXT("properties"), Props);
		}

		CompArr.Add(MakeShared<FJsonValueObject>(C));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
	Result->SetNumberField(TEXT("componentCount"), CompArr.Num());
	Result->SetArrayField(TEXT("components"), CompArr);
	return MCPResult(Result);
}

// #386/#387: compute target's transform expressed in reference's local space.
// Common dungeon/calibration workflow: figure out the local-space "snap rule"
// for an actor that was manually aligned to a parent actor. Previously this
// required execute_python with MathLibrary.inverse_transform_location.
TSharedPtr<FJsonValue> FLevelHandlers::GetRelativeTransform(const TSharedPtr<FJsonObject>& Params)
{
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World '%s' not available"), *WorldScope));

	// #983: both ends take a path. Two duplicated labels would otherwise
	// produce a relative transform between whichever pair the actor iterator
	// reached first, which is exactly the number this action exists to trust.
	FMCPActorSelector TargetSel;
	TargetSel.LabelKey = TEXT("targetLabel");
	TargetSel.PathKey = TEXT("targetPath");
	TargetSel.AltLabelKey = TEXT("target");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* TargetActor = MCPResolveActor(World, Params, ActorErr, TargetSel);
	if (!TargetActor) return ActorErr;

	FMCPActorSelector ReferenceSel;
	ReferenceSel.LabelKey = TEXT("referenceLabel");
	ReferenceSel.PathKey = TEXT("referencePath");
	ReferenceSel.AltLabelKey = TEXT("reference");
	AActor* ReferenceActor = MCPResolveActor(World, Params, ActorErr, ReferenceSel);
	if (!ReferenceActor) return ActorErr;

	const FString TargetLabel = TargetActor->GetActorLabel();
	const FString ReferenceLabel = ReferenceActor->GetActorLabel();

	const FTransform Target = TargetActor->GetActorTransform();
	const FTransform Reference = ReferenceActor->GetActorTransform();
	const FTransform Relative = Target.GetRelativeTransform(Reference);

	auto MakeVec = [](const FVector& V) {
		auto O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), V.X);
		O->SetNumberField(TEXT("y"), V.Y);
		O->SetNumberField(TEXT("z"), V.Z);
		return O;
	};
	auto MakeRot = [](const FRotator& R) {
		auto O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("pitch"), R.Pitch);
		O->SetNumberField(TEXT("yaw"), R.Yaw);
		O->SetNumberField(TEXT("roll"), R.Roll);
		return O;
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("targetLabel"), TargetLabel);
	Result->SetStringField(TEXT("targetPath"), TargetActor->GetPathName());
	Result->SetStringField(TEXT("referenceLabel"), ReferenceLabel);
	Result->SetStringField(TEXT("referencePath"), ReferenceActor->GetPathName());
	Result->SetObjectField(TEXT("location"), MakeVec(Relative.GetLocation()));
	Result->SetObjectField(TEXT("rotation"), MakeRot(Relative.GetRotation().Rotator()));
	Result->SetObjectField(TEXT("scale"), MakeVec(Relative.GetScale3D()));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetCurrentLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ULevel* CurrentLevel = World->GetCurrentLevel();
	if (!CurrentLevel)
	{
		return MCPError(TEXT("No current level"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("levelName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), World->GetPathName());

	// #166: Also return the map package path for tools that need the full asset reference
	UPackage* MapPackage = World->GetOutermost();
	if (MapPackage)
	{
		Result->SetStringField(TEXT("mapPackagePath"), MapPackage->GetName());
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListLevels(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// T3: paged. A streaming-heavy map carries hundreds of sublevels, and this
	// answered all of them at once.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, TEXT("list_levels"), /*DefaultLimit*/ 200, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	// Deliberately NOT sorted. The persistent level first and then the world's
	// own streaming-level array is an authored order that carries meaning, and
	// it is stable between two calls because it is a stored array rather than a
	// hash traversal.
	TArray<MCPPagination::FPageRow> Rows;

	// Add persistent level
	TSharedPtr<FJsonObject> PersistentObj = MakeShared<FJsonObject>();
	PersistentObj->SetStringField(TEXT("name"), World->GetName());
	PersistentObj->SetStringField(TEXT("type"), TEXT("persistent"));
	PersistentObj->SetBoolField(TEXT("isLoaded"), true);
	// The anchor is the level's package name, prefixed by its kind so the
	// persistent level cannot collide with a sublevel of the same name.
	Rows.Add({ FString::Printf(TEXT("persistent:%s"), *World->GetName()), MakeShared<FJsonValueObject>(PersistentObj) });

	// Add streaming levels
	const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();
	for (ULevelStreaming* StreamingLevel : StreamingLevels)
	{
		if (!StreamingLevel) continue;

		const FString PackageName = StreamingLevel->GetWorldAssetPackageFName().ToString();
		TSharedPtr<FJsonObject> LevelObj = MakeShared<FJsonObject>();
		LevelObj->SetStringField(TEXT("name"), PackageName);
		LevelObj->SetStringField(TEXT("type"), TEXT("streaming"));
		LevelObj->SetBoolField(TEXT("isLoaded"), StreamingLevel->IsLevelLoaded());
		LevelObj->SetBoolField(TEXT("isVisible"), StreamingLevel->IsLevelVisible());
		Rows.Add({ FString::Printf(TEXT("streaming:%s"), *PackageName), MakeShared<FJsonValueObject>(LevelObj) });
	}

	auto Result = MCPSuccess();
	MCPPagination::EmitPage(Page, Rows, TEXT("levels"), Result);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetSelectedActors(const TSharedPtr<FJsonObject>& Params)
{
	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection)
	{
		return MCPError(TEXT("Unable to get selection"));
	}

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	for (int32 i = 0; i < Selection->Num(); i++)
	{
		AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
		if (!Actor) continue;

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ActorObj->SetStringField(TEXT("path"), Actor->GetPathName());

		FVector Location = Actor->GetActorLocation();
		TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
		LocationObj->SetNumberField(TEXT("x"), Location.X);
		LocationObj->SetNumberField(TEXT("y"), Location.Y);
		LocationObj->SetNumberField(TEXT("z"), Location.Z);
		ActorObj->SetObjectField(TEXT("location"), LocationObj);

		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetNumberField(TEXT("count"), ActorsArray.Num());

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FLevelHandlers::MoveActor(const TSharedPtr<FJsonObject>& Params)
{
	FString Selector;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), Selector)) return Err;

	// #586: support the PIE world so a label from get_outliner {world:pie}
	// resolves and the live actor moves. The resolver also matches the runtime
	// instance name PIE shows.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	// LabelOrName, not label alone: a caller often has an internal name rather
	// than a label, because a PIE-spawned actor has no label worth guessing.
	// The label pass is still exhausted first and the name pass refuses on
	// ambiguity, so accepting the name cannot reintroduce a silent pick (#983).
	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelOrName;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;
	const FString ActorLabel = Actor->GetActorLabel();

	// Capture previous transform for rollback.
	const FVector PreviousLocation = Actor->GetActorLocation();
	const FRotator PreviousRotation = Actor->GetActorRotation();
	const FVector PreviousScale = Actor->GetActorScale3D();

	if (Params->HasField(TEXT("location")))
	{
		Actor->SetActorLocation(OptionalVec3(Params, TEXT("location"), Actor->GetActorLocation()));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		Actor->SetActorRotation(OptionalRotator(Params, TEXT("rotation"), Actor->GetActorRotation()));
	}
	if (Params->HasField(TEXT("scale")))
	{
		Actor->SetActorScale3D(OptionalVec3(Params, TEXT("scale"), Actor->GetActorScale3D()));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Actor->GetActorLocation()));
	Result->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Actor->GetActorRotation()));
	Result->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Actor->GetActorScale3D()));
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());

	// Self-inverse: call move_actor with previous transform. #983: the undo
	// travels by path, so replaying it cannot land on a different actor that
	// happens to share the label.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(PreviousLocation));
	Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(PreviousRotation));
	Payload->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(PreviousScale));
	MCPSetRollback(Result, TEXT("move_actor"), Payload);

	return MCPResult(Result);
}

// #566 aim_actor_at - rotate an actor so its +X (forward) points at a target
// point or another actor. Saves the "frame this from the bridge" round-trip of
// reading two transforms and computing the look-at client-side.
TSharedPtr<FJsonValue> FLevelHandlers::AimActorAt(const TSharedPtr<FJsonObject>& Params)
{
	FString Selector;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), Selector)) return Err;

	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	const FString ActorLabel = Actor->GetActorLabel();

	// Resolve the target point: an explicit target Vec3, or another actor's location.
	FVector TargetLocation;
	if (Params->HasField(TEXT("targetActor")) || Params->HasField(TEXT("targetActorPath")))
	{
		FMCPActorSelector TargetSel;
		TargetSel.LabelKey = TEXT("targetActor");
		TargetSel.PathKey = TEXT("targetActorPath");
		AActor* TargetActor = MCPResolveActor(World, Params, ActorErr, TargetSel);
		if (!TargetActor) return ActorErr;
		TargetLocation = TargetActor->GetActorLocation();
	}
	else if (Params->HasField(TEXT("target")))
	{
		TargetLocation = OptionalVec3(Params, TEXT("target"), FVector::ZeroVector);
	}
	else
	{
		return MCPError(TEXT("Supply 'target' (Vec3), 'targetActor' (label) or 'targetActorPath' (object path)"));
	}

	const FVector ActorLocation = Actor->GetActorLocation();
	const FVector Direction = TargetLocation - ActorLocation;
	if (Direction.IsNearlyZero())
	{
		return MCPError(TEXT("Actor and target are at the same location; look-at is undefined"));
	}

	const FRotator PreviousRotation = Actor->GetActorRotation();
	FRotator LookAt = FRotationMatrix::MakeFromX(Direction).Rotator();
	const double Roll = OptionalNumber(Params, TEXT("roll"), 0.0);
	LookAt.Roll = Roll;
	Actor->SetActorRotation(LookAt);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Actor->GetActorRotation()));
	Result->SetObjectField(TEXT("target"), MCPVec3ToJsonObject(TargetLocation));

	// Rollback: restore the prior rotation via move_actor, by path so the undo
	// cannot land on a namesake (#983).
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(PreviousRotation));
	MCPSetRollback(Result, TEXT("move_actor"), Payload);

	return MCPResult(Result);
}

// #585 nav_project_point - project a world point onto the navmesh, returning the
// nearest navigable location and whether the point is on the navmesh. Works in
// editor or PIE (navmesh must be built/generated for the world).
TSharedPtr<FJsonValue> FLevelHandlers::NavProjectPoint(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params->HasField(TEXT("point"))) return MCPError(TEXT("Missing 'point' (Vec3)"));
	const FVector Point = OptionalVec3(Params, TEXT("point"), FVector::ZeroVector);

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
	if (!Nav) return MCPError(TEXT("No navigation system in this world (add a NavMeshBoundsVolume and build navigation)"));

	const FVector Extent = Params->HasField(TEXT("extent"))
		? OptionalVec3(Params, TEXT("extent"), FVector(100.f, 100.f, 100.f))
		: FVector(100.f, 100.f, 100.f);

	FNavLocation Out;
	const bool bOnNav = Nav->ProjectPointToNavigation(Point, Out, Extent);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("onNavMesh"), bOnNav);
	Result->SetObjectField(TEXT("queryPoint"), MCPVec3ToJsonObject(Point));
	if (bOnNav) Result->SetObjectField(TEXT("projectedLocation"), MCPVec3ToJsonObject(Out.Location));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SelectActors(const TSharedPtr<FJsonObject>& Params)
{
	static const TArray<TSharedPtr<FJsonValue>> EmptySelection;
	const TArray<TSharedPtr<FJsonValue>>* ActorLabelsArray = &EmptySelection;
	const bool bHasLabels = Params->TryGetArrayField(TEXT("actorLabels"), ActorLabelsArray);
	if (!bHasLabels) ActorLabelsArray = &EmptySelection;
	if (!bHasLabels && !Params->HasField(TEXT("actorPaths")))
	{
		return MCPError(TEXT("Missing 'actorLabels' parameter (or 'actorPaths')"));
	}

	REQUIRE_EDITOR_WORLD(World);

	// What was selected before, by path, so the inverse restores exactly these
	// actors rather than whatever answers to their labels afterwards.
	TArray<TSharedPtr<FJsonValue>> PreviousPaths;
	TSet<FString> PreviousPathSet;
	if (USelection* PreviousSelection = GEditor->GetSelectedActors())
	{
		for (int32 i = 0; i < PreviousSelection->Num(); i++)
		{
			if (AActor* Selected = Cast<AActor>(PreviousSelection->GetSelectedObject(i)))
			{
				PreviousPaths.Add(MakeShared<FJsonValueString>(Selected->GetPathName()));
				PreviousPathSet.Add(Selected->GetPathName());
			}
		}
	}

	// Deselect all
	GEditor->SelectNone(true, true, false);

	TArray<TSharedPtr<FJsonValue>> SelectedArray;
	TArray<TSharedPtr<FJsonValue>> NotFoundArray;

	// #983: selection is the plural case, so a label naming several actors
	// selects all of them rather than one at random. selectedPaths reports
	// exactly which, and is what a follow-up write should target.
	TArray<TSharedPtr<FJsonValue>> SelectedPathsArray;
	for (const TSharedPtr<FJsonValue>& LabelValue : *ActorLabelsArray)
	{
		FString Label = LabelValue->AsString();
		TArray<AActor*> Matches;
		MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Matches);
		if (Matches.Num() == 0)
		{
			NotFoundArray.Add(MakeShared<FJsonValueString>(Label));
			continue;
		}
		for (AActor* Match : Matches)
		{
			GEditor->SelectActor(Match, true, true, true);
			SelectedPathsArray.Add(MakeShared<FJsonValueString>(Match->GetPathName()));
		}
		SelectedArray.Add(MakeShared<FJsonValueString>(Label));
	}

	// An explicit path list selects exactly what it names, with no label
	// resolution in the way at all.
	const TArray<TSharedPtr<FJsonValue>>* ActorPathsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("actorPaths"), ActorPathsArray))
	{
		for (const TSharedPtr<FJsonValue>& PathValue : *ActorPathsArray)
		{
			const FString Path = PathValue->AsString();
			if (AActor* Match = MCPFindActorByPath(World, Path))
			{
				GEditor->SelectActor(Match, true, true, true);
				SelectedPathsArray.Add(MakeShared<FJsonValueString>(Match->GetPathName()));
				SelectedArray.Add(MakeShared<FJsonValueString>(Match->GetActorLabel()));
			}
			else
			{
				NotFoundArray.Add(MakeShared<FJsonValueString>(Path));
			}
		}
	}

	// Same set, same selection: a replay of this call changed nothing.
	bool bUnchanged = SelectedPathsArray.Num() == PreviousPathSet.Num();
	if (bUnchanged)
	{
		for (const TSharedPtr<FJsonValue>& PathValue : SelectedPathsArray)
		{
			if (!PreviousPathSet.Contains(PathValue->AsString())) { bUnchanged = false; break; }
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("selected"), SelectedArray);
	Result->SetArrayField(TEXT("selectedPaths"), SelectedPathsArray);
	Result->SetArrayField(TEXT("notFound"), NotFoundArray);
	Result->SetNumberField(TEXT("selectedCount"), SelectedPathsArray.Num());
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);

	// select_actors replaces the selection wholesale, so replaying it with the
	// paths that were selected before restores it exactly.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("actorPaths"), PreviousPaths);
	MCPSetRollback(Result, TEXT("select_actors"), Payload);

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FLevelHandlers::AddComponentToActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	FString ComponentClass;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClass)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// Idempotency: check for an existing component with the same name on the actor.
	FName CompName = FName(*ComponentName);
	for (UActorComponent* Existing : Actor->GetComponents())
	{
		if (Existing && Existing->GetFName() == CompName)
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(
					TEXT("Component '%s' already exists on '%s'"), *ComponentName, *ActorLabel));
			}
			auto ExistingResult = MCPSuccess();
			MCPSetExisted(ExistingResult);
			ExistingResult->SetStringField(TEXT("actorLabel"), ActorLabel);
			ExistingResult->SetStringField(TEXT("actorPath"), Actor->GetPathName());
			ExistingResult->SetStringField(TEXT("componentName"), ComponentName);
			ExistingResult->SetStringField(TEXT("componentClass"), Existing->GetClass()->GetName());
			return MCPResult(ExistingResult);
		}
	}

	// (#137) Robust class resolution: full path, short name, or engine-module implicit lookup.
	UClass* CompClass = nullptr;
	if (ComponentClass.Contains(TEXT("/")) || ComponentClass.Contains(TEXT(".")))
	{
		CompClass = LoadObject<UClass>(nullptr, *ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = FindClassByShortName(ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = LoadObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ComponentClass));
	}

	if (!CompClass)
	{
		return MCPError(FString::Printf(TEXT("Component class not found: %s. Try the short name (e.g. 'StaticMeshComponent') or the full path ('/Script/Engine.StaticMeshComponent')."), *ComponentClass));
	}

	if (!CompClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Class '%s' is not an ActorComponent"), *ComponentClass));
	}

	UActorComponent* NewComponent = NewObject<UActorComponent>(Actor, CompClass, CompName);
	if (!NewComponent)
	{
		return MCPError(TEXT("Failed to create component"));
	}

	USceneComponent* SceneComp = Cast<USceneComponent>(NewComponent);
	if (SceneComp && Actor->GetRootComponent())
	{
		SceneComp->SetupAttachment(Actor->GetRootComponent());
	}

	NewComponent->RegisterComponent();
	Actor->AddInstanceComponent(NewComponent);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("componentClass"), NewComponent->GetClass()->GetName());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("componentName"), ComponentName);
	MCPSetRollback(Result, TEXT("remove_component_from_actor"), Payload);
	return MCPResult(Result);
}

// #426: symmetric remove of an instance component. Idempotent (returns
// alreadyDeleted=true when the actor has no component with that name).
TSharedPtr<FJsonValue> FLevelHandlers::RemoveComponentFromActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	const FName CompName(*ComponentName);
	UActorComponent* Target = nullptr;
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (Comp && Comp->GetFName() == CompName) { Target = Comp; break; }
	}

	if (!Target)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("actorLabel"), ActorLabel);
		Noop->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Noop->SetStringField(TEXT("componentName"), ComponentName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	const FString ComponentClass = Target->GetClass()->GetName();
	// The PATH, not the short name, for the inverse: add_component_to_actor
	// only reaches a short name through FindClassByShortName and a
	// /Script/Engine. probe, neither of which resolves a Blueprint-generated
	// component class.
	const FString ComponentClassPath = Target->GetClass()->GetPathName();
	// Two facts the inverse has to disclose. This loop searches GetComponents(),
	// which includes native default subobjects and SCS components, so the thing
	// being destroyed is not necessarily an instance component and is not
	// necessarily replaceable.
	const bool bWasInstanceComponent = Actor->GetInstanceComponents().Contains(Target);
	const bool bWasRootComponent = (Target == Actor->GetRootComponent());

	Actor->Modify();
	Target->Modify();
	Actor->RemoveInstanceComponent(Target);
	Target->DestroyComponent();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("componentClass"), ComponentClass);
	Result->SetStringField(TEXT("componentClassPath"), ComponentClassPath);
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetBoolField(TEXT("wasInstanceComponent"), bWasInstanceComponent);
	Result->SetBoolField(TEXT("wasRootComponent"), bWasRootComponent);

	if (bWasRootComponent)
	{
		// add_component_to_actor attaches a new SceneComponent to the actor's
		// root, and the root is what was just destroyed. The inverse would
		// attach to null and leave the actor rootless, which is worse than
		// leaving it broken in the way the caller already broke it.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The component removed was the actor's ROOT. add_component_to_actor attaches what it creates to the actor's root component, which no longer exists, so the inverse would leave the actor rootless rather than restore it. There is no action that sets an actor's root component, so this has to be repaired in the editor or by undoing the transaction."));
		return MCPResult(Result);
	}

	// add_component_to_actor puts a component of the same class back on the
	// same actor under the same name. Its property state is not captured here,
	// so the replacement is a default-constructed component.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("componentName"), ComponentName);
	Payload->SetStringField(TEXT("componentClass"), ComponentClassPath);
	MCPSetRollback(Result, TEXT("add_component_to_actor"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);

	FString LossNote =
		TEXT("The replacement is a default-constructed component attached to the actor's root. Its property values, its own attachment parent and socket, and its relative transform are not restored. It is created with the same FName in the same outer as the object just destroyed, which the engine resolves by renaming rather than reusing, so the restored component's internal name can differ from the one reported here even though componentName matches.");
	if (!bWasInstanceComponent)
	{
		// The lookup above searches GetComponents(), so this can and does
		// happen: a native default subobject or an SCS component belongs to the
		// class, and what comes back is an instance component instead.
		LossNote += TEXT(" This component was NOT an instance component: it was a native default subobject or an SCS component that belongs to the actor's class. The inverse adds an INSTANCE component, so the actor ends up with a per-instance override where it previously had a class-owned component, and a construction-script rerun or a class recompile will not treat the two the same.");
	}
	LossNote += TEXT(" On a World Partition map the inverse resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded between this call and the replay.");
	Result->SetStringField(TEXT("rollbackNote"), LossNote);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::LoadLevel(const TSharedPtr<FJsonObject>& Params)
{
	FString LevelPath;
	if (auto Err = RequireString(Params, TEXT("levelPath"), LevelPath)) return Err;

	if (!GEditor) return MCPError(TEXT("GEditor not available"));

	// "Open level X" inverts to opening the level that was open, which has to
	// be read before the swap.
	//
	// The test is whether the package EXISTS ON DISK, not what mount it sits
	// under. A /Game or /Engine prefix check would call a map in a plugin's
	// content mount unreopenable, which is false: it is saved, it is mounted
	// and load_level takes it. Only an untitled or temporary world genuinely
	// has no path that reopens it, and that is what DoesPackageExist answers.
	FString PreviousLevelPath;
	if (UWorld* PreviousWorld = GEditor->GetEditorWorldContext().World())
	{
		if (UPackage* PreviousPackage = PreviousWorld->GetOutermost())
		{
			const FString PreviousName = PreviousPackage->GetName();
			if (!PreviousName.IsEmpty() && FPackageName::DoesPackageExist(PreviousName))
			{
				PreviousLevelPath = PreviousName;
			}
		}
	}
	// Both sides reduced to a package name before comparing. What was captured
	// above is a package name and what the caller passes is commonly the object
	// path form (/Game/Maps/Foo.Foo), so comparing them raw reports "not the
	// same map" for the same map.
	const FString RequestedPackageName = FPackageName::ObjectPathToPackageName(LevelPath);
	const bool bAlreadyOpen = !PreviousLevelPath.IsEmpty() && PreviousLevelPath == RequestedPackageName;

	// Short-circuit, and this is the whole point of the flag. Falling through
	// would end the play session, run two full-purge garbage collections and
	// reload the map from disk, throwing away every unsaved change to the very
	// map the response then calls "already open". A replayed step must not do
	// that.
	if (bAlreadyOpen)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("levelPath"), LevelPath);
		Noop->SetStringField(TEXT("previousLevelPath"), PreviousLevelPath);
		Noop->SetBoolField(TEXT("alreadyOpen"), true);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetBoolField(TEXT("endedPlaySession"), false);
		if (UWorld* OpenWorld = GEditor->GetEditorWorldContext().World())
		{
			Noop->SetStringField(TEXT("worldName"), OpenWorld->GetName());
			Noop->SetStringField(TEXT("worldPath"), OpenWorld->GetPathName());
		}
		Noop->SetStringField(TEXT("note"),
			TEXT("This map was already the open one, so nothing was reloaded: no play session was ended, no garbage collection ran and no unsaved change was discarded. There is no rollback because nothing changed."));
		return MCPResult(Noop);
	}

	// #590/#589: loading a map right after a PIE session (or a level-script
	// recompile / duplicate) fatally asserts "World Memory Leaks: N leaks
	// objects and packages" - the previous world's objects are still
	// referenced when the engine tears it down. End any in-flight play session
	// and force a full GC first so those references are released before the map
	// swap. Mirrors what the editor does between map loads.
	bool bEndedPIE = false;
	if (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor)
	{
		GEditor->EndPlayMap();
		bEndedPIE = true;
	}
	// Trim transient/PIE packages then collect twice - the first pass unroots
	// the world, the second reaps objects the first pass' cluster dissolve freed.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);

	// Use the LevelEditorSubsystem to load the level
	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditorSubsystem)
	{
		return MCPError(TEXT("LevelEditorSubsystem not available"));
	}

	bool bSuccess = LevelEditorSubsystem->LoadLevel(LevelPath);
	if (!bSuccess)
	{
		return MCPError(FString::Printf(TEXT("Failed to load level: %s"), *LevelPath));
	}

	// Get info about the newly loaded world
	auto Result = MCPSuccess();
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		Result->SetStringField(TEXT("worldName"), World->GetName());
		Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	}

	Result->SetStringField(TEXT("levelPath"), LevelPath);
	Result->SetBoolField(TEXT("endedPlaySession"), bEndedPIE);
	// The already-open case returned above, so reaching here is a real load.
	Result->SetBoolField(TEXT("alreadyOpen"), false);
	Result->SetBoolField(TEXT("unchanged"), false);

	if (!PreviousLevelPath.IsEmpty())
	{
		Result->SetStringField(TEXT("previousLevelPath"), PreviousLevelPath);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("levelPath"), PreviousLevelPath);
		MCPSetRollback(Result, TEXT("load_level"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Reopening the previous map loads it from disk. Unsaved in-memory changes to it were discarded by this load and do not come back, and any play session this call ended is not restarted."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The map that was open has no package file on disk, so no levelPath reopens it. That is an untitled or temporary world; a saved map on any mounted content path, a plugin's included, does get an inverse."));
	}

	return MCPResult(Result);
}

int32 FLevelHandlers::ClearBlueprintGraphNodes(
	UBlueprint* Blueprint,
	bool bDryRun,
	TArray<TSharedPtr<FJsonValue>>& OutGraphs)
{
	if (!Blueprint)
	{
		return 0;
	}

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	int32 NodeCount = 0;

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph || Graph->Nodes.IsEmpty())
		{
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> Nodes;
		TArray<UEdGraphNode*> GraphNodes;
		GraphNodes.Reserve(Graph->Nodes.Num());
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			GraphNodes.Add(Node);
		}
		if (!bDryRun)
		{
			Graph->Modify();
		}
		for (UEdGraphNode* Node : GraphNodes)
		{
			if (!Node)
			{
				continue;
			}

			auto NodeJson = MakeShared<FJsonObject>();
			NodeJson->SetStringField(TEXT("name"), Node->GetName());
			NodeJson->SetStringField(TEXT("classPath"), Node->GetClass()->GetPathName());
			Nodes.Add(MakeShared<FJsonValueObject>(NodeJson));
			++NodeCount;

			if (!bDryRun)
			{
				Node->Modify();
				FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile*/ true);
			}
		}

		auto GraphJson = MakeShared<FJsonObject>();
		GraphJson->SetStringField(TEXT("name"), Graph->GetName());
		GraphJson->SetNumberField(TEXT("nodeCount"), Nodes.Num());
		GraphJson->SetArrayField(TEXT("nodes"), Nodes);
		OutGraphs.Add(MakeShared<FJsonValueObject>(GraphJson));
	}

	return NodeCount;
}

TSharedPtr<FJsonValue> FLevelHandlers::ClearLevelScript(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("GEditor not available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !World->PersistentLevel)
	{
		return MCPError(TEXT("No persistent editor level is loaded"));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);
	const bool bSave = OptionalBool(Params, TEXT("save"), false);
	if (!bDryRun && bSave && World->GetOutermost()->IsDirty())
	{
		return MCPError(TEXT("Current level already has unsaved changes; save or discard them before clear_level_script with save=true"));
	}
	ULevelScriptBlueprint* LevelScript =
		World->PersistentLevel->GetLevelScriptBlueprint(/*bDontCreate*/ true);

	TArray<TSharedPtr<FJsonValue>> Graphs;
	TArray<TSharedPtr<FJsonValue>> Variables;
	int32 NodeCount = 0;
	int32 VariableCount = 0;
	bool bCompileSucceeded = true;
	bool bSaved = false;

	if (LevelScript)
	{
		TArray<FName> VariableNames;
		VariableNames.Reserve(LevelScript->NewVariables.Num());
		for (const FBPVariableDescription& Variable : LevelScript->NewVariables)
		{
			VariableNames.Add(Variable.VarName);
			Variables.Add(MakeShared<FJsonValueString>(Variable.VarName.ToString()));
		}
		VariableCount = VariableNames.Num();

		if (bDryRun)
		{
			NodeCount = ClearBlueprintGraphNodes(LevelScript, true, Graphs);
		}
		else
		{
			const FScopedTransaction Transaction(
				NSLOCTEXT("UEMCPBridge", "ClearLevelScript", "MCP clear level script"));
			World->Modify();
			World->PersistentLevel->Modify();
			LevelScript->Modify();
			NodeCount = ClearBlueprintGraphNodes(LevelScript, false, Graphs);
			for (const FName VariableName : VariableNames)
			{
				FBlueprintEditorUtils::RemoveMemberVariable(LevelScript, VariableName);
			}

			if (NodeCount > 0 || VariableCount > 0)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(LevelScript);
				FKismetEditorUtilities::CompileBlueprint(LevelScript);
				bCompileSucceeded = LevelScript->Status != BS_Error;
				World->MarkPackageDirty();
				if (!bCompileSucceeded)
				{
					return MCPError(TEXT("Level script nodes were cleared but compilation failed; the level was not saved and the change can be undone"));
				}
			}

			if (bSave && (NodeCount > 0 || VariableCount > 0))
			{
				ULevelEditorSubsystem* LevelEditorSubsystem =
					GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
				if (!LevelEditorSubsystem)
				{
					return MCPError(TEXT("LevelEditorSubsystem not available; level was changed but not saved"));
				}
				bSaved = LevelEditorSubsystem->SaveCurrentLevel();
				if (!bSaved)
				{
					return MCPError(TEXT("Level script was cleared and compiled, but the current level could not be saved"));
				}
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("levelPath"), World->GetOutermost()->GetName());
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("hasLevelScript"), LevelScript != nullptr);
	Result->SetNumberField(TEXT("graphCount"), Graphs.Num());
	Result->SetNumberField(TEXT("nodeCount"), NodeCount);
	Result->SetArrayField(TEXT("graphs"), Graphs);
	Result->SetNumberField(TEXT("variableCount"), VariableCount);
	Result->SetArrayField(TEXT("variables"), Variables);
	Result->SetBoolField(TEXT("compileSucceeded"), bCompileSucceeded);
	Result->SetBoolField(TEXT("saved"), bSaved);
	Result->SetBoolField(TEXT("unchanged"), bDryRun || (NodeCount == 0 && VariableCount == 0));
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("There is no action that rebuilds a level blueprint's graphs. The response names every node and variable removed, but a node's pins, its connections and its literal values are not captured, so nothing can replay them. Undo in the editor is the recovery path while the transaction is still on the stack, and clearing with save=false is what keeps it there."));
	return MCPResult(Result);
}

// Resolve a component on a placed actor by name, case-insensitively, across
// all components GetComponents returns (which includes inherited/SCS
// components on placed Blueprint instances). Empty name -> root component.
// (#539: case-sensitive exact-match was missing SCS components whose instance
// name differed only in case, reporting "component not found".)
static UActorComponent* FindComponentOnActor(AActor* Actor, const FString& Name)
{
	if (!Actor) return nullptr;
	if (Name.IsEmpty()) return Actor->GetRootComponent();

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	// Pass 1: exact match (case-insensitive) by instance name or class name.
	for (UActorComponent* Comp : Components)
	{
		if (Comp->GetName().Equals(Name, ESearchCase::IgnoreCase) ||
			Comp->GetClass()->GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Comp;
		}
	}
	// Pass 2: prefix match (e.g. "StaticMeshComponent" -> "StaticMeshComponent0").
	for (UActorComponent* Comp : Components)
	{
		if (Comp->GetName().StartsWith(Name, ESearchCase::IgnoreCase) ||
			Comp->GetClass()->GetName().StartsWith(Name, ESearchCase::IgnoreCase))
		{
			return Comp;
		}
	}
	// Pass 3: substring (handles _GEN_VARIABLE suffixes and decorated names).
	for (UActorComponent* Comp : Components)
	{
		if (Comp->GetName().Contains(Name, ESearchCase::IgnoreCase))
		{
			return Comp;
		}
	}
	return nullptr;
}

// Mutation handlers must not use FindComponentOnActor's class/prefix/substring
// fallbacks: a fuzzy selector can silently mutate the wrong sibling component.
// Resolve a named component by its instance name only, case-insensitively.
static UActorComponent* FindNamedComponentOnActor(AActor* Actor, const FString& Name)
{
	if (!Actor || Name.IsEmpty()) return nullptr;

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component && Component->GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Component;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonValue> FLevelHandlers::SetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	FString ComponentName = OptionalString(Params, TEXT("componentName"));

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// #763: this was hard-gated to the editor world, so runtime component
	// writes - setting a movement mode or a gameplay field on a live PIE
	// component - had no native path at all. Honour world/pieInstance like the
	// other actor-facing actions.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(WorldScope == TEXT("pie")
			? TEXT("PIE not running (or no such pieInstance). See editor(list_pie_instances).")
			: TEXT("Editor world not available"));
	}
	const bool bRuntimeWorld = World->IsGameWorld();

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	ActorSel.WorldLabel = bRuntimeWorld ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* TargetActor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!TargetActor) return ActorErr;
	ActorLabel = TargetActor->GetActorLabel();

	UActorComponent* TargetComp = FindComponentOnActor(TargetActor, ComponentName);
	if (!TargetComp)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *ActorLabel));
	}

	// #216: walk dotted property paths so callers can write
	// "GraphInstance.Graph" without us silently no-oping at the top level.
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));
	if (PathParts.Num() == 0)
	{
		return MCPError(TEXT("Empty propertyName"));
	}

	UStruct* CurrentStruct = TargetComp->GetClass();
	void* CurrentContainer = TargetComp;
	FProperty* Prop = nullptr;
	// #927: same fixed-array indexing as set_actor_property. A component
	// property declared `float Foo[4]` is one FProperty with ArrayDim 4, and
	// without an index every write lands on element 0.
	int32 LeafArrayIndex = 0;
	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FString Token = PathParts[i];
		int32 SegmentIndex = 0;
		bool bHasSegmentIndex = false;
		{
			int32 OpenBracket = INDEX_NONE;
			int32 CloseBracket = INDEX_NONE;
			if (Token.FindChar(TEXT('['), OpenBracket) &&
				Token.FindChar(TEXT(']'), CloseBracket) &&
				CloseBracket > OpenBracket)
			{
				SegmentIndex = FCString::Atoi(*Token.Mid(OpenBracket + 1, CloseBracket - OpenBracket - 1));
				Token = Token.Left(OpenBracket);
				bHasSegmentIndex = true;
			}
		}

		FProperty* SegmentProp = CurrentStruct->FindPropertyByName(FName(*Token));
		if (!SegmentProp)
		{
			return MCPError(FString::Printf(TEXT("Property '%s' not found at '%s'"), *Token, *PropertyName));
		}
		if (bHasSegmentIndex)
		{
			if (CastField<FArrayProperty>(SegmentProp))
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is a TArray. Indexing a dynamic array is not supported here; use asset(set_property) for dotted TArray paths. An index on this action addresses a C-style fixed array such as `float Foo[4]`."),
					*Token));
			}
			if (SegmentProp->ArrayDim <= 1)
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a fixed array, so it cannot be indexed [%d]"), *Token, SegmentIndex));
			}
			if (SegmentIndex < 0 || SegmentIndex >= SegmentProp->ArrayDim)
			{
				return MCPError(FString::Printf(
					TEXT("Index %d is out of range on '%s', which has ArrayDim %d"),
					SegmentIndex, *Token, SegmentProp->ArrayDim));
			}
		}
		if (i < PathParts.Num() - 1)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(SegmentProp))
			{
				CurrentContainer = SP->ContainerPtrToValuePtr<void>(CurrentContainer, SegmentIndex);
				CurrentStruct = SP->Struct;
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(SegmentProp))
			{
				// #305: descend through Instanced UObject sub-objects.
				UObject* SubObject = OP->GetObjectPropertyValue(
					OP->ContainerPtrToValuePtr<void>(CurrentContainer, SegmentIndex));
				if (!SubObject)
				{
					return MCPError(FString::Printf(
						TEXT("Cannot descend into '%s' - the sub-object reference is null"),
						*PathParts[i]));
				}
				SubObject->Modify();
				CurrentContainer = SubObject;
				CurrentStruct = SubObject->GetClass();
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a struct or sub-object - cannot descend"), *PathParts[i]));
			}
		}
		else
		{
			Prop = SegmentProp;
			LeafArrayIndex = SegmentIndex;
		}
	}

	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField || !(*ValueField).IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CurrentContainer, LeafArrayIndex);

	// Capture previous value as a string for self-inverse rollback.
	FString PreviousValueStr;
	Prop->ExportText_Direct(PreviousValueStr, ValuePtr, ValuePtr, TargetComp, PPF_None);

	FString ValueStr;
	if ((*ValueField)->TryGetString(ValueStr))
	{
		// #121: resolve bare actor labels (e.g. TargetActor=BP_Portcullis) to full object paths
		// so ImportText_Direct can resolve TObjectPtr<AActor> fields in struct arrays.
		if (!ValueStr.IsEmpty() && ValueStr.Contains(TEXT("=")))
		{
			FString Result;
			Result.Reserve(ValueStr.Len());
			int32 i = 0;
			while (i < ValueStr.Len())
			{
				TCHAR C = ValueStr[i];
				Result.AppendChar(C);
				if (C == TEXT('='))
				{
					// Gather the following identifier token (letters, digits, underscore) - stop before quotes/parens/paths
					int32 Start = i + 1;
					int32 End = Start;
					while (End < ValueStr.Len())
					{
						TCHAR TC = ValueStr[End];
						if (FChar::IsAlnum(TC) || TC == TEXT('_')) End++;
						else break;
					}
					if (End > Start && (End >= ValueStr.Len() || ValueStr[End] == TEXT(',') || ValueStr[End] == TEXT(')') || ValueStr[End] == TEXT(']') || ValueStr[End] == TEXT('}')))
					{
						FString Token = ValueStr.Mid(Start, End - Start);
						// Skip obvious non-identifiers
						if (Token != TEXT("True") && Token != TEXT("False") && Token != TEXT("None") && !Token.IsNumeric())
						{
							// #983: a duplicated label here would wire the
							// struct's object reference to whichever namesake
							// the iterator reached first, so it is refused.
							// An unmatched token is left alone, as before: it
							// is probably an enum literal, not an actor.
							TArray<AActor*> TokenMatches;
							MCPCollectActorsByToken(World, Token, EMCPActorMatch::Label, TokenMatches);
							if (TokenMatches.Num() > 1)
							{
								return MCPAmbiguousActorError(
									Token, TEXT("value"), TEXT("actorPath"), TEXT("editor label"), TokenMatches);
							}
							if (TokenMatches.Num() == 1)
							{
								Result.Append(TokenMatches[0]->GetPathName());
								i = End;
								goto AppendDone;
							}
						}
					}
				}
			AppendDone:
				i++;
			}
			ValueStr = Result;
		}
		Prop->ImportText_Direct(*ValueStr, ValuePtr, TargetComp, PPF_None);
	}
	else
	{
		double NumValue;
		if ((*ValueField)->TryGetNumber(NumValue))
		{
			ValueStr = FString::SanitizeFloat(NumValue);
			Prop->ImportText_Direct(*ValueStr, ValuePtr, TargetComp, PPF_None);
		}
		else
		{
			bool BoolValue;
			if ((*ValueField)->TryGetBool(BoolValue))
			{
				ValueStr = BoolValue ? TEXT("true") : TEXT("false");
				Prop->ImportText_Direct(*ValueStr, ValuePtr, TargetComp, PPF_None);
			}
			else
			{
				// #216: structured JSON values (objects/arrays). Drives UObject
				// asset paths, FVector {x,y,z}, nested struct fields, etc.
				FString SetErr;
				if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValuePtr, *ValueField, SetErr))
				{
					return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
				}
			}
		}
	}

	// #539: writing RelativeLocation/RelativeRotation/RelativeScale3D on a scene
	// component only moves it once the transform is recomputed. Refresh so the
	// change is visible and persisted, not just stored on the property.
	if (USceneComponent* SceneComp = Cast<USceneComponent>(TargetComp))
	{
		SceneComp->UpdateComponentToWorld();
		SceneComp->MarkRenderStateDirty();
	}
	{
		FPropertyChangedEvent CompChange(Prop);
		TargetComp->PostEditChangeProperty(CompChange);
	}
	TargetComp->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
	Result->SetStringField(TEXT("componentClass"), TargetComp->GetClass()->GetName());
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("previousValue"), PreviousValueStr);

	// Self-inverse: same handler with previous value as string, addressed by
	// path so the undo cannot land on a namesake (#983).
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	if (!ComponentName.IsEmpty()) Payload->SetStringField(TEXT("componentName"), ComponentName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PreviousValueStr);
	MCPSetRollback(Result, TEXT("set_component_property"), Payload);

	return MCPResult(Result);
}

// get_component_details -- read a placed actor's component(s), including
// relative/world transforms. With componentName, returns that component's
// transform + class; without it, lists every component with its transform so
// callers can read a lid's open-pose rotation without execute_python. (#539)
TSharedPtr<FJsonValue> FLevelHandlers::GetComponentDetails(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	// #584: optionally dump arbitrary UPROPERTY values (custom fields,
	// CharacterMovement MaxWalkSpeed, etc.). world lets this read a PIE
	// actor's live component instance too.
	const bool bIncludeValues = OptionalBool(Params, TEXT("includeValues"), false);
	const TArray<TSharedPtr<FJsonValue>>* PropNamesArr = nullptr;
	Params->TryGetArrayField(TEXT("propertyNames"), PropNamesArr);
	TArray<FString> PropFilter = JsonArrayToStringList(PropNamesArr);

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* TargetActor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!TargetActor) return ActorErr;
	ActorLabel = TargetActor->GetActorLabel();

	auto DescribeComponent = [bIncludeValues, &PropFilter](UActorComponent* Comp) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Comp->GetName());
		Obj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		if (USceneComponent* Scene = Cast<USceneComponent>(Comp))
		{
			const FVector RelLoc = Scene->GetRelativeLocation();
			const FRotator RelRot = Scene->GetRelativeRotation();
			const FVector RelScale = Scene->GetRelativeScale3D();
			const FTransform World = Scene->GetComponentTransform();

			auto VecObj = [](const FVector& V)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("x"), V.X); O->SetNumberField(TEXT("y"), V.Y); O->SetNumberField(TEXT("z"), V.Z);
				return O;
			};
			auto RotObj = [](const FRotator& R)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("pitch"), R.Pitch); O->SetNumberField(TEXT("yaw"), R.Yaw); O->SetNumberField(TEXT("roll"), R.Roll);
				return O;
			};
			Obj->SetObjectField(TEXT("relativeLocation"), VecObj(RelLoc));
			Obj->SetObjectField(TEXT("relativeRotation"), RotObj(RelRot));
			Obj->SetObjectField(TEXT("relativeScale3D"), VecObj(RelScale));
			Obj->SetObjectField(TEXT("worldLocation"), VecObj(World.GetLocation()));
			Obj->SetObjectField(TEXT("worldRotation"), RotObj(World.Rotator()));
			USceneComponent* Parent = Scene->GetAttachParent();
			Obj->SetStringField(TEXT("attachParent"), Parent ? Parent->GetName() : TEXT(""));
		}
		// #584: dump arbitrary UPROPERTY values so callers can read custom
		// fields / movement speeds without execute_python.
		if (bIncludeValues)
		{
			TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Comp->GetClass()); It; ++It)
			{
				FProperty* Prop = *It;
				const FString PName = Prop->GetName();
				if (PropFilter.Num() > 0 && !PropFilter.Contains(PName)) continue;
				// #927: fixed arrays come back as a JSON array of elements
				// rather than as element 0 wearing the whole property's name.
				Values->SetField(PName, MCPExportPropertyValue(Prop, Comp));
			}
			Obj->SetObjectField(TEXT("values"), Values);
		}
		return Obj;
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());

	if (!ComponentName.IsEmpty())
	{
		UActorComponent* TargetComp = FindComponentOnActor(TargetActor, ComponentName);
		if (!TargetComp)
		{
			return MCPError(FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *ActorLabel));
		}
		Result->SetObjectField(TEXT("component"), DescribeComponent(TargetComp));
		return MCPResult(Result);
	}

	TArray<UActorComponent*> Components;
	TargetActor->GetComponents(Components);
	TArray<TSharedPtr<FJsonValue>> CompArray;
	for (UActorComponent* Comp : Components)
	{
		CompArray.Add(MakeShared<FJsonValueObject>(DescribeComponent(Comp)));
	}
	Result->SetNumberField(TEXT("componentCount"), CompArray.Num());
	Result->SetArrayField(TEXT("components"), CompArray);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetWorldSettings(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	AWorldSettings* Settings = World->GetWorldSettings();
	if (!Settings)
	{
		return MCPError(TEXT("WorldSettings not available"));
	}

	// DefaultGameMode
	auto Result = MCPSuccess();
	if (Settings->DefaultGameMode)
	{
		Result->SetStringField(TEXT("defaultGameMode"), Settings->DefaultGameMode->GetPathName());
	}
	else
	{
		Result->SetStringField(TEXT("defaultGameMode"), TEXT("None"));
	}

	// KillZ
	Result->SetNumberField(TEXT("killZ"), Settings->KillZ);

	// GlobalGravityZ
	Result->SetNumberField(TEXT("globalGravityZ"), Settings->GlobalGravityZ);

	// bEnableWorldBoundsChecks
	Result->SetBoolField(TEXT("enableWorldBoundsChecks"), Settings->bEnableWorldBoundsChecks);

	// bEnableNavigationSystem
	Result->SetBoolField(TEXT("enableNavigationSystem"), Settings->IsNavigationSystemEnabled());

	// World name
	Result->SetStringField(TEXT("worldName"), World->GetName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetWorldSettings(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	AWorldSettings* Settings = World->GetWorldSettings();
	if (!Settings)
	{
		return MCPError(TEXT("WorldSettings not available"));
	}

	// Capture previous values for rollback before mutating.
	const FString PrevGameMode = Settings->DefaultGameMode ? Settings->DefaultGameMode->GetPathName() : TEXT("None");
	const double PrevKillZ = Settings->KillZ;
	const double PrevGravityZ = Settings->GlobalGravityZ;
	const bool PrevBoundsChecks = Settings->bEnableWorldBoundsChecks;

	TArray<TSharedPtr<FJsonValue>> Changes;
	TSharedPtr<FJsonObject> PrevPayload = MakeShared<FJsonObject>();

	FString GameModeStr;
	if (Params->TryGetStringField(TEXT("defaultGameMode"), GameModeStr))
	{
		if (GameModeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase) || GameModeStr.IsEmpty())
		{
			Settings->DefaultGameMode = nullptr;
			Changes.Add(MakeShared<FJsonValueString>(TEXT("defaultGameMode")));
			PrevPayload->SetStringField(TEXT("defaultGameMode"), PrevGameMode);
		}
		else
		{
			UClass* GMClass = LoadObject<UClass>(nullptr, *GameModeStr);
			if (!GMClass)
			{
				GMClass = FindClassByShortName(GameModeStr);
			}
			if (GMClass && GMClass->IsChildOf(AGameModeBase::StaticClass()))
			{
				Settings->DefaultGameMode = GMClass;
				Changes.Add(MakeShared<FJsonValueString>(TEXT("defaultGameMode")));
				PrevPayload->SetStringField(TEXT("defaultGameMode"), PrevGameMode);
			}
			else
			{
				return MCPError(FString::Printf(TEXT("GameMode class not found or invalid: %s"), *GameModeStr));
			}
		}
	}

	double KillZ;
	if (Params->TryGetNumberField(TEXT("killZ"), KillZ))
	{
		Settings->KillZ = KillZ;
		Changes.Add(MakeShared<FJsonValueString>(TEXT("killZ")));
		PrevPayload->SetNumberField(TEXT("killZ"), PrevKillZ);
	}

	double GravityZ;
	if (Params->TryGetNumberField(TEXT("globalGravityZ"), GravityZ))
	{
		Settings->GlobalGravityZ = GravityZ;
		Changes.Add(MakeShared<FJsonValueString>(TEXT("globalGravityZ")));
		PrevPayload->SetNumberField(TEXT("globalGravityZ"), PrevGravityZ);
	}

	bool bBoundsChecks;
	if (Params->TryGetBoolField(TEXT("enableWorldBoundsChecks"), bBoundsChecks))
	{
		Settings->bEnableWorldBoundsChecks = bBoundsChecks;
		Changes.Add(MakeShared<FJsonValueString>(TEXT("enableWorldBoundsChecks")));
		PrevPayload->SetBoolField(TEXT("enableWorldBoundsChecks"), PrevBoundsChecks);
	}

	Settings->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetArrayField(TEXT("changes"), Changes);
	Result->SetStringField(TEXT("worldName"), World->GetName());

	if (Changes.Num() > 0)
	{
		MCPSetRollback(Result, TEXT("set_world_settings"), PrevPayload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetActorMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	FString MaterialPath;
	if (auto Err = RequireString(Params, TEXT("materialPath"), MaterialPath)) return Err;

	int32 SlotIndex = OptionalInt(Params, TEXT("slotIndex"), 0);

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	UPrimitiveComponent* PrimComp = Actor->FindComponentByClass<UPrimitiveComponent>();
	if (!PrimComp)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' has no primitive component"), *ActorLabel));
	}

	// Capture previous material BEFORE mutating so rollback can restore it.
	FString PreviousMaterialPath;
	if (UMaterialInterface* Prev = PrimComp->GetMaterial(SlotIndex))
	{
		PreviousMaterialPath = Prev->GetPathName();
	}

	PrimComp->SetMaterial(SlotIndex, Material);
	PrimComp->MarkRenderStateDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Result->SetStringField(TEXT("previousMaterialPath"), PreviousMaterialPath);

	// Self-inverse: call set_actor_material again with the previous path.
	// (If previous was unset, passing an empty path would fail material load;
	//  skip the rollback record in that case - best-effort.)
	if (!PreviousMaterialPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("materialPath"), PreviousMaterialPath);
		Payload->SetNumberField(TEXT("slotIndex"), SlotIndex);
		MCPSetRollback(Result, TEXT("set_actor_material"), Payload);
	}

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FLevelHandlers::GetActorsByClass(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	// #675: resolve className to an actual UClass so Blueprint subclasses of a
	// native base match (IsChildOf), not just actors whose class-name string
	// happens to contain the query. Accepts a short native name ("StaticMeshActor"),
	// a /Script path, or a Blueprint class path ("/Game/BP_Foo.BP_Foo_C").
	const bool bMatchSubclasses = OptionalBool(Params, TEXT("matchSubclasses"), true);
	const bool bIncludeTransforms = OptionalBool(Params, TEXT("includeTransforms"), true);
	UClass* TargetClass = nullptr;
	if (bMatchSubclasses)
	{
		TargetClass = FindClassByShortName(ClassName);
		if (!TargetClass) TargetClass = LoadClass<UObject>(nullptr, *ClassName);
		if (!TargetClass) TargetClass = LoadObject<UClass>(nullptr, *ClassName);
		// Blueprint class path given without the _C suffix.
		if (!TargetClass && !ClassName.EndsWith(TEXT("_C")) && ClassName.StartsWith(TEXT("/")))
		{
			TargetClass = LoadObject<UClass>(nullptr, *(ClassName + TEXT("_C")));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		FString CName = A->GetClass()->GetName();
		const bool bMatch = TargetClass
			? A->GetClass()->IsChildOf(TargetClass)
			: (CName == ClassName || (A->GetClass()->IsChildOf(AActor::StaticClass()) && CName.Contains(ClassName)));
		if (bMatch)
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("label"), A->GetActorLabel());
			E->SetStringField(TEXT("class"), CName);
			E->SetStringField(TEXT("path"), A->GetPathName());
			if (bIncludeTransforms)
			{
				const FTransform Xf = A->GetActorTransform();
				E->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Xf.GetLocation()));
				E->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Xf.Rotator()));
				E->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Xf.GetScale3D()));
			}
			Out.Add(MakeShared<FJsonValueObject>(E));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("actors"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	if (bMatchSubclasses && !TargetClass)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("className did not resolve to a loaded UClass; fell back to name-substring matching. Pass a /Script/<Module>.<Class> path or a Blueprint class path for subclass matching."));
	}
	return MCPResult(Result);
}

// #582 find actors that own a component of a given class. Matches by component
// class name (exact or substring), mirroring get_actors_by_class. Reports the
// matched component name(s) so callers can target them directly afterwards.
TSharedPtr<FJsonValue> FLevelHandlers::GetActorsByComponentClass(const TSharedPtr<FJsonObject>& Params)
{
	FString ComponentClass;
	if (auto Err = RequireStringAlt(Params, TEXT("componentClass"), TEXT("className"), ComponentClass)) return Err;

	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	TArray<TSharedPtr<FJsonValue>> Out;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;

		TArray<TSharedPtr<FJsonValue>> Matched;
		for (UActorComponent* Comp : A->GetComponents())
		{
			if (!Comp) continue;
			const FString CompCName = Comp->GetClass()->GetName();
			if (CompCName == ComponentClass || CompCName.Contains(ComponentClass))
			{
				TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
				C->SetStringField(TEXT("name"), Comp->GetName());
				C->SetStringField(TEXT("class"), CompCName);
				Matched.Add(MakeShared<FJsonValueObject>(C));
			}
		}

		if (Matched.Num() > 0)
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("label"), A->GetActorLabel());
			E->SetStringField(TEXT("class"), A->GetClass()->GetName());
			E->SetStringField(TEXT("path"), A->GetPathName());
			E->SetArrayField(TEXT("matchedComponents"), Matched);
			Out.Add(MakeShared<FJsonValueObject>(E));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("actors"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	return MCPResult(Result);
}

// #146: histogram of actors by class name. Cheaper than get_outliner when
// the caller only needs counts (e.g. "how many PCGVolume are loaded?").
TSharedPtr<FJsonValue> FLevelHandlers::CountActorsByClass(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	const int32 TopN = OptionalInt(Params, TEXT("topN"), 0);

	TMap<FString, int32> Counts;
	int32 Total = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		const FString CName = A->GetClass()->GetName();
		int32& Ref = Counts.FindOrAdd(CName);
		Ref++;
		Total++;
	}

	// Sort by count desc
	TArray<TPair<FString, int32>> Sorted;
	Sorted.Reserve(Counts.Num());
	for (const auto& Pair : Counts) { Sorted.Emplace(Pair.Key, Pair.Value); }
	Sorted.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });

	if (TopN > 0 && Sorted.Num() > TopN)
	{
		Sorted.SetNum(TopN);
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	for (const auto& Pair : Sorted)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), Pair.Key);
		Entry->SetNumberField(TEXT("count"), Pair.Value);
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("classes"), Out);
	Result->SetNumberField(TEXT("uniqueClasses"), Counts.Num());
	Result->SetNumberField(TEXT("totalActors"), Total);
	return MCPResult(Result);
}

// #150: compact RVT volume summary. Returns each RuntimeVirtualTextureVolume
// actor with its RVT component's bound VirtualTexture asset path. Avoids the
// Python workaround that ranged across 'virtual_texture' / 'VirtualTexture'
// property-name variants and reflected get_editor_property by class name.
TSharedPtr<FJsonValue> FLevelHandlers::GetRVTSummary(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	TArray<TSharedPtr<FJsonValue>> VolumesArr;
	TSet<FString> UniqueTextures;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		const FString ClassName = A->GetClass()->GetName();
		if (!ClassName.Contains(TEXT("RuntimeVirtualTexture"))) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("label"), A->GetActorLabel());
		Entry->SetStringField(TEXT("class"), ClassName);
		Entry->SetStringField(TEXT("path"), A->GetPathName());

		// Reflectively walk components for a RuntimeVirtualTextureComponent
		TArray<UActorComponent*> Comps;
		A->GetComponents(Comps);
		TArray<TSharedPtr<FJsonValue>> CompArr;
		for (UActorComponent* C : Comps)
		{
			if (!C) continue;
			const FString CName = C->GetClass()->GetName();
			if (!CName.Contains(TEXT("RuntimeVirtualTexture"))) continue;

			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("name"), C->GetName());
			CObj->SetStringField(TEXT("class"), CName);
			// Try both common property names - UE has renamed this across versions.
			if (FObjectProperty* VT = CastField<FObjectProperty>(C->GetClass()->FindPropertyByName(TEXT("VirtualTexture"))))
			{
				if (UObject* Asset = VT->GetObjectPropertyValue_InContainer(C))
				{
					CObj->SetStringField(TEXT("virtualTexture"), Asset->GetPathName());
					UniqueTextures.Add(Asset->GetPathName());
				}
			}
			CompArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("components"), CompArr);

		const FVector Loc = A->GetActorLocation();
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), Loc.X);
		LocObj->SetNumberField(TEXT("y"), Loc.Y);
		LocObj->SetNumberField(TEXT("z"), Loc.Z);
		Entry->SetObjectField(TEXT("location"), LocObj);

		VolumesArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TArray<TSharedPtr<FJsonValue>> UniqueTexArr;
	for (const FString& T : UniqueTextures) UniqueTexArr.Add(MakeShared<FJsonValueString>(T));

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("volumes"), VolumesArr);
	Result->SetNumberField(TEXT("volumeCount"), VolumesArr.Num());
	Result->SetArrayField(TEXT("uniqueVirtualTextures"), UniqueTexArr);
	return MCPResult(Result);
}

// ─── #151 set_water_body_property ───────────────────────────────────
// Set a property on the first UWaterBodyComponent of an actor (ShapeDilation,
// WaterLevel, etc.). Uses runtime class lookup so the Water plugin is not a
// hard build dependency - if the plugin isn't loaded, the handler returns
// a clear error rather than failing to link.
TSharedPtr<FJsonValue> FLevelHandlers::SetWaterBodyProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	FString ValueStr;
	bool bHaveValue = false;
	TSharedPtr<FJsonValue> V = Params->TryGetField(TEXT("value"));
	if (V.IsValid())
	{
		if (V->TryGetString(ValueStr)) bHaveValue = true;
		else if (V->Type == EJson::Number) { ValueStr = FString::SanitizeFloat(V->AsNumber()); bHaveValue = true; }
		else if (V->Type == EJson::Boolean) { ValueStr = V->AsBool() ? TEXT("true") : TEXT("false"); bHaveValue = true; }
	}
	if (!bHaveValue) return MCPError(TEXT("Missing or non-coerceable 'value' parameter"));

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	UClass* WBClass = LoadClass<UActorComponent>(nullptr, TEXT("/Script/Water.WaterBodyComponent"));
	if (!WBClass)
	{
		return MCPError(TEXT("WaterBodyComponent class not available - enable the Water plugin"));
	}

	UActorComponent* WBComp = nullptr;
	TArray<UActorComponent*> Comps;
	Actor->GetComponents(Comps);
	for (UActorComponent* C : Comps)
	{
		if (C && C->GetClass()->IsChildOf(WBClass)) { WBComp = C; break; }
	}
	if (!WBComp) return MCPError(FString::Printf(TEXT("Actor '%s' has no WaterBodyComponent"), *ActorLabel));

	FProperty* Prop = WBComp->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) return MCPError(FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *WBComp->GetClass()->GetName()));

	WBComp->Modify();
	void* Addr = Prop->ContainerPtrToValuePtr<void>(WBComp);
	// Read the value out in the same export-text form this handler imports, so
	// the inverse is literally the call that puts the old value back.
	FString PreviousValue;
	Prop->ExportTextItem_Direct(PreviousValue, Addr, nullptr, WBComp, PPF_None);
	const TCHAR* R = Prop->ImportText_Direct(*ValueStr, Addr, WBComp, PPF_None);
	if (R == nullptr) return MCPError(FString::Printf(TEXT("ImportText failed for '%s'"), *ValueStr));

	// Read back in the same form to say whether the write moved anything. The
	// caller's text and the export text are not comparable ("1" against
	// "1.000000"), so the comparison has to be export against export.
	FString WrittenValue;
	Prop->ExportTextItem_Direct(WrittenValue, Addr, nullptr, WBComp, PPF_None);
	const bool bValueChanged = WrittenValue != PreviousValue;

	// Fire PostEditChangeProperty so the water body rebuilds / re-renders.
	FPropertyChangedEvent Evt(Prop);
	WBComp->PostEditChangeProperty(Evt);
	Actor->MarkPackageDirty();

	auto Result = MCPSuccess();
	if (bValueChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bValueChanged);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), WBComp->GetName());
	Result->SetStringField(TEXT("componentClass"), WBComp->GetClass()->GetName());
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), ValueStr);
	Result->SetStringField(TEXT("previousValue"), PreviousValue);

	// The undo travels by actor path so replaying it cannot land on a namesake.
	// Emitted only when the value actually moved: restoring a value that was
	// already there is a second write, not an undo.
	if (bValueChanged)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("propertyName"), PropertyName);
		Payload->SetStringField(TEXT("value"), PreviousValue);
		MCPSetRollback(Result, TEXT("set_water_body_property"), Payload);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("On a World Partition map the inverse resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded between this call and the replay."));
	}
	return MCPResult(Result);
}

// ─── #188 get_actor_bounds ──────────────────────────────────────────
// Returns the axis-aligned bounding box (origin + extent) for an actor
// named by its editor label or its object path.
TSharedPtr<FJsonValue> FLevelHandlers::GetActorBounds(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	FVector Origin;
	FVector Extent;
	const bool bOnlyColliding = OptionalBool(Params, TEXT("onlyColliding"), false);
	// #677: GetActorBounds(false) returns a degenerate box for skeletal-mesh
	// actors whose component bounds aren't primed. GetComponentsBoundingBox
	// (non-colliding=true) aggregates every primitive component's real bounds,
	// which is robust for skinned meshes.
	FBox Box = Actor->GetComponentsBoundingBox(/*bNonColliding*/ !bOnlyColliding, /*bIncludeFromChildActors*/ true);
	if (Box.IsValid)
	{
		Origin = Box.GetCenter();
		Extent = Box.GetExtent();
	}
	else
	{
		Actor->GetActorBounds(bOnlyColliding, Origin, Extent);
	}

	TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
	OriginObj->SetNumberField(TEXT("x"), Origin.X);
	OriginObj->SetNumberField(TEXT("y"), Origin.Y);
	OriginObj->SetNumberField(TEXT("z"), Origin.Z);

	TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
	ExtentObj->SetNumberField(TEXT("x"), Extent.X);
	ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
	ExtentObj->SetNumberField(TEXT("z"), Extent.Z);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetObjectField(TEXT("origin"), OriginObj);
	Result->SetObjectField(TEXT("extent"), ExtentObj);
	return MCPResult(Result);
}

// ─── #178 resolve_actor ─────────────────────────────────────────────
// Resolves an actor by its internal/runtime UObject name (e.g.
// "StaticMeshActor_141") and returns its label, path, class, and location.
TSharedPtr<FJsonValue> FLevelHandlers::ResolveActor(const TSharedPtr<FJsonObject>& Params)
{
	FString InternalName;
	if (auto Err = RequireString(Params, TEXT("internalName"), InternalName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == InternalName)
		{
			Actor = *It;
			break;
		}
	}

	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found by internal name: %s"), *InternalName));
	}

	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	FVector Location = Actor->GetActorLocation();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("className"), Actor->GetClass()->GetName());
	Result->SetObjectField(TEXT("location"), LocationObj);
	return MCPResult(Result);
}

// #202/#230: generic per-instance UPROPERTY writer for level actors. Resolves
// the actor by label or object path, walks dotted property paths, and routes
// the value through the recursive JSON setter so object refs / vectors /
// nested structs all apply. The optional `force` flag flips off the
// EditDefaultsOnly gate so per-instance overrides on EditDefaultsOnly
// properties go through (the per-instance value always existed - the editor
// UI just hides it).
TSharedPtr<FJsonValue> FLevelHandlers::SetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField || !(*ValueField).IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	const bool bForce = OptionalBool(Params, TEXT("force"), false);
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));

	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	AActor* TargetActor = nullptr;
	const bool bWorldSettings =
		ActorLabel.Equals(TEXT("WorldSettings"), ESearchCase::IgnoreCase)
		&& !Params->HasField(TEXT("actorPath"));
	if (bWorldSettings)
	{
		TargetActor = World->GetWorldSettings();
		if (!TargetActor) return MCPError(TEXT("World settings not available"));
	}
	else
	{
		TSharedPtr<FJsonValue> ActorErr;
		FMCPActorSelector ActorSel;
		ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
		TargetActor = MCPResolveActor(World, Params, ActorErr, ActorSel);
		if (!TargetActor) return ActorErr;
		ActorLabel = TargetActor->GetActorLabel();
	}

	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));
	if (PathParts.Num() == 0) return MCPError(TEXT("Empty propertyName"));

	UStruct* CurrentStruct = TargetActor->GetClass();
	void* CurrentContainer = TargetActor;
	FProperty* Prop = nullptr;
	// #927: the leaf element of a C-style fixed array, `int32 Foo[3]`. That is
	// ONE FProperty with ArrayDim == 3, so without an index the write lands on
	// element 0 and the other elements are unreachable. The read side has the
	// lint-prose-allow: tier  RecastNavMesh's own name for its three generation tiers
	// mirror of this bug; a read that shows three tiers and a write that can
	// only reach the first is not a usable pair.
	int32 LeafArrayIndex = 0;
	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FString Token = PathParts[i];
		int32 SegmentIndex = 0;
		bool bHasSegmentIndex = false;
		{
			int32 OpenBracket = INDEX_NONE;
			int32 CloseBracket = INDEX_NONE;
			if (Token.FindChar(TEXT('['), OpenBracket) &&
				Token.FindChar(TEXT(']'), CloseBracket) &&
				CloseBracket > OpenBracket)
			{
				SegmentIndex = FCString::Atoi(*Token.Mid(OpenBracket + 1, CloseBracket - OpenBracket - 1));
				Token = Token.Left(OpenBracket);
				bHasSegmentIndex = true;
			}
		}

		FProperty* Seg = CurrentStruct->FindPropertyByName(FName(*Token));
		if (!Seg) return MCPError(FString::Printf(TEXT("Property '%s' not found at '%s'"), *Token, *PropertyName));

		if (bHasSegmentIndex)
		{
			if (CastField<FArrayProperty>(Seg))
			{
				// A TArray element needs the shared resolver's array helper,
				// which this walker does not have. Say which action does
				// rather than writing element 0 and calling it a success.
				return MCPError(FString::Printf(
					TEXT("'%s' is a TArray. Indexing a dynamic array is not supported here; use asset(set_property) for dotted TArray paths. An index on this action addresses a C-style fixed array such as `int32 Foo[3]`."),
					*Token));
			}
			if (Seg->ArrayDim <= 1)
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a fixed array, so it cannot be indexed [%d]"), *Token, SegmentIndex));
			}
			if (SegmentIndex < 0 || SegmentIndex >= Seg->ArrayDim)
			{
				return MCPError(FString::Printf(
					TEXT("Index %d is out of range on '%s', which has ArrayDim %d"),
					SegmentIndex, *Token, Seg->ArrayDim));
			}
		}

		if (i < PathParts.Num() - 1)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(Seg))
			{
				CurrentContainer = SP->ContainerPtrToValuePtr<void>(CurrentContainer, SegmentIndex);
				CurrentStruct = SP->Struct;
			}
			// #305: descend through Instanced UObject sub-objects too. The path
			// "APCGWorldActor.LandscapeCacheObject.SerializationMode" hits an
			// FObjectProperty (not a struct) - the previous "is not a struct"
			// rejection forced execute_python on every instanced-subobject write.
			else if (FObjectProperty* OP = CastField<FObjectProperty>(Seg))
			{
				UObject* SubObject = OP->GetObjectPropertyValue(
					OP->ContainerPtrToValuePtr<void>(CurrentContainer, SegmentIndex));
				if (!SubObject)
				{
					return MCPError(FString::Printf(
						TEXT("Cannot descend into '%s' - the sub-object reference is null"),
						*PathParts[i]));
				}
				SubObject->Modify();
				CurrentContainer = SubObject;
				CurrentStruct = SubObject->GetClass();
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a struct or sub-object - cannot descend"), *PathParts[i]));
			}
		}
		else
		{
			Prop = Seg;
			LeafArrayIndex = SegmentIndex;
		}
	}

	// Strip the EditDefaultsOnly gate locally for the duration of the write,
	// then restore. Other UPROPERTY flags stay untouched.
	const EPropertyFlags OriginalFlags = Prop->PropertyFlags;
	if (bForce)
	{
		Prop->PropertyFlags &= ~CPF_DisableEditOnInstance;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CurrentContainer, LeafArrayIndex);

	FString PrevValue;
	Prop->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, TargetActor, PPF_None);

	TargetActor->Modify();

	// If the JSON value is a string and the property is an object reference,
	// try resolving the string as an actor label or object path first, so
	// callers can write {value: "Hopper_01"} for AHopper* references and hand
	// back an actorPath when the label is not unique.
	TSharedPtr<FJsonValue> Value = *ValueField;
	if (Value->Type == EJson::String)
	{
		FString S = Value->AsString();
		if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
		{
			// LabelNameOrPath, where this used to be label alone: the value
			// slot has to take an actorPath back, which is the whole point of
			// returning one. An actor object path contains ":PersistentLevel."
			// and so cannot collide with an asset path, and a value matching
			// nothing still falls through to the generic setter as before.
			TArray<AActor*> RefMatches;
			MCPCollectActorsByToken(World, S, EMCPActorMatch::LabelNameOrPath, RefMatches);
			// #983: wiring a reference to whichever namesake came first is the
			// silent wrong write this issue is about, so it is refused here too.
			if (RefMatches.Num() > 1)
			{
				Prop->PropertyFlags = OriginalFlags;
				return MCPAmbiguousActorError(S, TEXT("value"), TEXT("actorPath"), TEXT("editor label"), RefMatches);
			}
			AActor* RefActor = RefMatches.Num() == 1 ? RefMatches[0] : nullptr;
			if (RefActor && RefActor->IsA(OP->PropertyClass))
			{
				OP->SetObjectPropertyValue(ValuePtr, RefActor);
				goto WriteDone;
			}
		}
	}

	// #538: a TArray of actor references populated from a JSON array of actor
	// labels (e.g. TArray<APointLight*>). The generic setter would treat each
	// string as an asset path; resolve labels against the world instead. Tolerate
	// a stringified JSON array ("[\"A\",\"B\"]") the same way the keystone fix does.
	if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
	{
		if (FObjectProperty* InnerObj = CastField<FObjectProperty>(ArrProp->Inner);
			InnerObj && InnerObj->PropertyClass && InnerObj->PropertyClass->IsChildOf(AActor::StaticClass()))
		{
			TSharedPtr<FJsonValue> ArrValue = Value;
			if (ArrValue->Type == EJson::String)
			{
				const FString Trimmed = ArrValue->AsString().TrimStartAndEnd();
				if (Trimmed.StartsWith(TEXT("[")))
				{
					TSharedPtr<FJsonValue> Reparsed;
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
					if (FJsonSerializer::Deserialize(Reader, Reparsed) && Reparsed.IsValid()) ArrValue = Reparsed;
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (ArrValue->TryGetArray(Items) && Items)
			{
				FScriptArrayHelper H(ArrProp, ValuePtr);
				H.Resize(Items->Num());
				for (int32 i = 0; i < Items->Num(); ++i)
				{
					FString Label;
					(*Items)[i]->TryGetString(Label);
					TArray<AActor*> ElementMatches;
					MCPCollectActorsByToken(World, Label, EMCPActorMatch::LabelNameOrPath, ElementMatches);
					if (ElementMatches.Num() > 1)
					{
						// #983: one ambiguous entry poisons the whole array,
						// so the write is refused before any element lands.
						Prop->PropertyFlags = OriginalFlags;
						return MCPAmbiguousActorError(Label, TEXT("value"), TEXT("actorPath"), TEXT("editor label"), ElementMatches);
					}
					AActor* Ref = ElementMatches.Num() == 1 ? ElementMatches[0] : nullptr;
					if (!Ref)
					{
						Prop->PropertyFlags = OriginalFlags;
						return MCPError(FString::Printf(TEXT("Actor not found for '%s' element [%d]: '%s'"), *PropertyName, i, *Label));
					}
					if (!Ref->IsA(InnerObj->PropertyClass))
					{
						Prop->PropertyFlags = OriginalFlags;
						return MCPError(FString::Printf(TEXT("Actor '%s' is not a %s (element [%d] of '%s')"), *Label, *InnerObj->PropertyClass->GetName(), i, *PropertyName));
					}
					InnerObj->SetObjectPropertyValue(H.GetRawPtr(i), Ref);
				}
				goto WriteDone;
			}
		}
	}

	{
		FString SetErr;
		if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValuePtr, Value, SetErr))
		{
			Prop->PropertyFlags = OriginalFlags;
			return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
		}
	}

WriteDone:
	Prop->PropertyFlags = OriginalFlags;

	FPropertyChangedEvent ChangeEvent(Prop);
	TargetActor->PostEditChangeProperty(ChangeEvent);
	TargetActor->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("previousValue"), PrevValue);

	// The undo travels by path so replaying it cannot land on a namesake (#983).
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	if (!bWorldSettings) Payload->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	if (bForce) Payload->SetBoolField(TEXT("force"), true);
	MCPSetRollback(Result, TEXT("set_actor_property"), Payload);

	return MCPResult(Result);
}

namespace
{
}

// #453: per-actor motion snapshot. Reads location, rotation, velocity,
// angular velocity, scale, and ground state in one call. Works against
// either the editor world or the PIE world (default: PIE when available).
// Callers driving a long telemetry probe loop this at their desired
// sample interval - the bridge stays request/response.
//
// Params:
//   actorLabel? (single) OR actorLabels? (string[])
//   world?: "pie" | "editor" (default: "pie" with editor fallback)
//   pieInstance?: which PIE world when several are running
TSharedPtr<FJsonValue> FLevelHandlers::ReadActorMotion(const TSharedPtr<FJsonObject>& Params)
{
	// Shared resolver so pieInstance selects the client, matching every other
	// PIE-aware read. Bare GetPIEWorld() always returned the first (server)
	// context, which reads as success while sampling the wrong actor.
	// "auto", not "pie": ResolveWorldScope only falls back to the editor world
	// for "auto", and this action has always documented a PIE-preferred read
	// that still answers with the editor world when PIE is not running.
	UWorld* TargetWorld = ResolveWorldFromParams(Params, TEXT("auto"));
	if (!TargetWorld) return MCPError(TEXT("No world available (editor + PIE both null)"));

	TArray<FString> Labels;
	FString Single;
	if (Params->TryGetStringField(TEXT("actorLabel"), Single) && !Single.IsEmpty())
	{
		Labels.Add(Single);
	}
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("actorLabels"), LabelsArr) && LabelsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *LabelsArr)
		{
			FString L; if (V->TryGetString(L) && !L.IsEmpty()) Labels.Add(L);
		}
	}
	// #983: the same list spelled as object paths, which is what a caller
	// reaches for when several actors share a label. These are kept apart from
	// the label list and resolved by MCPFindActorByPath rather than folded into
	// the label token pass, so the export-text form and a case difference both
	// resolve here exactly as they do everywhere else.
	TArray<FString> Paths;
	FString SinglePath;
	if (Params->TryGetStringField(TEXT("actorPath"), SinglePath) && !SinglePath.IsEmpty())
	{
		Paths.Add(SinglePath);
	}
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("actorPaths"), PathsArr) && PathsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *PathsArr)
		{
			FString P; if (V->TryGetString(P) && !P.IsEmpty()) Paths.Add(P);
		}
	}
	if (Labels.Num() == 0 && Paths.Num() == 0)
	{
		return MCPError(TEXT("Pass at least one of 'actorLabel', 'actorLabels', 'actorPath' or 'actorPaths'"));
	}

	auto VecToJson = [](const FVector& V) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X); Obj->SetNumberField(TEXT("y"), V.Y); Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	};
	auto RotToJson = [](const FRotator& R) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("pitch"), R.Pitch); Obj->SetNumberField(TEXT("yaw"), R.Yaw); Obj->SetNumberField(TEXT("roll"), R.Roll);
		return Obj;
	};

	TArray<TSharedPtr<FJsonValue>> Samples;
	TArray<TSharedPtr<FJsonValue>> Missing;
	TArray<AActor*> Targets;
	for (const FString& Label : Labels)
	{
		// #983: this is a read, and a plural one, so a label naming several
		// actors samples all of them rather than one at random. Each row
		// carries actorPath, which is what a follow-up write should target.
		TArray<AActor*> Matches;
		MCPCollectActorsByToken(TargetWorld, Label, EMCPActorMatch::LabelNameOrPath, Matches);
		if (Matches.Num() == 0)
		{
			Missing.Add(MakeShared<FJsonValueString>(Label));
			continue;
		}
		for (AActor* Match : Matches) Targets.AddUnique(Match);
	}
	for (const FString& Path : Paths)
	{
		if (AActor* ByPath = MCPFindActorByPath(TargetWorld, Path))
		{
			Targets.AddUnique(ByPath);
		}
		else
		{
			Missing.Add(MakeShared<FJsonValueString>(Path));
		}
	}
	for (AActor* Actor : Targets)
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		S->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		S->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		S->SetObjectField(TEXT("location"), VecToJson(Actor->GetActorLocation()));
		S->SetObjectField(TEXT("rotation"), RotToJson(Actor->GetActorRotation()));
		S->SetObjectField(TEXT("scale"), VecToJson(Actor->GetActorScale3D()));
		S->SetObjectField(TEXT("velocity"), VecToJson(Actor->GetVelocity()));

		// Physics: drill into the root primitive for angular velocity + grounded.
		if (UPrimitiveComponent* Prim = Actor->FindComponentByClass<UPrimitiveComponent>())
		{
			if (Prim->IsSimulatingPhysics())
			{
				S->SetBoolField(TEXT("simulatingPhysics"), true);
				S->SetObjectField(TEXT("angularVelocity"), VecToJson(Prim->GetPhysicsAngularVelocityInDegrees()));
				S->SetNumberField(TEXT("mass"), Prim->GetMass());
			}
			else
			{
				S->SetBoolField(TEXT("simulatingPhysics"), false);
			}
		}

		// CharacterMovement-style grounded check via downward trace from feet.
		FHitResult Hit;
		const FVector Start = Actor->GetActorLocation();
		const FVector End = Start - FVector(0, 0, 200);
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MCPMotionGround), true, Actor);
		const bool bGrounded = TargetWorld->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Q);
		S->SetBoolField(TEXT("grounded"), bGrounded);
		if (bGrounded) S->SetNumberField(TEXT("distanceToGround"), (Start - Hit.ImpactPoint).Size());

		Samples.Add(MakeShared<FJsonValueObject>(S));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("worldType"), TargetWorld->WorldType == EWorldType::PIE ? TEXT("pie") : TEXT("editor"));
	Result->SetNumberField(TEXT("timeSeconds"), TargetWorld->GetTimeSeconds());
	Result->SetArrayField(TEXT("samples"), Samples);
	if (Missing.Num() > 0) Result->SetArrayField(TEXT("missing"), Missing);
	return MCPResult(Result);
}

// #434: add instance transforms to a HISMC / ISMC component. The reporter
// hit a Python add_instance crash on UE 5.7; the C++ path through
// UInstancedStaticMeshComponent::AddInstance is stable and HISMC inherits
// it (UHierarchicalInstancedStaticMeshComponent extends UInstancedStaticMeshComponent).
//
// Params:
//   actorLabel: actor that owns the HISMC/ISMC
//   componentName?: pick a specific InstancedStaticMeshComponent on the actor;
//                   omitted = first ISMC/HISMC found
//   transforms: array of [{location: {x,y,z}, rotation? : {pitch,yaw,roll},
//                          scale? : {x,y,z}}]
//   worldSpace? (default true)
TSharedPtr<FJsonValue> FLevelHandlers::AddHismcInstances(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UInstancedStaticMeshComponent* ISMC = nullptr;
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		UInstancedStaticMeshComponent* AsISMC = Cast<UInstancedStaticMeshComponent>(Comp);
		if (!AsISMC) continue;
		if (ComponentName.IsEmpty()) { ISMC = AsISMC; break; }
		if (AsISMC->GetName() == ComponentName) { ISMC = AsISMC; break; }
	}
	if (!ISMC)
	{
		return MCPError(FString::Printf(TEXT("No InstancedStaticMeshComponent / HISMC on actor '%s'%s"),
			*ActorLabel, ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName)));
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Params->TryGetArrayField(TEXT("transforms"), Arr) || !Arr)
	{
		return MCPError(TEXT("Missing 'transforms' array ([{location, rotation?, scale?}])"));
	}
	const bool bWorldSpace = OptionalBool(Params, TEXT("worldSpace"), true);

	auto ReadVec = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FVector& Out, double DefaultX = 0, double DefaultY = 0, double DefaultZ = 0) -> bool
	{
		const TSharedPtr<FJsonObject>* VObj = nullptr;
		if (Obj->TryGetObjectField(Key, VObj) && *VObj)
		{
			double X = DefaultX, Y = DefaultY, Z = DefaultZ;
			(*VObj)->TryGetNumberField(TEXT("x"), X);
			(*VObj)->TryGetNumberField(TEXT("y"), Y);
			(*VObj)->TryGetNumberField(TEXT("z"), Z);
			Out = FVector(X, Y, Z);
			return true;
		}
		return false;
	};

	TArray<FTransform> Transforms;
	Transforms.Reserve(Arr->Num());
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject>* TObj = nullptr;
		if (!V->TryGetObject(TObj) || !*TObj) continue;
		FVector Location = FVector::ZeroVector;
		FVector Scale = FVector(1, 1, 1);
		ReadVec(*TObj, TEXT("location"), Location);
		ReadVec(*TObj, TEXT("scale"), Scale, 1, 1, 1);

		FRotator Rotator = FRotator::ZeroRotator;
		const TSharedPtr<FJsonObject>* RObj = nullptr;
		if ((*TObj)->TryGetObjectField(TEXT("rotation"), RObj) && *RObj)
		{
			double P = 0, Y = 0, R = 0;
			(*RObj)->TryGetNumberField(TEXT("pitch"), P);
			(*RObj)->TryGetNumberField(TEXT("yaw"), Y);
			(*RObj)->TryGetNumberField(TEXT("roll"), R);
			Rotator = FRotator(P, Y, R);
		}

		Transforms.Add(FTransform(Rotator, Location, Scale));
	}

	if (Transforms.Num() == 0)
	{
		return MCPError(TEXT("transforms array contained no valid entries"));
	}

	ISMC->Modify();
	const int32 FirstIndex = ISMC->GetInstanceCount();
	const TArray<int32> AddedIndices = ISMC->AddInstances(Transforms, /*bShouldReturnIndices*/ true, bWorldSpace);
	ISMC->MarkRenderStateDirty();

	TArray<TSharedPtr<FJsonValue>> IndicesJson;
	IndicesJson.Reserve(AddedIndices.Num());
	for (int32 Idx : AddedIndices) IndicesJson.Add(MakeShared<FJsonValueNumber>(Idx));

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetStringField(TEXT("componentClass"), ISMC->GetClass()->GetName());
	Result->SetNumberField(TEXT("addedCount"), AddedIndices.Num());
	Result->SetNumberField(TEXT("firstIndex"), FirstIndex);
	Result->SetNumberField(TEXT("totalInstances"), ISMC->GetInstanceCount());
	Result->SetArrayField(TEXT("instanceIndices"), IndicesJson);
	Result->SetBoolField(TEXT("worldSpace"), bWorldSpace);

	// remove_instance takes one index per call, so a single inverse exists only
	// when a single instance was added. Naming it for a multi-instance add
	// would leave every other instance in place while reporting a rollback.
	if (AddedIndices.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("componentName"), ISMC->GetName());
		Payload->SetNumberField(TEXT("index"), AddedIndices[0]);
		MCPSetRollback(Result, TEXT("remove_instance"), Payload);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Instances are addressed by index, and an index is only a stable handle while the component's instance list is unchanged. The inverse removes the instance this call appended, so it has to run before anything else adds or removes one on the same component. The instance actions do NOT compose under a reverse-order unwind either: this one inverts by index while remove_instance inverts by appending, so unwinding a mixed sequence restores the instance COUNT but not the numbering, and this index-keyed inverse would then delete a different instance. On a World Partition map the inverse resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded in between."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("%d instances were added and level(remove_instance) removes one index per call, so no single inverse undoes this. instanceIndices names every index added, and removing them highest-first is what unwinds it."),
			AddedIndices.Num()));
	}
	return MCPResult(Result);
}

namespace
{
	// Resolve an ISMC/HISMC on an actor by optional name (first match if empty).
	UInstancedStaticMeshComponent* ResolveISMC(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor) return nullptr;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			UInstancedStaticMeshComponent* AsISMC = Cast<UInstancedStaticMeshComponent>(Comp);
			if (!AsISMC) continue;
			if (ComponentName.IsEmpty() || AsISMC->GetName() == ComponentName) return AsISMC;
		}
		return nullptr;
	}
}

// #697: read back every instance transform on an actor's ISMC/HISMC.
TSharedPtr<FJsonValue> FLevelHandlers::GetInstanceTransforms(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, ComponentName);
	if (!ISMC) return MCPError(FString::Printf(TEXT("No InstancedStaticMeshComponent on actor '%s'"), *ActorLabel));

	const bool bWorldSpace = OptionalBool(Params, TEXT("worldSpace"), true);
	TArray<TSharedPtr<FJsonValue>> Instances;
	const int32 Count = ISMC->GetInstanceCount();
	for (int32 i = 0; i < Count; ++i)
	{
		FTransform Xf;
		if (!ISMC->GetInstanceTransform(i, Xf, bWorldSpace)) continue;
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetNumberField(TEXT("index"), i);
		E->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Xf.GetLocation()));
		E->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Xf.Rotator()));
		E->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Xf.GetScale3D()));
		Instances.Add(MakeShared<FJsonValueObject>(E));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetBoolField(TEXT("worldSpace"), bWorldSpace);
	Result->SetArrayField(TEXT("instances"), Instances);
	return MCPResult(Result);
}

// #697: update a single instance transform on an ISMC/HISMC by index.
TSharedPtr<FJsonValue> FLevelHandlers::UpdateInstanceTransform(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, ComponentName);
	if (!ISMC) return MCPError(FString::Printf(TEXT("No InstancedStaticMeshComponent on actor '%s'"), *ActorLabel));

	if (!Params->HasField(TEXT("index"))) return MCPError(TEXT("Missing 'index'"));
	const int32 Index = OptionalInt(Params, TEXT("index"), -1);
	if (Index < 0 || Index >= ISMC->GetInstanceCount())
	{
		return MCPError(FString::Printf(TEXT("index %d out of range (0..%d)"), Index, ISMC->GetInstanceCount() - 1));
	}
	const bool bWorldSpace = OptionalBool(Params, TEXT("worldSpace"), true);

	// Start from the current transform so partially-specified updates preserve
	// unspecified components.
	FTransform Xf;
	ISMC->GetInstanceTransform(Index, Xf, bWorldSpace);
	FVector Loc = Xf.GetLocation();
	FRotator Rot = Xf.Rotator();
	FVector Scale = Xf.GetScale3D();
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), Sub) && Sub) ReadVec3Fields(*Sub, Loc);
	if (Params->TryGetObjectField(TEXT("rotation"), Sub) && Sub) ReadRotatorFields(*Sub, Rot);
	if (Params->TryGetObjectField(TEXT("scale"), Sub) && Sub) ReadVec3Fields(*Sub, Scale);

	// The transform this call is about to overwrite, in the same space the
	// inverse will be replayed in.
	const FVector PreviousLoc = Xf.GetLocation();
	const FRotator PreviousRot = Xf.Rotator();
	const FVector PreviousScale = Xf.GetScale3D();

	// Both transforms are in the same space, so this says whether the write
	// moved the instance meaningfully. FTransform::Equals carries a 1e-4
	// tolerance, so it answers "worth reporting as an update", NOT "no write
	// happened": the write below runs either way and a sub-tolerance delta is
	// still a delta on disk. Only the marker keys off this. The rollback does
	// not, or a real edit would be left unrecoverable.
	const FTransform NewTransform(Rot, Loc, Scale);
	const bool bTransformChanged = !Xf.Equals(NewTransform);

	ISMC->Modify();
	const bool bOk = ISMC->UpdateInstanceTransform(Index, NewTransform, bWorldSpace, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
	if (!bOk) return MCPError(FString::Printf(TEXT("UpdateInstanceTransform failed for index %d"), Index));

	auto Result = MCPSuccess();
	if (bTransformChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bTransformChanged);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetNumberField(TEXT("index"), Index);

	// Emitted unconditionally: UpdateInstanceTransform above already ran.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("componentName"), ISMC->GetName());
	Payload->SetNumberField(TEXT("index"), Index);
	Payload->SetBoolField(TEXT("worldSpace"), bWorldSpace);
	Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(PreviousLoc));
	Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(PreviousRot));
	Payload->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(PreviousScale));
	MCPSetRollback(Result, TEXT("update_instance_transform"), Payload);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Instances are addressed by index, so the inverse has to run before anything else adds or removes an instance on the same component. Note that the instance actions do NOT compose under a reverse-order unwind: add_instances inverts by index while remove_instance inverts by appending, so a sequence mixing them restores the instance COUNT but not the numbering, and an index-keyed inverse replayed after that reaches a different instance. On a World Partition map the inverse also resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded in between."));
	return MCPResult(Result);
}

// #697: remove a single instance on an ISMC/HISMC by index.
TSharedPtr<FJsonValue> FLevelHandlers::RemoveInstance(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, ComponentName);
	if (!ISMC) return MCPError(FString::Printf(TEXT("No InstancedStaticMeshComponent on actor '%s'"), *ActorLabel));

	if (!Params->HasField(TEXT("index"))) return MCPError(TEXT("Missing 'index'"));
	const int32 Index = OptionalInt(Params, TEXT("index"), -1);
	if (Index < 0 || Index >= ISMC->GetInstanceCount())
	{
		return MCPError(FString::Printf(TEXT("index %d out of range (0..%d)"), Index, ISMC->GetInstanceCount() - 1));
	}

	// Capture the transform before it is dropped. World space, because that is
	// what add_instances defaults to and what makes the restore independent of
	// the component's own transform.
	FTransform RemovedTransform;
	const bool bHaveTransform = ISMC->GetInstanceTransform(Index, RemovedTransform, /*bWorldSpace*/ true);

	ISMC->Modify();
	const bool bOk = ISMC->RemoveInstance(Index);
	ISMC->MarkRenderStateDirty();

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), bOk);
	Result->SetBoolField(TEXT("unchanged"), !bOk);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetNumberField(TEXT("remainingInstances"), ISMC->GetInstanceCount());

	if (bOk && bHaveTransform)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(RemovedTransform.GetLocation()));
		Entry->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(RemovedTransform.Rotator()));
		Entry->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(RemovedTransform.GetScale3D()));
		TArray<TSharedPtr<FJsonValue>> Transforms;
		Transforms.Add(MakeShared<FJsonValueObject>(Entry));

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("componentName"), ISMC->GetName());
		Payload->SetBoolField(TEXT("worldSpace"), true);
		Payload->SetArrayField(TEXT("transforms"), Transforms);
		MCPSetRollback(Result, TEXT("add_instances"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The instance comes back at the same world transform but is appended, so it does not return to index %d. Removing an instance also renumbers the ones after it, and those indices are not restored either. Per-instance custom float data is not captured and does not come back. Because this inverse appends while add_instances and update_instance_transform invert BY INDEX, the instance actions do not compose under a reverse-order unwind: the count comes back but the numbering does not, so an index-keyed inverse replayed afterwards reaches a different instance. On a World Partition map the inverse also resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded in between."),
			Index));
	}
	return MCPResult(Result);
}

// #696: enable + force-build Nanite on a UStaticMesh asset.
TSharedPtr<FJsonValue> FLevelHandlers::SetNaniteSettings(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("meshPath"), AssetPath)) return Err;
	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	const bool bEnabled = OptionalBool(Params, TEXT("enabled"), true);
	// Use the accessor pair (GetNaniteSettings/SetNaniteSettings) - direct
	// member access to NaniteSettings is deprecated in 5.7+.
	FMeshNaniteSettings Settings = MCPGetNaniteSettings(Mesh);
	const bool bPreviousEnabled = Settings.bEnabled != 0;
	const int32 PreviousPositionPrecision = Settings.PositionPrecision;
	Settings.bEnabled = bEnabled;
	if (Params->HasField(TEXT("positionPrecision")))
	{
		Settings.PositionPrecision = OptionalInt(Params, TEXT("positionPrecision"), Settings.PositionPrecision);
	}

	// Decided BEFORE anything is written. Build() regenerates the Nanite data
	// and SaveAssetPackage rewrites the uasset, so running them and then
	// reporting unchanged:true would mean a no-op call still rebuilt a mesh
	// and produced a new file on disk. An idempotency marker that costs a
	// rebuild is not idempotency.
	const bool bSettingsChanged =
		(Settings.bEnabled != 0) != bPreviousEnabled ||
		Settings.PositionPrecision != PreviousPositionPrecision;

	bool bSaved = false;
	if (bSettingsChanged)
	{
		// Modify() lives INSIDE the branch. It defaults to bAlwaysMarkDirty, so
		// running it on the no-op path dirtied the package while the response
		// said unchanged:true and saved:false, leaving the asset dirty with
		// nothing written and nothing to save.
		Mesh->Modify();
		MCPSetNaniteSettings(Mesh, Settings);

		// Force a rebuild so the Nanite data is generated immediately rather
		// than on next cook. Build() is the editor's explicit rebuild entry point.
		Mesh->Build(/*bSilent*/ true);
		Mesh->PostEditChange();
		bSaved = SaveAssetPackage(Mesh);
	}

	const bool bNowEnabled = MCPGetNaniteSettings(Mesh).bEnabled != 0;
	const int32 NowPositionPrecision = MCPGetNaniteSettings(Mesh).PositionPrecision;

	auto Result = MCPSuccess();
	if (bSettingsChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bSettingsChanged);
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Result->SetBoolField(TEXT("naniteEnabled"), bNowEnabled);
	Result->SetNumberField(TEXT("positionPrecision"), NowPositionPrecision);
	Result->SetBoolField(TEXT("saved"), bSaved);
	Result->SetBoolField(TEXT("previousNaniteEnabled"), bPreviousEnabled);
	Result->SetNumberField(TEXT("previousPositionPrecision"), PreviousPositionPrecision);
	Result->SetBoolField(TEXT("rebuilt"), bSettingsChanged);
	if (!bSettingsChanged)
	{
		// The skip is a real capability change and has to be said out loud.
		// This action used to call Build() on every invocation, which is the
		// only way anything in this category forces a static-mesh rebuild:
		// get_nanite_info only reads. A caller who was relying on that to
		// regenerate stale data now gets nothing, and would otherwise see only
		// unchanged:true with no reason.
		Result->SetStringField(TEXT("note"),
			TEXT("The Nanite settings already held these values, so nothing was written: no rebuild ran, PostEditChange was not called and the package was neither dirtied nor saved. ")
			TEXT("That also means stale or missing Nanite data was NOT regenerated. If the mesh was imported or duplicated without a build, or the derived data cache missed, this call did not fix it: ")
			TEXT("flip enabled and set it back to force the rebuild, or rebuild the mesh from the editor."));
	}

	// This writes exactly the two fields it names, so replaying it with the
	// values that were there restores the settings struct. Emitted only when
	// the settings moved, which here is safe rather than approximate: the
	// comparison is over an exact bool and an exact int decided before the
	// write, and when it is false NOTHING was written, so there is genuinely
	// nothing to undo.
	if (bSettingsChanged)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
		Payload->SetBoolField(TEXT("enabled"), bPreviousEnabled);
		Payload->SetNumberField(TEXT("positionPrecision"), PreviousPositionPrecision);
		MCPSetRollback(Result, TEXT("set_nanite_settings"), Payload);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The inverse rebuilds the mesh with the previous settings and saves the package again, so the asset returns to its previous Nanite state rather than to its previous file bytes."));
	}
	return MCPResult(Result);
#else
	return MCPError(TEXT("SetNaniteSettings requires the editor"));
#endif
}

// #696: read a UStaticMesh's Nanite state.
TSharedPtr<FJsonValue> FLevelHandlers::GetNaniteInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("meshPath"), AssetPath)) return Err;
	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	const FMeshNaniteSettings Settings = MCPGetNaniteSettings(Mesh);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Result->SetBoolField(TEXT("naniteEnabled"), Settings.bEnabled != 0);
	Result->SetNumberField(TEXT("positionPrecision"), Settings.PositionPrecision);
	Result->SetNumberField(TEXT("numLODs"), Mesh->GetNumLODs());
	return MCPResult(Result);
}

// #637: export a selected actor's skeletal/static mesh to FBX and write a
// metadata sidecar JSON (actor transform, mesh, materials, skeleton) that a
// downstream bridge (e.g. MetaTailor) can consume alongside the FBX.
TSharedPtr<FJsonValue> FLevelHandlers::ExportActorFbx(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	FString OutputPath;
	if (auto Err = RequireStringAlt(Params, TEXT("outputPath"), TEXT("filePath"), OutputPath)) return Err;

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// Resolve the mesh asset from the actor (skeletal first, then static).
	UObject* MeshAsset = nullptr;
	FString MeshKind;
	TArray<FString> MaterialPaths;
	FString SkeletonPath;
	if (USkeletalMeshComponent* SKC = Actor->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (USkeletalMesh* SM = Cast<USkeletalMesh>(SKC->GetSkinnedAsset()))
		{
			MeshAsset = SM; MeshKind = TEXT("SkeletalMesh");
			if (USkeleton* Sk = SM->GetSkeleton()) SkeletonPath = Sk->GetPathName();
			for (int32 i = 0; i < SKC->GetNumMaterials(); ++i)
			{
				if (UMaterialInterface* M = SKC->GetMaterial(i)) MaterialPaths.Add(M->GetPathName());
			}
		}
	}
	if (!MeshAsset)
	{
		if (UStaticMeshComponent* SMC = Actor->FindComponentByClass<UStaticMeshComponent>())
		{
			if (UStaticMesh* SM = SMC->GetStaticMesh())
			{
				MeshAsset = SM; MeshKind = TEXT("StaticMesh");
				for (int32 i = 0; i < SMC->GetNumMaterials(); ++i)
				{
					if (UMaterialInterface* M = SMC->GetMaterial(i)) MaterialPaths.Add(M->GetPathName());
				}
			}
		}
	}
	if (!MeshAsset) return MCPError(FString::Printf(TEXT("Actor '%s' has no skeletal/static mesh to export"), *ActorLabel));

	FString AbsPath = OutputPath;
	if (FPaths::IsRelative(AbsPath)) AbsPath = FPaths::Combine(FPaths::ProjectDir(), AbsPath);
	if (!AbsPath.EndsWith(TEXT(".fbx"))) AbsPath += TEXT(".fbx");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsPath), /*Tree*/ true);

	UAssetExportTask* Task = NewObject<UAssetExportTask>();
	FGCRootScope TaskRoot(Task);
	Task->Object = MeshAsset;
	Task->Filename = AbsPath;
	Task->bAutomated = true;
	Task->bPrompt = false;
	Task->bReplaceIdentical = true;
	UFbxExportOption* Options = NewObject<UFbxExportOption>();
	Task->Options = Options;
	const bool bExported = UExporter::RunAssetExportTask(Task);
	if (!bExported) return MCPError(FString::Printf(TEXT("FBX export failed for %s"), *MeshAsset->GetPathName()));

	// Metadata sidecar next to the FBX.
	const FTransform Xf = Actor->GetActorTransform();
	TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Meta->SetStringField(TEXT("meshKind"), MeshKind);
	Meta->SetStringField(TEXT("mesh"), MeshAsset->GetPathName());
	if (!SkeletonPath.IsEmpty()) Meta->SetStringField(TEXT("skeleton"), SkeletonPath);
	Meta->SetStringField(TEXT("fbx"), AbsPath);
	Meta->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Xf.GetLocation()));
	Meta->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Xf.Rotator()));
	Meta->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Xf.GetScale3D()));
	TArray<TSharedPtr<FJsonValue>> MatArr;
	for (const FString& MP : MaterialPaths) MatArr.Add(MakeShared<FJsonValueString>(MP));
	Meta->SetArrayField(TEXT("materials"), MatArr);

	FString MetaStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&MetaStr);
	FJsonSerializer::Serialize(Meta.ToSharedRef(), Writer);
	const FString MetaPath = FPaths::ChangeExtension(AbsPath, TEXT("json"));
	FFileHelper::SaveStringToFile(MetaStr, *MetaPath);

	const int64 FbxSize = IFileManager::Get().FileSize(*AbsPath);
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	// Nothing in the project or the level changed, but two files did, and a
	// caller that pointed outputPath at an existing FBX has lost it.
	MCPSetNoRollback(Result,
		TEXT("The FBX and its .json sidecar were written to the caller's outputPath, overwriting whatever was already there, and any missing directories were created. ")
		TEXT("Undoing that would need a call that deletes or restores a file on disk outside the content browser, and the bridge has none."));
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("fbx"), AbsPath);
	Result->SetStringField(TEXT("metadata"), MetaPath);
	Result->SetStringField(TEXT("meshKind"), MeshKind);
	Result->SetNumberField(TEXT("fbxSizeBytes"), (double)FbxSize);
	return MCPResult(Result);
}

// #679/#677: spawn a SkeletalMeshActor with a mesh (+ optional materials and
// single-node animation) for visual and deform verification.
TSharedPtr<FJsonValue> FLevelHandlers::SpawnSkeletalMeshActor(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString MeshPath;
	if (auto Err = RequireStringAlt(Params, TEXT("skeletalMesh"), TEXT("meshPath"), MeshPath)) return Err;
	REQUIRE_ASSET(USkeletalMesh, Mesh, MeshPath);

	const FString Label = OptionalString(Params, TEXT("label"));
	if (TSharedPtr<FJsonValue> Existing = MCPCheckActorLabelExists(World, Label, OptionalString(Params, TEXT("onConflict")), TEXT("SkeletalMeshActor")))
	{
		return Existing;
	}

	const FTransform SpawnXf = OptionalTransform(Params, TEXT("transform"));
	FVector Loc = OptionalVec3(Params, TEXT("location"), SpawnXf.GetLocation());
	FRotator Rot = OptionalRotator(Params, TEXT("rotation"), SpawnXf.Rotator());
	FVector Scale = OptionalVec3(Params, TEXT("scale"), SpawnXf.GetScale3D());

	FActorSpawnParameters SpawnParams;
	ASkeletalMeshActor* Actor = World->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), FTransform(Rot, Loc, Scale), SpawnParams);
	if (!Actor) return MCPError(TEXT("Failed to spawn SkeletalMeshActor"));
	if (!Label.IsEmpty()) Actor->SetActorLabel(Label);

	// #946: per-slot COMPONENT material overrides, reported rather than
	// applied silently. These write the component's OverrideMaterials, which
	// is a different thing from the mesh ASSET's own slots: the asset is
	// untouched here. For an actor that is already placed, or to apply one
	// material to every slot, use level(set_component_materials).
	TArray<TSharedPtr<FJsonValue>> MaterialResults;
	int32 FailedMaterials = 0;
	USkeletalMeshComponent* Comp = Actor->GetSkeletalMeshComponent();
	if (Comp)
	{
		Comp->SetSkeletalMeshAsset(Mesh);

		const TArray<TSharedPtr<FJsonValue>>* Mats = nullptr;
		if (Params->TryGetArrayField(TEXT("materials"), Mats) && Mats)
		{
			const int32 SlotCount = Comp->GetNumMaterials();
			for (int32 i = 0; i < Mats->Num(); ++i)
			{
				FString MatPath;
				const bool bHasPath =
					(*Mats)[i].IsValid() && (*Mats)[i]->TryGetString(MatPath) && !MatPath.IsEmpty();
				if (!bHasPath) continue;

				TSharedPtr<FJsonObject> MatRow = MakeShared<FJsonObject>();
				MatRow->SetNumberField(TEXT("slotIndex"), i);
				MatRow->SetStringField(TEXT("materialPath"), MatPath);

				// A slot index past the end, or a path that does not load,
				// used to be dropped on the floor. That reads as a successful
				// assignment that never happened.
				if (i >= SlotCount)
				{
					MatRow->SetBoolField(TEXT("ok"), false);
					MatRow->SetStringField(TEXT("error"), FString::Printf(
						TEXT("slot %d is past the mesh's %d material slots"), i, SlotCount));
					++FailedMaterials;
				}
				else if (UMaterialInterface* Mat = LoadAssetByPath<UMaterialInterface>(MatPath))
				{
					Comp->SetMaterial(i, Mat);
					MatRow->SetBoolField(TEXT("ok"), true);
					MatRow->SetStringField(TEXT("source"), TEXT("componentOverride"));
				}
				else
				{
					MatRow->SetBoolField(TEXT("ok"), false);
					MatRow->SetStringField(TEXT("error"), TEXT("material not found"));
					++FailedMaterials;
				}
				MaterialResults.Add(MakeShared<FJsonValueObject>(MatRow));
			}
		}

		// Optional single-node animation preview (visual deform check).
		FString AnimPath = OptionalString(Params, TEXT("animSequence"));
		if (!AnimPath.IsEmpty())
		{
			if (UAnimSequence* Anim = LoadAssetByPath<UAnimSequence>(AnimPath))
			{
				const bool bLoop = OptionalBool(Params, TEXT("loop"), true);
				Comp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				Comp->SetAnimation(Anim);
				Comp->Play(bLoop);

				// #766/#790: SetAnimation() only drives the RUNTIME single-node
				// player. The editable AnimationData struct is what gets
				// serialised with the level, so without also writing it the
				// saved map stored AnimToPlay=None and every actor came back in
				// A-pose after a reload - with no error to explain why.
				Comp->AnimationData.AnimToPlay = Anim;
				Comp->AnimationData.bSavedLooping = bLoop;
				Comp->AnimationData.bSavedPlaying = true;
				Comp->AnimationData.SavedPosition = 0.0f;
				Comp->AnimationData.SavedPlayRate = 1.0f;
				Comp->Modify();
			}
		}
	}

	const FVector BoxExtent = Actor->GetComponentsBoundingBox(true).GetExtent();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetBoolField(TEXT("success"), FailedMaterials == 0);
	if (FailedMaterials > 0)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The actor was spawned but %d material override(s) did not apply; see materials[]."),
			FailedMaterials));
	}
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
	Result->SetNumberField(TEXT("materialSlotCount"), Comp ? Comp->GetNumMaterials() : 0);
	Result->SetArrayField(TEXT("materials"), MaterialResults);
	Result->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Loc));
	Result->SetObjectField(TEXT("boxExtent"), MCPVec3ToJsonObject(BoxExtent));
	TSharedPtr<FJsonObject> Rb = MakeShared<FJsonObject>();
	Rb->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	MCPSetRollback(Result, TEXT("delete_actor"), Rb);
	return MCPResult(Result);
}

// #220: bulk delete actors matching label prefix / class / tag.
// #767: assign World Outliner folder paths in bulk. Editor-only organisation,
// so it deliberately does not save the level - the caller decides when to
// persist. Everything runs inside one transaction so a bulk move is a single
// undo, and the write is read back per actor instead of being assumed.
TSharedPtr<FJsonValue> FLevelHandlers::SetActorFolderPath(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// An empty folder path is legitimate - it moves actors back to the root -
	// so the parameter must be PRESENT but may be empty. RequireString rejects
	// empty strings, which made the documented root-move impossible.
	if (!Params->HasField(TEXT("folderPath")))
	{
		return MCPError(TEXT("Missing required parameter 'folderPath' (pass \"\" to move actors to the root)"));
	}
	FString FolderPath = OptionalString(Params, TEXT("folderPath"));
	FolderPath = FolderPath.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));

	const FString LabelPrefix = OptionalString(Params, TEXT("labelPrefix"));
	const FString ClassName = OptionalString(Params, TEXT("className"));
	const FString Tag = OptionalString(Params, TEXT("tag"));
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	TSet<FString> ExactLabels;
	const TArray<TSharedPtr<FJsonValue>>* LabelValues = nullptr;
	if (Params->TryGetArrayField(TEXT("actorLabels"), LabelValues) && LabelValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *LabelValues)
		{
			FString Label;
			if (Value.IsValid() && Value->TryGetString(Label) && !Label.IsEmpty())
			{
				ExactLabels.Add(Label);
			}
		}
	}

	if (ExactLabels.Num() == 0 && LabelPrefix.IsEmpty() && ClassName.IsEmpty() && Tag.IsEmpty())
	{
		return MCPError(TEXT("Provide at least one filter: actorLabels, labelPrefix, className, or tag"));
	}

	TArray<AActor*> Matches;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		const FString Label = A->GetActorLabel();
		if (ExactLabels.Num() > 0 && !ExactLabels.Contains(Label)) continue;
		if (!LabelPrefix.IsEmpty() && !Label.StartsWith(LabelPrefix)) continue;
		if (!ClassName.IsEmpty() && !A->GetClass()->GetName().Contains(ClassName)) continue;
		if (!Tag.IsEmpty() && !A->ActorHasTag(FName(*Tag))) continue;
		Matches.Add(A);
	}

	// Report labels the caller asked for by name that no actor answers to, so
	// a typo does not read as "nothing needed moving".
	TArray<TSharedPtr<FJsonValue>> MissingLabels;
	if (ExactLabels.Num() > 0)
	{
		TSet<FString> Found;
		for (AActor* A : Matches) Found.Add(A->GetActorLabel());
		for (const FString& Label : ExactLabels)
		{
			if (!Found.Contains(Label)) MissingLabels.Add(MakeShared<FJsonValueString>(Label));
		}
	}

	const FName NewFolder(*FolderPath);
	TArray<TSharedPtr<FJsonValue>> Entries;
	int32 Changed = 0;
	int32 Verified = 0;

	auto Apply = [&]()
	{
		for (AActor* A : Matches)
		{
			const FString Previous = A->GetFolderPath().ToString();
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("label"), A->GetActorLabel());
			Entry->SetStringField(TEXT("previousFolderPath"), Previous);
			Entry->SetStringField(TEXT("folderPath"), FolderPath);

			const bool bNeedsChange = Previous != FolderPath;
			Entry->SetBoolField(TEXT("changed"), bNeedsChange && !bDryRun);

			if (bNeedsChange && !bDryRun)
			{
				A->Modify();
				A->SetFolderPath(NewFolder);
				++Changed;
				// Read the value back rather than trusting the setter.
				const bool bOk = A->GetFolderPath().ToString() == FolderPath;
				Entry->SetBoolField(TEXT("verified"), bOk);
				if (bOk) ++Verified;
			}
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	};

	if (bDryRun)
	{
		Apply();
	}
	else
	{
		const FScopedTransaction Transaction(
			FText::FromString(OptionalString(Params, TEXT("transactionLabel"), TEXT("Set actor folder paths"))));
		Apply();
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetStringField(TEXT("folderPath"), FolderPath);
	Result->SetNumberField(TEXT("matched"), Matches.Num());
	Result->SetNumberField(TEXT("changed"), Changed);
	Result->SetNumberField(TEXT("verified"), Verified);
	Result->SetArrayField(TEXT("actors"), Entries);
	Result->SetArrayField(TEXT("missingLabels"), MissingLabels);
	Result->SetStringField(TEXT("note"),
		TEXT("Folder paths are editor-only organisation. The level is left dirty and unsaved; save it yourself when ready."));
	Result->SetBoolField(TEXT("unchanged"), Changed == 0);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Undoing this is one move per actor back to that actor's own previous folder, and no action here takes a per-actor folder list. Its only selectors address actors by editor label, which is not unique, so a single restore call could also move a namesake this call never touched. actors[].previousFolderPath carries every value a caller needs to replay it one actor at a time."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DeleteActors(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// #963: the filter names say what they match, and the two that were
	// previously reachable only through get_outliner's looser nameFilter are
	// now first class here. labelPrefix stays a CASE-SENSITIVE PREFIX over the
	// EDITOR LABEL, which is what it always was; labelContains and nameContains
	// are the substring forms, over the label and the internal name
	// respectively. Overloading one parameter to mean both is how a filter that
	// selects fifteen actors in one action selects none in another.
	const FString LabelPrefix = OptionalString(Params, TEXT("labelPrefix"));
	const FString LabelContains = OptionalString(Params, TEXT("labelContains"));
	const FString NameContains = OptionalString(Params, TEXT("nameContains"));
	const FString ClassName = OptionalString(Params, TEXT("className"));
	const FString Tag = OptionalString(Params, TEXT("tag"));
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	TArray<FString> ClassPathNeedles;
	const FString ClassPathContains = OptionalString(Params, TEXT("classPathContains"));
	if (!ClassPathContains.IsEmpty())
	{
		ClassPathNeedles.Add(ClassPathContains);
	}
	const TArray<TSharedPtr<FJsonValue>>* ClassPathAny = nullptr;
	if (Params.IsValid() && Params->TryGetArrayField(TEXT("classPathContainsAny"), ClassPathAny) && ClassPathAny)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ClassPathAny)
		{
			FString Needle;
			if (Value.IsValid() && Value->TryGetString(Needle) && !Needle.IsEmpty())
			{
				ClassPathNeedles.Add(Needle);
			}
		}
	}

	// #924 added the class-path filters and #963 added the label/name ones. Both
	// are live, so the guard has to accept either family; requiring only one
	// family's filters would make the other silently unusable.
	if (LabelPrefix.IsEmpty() && LabelContains.IsEmpty() && NameContains.IsEmpty() &&
		ClassName.IsEmpty() && Tag.IsEmpty() && ClassPathNeedles.Num() == 0)
	{
		return MCPError(TEXT("Provide at least one filter: labelPrefix (case-sensitive prefix over the editor label), labelContains (case-insensitive substring over the label), nameContains (case-insensitive substring over the internal name), className, tag, classPathContains, or classPathContainsAny"));
	}

	TArray<AActor*> Matches;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (!LabelPrefix.IsEmpty() && !A->GetActorLabel().StartsWith(LabelPrefix)) continue;
		if (!LabelContains.IsEmpty() && !A->GetActorLabel().Contains(LabelContains, ESearchCase::IgnoreCase)) continue;
		if (!NameContains.IsEmpty() && !A->GetName().Contains(NameContains, ESearchCase::IgnoreCase)) continue;
		if (!ClassName.IsEmpty())
		{
			const FString CName = A->GetClass()->GetName();
			if (!CName.Contains(ClassName)) continue;
		}
		if (!Tag.IsEmpty() && !A->ActorHasTag(FName(*Tag))) continue;
		if (ClassPathNeedles.Num() > 0)
		{
			const FString ClassPath = A->GetClass()->GetPathName();
			bool bPathMatch = false;
			for (const FString& Needle : ClassPathNeedles)
			{
				if (ClassPath.Contains(Needle, ESearchCase::IgnoreCase))
				{
					bPathMatch = true;
					break;
				}
			}
			if (!bPathMatch) continue;
		}
		Matches.Add(A);
	}

	TArray<TSharedPtr<FJsonValue>> Labels;
	TArray<TSharedPtr<FJsonValue>> ClassPaths;
	for (AActor* A : Matches)
	{
		Labels.Add(MakeShared<FJsonValueString>(A->GetActorLabel()));
		ClassPaths.Add(MakeShared<FJsonValueString>(A->GetClass()->GetPathName()));
	}

	int32 Deleted = 0;
	if (!bDryRun)
	{
		UEditorActorSubsystem* EAS = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
		for (AActor* A : Matches)
		{
			bool bOk = false;
			if (EAS)
			{
				bOk = EAS->DestroyActor(A);
			}
			if (!bOk && A)
			{
				bOk = World->DestroyActor(A);
			}
			if (bOk) Deleted++;
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("matched"), Matches.Num());
	Result->SetNumberField(TEXT("deleted"), Deleted);
	Result->SetBoolField(TEXT("unchanged"), Deleted == 0);
	Result->SetArrayField(TEXT("labels"), Labels);
	Result->SetArrayField(TEXT("classPaths"), ClassPaths);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("This deletes whatever the filters matched, across any number of classes, and nothing here spawns a list of actors of differing classes at captured transforms. The actors' transforms, properties, components and attachments are not captured before the destroy, so no call can put them back. dryRun=true is the preflight; the editor's undo stack is the recovery path."));

	// #963: a destructive action that matched nothing must not answer with a
	// bare success and a zero. A caller who trusts that concludes there is
	// nothing to delete and moves on, which is exactly what happened. Two
	// things can produce a wrong zero here, and the response now names both.
	if (Matches.IsEmpty())
	{
		Result->SetStringField(TEXT("zeroMatchNote"),
			TEXT("No actor matched. This is a filter result, not a statement that the actors do not exist."));
		const FString Needle = !LabelPrefix.IsEmpty()
			? LabelPrefix
			: (!LabelContains.IsEmpty() ? LabelContains : NameContains);
		if (const TSharedPtr<FJsonObject> Hint = MCPDescribeZeroActorMatch(World, Needle))
		{
			Result->SetObjectField(TEXT("zeroMatchHint"), Hint);
		}
		MCPNoteLoadedOnlyEnumeration(World, Result);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::AddActorTag(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return E;
	FString Tag; if (auto E = RequireString(Params, TEXT("tag"), Tag)) return E;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();

	const FName TagName(*Tag);
	const bool bAlreadyHad = A->Tags.Contains(TagName);
	if (!bAlreadyHad)
	{
		A->Modify();
		A->Tags.Add(TagName);
		A->MarkPackageDirty();
	}
	auto Result = MCPSuccess();
	if (bAlreadyHad) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	Result->SetStringField(TEXT("tag"), Tag);
	TArray<TSharedPtr<FJsonValue>> TagsOut;
	for (const FName& T : A->Tags) TagsOut.Add(MakeShared<FJsonValueString>(T.ToString()));
	Result->SetArrayField(TEXT("tags"), TagsOut);

	// Only the call that actually added the tag has an inverse. Removing one
	// the actor already carried would undo something this call did not do.
	if (!bAlreadyHad)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), A->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("tag"), Tag);
		MCPSetRollback(Result, TEXT("remove_actor_tag"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::RemoveActorTag(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return E;
	FString Tag; if (auto E = RequireString(Params, TEXT("tag"), Tag)) return E;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();

	const FName TagName(*Tag);
	const int32 Removed = A->Tags.Remove(TagName);
	if (Removed > 0)
	{
		A->Modify();
		A->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	if (Removed == 0) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	Result->SetStringField(TEXT("tag"), Tag);
	Result->SetNumberField(TEXT("removed"), Removed);

	if (Removed > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), A->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("tag"), Tag);
		MCPSetRollback(Result, TEXT("add_actor_tag"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("AActor::Tags is an authored array and add_actor_tag appends, so the tag comes back at the end rather than at the position it held. It also comes back once, which is short if the actor carried the same tag more than once."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetActorTags(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return E;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();

	const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("tags"), TagsArr) || !TagsArr)
	{
		return MCPError(TEXT("Missing 'tags' array"));
	}

	// The whole array, as it stands, is what set_actor_tags puts back.
	TArray<TSharedPtr<FJsonValue>> PreviousTags;
	for (const FName& T : A->Tags) PreviousTags.Add(MakeShared<FJsonValueString>(T.ToString()));

	A->Modify();
	A->Tags.Reset();
	for (const TSharedPtr<FJsonValue>& V : *TagsArr)
	{
		FString S;
		if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
		{
			A->Tags.AddUnique(FName(*S));
		}
	}
	A->MarkPackageDirty();

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FName& T : A->Tags) Out.Add(MakeShared<FJsonValueString>(T.ToString()));

	// Order matters here: Tags is an authored array, so a reordering is a real
	// change even when the set is identical.
	bool bTagsChanged = Out.Num() != PreviousTags.Num();
	if (!bTagsChanged)
	{
		for (int32 i = 0; i < Out.Num(); ++i)
		{
			if (Out[i]->AsString() != PreviousTags[i]->AsString()) { bTagsChanged = true; break; }
		}
	}
	if (bTagsChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bTagsChanged);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	Result->SetArrayField(TEXT("tags"), Out);
	Result->SetArrayField(TEXT("previousTags"), PreviousTags);

	// This action replaces the array wholesale, so replaying it with the array
	// that was there restores it, order included.
	if (bTagsChanged)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), A->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetArrayField(TEXT("tags"), PreviousTags);
		MCPSetRollback(Result, TEXT("set_actor_tags"), Payload);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("On a World Partition map the inverse resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded between this call and the replay."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListActorTags(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return E;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();

	// T3: paged. Tag lists are usually short, but a data-driven actor can carry
	// hundreds, and the category pages uniformly rather than making the caller
	// remember which list actions accept a cursor.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_actor_tags|actor=%s"), *A->GetPathName()),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	// Deliberately NOT sorted: AActor::Tags is an authored array and its order
	// is what the caller sees in the details panel. The tag itself is the
	// anchor, which is why the emitted order can stay as authored.
	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(A->Tags.Num());
	for (const FName& T : A->Tags)
	{
		const FString Tag = T.ToString();
		Rows.Add({ Tag, MakeShared<FJsonValueString>(Tag) });
	}
	MCPPagination::EmitPage(Page, Rows, TEXT("tags"), Result);
	return MCPResult(Result);
}

// #205: attach an actor's root component to a parent actor.
TSharedPtr<FJsonValue> FLevelHandlers::AttachActor(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireStringAlt(Params, TEXT("childLabel"), TEXT("childPath"), ChildLabel)) return E;
	FString ParentLabel; if (auto E = RequireStringAlt(Params, TEXT("parentLabel"), TEXT("parentPath"), ParentLabel)) return E;

	// #983: both ends of an attachment take a path. Attaching to whichever
	// namesake the iterator reached first is how a prop ends up parented to a
	// building at the other end of the map.
	FMCPActorSelector ChildSel; ChildSel.LabelKey = TEXT("childLabel"); ChildSel.PathKey = TEXT("childPath");
	FMCPActorSelector ParentSel; ParentSel.LabelKey = TEXT("parentLabel"); ParentSel.PathKey = TEXT("parentPath");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Child = MCPResolveActor(World, Params, ActorErr, ChildSel);
	if (!Child) return ActorErr;
	AActor* Parent = MCPResolveActor(World, Params, ActorErr, ParentSel);
	if (!Parent) return ActorErr;
	ChildLabel = Child->GetActorLabel();
	ParentLabel = Parent->GetActorLabel();

	const FString RuleStr = OptionalString(Params, TEXT("attachRule"), TEXT("KeepWorld")).ToLower();
	EAttachmentRule Loc = EAttachmentRule::KeepWorld;
	if (RuleStr.Contains(TEXT("relative"))) Loc = EAttachmentRule::KeepRelative;
	else if (RuleStr.Contains(TEXT("snap"))) Loc = EAttachmentRule::SnapToTarget;

	const FString SocketName = OptionalString(Params, TEXT("socketName"));

	Child->Modify();
	const bool bOk = Child->AttachToActor(Parent, FAttachmentTransformRules(Loc, Loc, Loc, true), FName(*SocketName));
	Child->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("childLabel"), ChildLabel);
	Result->SetStringField(TEXT("parentLabel"), ParentLabel);
	Result->SetStringField(TEXT("childPath"), Child->GetPathName());
	Result->SetStringField(TEXT("parentPath"), Parent->GetPathName());
	Result->SetBoolField(TEXT("attached"), bOk);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("childLabel"), ChildLabel);
	Payload->SetStringField(TEXT("childPath"), Child->GetPathName());
	MCPSetRollback(Result, TEXT("detach_actor"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DetachActor(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireStringAlt(Params, TEXT("childLabel"), TEXT("childPath"), ChildLabel)) return E;

	FMCPActorSelector ChildSel; ChildSel.LabelKey = TEXT("childLabel"); ChildSel.PathKey = TEXT("childPath");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Child = MCPResolveActor(World, Params, ActorErr, ChildSel);
	if (!Child) return ActorErr;
	ChildLabel = Child->GetActorLabel();

	// The parent this call is about to drop, by path, plus the socket it was
	// attached to. Both are needed to name the attach that undoes it.
	AActor* PreviousParent = Child->GetAttachParentActor();
	const FString PreviousParentPath = PreviousParent ? PreviousParent->GetPathName() : FString();
	const FString PreviousParentLabel = PreviousParent ? PreviousParent->GetActorLabel() : FString();
	FString PreviousSocketName;
	if (PreviousParent && Child->GetRootComponent())
	{
		const FName Socket = Child->GetRootComponent()->GetAttachSocketName();
		if (Socket != NAME_None) PreviousSocketName = Socket.ToString();
	}
	const bool bWasAttached = PreviousParent != nullptr;

	Child->Modify();
	Child->DetachFromActor(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
	Child->MarkPackageDirty();

	auto Result = MCPSuccess();
	if (bWasAttached) MCPSetUpdated(Result); else MCPSetExisted(Result);
	// Present in both branches: MCPSetExisted does not write `updated`, and
	// this handler emitted it unconditionally before, so a consumer branching
	// on it must not start reading undefined.
	Result->SetBoolField(TEXT("updated"), bWasAttached);
	Result->SetBoolField(TEXT("unchanged"), !bWasAttached);
	Result->SetStringField(TEXT("childLabel"), ChildLabel);
	Result->SetStringField(TEXT("childPath"), Child->GetPathName());
	Result->SetBoolField(TEXT("detached"), true);
	Result->SetBoolField(TEXT("alreadyDetached"), !bWasAttached);
	Result->SetStringField(TEXT("previousParentLabel"), PreviousParentLabel);
	Result->SetStringField(TEXT("previousParentPath"), PreviousParentPath);

	if (bWasAttached)
	{
		// Detach kept the world transform, and attaching back with KeepWorld
		// keeps it too, so the relative transform is recomputed to the same
		// place it held before.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("childPath"), Child->GetPathName());
		Payload->SetStringField(TEXT("childLabel"), ChildLabel);
		Payload->SetStringField(TEXT("parentPath"), PreviousParentPath);
		Payload->SetStringField(TEXT("parentLabel"), PreviousParentLabel);
		Payload->SetStringField(TEXT("attachRule"), TEXT("KeepWorld"));
		if (!PreviousSocketName.IsEmpty()) Payload->SetStringField(TEXT("socketName"), PreviousSocketName);
		MCPSetRollback(Result, TEXT("attach_actor"), Payload);
	}
	return MCPResult(Result);
}

// Attach an exact named/root SceneComponent to an exact named/root parent
// SceneComponent. Unlike attach_actor, selecting a non-root child only changes
// that component's hierarchy; it does not parent or replicate the owning actor.
TSharedPtr<FJsonValue> FLevelHandlers::AttachComponent(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireStringAlt(Params, TEXT("childLabel"), TEXT("childPath"), ChildLabel)) return E;
	FString ParentLabel; if (auto E = RequireStringAlt(Params, TEXT("parentLabel"), TEXT("parentPath"), ParentLabel)) return E;

	// #983: both ends of an attachment take a path. Attaching to whichever
	// namesake the iterator reached first is how a prop ends up parented to a
	// building at the other end of the map.
	FMCPActorSelector ChildSel; ChildSel.LabelKey = TEXT("childLabel"); ChildSel.PathKey = TEXT("childPath");
	FMCPActorSelector ParentSel; ParentSel.LabelKey = TEXT("parentLabel"); ParentSel.PathKey = TEXT("parentPath");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Child = MCPResolveActor(World, Params, ActorErr, ChildSel);
	if (!Child) return ActorErr;
	AActor* Parent = MCPResolveActor(World, Params, ActorErr, ParentSel);
	if (!Parent) return ActorErr;
	ChildLabel = Child->GetActorLabel();
	ParentLabel = Parent->GetActorLabel();

	const FString ChildComponentSelector = OptionalString(Params, TEXT("childComponentName"));
	const FString ParentComponentSelector = OptionalString(Params, TEXT("parentComponentName"));

	UActorComponent* ResolvedChildComponent = ChildComponentSelector.IsEmpty()
		? static_cast<UActorComponent*>(Child->GetRootComponent())
		: FindNamedComponentOnActor(Child, ChildComponentSelector);
	if (!ResolvedChildComponent)
	{
		return ChildComponentSelector.IsEmpty()
			? MCPError(FString::Printf(TEXT("Child actor '%s' has no root component"), *ChildLabel))
			: MCPError(FString::Printf(TEXT("Child component '%s' not found on actor '%s'"), *ChildComponentSelector, *ChildLabel));
	}
	USceneComponent* ChildComponent = Cast<USceneComponent>(ResolvedChildComponent);
	if (!ChildComponent)
	{
		return MCPError(FString::Printf(TEXT("Child component '%s' on actor '%s' is not a SceneComponent"), *ResolvedChildComponent->GetName(), *ChildLabel));
	}

	UActorComponent* ResolvedParentComponent = ParentComponentSelector.IsEmpty()
		? static_cast<UActorComponent*>(Parent->GetRootComponent())
		: FindNamedComponentOnActor(Parent, ParentComponentSelector);
	if (!ResolvedParentComponent)
	{
		return ParentComponentSelector.IsEmpty()
			? MCPError(FString::Printf(TEXT("Parent actor '%s' has no root component"), *ParentLabel))
			: MCPError(FString::Printf(TEXT("Parent component '%s' not found on actor '%s'"), *ParentComponentSelector, *ParentLabel));
	}
	USceneComponent* ParentComponent = Cast<USceneComponent>(ResolvedParentComponent);
	if (!ParentComponent)
	{
		return MCPError(FString::Printf(TEXT("Parent component '%s' on actor '%s' is not a SceneComponent"), *ResolvedParentComponent->GetName(), *ParentLabel));
	}

	const FString RequestedRule = OptionalString(Params, TEXT("attachRule"), TEXT("KeepWorld"));
	FString RuleKey = RequestedRule;
	RuleKey.TrimStartAndEndInline();
	RuleKey = RuleKey.ToLower();
	EAttachmentRule Rule = EAttachmentRule::KeepWorld;
	FString CanonicalRule = TEXT("KeepWorld");
	if (RuleKey == TEXT("keeprelative"))
	{
		Rule = EAttachmentRule::KeepRelative;
		CanonicalRule = TEXT("KeepRelative");
	}
	else if (RuleKey == TEXT("snaptotarget"))
	{
		Rule = EAttachmentRule::SnapToTarget;
		CanonicalRule = TEXT("SnapToTarget");
	}
	else if (RuleKey != TEXT("keepworld"))
	{
		return MCPError(FString::Printf(TEXT("Invalid attachRule '%s'. Expected KeepWorld, KeepRelative, or SnapToTarget"), *RequestedRule));
	}
	const bool bWeldSimulatedBodies = OptionalBool(Params, TEXT("weldSimulatedBodies"), false);

	const FString SocketName = OptionalString(Params, TEXT("socketName"));
	const FName Socket = SocketName.IsEmpty() ? NAME_None : FName(*SocketName);
	if (Socket != NAME_None && !ParentComponent->DoesSocketExist(Socket))
	{
		return MCPError(FString::Printf(
			TEXT("Socket '%s' does not exist on parent component '%s' (%s) of actor '%s'"),
			*SocketName,
			*ParentComponent->GetName(),
			*ParentComponent->GetClass()->GetName(),
			*ParentLabel));
	}

	USceneComponent* PreviousParent = ChildComponent->GetAttachParent();
	const FName PreviousSocket = ChildComponent->GetAttachSocketName();
	AActor* PreviousParentActor = PreviousParent ? PreviousParent->GetOwner() : nullptr;
	const bool bAlreadyAttached = PreviousParent == ParentComponent && PreviousSocket == Socket;

	auto PopulateResult = [Child, Parent, ChildComponent, ParentComponent, Socket, &CanonicalRule, bWeldSimulatedBodies](TSharedPtr<FJsonObject> Result)
	{
		Result->SetStringField(TEXT("childLabel"), Child->GetActorLabel());
		Result->SetStringField(TEXT("parentLabel"), Parent->GetActorLabel());
		Result->SetStringField(TEXT("childComponentName"), ChildComponent->GetName());
		Result->SetStringField(TEXT("childComponentClass"), ChildComponent->GetClass()->GetName());
		Result->SetBoolField(TEXT("childIsRoot"), ChildComponent == Child->GetRootComponent());
		Result->SetStringField(TEXT("parentComponentName"), ParentComponent->GetName());
		Result->SetStringField(TEXT("parentComponentClass"), ParentComponent->GetClass()->GetName());
		Result->SetBoolField(TEXT("parentIsRoot"), ParentComponent == Parent->GetRootComponent());
		Result->SetStringField(TEXT("socketName"), Socket == NAME_None ? FString() : Socket.ToString());
		Result->SetStringField(TEXT("attachRule"), CanonicalRule);
		Result->SetBoolField(TEXT("weldSimulatedBodies"), bWeldSimulatedBodies);
		Result->SetBoolField(TEXT("attached"), true);
	};

	if (bAlreadyAttached)
	{
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		PopulateResult(Result);
		Result->SetBoolField(TEXT("alreadyAttached"), true);
		Result->SetBoolField(TEXT("attachmentChanged"), false);
		Result->SetBoolField(TEXT("attachmentRulesApplied"), false);
		return MCPResult(Result);
	}

	// Reject topology that native AttachToComponent would refuse before calling
	// Modify(), so failed self/cycle requests cannot create undo or dirty state.
	if (ChildComponent == ParentComponent)
	{
		return MCPError(FString::Printf(
			TEXT("Cannot attach component '%s' on actor '%s' to itself"),
			*ChildComponent->GetName(),
			*ChildLabel));
	}
	if (ParentComponent->IsAttachedTo(ChildComponent))
	{
		return MCPError(FString::Printf(
			TEXT("Cannot attach component '%s' on actor '%s' beneath its descendant component '%s' on actor '%s'"),
			*ChildComponent->GetName(),
			*ChildLabel,
			*ParentComponent->GetName(),
			*ParentLabel));
	}
	if (!ParentComponent->CanAttachAsChild(ChildComponent, Socket))
	{
		return MCPError(FString::Printf(
			TEXT("Parent component '%s' on actor '%s' cannot accept child component '%s' on actor '%s' at socket '%s'"),
			*ParentComponent->GetName(),
			*ParentLabel,
			*ChildComponent->GetName(),
			*ChildLabel,
			Socket == NAME_None ? TEXT("") : *Socket.ToString()));
	}

	// Even when only a named non-root component is reparented, a cross-actor
	// reference must obey the editor's actor-domain rules (level, content bundle,
	// external data layer, World Partition ownership, and actor-level cycles).
	if (Child != Parent)
	{
		if (!GEditor)
		{
			return MCPError(TEXT("Editor actor-parenting validation is unavailable"));
		}
		FText ParentingReason;
		if (!GEditor->CanParentActors(Parent, Child, &ParentingReason))
		{
			return MCPError(ParentingReason.IsEmpty()
				? FString::Printf(TEXT("Actor '%s' cannot be attached to actor '%s'"), *ChildLabel, *ParentLabel)
				: ParentingReason.ToString());
		}
	}
	if (ChildComponent->Mobility == EComponentMobility::Static && ParentComponent->Mobility != EComponentMobility::Static)
	{
		const TCHAR* ParentMobility = ParentComponent->Mobility == EComponentMobility::Stationary
			? TEXT("Stationary")
			: TEXT("Movable");
		return MCPError(FString::Printf(
			TEXT("Cannot attach Static child component '%s' on actor '%s' to %s parent component '%s' on actor '%s'"),
			*ChildComponent->GetName(),
			*ChildLabel,
			ParentMobility,
			*ParentComponent->GetName(),
			*ParentLabel));
	}

	// Record transaction state without dirtying until native attachment succeeds.
	Child->Modify(false);
	ChildComponent->Modify(false);
	Parent->Modify(false);
	ParentComponent->Modify(false);
	if (PreviousParent)
	{
		PreviousParent->Modify(false);
		if (AActor* PreviousOwner = PreviousParent->GetOwner())
		{
			PreviousOwner->Modify(false);
		}
	}
	const bool bAttached = ChildComponent->AttachToComponent(
		ParentComponent,
		FAttachmentTransformRules(Rule, Rule, Rule, bWeldSimulatedBodies),
		Socket);
	const bool bTopologyMatches =
		ChildComponent->GetAttachParent() == ParentComponent &&
		ChildComponent->GetAttachSocketName() == Socket;
	if (!bAttached || !bTopologyMatches)
	{
		const FString SocketSuffix = Socket == NAME_None
			? FString()
			: FString::Printf(TEXT(" at socket '%s'"), *SocketName);
		return MCPError(FString::Printf(
			TEXT("Failed to attach child component '%s' on actor '%s' to parent component '%s' on actor '%s'%s"),
			*ChildComponent->GetName(),
			*ChildLabel,
			*ParentComponent->GetName(),
			*ParentLabel,
			*SocketSuffix));
	}
	Child->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	PopulateResult(Result);
	Result->SetBoolField(TEXT("alreadyAttached"), false);
	Result->SetBoolField(TEXT("attachmentChanged"), true);
	Result->SetBoolField(TEXT("attachmentRulesApplied"), true);
	Result->SetStringField(TEXT("previousParentLabel"), PreviousParentActor ? PreviousParentActor->GetActorLabel() : FString());
	Result->SetStringField(TEXT("previousParentComponentName"), PreviousParent ? PreviousParent->GetName() : FString());
	Result->SetStringField(TEXT("previousParentComponentClass"), PreviousParent ? PreviousParent->GetClass()->GetName() : FString());
	Result->SetStringField(TEXT("previousSocketName"), PreviousSocket == NAME_None ? FString() : PreviousSocket.ToString());

	// Detach is an exact inverse only for a previously-unattached component
	// whose world transform was preserved and whose physics bodies were not
	// welded. Do not advertise a lossy rollback for reparent/snap operations.
	if (!PreviousParent && Rule == EAttachmentRule::KeepWorld && !bWeldSimulatedBodies)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("childLabel"), Child->GetActorLabel());
		if (!ChildComponentSelector.IsEmpty())
		{
			Payload->SetStringField(TEXT("childComponentName"), ChildComponent->GetName());
		}
		MCPSetRollback(Result, TEXT("detach_component"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DetachComponent(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireStringAlt(Params, TEXT("childLabel"), TEXT("childPath"), ChildLabel)) return E;

	FMCPActorSelector ChildSel; ChildSel.LabelKey = TEXT("childLabel"); ChildSel.PathKey = TEXT("childPath");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Child = MCPResolveActor(World, Params, ActorErr, ChildSel);
	if (!Child) return ActorErr;
	ChildLabel = Child->GetActorLabel();

	const FString ChildComponentSelector = OptionalString(Params, TEXT("childComponentName"));
	UActorComponent* ResolvedChildComponent = ChildComponentSelector.IsEmpty()
		? static_cast<UActorComponent*>(Child->GetRootComponent())
		: FindNamedComponentOnActor(Child, ChildComponentSelector);
	if (!ResolvedChildComponent)
	{
		return ChildComponentSelector.IsEmpty()
			? MCPError(FString::Printf(TEXT("Actor '%s' has no root component"), *ChildLabel))
			: MCPError(FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ChildComponentSelector, *ChildLabel));
	}
	USceneComponent* ChildComponent = Cast<USceneComponent>(ResolvedChildComponent);
	if (!ChildComponent)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' on actor '%s' is not a SceneComponent"), *ResolvedChildComponent->GetName(), *ChildLabel));
	}

	USceneComponent* PreviousParent = ChildComponent->GetAttachParent();
	AActor* PreviousParentActor = PreviousParent ? PreviousParent->GetOwner() : nullptr;
	const FString PreviousParentLabel = PreviousParentActor ? PreviousParentActor->GetActorLabel() : FString();
	const FString PreviousParentName = PreviousParent ? PreviousParent->GetName() : FString();
	const FString PreviousParentClass = PreviousParent ? PreviousParent->GetClass()->GetName() : FString();
	const FString PreviousSocketName = PreviousParent && ChildComponent->GetAttachSocketName() != NAME_None
		? ChildComponent->GetAttachSocketName().ToString()
		: FString();
	const bool bWasAttached = PreviousParent != nullptr;

	if (bWasAttached)
	{
		Child->Modify();
		ChildComponent->Modify();
		ChildComponent->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
		Child->MarkPackageDirty();
	}
	if (ChildComponent->GetAttachParent() != nullptr)
	{
		return MCPError(FString::Printf(TEXT("Failed to detach component '%s' on actor '%s'"), *ChildComponent->GetName(), *ChildLabel));
	}

	auto Result = MCPSuccess();
	if (bWasAttached) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("childLabel"), Child->GetActorLabel());
	Result->SetStringField(TEXT("childComponentName"), ChildComponent->GetName());
	Result->SetStringField(TEXT("childComponentClass"), ChildComponent->GetClass()->GetName());
	Result->SetBoolField(TEXT("childIsRoot"), ChildComponent == Child->GetRootComponent());
	Result->SetStringField(TEXT("previousParentLabel"), PreviousParentLabel);
	Result->SetStringField(TEXT("previousParentComponentName"), PreviousParentName);
	Result->SetStringField(TEXT("previousParentComponentClass"), PreviousParentClass);
	Result->SetStringField(TEXT("previousSocketName"), PreviousSocketName);
	Result->SetBoolField(TEXT("detached"), true);
	Result->SetBoolField(TEXT("alreadyDetached"), !bWasAttached);
	Result->SetBoolField(TEXT("detachmentChanged"), bWasAttached);

	if (bWasAttached && PreviousParentActor)
	{
		// attach_component names both ends by path and re-resolves the two
		// components by name, which is exactly what was recorded above. The
		// detach kept the world transform and KeepWorld puts it back.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("childPath"), Child->GetPathName());
		Payload->SetStringField(TEXT("childLabel"), Child->GetActorLabel());
		Payload->SetStringField(TEXT("childComponentName"), ChildComponent->GetName());
		Payload->SetStringField(TEXT("parentPath"), PreviousParentActor->GetPathName());
		Payload->SetStringField(TEXT("parentLabel"), PreviousParentLabel);
		Payload->SetStringField(TEXT("parentComponentName"), PreviousParentName);
		Payload->SetStringField(TEXT("attachRule"), TEXT("KeepWorld"));
		if (!PreviousSocketName.IsEmpty()) Payload->SetStringField(TEXT("socketName"), PreviousSocketName);
		MCPSetRollback(Result, TEXT("attach_component"), Payload);
	}
	return MCPResult(Result);
}

// #205: set USceneComponent::Mobility on the actor's root component.
TSharedPtr<FJsonValue> FLevelHandlers::SetActorMobility(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return E;
	FString MobilityStr; if (auto E = RequireString(Params, TEXT("mobility"), MobilityStr)) return E;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();
	USceneComponent* Root = A->GetRootComponent();
	if (!Root) return MCPError(FString::Printf(TEXT("Actor '%s' has no root component"), *ActorLabel));

	const FString L = MobilityStr.ToLower();
	EComponentMobility::Type M = EComponentMobility::Static;
	if (L == TEXT("movable") || L == TEXT("moveable")) M = EComponentMobility::Movable;
	else if (L == TEXT("stationary")) M = EComponentMobility::Stationary;
	else if (L == TEXT("static")) M = EComponentMobility::Static;
	else return MCPError(FString::Printf(TEXT("Unknown mobility '%s' (expected static|stationary|movable)"), *MobilityStr));

	const EComponentMobility::Type Prev = Root->Mobility;
	Root->Modify();
	Root->SetMobility(M);
	A->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	Result->SetStringField(TEXT("mobility"), MobilityStr);

	const TCHAR* PrevStr = Prev == EComponentMobility::Movable ? TEXT("movable")
		: Prev == EComponentMobility::Stationary ? TEXT("stationary") : TEXT("static");
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("actorPath"), A->GetPathName());
	Payload->SetStringField(TEXT("mobility"), PrevStr);
	MCPSetRollback(Result, TEXT("set_actor_mobility"), Payload);
	return MCPResult(Result);
}

// #204: read the current edit-target sub-level. UE drops new actors into this
// level when multiple sub-levels are loaded; without a way to query/set it the
// caller can't reliably target a particular streaming sub-level for spawns.
TSharedPtr<FJsonValue> FLevelHandlers::GetCurrentEditLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	ULevel* Cur = World->GetCurrentLevel();
	auto Result = MCPSuccess();
	if (Cur)
	{
		Result->SetStringField(TEXT("levelName"), Cur->GetOuter()->GetName());
		Result->SetStringField(TEXT("levelPath"), Cur->GetOuter()->GetPathName());
		Result->SetBoolField(TEXT("isPersistent"), Cur == World->PersistentLevel);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetCurrentEditLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString LevelName;
	if (!Params->TryGetStringField(TEXT("levelName"), LevelName))
	{
		Params->TryGetStringField(TEXT("levelPath"), LevelName);
	}
	if (LevelName.IsEmpty()) return MCPError(TEXT("Missing levelName (or levelPath)"));

	ULevelEditorSubsystem* LES = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LES) return MCPError(TEXT("LevelEditorSubsystem not available"));

	// The level that was current. Held as a POINTER as well as a name: two
	// sub-levels can share a name, so comparing names afterwards would report
	// a real switch between namesakes as "nothing happened".
	ULevel* PreviousLevel = World->GetCurrentLevel();
	FString PreviousLevelName;
	FString PreviousLevelPath;
	if (PreviousLevel)
	{
		PreviousLevelName = PreviousLevel->GetOuter()->GetName();
		PreviousLevelPath = PreviousLevel->GetOuter()->GetPathName();
	}

	// SetCurrentLevelByName is first-match-wins on a duplicated name, and the
	// payload can carry nothing but that name, so count the namesakes now and
	// let the response say whether the inverse is exact or a coin flip.
	int32 PreviousNameMatches = 0;
	if (!PreviousLevelName.IsEmpty())
	{
		for (ULevel* Candidate : World->GetLevels())
		{
			if (Candidate && Candidate->GetOuter() &&
				Candidate->GetOuter()->GetName() == PreviousLevelName)
			{
				++PreviousNameMatches;
			}
		}
	}

	const bool bOk = LES->SetCurrentLevelByName(FName(*LevelName));
	if (!bOk)
	{
		return MCPError(FString::Printf(TEXT("No loaded sub-level named '%s'"), *LevelName));
	}

	ULevel* Cur = World->GetCurrentLevel();
	auto Result = MCPSuccess();
	const FString NewLevelName = Cur ? Cur->GetOuter()->GetName() : FString();
	const bool bLevelChanged = Cur != PreviousLevel;
	if (bLevelChanged) MCPSetUpdated(Result); else MCPSetExisted(Result);
	// Present in both branches: MCPSetExisted does not write `updated`, and a
	// consumer branching on it must not read undefined.
	Result->SetBoolField(TEXT("updated"), bLevelChanged);
	Result->SetBoolField(TEXT("unchanged"), !bLevelChanged);
	if (Cur)
	{
		Result->SetStringField(TEXT("levelName"), NewLevelName);
		Result->SetStringField(TEXT("levelPath"), Cur->GetOuter()->GetPathName());
	}
	Result->SetStringField(TEXT("previousLevelName"), PreviousLevelName);
	Result->SetStringField(TEXT("previousLevelPath"), PreviousLevelPath);

	if (bLevelChanged && !PreviousLevelName.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("levelName"), PreviousLevelName);
		MCPSetRollback(Result, TEXT("set_current_edit_level"), Payload);
		if (PreviousNameMatches > 1)
		{
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("%d loaded sub-levels answer to the name '%s', and SetCurrentLevelByName takes the first one it reaches. This action's only parameter is that name, so the inverse can set the current level to a DIFFERENT sub-level of the same name, and everything spawned afterwards would land in the wrong package. previousLevelPath names the one this call left; check the current level against it after replaying."),
				PreviousNameMatches, *PreviousLevelName));
		}
	}
	return MCPResult(Result);
}

// #206: streaming sub-level CRUD
namespace
{
	static ULevelStreaming* FindStreamingByName(UWorld* World, const FString& NameOrPath)
	{
		if (!World) return nullptr;
		for (ULevelStreaming* SL : World->GetStreamingLevels())
		{
			if (!SL) continue;
			const FString PkgName = SL->GetWorldAssetPackageName();
			if (PkgName == NameOrPath) return SL;
			if (FPaths::GetBaseFilename(PkgName) == NameOrPath) return SL;
			if (SL->GetName() == NameOrPath) return SL;
		}
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FLevelHandlers::ListStreamingSublevels(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> Out;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		const FString PkgName = SL->GetWorldAssetPackageName();
		O->SetStringField(TEXT("levelName"), FPaths::GetBaseFilename(PkgName));
		O->SetStringField(TEXT("packageName"), PkgName);
		O->SetStringField(TEXT("streamingClass"), SL->GetClass()->GetName());
		O->SetBoolField(TEXT("initiallyLoaded"), SL->ShouldBeLoaded());
		O->SetBoolField(TEXT("initiallyVisible"), SL->GetShouldBeVisibleFlag());
		O->SetBoolField(TEXT("loaded"), SL->IsLevelLoaded());
		O->SetBoolField(TEXT("visible"), SL->GetShouldBeVisibleFlag());
		const FTransform T = SL->LevelTransform;
		TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
		Loc->SetNumberField(TEXT("x"), T.GetLocation().X);
		Loc->SetNumberField(TEXT("y"), T.GetLocation().Y);
		Loc->SetNumberField(TEXT("z"), T.GetLocation().Z);
		O->SetObjectField(TEXT("location"), Loc);
		Out.Add(MakeShared<FJsonValueObject>(O));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("sublevels"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::AddStreamingSublevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString LevelPath; if (auto E = RequireString(Params, TEXT("levelPath"), LevelPath)) return E;

	const FString StreamingClassName = OptionalString(Params, TEXT("streamingClass"), TEXT("LevelStreamingDynamic"));
	UClass* StreamingClass = ULevelStreamingDynamic::StaticClass();
	if (StreamingClassName.Equals(TEXT("LevelStreamingAlwaysLoaded"), ESearchCase::IgnoreCase))
	{
		StreamingClass = LoadClass<ULevelStreaming>(nullptr, TEXT("/Script/Engine.LevelStreamingAlwaysLoaded"));
		if (!StreamingClass) StreamingClass = ULevelStreamingDynamic::StaticClass();
	}

	ULevelStreaming* SL = UEditorLevelUtils::AddLevelToWorld(World, *LevelPath, StreamingClass);
	if (!SL)
	{
		return MCPError(FString::Printf(TEXT("Failed to add sub-level '%s'"), *LevelPath));
	}

	if (Params->HasField(TEXT("initiallyLoaded"))) SL->SetShouldBeLoaded(OptionalBool(Params, TEXT("initiallyLoaded"), true));
	if (Params->HasField(TEXT("initiallyVisible"))) SL->SetShouldBeVisible(OptionalBool(Params, TEXT("initiallyVisible"), true));

	if (Params->HasField(TEXT("location")))
	{
		FTransform T = SL->LevelTransform;
		T.SetLocation(OptionalVec3(Params, TEXT("location")));
		SL->LevelTransform = T;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("levelPath"), LevelPath);
	Result->SetStringField(TEXT("levelName"), FPaths::GetBaseFilename(LevelPath));

	// remove_streaming_sublevel resolves by package name, which is what
	// AddLevelToWorld recorded on the streaming level it returned.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("levelName"), SL->GetWorldAssetPackageName());
	MCPSetRollback(Result, TEXT("remove_streaming_sublevel"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::RemoveStreamingSublevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString Name;
	if (!Params->TryGetStringField(TEXT("levelName"), Name)) Params->TryGetStringField(TEXT("levelPath"), Name);
	if (Name.IsEmpty()) return MCPError(TEXT("Missing levelName (or levelPath)"));

	ULevelStreaming* SL = FindStreamingByName(World, Name);
	if (!SL)
	{
		// Idempotent: a sub-level that is not in the world is the state this
		// call asks for, so a replayed rollback is a no-op rather than a fail.
		//
		// #963's rule applies though: a destructive action that matched nothing
		// must not answer with a bare success. A typo and a completed removal
		// produce the same alreadyRemoved=true, so the response names what IS
		// in the world and says which of the two this might be.
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("levelName"), Name);
		Noop->SetBoolField(TEXT("removed"), false);
		Noop->SetBoolField(TEXT("alreadyRemoved"), true);
		Noop->SetStringField(TEXT("zeroMatchNote"),
			TEXT("No streaming sub-level answers to that name. This is idempotent, not a statement that it was ever there: an already-removed sub-level and a misspelt name look identical here. Compare against candidates[] before concluding the removal happened."));
		TArray<TSharedPtr<FJsonValue>> Candidates;
		for (ULevelStreaming* Other : World->GetStreamingLevels())
		{
			if (!Other) continue;
			Candidates.Add(MakeShared<FJsonValueString>(Other->GetWorldAssetPackageName()));
		}
		Noop->SetArrayField(TEXT("candidates"), Candidates);
		return MCPResult(Noop);
	}

	// Everything add_streaming_sublevel can put back, read before the removal.
	const FString PackageName = SL->GetWorldAssetPackageName();
	const bool bWasLoaded = SL->ShouldBeLoaded();
	const bool bWasVisible = SL->GetShouldBeVisibleFlag();
	const FVector PreviousLocation = SL->LevelTransform.GetLocation();
	const bool bWasAlwaysLoaded = SL->GetClass()->GetName().Contains(TEXT("AlwaysLoaded"));

	ULevel* Loaded = SL->GetLoadedLevel();
	if (Loaded)
	{
		UEditorLevelUtils::RemoveLevelFromWorld(Loaded);
	}
	World->RemoveStreamingLevels({ SL });

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("levelName"), Name);
	Result->SetStringField(TEXT("packageName"), PackageName);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetBoolField(TEXT("alreadyRemoved"), false);

	if (!PackageName.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("levelPath"), PackageName);
		Payload->SetStringField(TEXT("streamingClass"),
			bWasAlwaysLoaded ? TEXT("LevelStreamingAlwaysLoaded") : TEXT("LevelStreamingDynamic"));
		Payload->SetBoolField(TEXT("initiallyLoaded"), bWasLoaded);
		Payload->SetBoolField(TEXT("initiallyVisible"), bWasVisible);
		Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(PreviousLocation));
		MCPSetRollback(Result, TEXT("add_streaming_sublevel"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The biggest loss first: removing a sub-level discards the loaded ULevel, and adding it back LOADS IT FROM DISK, so every unsaved edit to every actor inside it is gone and does not come back. Beyond that, the sub-level returns with its package, its loaded and visible flags and its transform location, but add_streaming_sublevel only builds a LevelStreamingDynamic or a LevelStreamingAlwaysLoaded, so any other streaming class becomes the nearest of those two, and the streaming level's remaining properties, the transform rotation and its position in the world's streaming list are not restored."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetStreamingSublevelProperties(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString Name;
	if (!Params->TryGetStringField(TEXT("levelName"), Name)) Params->TryGetStringField(TEXT("levelPath"), Name);
	if (Name.IsEmpty()) return MCPError(TEXT("Missing levelName (or levelPath)"));

	ULevelStreaming* SL = FindStreamingByName(World, Name);
	if (!SL) return MCPError(FString::Printf(TEXT("Streaming sub-level not found: %s"), *Name));

	// Every field this action can write, as it stands, so the inverse is this
	// same call with the values that were there.
	const bool bPreviousLoaded = SL->ShouldBeLoaded();
	const bool bPreviousVisible = SL->GetShouldBeVisibleFlag();
	const FVector PreviousLocation = SL->LevelTransform.GetLocation();
	// Read the field the write actually drives. UEditorLevelUtils::SetLevelVisibility
	// sets ULevelStreaming::bShouldBeVisibleInEditor; ULevel::bIsVisible is the
	// transient "is it associated with the world right now" state, which is a
	// different question and can disagree mid-transition. Restoring from the
	// wrong one would put back a value this call never changed.
	const bool bPreviousEditorVisible = SL->GetShouldBeVisibleInEditor();

	bool bChanged = false;
	if (Params->HasField(TEXT("initiallyLoaded"))) { SL->SetShouldBeLoaded(OptionalBool(Params, TEXT("initiallyLoaded"), true)); bChanged = true; }
	if (Params->HasField(TEXT("initiallyVisible"))) { SL->SetShouldBeVisible(OptionalBool(Params, TEXT("initiallyVisible"), true)); bChanged = true; }

	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocObj) && LocObj && (*LocObj).IsValid())
	{
		double X = 0, Y = 0, Z = 0;
		(*LocObj)->TryGetNumberField(TEXT("x"), X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Z);
		FTransform T = SL->LevelTransform;
		T.SetLocation(FVector(X, Y, Z));
		SL->LevelTransform = T;
		bChanged = true;
	}

	bool bEditorVisibleSet = false;
	bool bEditorVisibleSkipped = false;
	const bool bEditorVisible = OptionalBool(Params, TEXT("editorVisible"), true);
	if (Params->HasField(TEXT("editorVisible")))
	{
		ULevel* Loaded = SL->GetLoadedLevel();
		if (Loaded)
		{
			UEditorLevelUtils::SetLevelVisibility(Loaded, bEditorVisible, false);
			bEditorVisibleSet = true;
		}
		else
		{
			// The sub-level is not loaded, so there is nothing to show or hide
			// and no write happened. Flagging it as set anyway made the
			// response claim a write and emit a rollback record for it.
			bEditorVisibleSkipped = true;
		}
	}

	auto Result = MCPSuccess();
	if (bChanged) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("levelName"), Name);
	Result->SetBoolField(TEXT("initiallyLoaded"), SL->ShouldBeLoaded());
	Result->SetBoolField(TEXT("initiallyVisible"), SL->GetShouldBeVisibleFlag());
	if (bEditorVisibleSet) Result->SetBoolField(TEXT("editorVisible"), bEditorVisible);
	if (bEditorVisibleSkipped)
	{
		Result->SetStringField(TEXT("editorVisibleNote"),
			TEXT("editorVisible was passed but the sub-level is not loaded, so there was nothing to show or hide and no write was made. Load it first if you meant to change its editor visibility."));
	}

	if (bChanged || bEditorVisibleSet)
	{
		// Restate only the fields this call actually wrote. Passing the others
		// would write values the caller never asked to change.
		//
		// Addressed by the resolved PACKAGE NAME, never by the caller's token.
		// FindStreamingByName also matches on the base filename, and
		// /Game/A/Sub and /Game/B/Sub both answer to "Sub", so replaying the
		// token could write this sub-level's old flags onto a different one.
		// The package name is the pass that resolver checks first and the only
		// one that is unique.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("levelName"), SL->GetWorldAssetPackageName());
		if (Params->HasField(TEXT("initiallyLoaded"))) Payload->SetBoolField(TEXT("initiallyLoaded"), bPreviousLoaded);
		if (Params->HasField(TEXT("initiallyVisible"))) Payload->SetBoolField(TEXT("initiallyVisible"), bPreviousVisible);
		if (Params->HasField(TEXT("location"))) Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(PreviousLocation));
		if (bEditorVisibleSet) Payload->SetBoolField(TEXT("editorVisible"), bPreviousEditorVisible);
		MCPSetRollback(Result, TEXT("set_streaming_sublevel_properties"), Payload);
	}
	return MCPResult(Result);
}

// #203: batch spawn StaticMeshActors on a 3D grid (or jittered cloud) so
// agents don't ship one place_actor per mesh. Bounds are an FBox; density
// drives count along each axis.
TSharedPtr<FJsonValue> FLevelHandlers::SpawnGrid(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString MeshPath; if (auto E = RequireString(Params, TEXT("staticMesh"), MeshPath)) return E;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!Mesh) return MCPError(FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath));

	FVector Min, Max;
	if (auto Err = RequireVec3(Params, TEXT("min"), Min)) return Err;
	if (auto Err = RequireVec3(Params, TEXT("max"), Max)) return Err;

	const int32 CountX = FMath::Max(1, OptionalInt(Params, TEXT("countX"), 4));
	const int32 CountY = FMath::Max(1, OptionalInt(Params, TEXT("countY"), 4));
	const int32 CountZ = FMath::Max(1, OptionalInt(Params, TEXT("countZ"), 1));
	const double Jitter = OptionalNumber(Params, TEXT("jitter"), 0.0);
	const FString LabelPrefix = OptionalString(Params, TEXT("labelPrefix"), TEXT("Grid"));

	const FVector Step = FVector(
		CountX > 1 ? (Max.X - Min.X) / (CountX - 1) : 0,
		CountY > 1 ? (Max.Y - Min.Y) / (CountY - 1) : 0,
		CountZ > 1 ? (Max.Z - Min.Z) / (CountZ - 1) : 0);

	FRandomStream Rand((int32)FDateTime::Now().GetTicks());

	TArray<TSharedPtr<FJsonValue>> Spawned;
	int32 Index = 0;
	for (int32 zi = 0; zi < CountZ; ++zi)
	for (int32 yi = 0; yi < CountY; ++yi)
	for (int32 xi = 0; xi < CountX; ++xi)
	{
		FVector Loc = Min + FVector(xi * Step.X, yi * Step.Y, zi * Step.Z);
		if (Jitter > 0.0)
		{
			Loc += FVector(Rand.FRandRange(-Jitter, Jitter), Rand.FRandRange(-Jitter, Jitter), Rand.FRandRange(-Jitter, Jitter));
		}
		FActorSpawnParameters SpawnParams;
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Loc, FRotator::ZeroRotator, SpawnParams);
		if (!SMA) continue;
		SMA->SetMobility(EComponentMobility::Movable);
		if (UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent())
		{
			SMC->SetStaticMesh(Mesh);
		}
		SMA->SetActorLabel(FString::Printf(TEXT("%s_%d"), *LabelPrefix, Index));
		Spawned.Add(MakeShared<FJsonValueString>(SMA->GetActorLabel()));
		Index++;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetNumberField(TEXT("count"), Spawned.Num());
	Result->SetArrayField(TEXT("labels"), Spawned);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Undoing this means deleting the actors it spawned, and level(delete_actor) takes one actor path per call while level(delete_actors) takes filters rather than a path list. A single delete_actors on the shared labelPrefix would also delete any pre-existing actor whose label starts with it, so no inverse is named. labels[] is the list to delete one at a time."));
	return MCPResult(Result);
}

// #203: batch translate by label list or tag.
TSharedPtr<FJsonValue> FLevelHandlers::BatchTranslate(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FVector Offset;
	if (auto Err = RequireVec3(Params, TEXT("offset"), Offset)) return Err;

	TSet<AActor*> Targets;
	const TArray<TSharedPtr<FJsonValue>>* LabelArr = nullptr;
	if (Params->TryGetArrayField(TEXT("actorLabels"), LabelArr) && LabelArr)
	{
		for (const auto& V : *LabelArr)
		{
			FString S; if (V.IsValid() && V->TryGetString(S))
			{
				// #983: a batch is the plural case, so a label naming several
				// actors moves all of them rather than one at random.
				TArray<AActor*> Matches;
				MCPCollectActorsByToken(World, S, EMCPActorMatch::Label, Matches);
				for (AActor* Match : Matches) Targets.Add(Match);
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* PathArr = nullptr;
	if (Params->TryGetArrayField(TEXT("actorPaths"), PathArr) && PathArr)
	{
		for (const auto& V : *PathArr)
		{
			FString S; if (V.IsValid() && V->TryGetString(S))
			{
				if (AActor* A = MCPFindActorByPath(World, S)) Targets.Add(A);
			}
		}
	}
	FString TagFilter; if (Params->TryGetStringField(TEXT("tag"), TagFilter) && !TagFilter.IsEmpty())
	{
		const FName TagName(*TagFilter);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(TagName)) Targets.Add(*It);
		}
	}
	if (Targets.Num() == 0) return MCPError(TEXT("Provide actorLabels[], actorPaths[] or tag matching at least one actor"));

	// Resolve the selector to paths as the move happens. The inverse names the
	// same actors by path rather than replaying a label or tag selector that
	// may match a different set by the time it runs.
	TArray<TSharedPtr<FJsonValue>> TargetPaths;
	for (AActor* A : Targets)
	{
		A->Modify();
		A->SetActorLocation(A->GetActorLocation() + Offset);
		A->MarkPackageDirty();
		TargetPaths.Add(MakeShared<FJsonValueString>(A->GetPathName()));
	}

	// A zero offset moved nothing, however many actors it walked. Reporting
	// updated and handing back an inverse that translates by -0 would be two
	// falsehoods about the same no-op.
	const bool bOffsetMoves = !Offset.IsNearlyZero();

	auto Result = MCPSuccess();
	if (bOffsetMoves) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bOffsetMoves);
	Result->SetNumberField(TEXT("count"), Targets.Num());
	Result->SetArrayField(TEXT("actorPaths"), TargetPaths);

	// A translation by an offset inverts to a translation by its negation, on
	// exactly the actors that moved.
	if (bOffsetMoves)
	{
		TSharedPtr<FJsonObject> InverseOffset = MakeShared<FJsonObject>();
		InverseOffset->SetNumberField(TEXT("x"), -Offset.X);
		InverseOffset->SetNumberField(TEXT("y"), -Offset.Y);
		InverseOffset->SetNumberField(TEXT("z"), -Offset.Z);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("actorPaths"), TargetPaths);
		Payload->SetObjectField(TEXT("offset"), InverseOffset);
		MCPSetRollback(Result, TEXT("batch_translate"), Payload);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("On a World Partition map the inverse resolves each actor by path against loaded actors only, so any actor whose cell unloaded between this call and the replay is silently not moved back: the count in the response is what was moved, not what the inverse will reach."));
	}
	return MCPResult(Result);
}

// #264 - place_actors_batch: spawn many StaticMeshActors with per-instance
// mesh + transform. Avoids the chatty place_actor-per-row pattern that filled
// up the workaround log for procedural placement scripts.
TSharedPtr<FJsonValue> FLevelHandlers::PlaceActorsBatch(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const TArray<TSharedPtr<FJsonValue>>* ActorsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("actors"), ActorsArr) || !ActorsArr)
	{
		return MCPError(TEXT("Missing 'actors' (array of {staticMesh, location?, rotation?, scale?, label?})"));
	}

	auto ReadVec = [](const TSharedPtr<FJsonObject>& Obj, FVector Default) -> FVector
	{
		if (!Obj.IsValid()) return Default;
		FVector V = Default;
		double X = 0; if (Obj->TryGetNumberField(TEXT("x"), X)) V.X = X;
		double Y = 0; if (Obj->TryGetNumberField(TEXT("y"), Y)) V.Y = Y;
		double Z = 0; if (Obj->TryGetNumberField(TEXT("z"), Z)) V.Z = Z;
		return V;
	};
	auto ReadRot = [](const TSharedPtr<FJsonObject>& Obj) -> FRotator
	{
		if (!Obj.IsValid()) return FRotator::ZeroRotator;
		FRotator R(0, 0, 0);
		double V = 0; if (Obj->TryGetNumberField(TEXT("pitch"), V)) R.Pitch = V;
		if (Obj->TryGetNumberField(TEXT("yaw"), V)) R.Yaw = V;
		if (Obj->TryGetNumberField(TEXT("roll"), V)) R.Roll = V;
		return R;
	};

	// Cache mesh loads by path so a 1000-row batch with 5 unique meshes only
	// does 5 LoadObject calls.
	TMap<FString, UStaticMesh*> MeshCache;
	auto ResolveMesh = [&MeshCache](const FString& Path) -> UStaticMesh*
	{
		if (Path.IsEmpty()) return nullptr;
		if (UStaticMesh** Cached = MeshCache.Find(Path)) return *Cached;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		MeshCache.Add(Path, Mesh);
		return Mesh;
	};

	int32 Spawned = 0, FailedMesh = 0, FailedSpawn = 0;
	TArray<TSharedPtr<FJsonValue>> Labels;
	TArray<TSharedPtr<FJsonValue>> Errors;

	for (int32 i = 0; i < ActorsArr->Num(); i++)
	{
		const TSharedPtr<FJsonValue>& Entry = (*ActorsArr)[i];
		const TSharedPtr<FJsonObject> Row = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!Row.IsValid()) { FailedSpawn++; continue; }

		FString MeshPath;
		Row->TryGetStringField(TEXT("staticMesh"), MeshPath);
		UStaticMesh* Mesh = ResolveMesh(MeshPath);
		if (!Mesh)
		{
			FailedMesh++;
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetNumberField(TEXT("index"), i);
			Err->SetStringField(TEXT("staticMesh"), MeshPath);
			Err->SetStringField(TEXT("reason"), TEXT("static_mesh_not_found"));
			Errors.Add(MakeShared<FJsonValueObject>(Err));
			continue;
		}

		const TSharedPtr<FJsonObject>* LocObj = nullptr;
		const TSharedPtr<FJsonObject>* RotObj = nullptr;
		const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
		Row->TryGetObjectField(TEXT("location"), LocObj);
		Row->TryGetObjectField(TEXT("rotation"), RotObj);
		Row->TryGetObjectField(TEXT("scale"), ScaleObj);

		const FVector Loc   = LocObj ? ReadVec(*LocObj, FVector::ZeroVector) : FVector::ZeroVector;
		const FRotator Rot  = RotObj ? ReadRot(*RotObj) : FRotator::ZeroRotator;
		const FVector Scale = ScaleObj ? ReadVec(*ScaleObj, FVector::OneVector) : FVector::OneVector;

		FActorSpawnParameters SpawnParams;
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Loc, Rot, SpawnParams);
		if (!SMA) { FailedSpawn++; continue; }
		SMA->SetMobility(EComponentMobility::Movable);
		if (UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent())
		{
			SMC->SetStaticMesh(Mesh);
			SMC->SetWorldScale3D(Scale);
		}

		FString Label;
		if (Row->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
		{
			SMA->SetActorLabel(Label);
		}
		Labels.Add(MakeShared<FJsonValueString>(SMA->GetActorLabel()));
		Spawned++;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetNumberField(TEXT("requested"), ActorsArr->Num());
	Result->SetNumberField(TEXT("spawned"), Spawned);
	Result->SetNumberField(TEXT("failedMesh"), FailedMesh);
	Result->SetNumberField(TEXT("failedSpawn"), FailedSpawn);
	Result->SetArrayField(TEXT("labels"), Labels);
	if (Errors.Num() > 0) Result->SetArrayField(TEXT("errors"), Errors);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Undoing this means deleting the actors it spawned, and level(delete_actor) takes one actor path per call while level(delete_actors) takes filters rather than a path list. Deleting by the labels these actors carry would reach any namesake as well, so no inverse is named. labels[] is the list to delete one at a time."));
	return MCPResult(Result);
}

