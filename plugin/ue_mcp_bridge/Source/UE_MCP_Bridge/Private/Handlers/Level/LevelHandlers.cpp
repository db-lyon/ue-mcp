#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerSkinnedAsset.h"
#include "Handlers/VolumeHelpers_Internal.h"
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
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("level"));

	// #1057: every handler here declares its parameters in its spec and nowhere
	// else; the TS surface is generated from a recording of these. One whose
	// contract values would reach a write before anything fails says why in its
	// ContractExempt reason.
	using EType = EMCPParamType;
	const FMCPParamSpec SpecActorLabel = MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Actor editor label; pass actorLabel or actorPath"));
	const FMCPParamSpec SpecActorPath = MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Full actor object path; the unambiguous selector, and it wins over actorLabel"));
	const FMCPParamSpec SpecWorld = MCPParam::Optional(TEXT("world"), EType::String, TEXT("World scope: editor (default) | pie"));
	const FMCPParamSpec SpecPieInstance = MCPParam::Optional(TEXT("pieInstance"), EType::Integer, TEXT("Which PIE world when world is pie: 0 = server or primary, 1..N = clients"));
	const FMCPParamSpec SpecCursor = MCPParam::Optional(TEXT("cursor"), EType::String, TEXT("Resume a paged read: pass back the nextCursor from the previous page, unmodified"));
	const FMCPParamSpec SpecLimit = MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows on this page"));
	const FMCPParamSpec SpecLocation = MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World location"));
	const FMCPParamSpec SpecRotation = MCPParam::Optional(TEXT("rotation"), EType::Rotator, TEXT("World rotation"));
	const FMCPParamSpec SpecScale = MCPParam::Optional(TEXT("scale"), EType::Vec3, TEXT("Actor scale"));
	const FMCPParamSpec SpecComponentName = MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Component instance name"));
	const FMCPParamSpec SpecLabel = MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label; an existing actor with this label is reported rather than duplicated"));
	const FMCPParamSpec SpecOnConflict = MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When the label or name is taken: skip (default) | error"));
	const FMCPParamSpec SpecPropertyName = MCPParam::Required(TEXT("propertyName"), EType::String, TEXT("Property name, dotted paths supported"));
	const FMCPParamSpec SpecWorldSpace = MCPParam::Optional(TEXT("worldSpace"), EType::Boolean, TEXT("Treat transforms as world space (default true)"));
	const FMCPParamSpec SpecIndex = MCPParam::Required(TEXT("index"), EType::Integer, TEXT("Instance index"));
	const FMCPParamSpec SpecTag = MCPParam::Required(TEXT("tag"), EType::String, TEXT("Actor tag"));
	const FMCPParamSpec SpecChildLabel = MCPParam::Optional(TEXT("childLabel"), EType::String, TEXT("Child actor label; pass childLabel or childPath"));
	const FMCPParamSpec SpecChildPath = MCPParam::Optional(TEXT("childPath"), EType::String, TEXT("Child actor object path"));
	const FMCPParamSpec SpecParentLabel = MCPParam::Optional(TEXT("parentLabel"), EType::String, TEXT("Parent actor label; pass parentLabel or parentPath"));
	const FMCPParamSpec SpecParentPath = MCPParam::Optional(TEXT("parentPath"), EType::String, TEXT("Parent actor object path"));
	const FMCPParamSpec SpecAttachRule = MCPParam::Optional(TEXT("attachRule"), EType::String, TEXT("KeepWorld | KeepRelative | SnapToTarget"));
	const FMCPParamSpec SpecSocketName = MCPParam::Optional(TEXT("socketName"), EType::String, TEXT("Socket or bone on the resolved parent component"));
	const FMCPParamSpec SpecLevelName = MCPParam::Required(TEXT("levelName"), EType::String, TEXT("Streaming sub-level name or package path")).Alias(TEXT("levelPath"));
	const FMCPParamSpec SpecMobility = MCPParam::Optional(TEXT("mobility"), EType::String, TEXT("static | stationary | movable"));
	const FMCPParamSpec SpecIntensity = MCPParam::Optional(TEXT("intensity"), EType::Number, TEXT("Light intensity"));
	const FMCPParamSpec SpecColor = MCPParam::Optional(TEXT("color"), EType::Object, TEXT("Colour {r, g, b} in 0-255"));
	const FMCPParamSpec SpecPostProcessComponent = MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Component holding the post-process settings, when the actor is not a PostProcessVolume"));
	const FMCPParamSpec SpecPostProcessProperty = MCPParam::Optional(TEXT("propertyName"), EType::String, TEXT("FPostProcessSettings property on that component"));
	const FMCPParamSpec SpecDryRun = MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Report what would change without writing"));
	const FMCPParamSpec SpecTransactionLabel = MCPParam::Optional(TEXT("transactionLabel"), EType::String, TEXT("Undo-stack entry name"));
	const FMCPParamSpec SpecActorLabels = MCPParam::Optional(TEXT("actorLabels"), EType::Array, TEXT("Exact actor editor labels")).Items(EType::String);
	const FMCPParamSpec SpecLabelPrefix = MCPParam::Optional(TEXT("labelPrefix"), EType::String, TEXT("Case-sensitive prefix over the actor's editor label"));
	const FMCPParamSpec SpecLabelContains = MCPParam::Optional(TEXT("labelContains"), EType::String, TEXT("Case-insensitive substring over the actor's editor label"));
	const FMCPParamSpec SpecSelectorTag = MCPParam::Optional(TEXT("tag"), EType::String, TEXT("Actor must carry this tag"));
	const FMCPParamSpec SpecFolderPath = MCPParam::Optional(TEXT("folderPath"), EType::String, TEXT("World Outliner folder, matched exactly"));
	const FMCPParamSpec SpecFolderPathPrefix = MCPParam::Optional(TEXT("folderPathPrefix"), EType::String, TEXT("World Outliner folder prefix"));
	const FMCPParamSpec SpecMatchSubclasses = MCPParam::Optional(TEXT("matchSubclasses"), EType::Boolean, TEXT("Match subclasses of the class filter (default true)"));

	Registry.RegisterHandler(TEXT("get_world_outliner"), &GetOutliner, {
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Case-sensitive substring over the class name")),
		MCPParam::Optional(TEXT("exactClass"), EType::Boolean, TEXT("Require classFilter to be the exact class name")),
		MCPParam::Optional(TEXT("nameFilter"), EType::String, TEXT("Case-sensitive substring over the internal name or the label")),
		SpecFolderPath, SpecFolderPathPrefix,
		MCPParam::Optional(TEXT("editorHidden"), EType::Boolean, TEXT("Only editor-hidden (true) or only visible (false) actors")),
		MCPParam::Optional(TEXT("includeStreaming"), EType::Boolean, TEXT("Include World Partition streaming-proxy and HLOD actors (default false)")),
		SpecWorld, SpecPieInstance, SpecCursor, SpecLimit,
	});
	// #717: query/set per-actor editor-only visibility (temporarily hidden).
	Registry.RegisterHandler(TEXT("set_editor_visibility"), &SetEditorVisibility, {
		MCPParam::Required(TEXT("hidden"), EType::Boolean, TEXT("true hides the actors in the editor, false shows them")),
		SpecActorLabels,
		MCPParam::Optional(TEXT("all"), EType::Boolean, TEXT("Target every actor")),
	});
	Registry.RegisterHandler(TEXT("place_actor"), &PlaceActor, {
		MCPParam::Required(TEXT("actorClass"), EType::String, TEXT("Actor class: short name, /Script path or Blueprint class path")),
		SpecLabel, SpecOnConflict, SpecLocation, SpecRotation, SpecScale,
		MCPParam::Optional(TEXT("staticMesh"), EType::String, TEXT("Static mesh for a StaticMeshActor")),
		MCPParam::Optional(TEXT("material"), EType::String, TEXT("Material applied at slot 0")),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("delete_actor"), &DeleteActor, {
		SpecActorLabel, SpecActorPath,
	});
	Registry.RegisterHandler(TEXT("get_actor_details"), &GetActorDetails, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Optional(TEXT("includeProperties"), EType::Boolean, TEXT("Include reflected UPROPERTY values")),
		MCPParam::Optional(TEXT("propertyName"), EType::String, TEXT("Only this property, with includeProperties")),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("get_component_tree"), &GetComponentTree, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Optional(TEXT("includeProperties"), EType::Boolean, TEXT("Include reflected UPROPERTY values")),
		MCPParam::Optional(TEXT("componentClass"), EType::String, TEXT("Case-insensitive substring over the component class name")),
		MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Only this component, by instance name")),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("get_relative_transform"), &GetRelativeTransform, {
		MCPParam::Optional(TEXT("targetLabel"), EType::String, TEXT("Target actor label; pass targetLabel or targetPath")).Alias(TEXT("target")),
		MCPParam::Optional(TEXT("targetPath"), EType::String, TEXT("Target actor object path")),
		MCPParam::Optional(TEXT("referenceLabel"), EType::String, TEXT("Reference actor label; pass referenceLabel or referencePath")).Alias(TEXT("reference")),
		MCPParam::Optional(TEXT("referencePath"), EType::String, TEXT("Reference actor object path")),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("get_current_level"), &GetCurrentLevel, {});
	// #964: LevelHandlers_Save.cpp. Saves through the same package path
	// editor(save_dirty) uses, so the two cannot disagree about one package,
	// and reports the package, the file and the engine's own reason on failure.
	Registry.RegisterHandler(TEXT("save_level"), &SaveLevel, {
		MCPParam::Optional(TEXT("force"), EType::Boolean, TEXT("Write a package even when it is already clean (default false)")),
		MCPParam::Optional(TEXT("includeExternalActors"), EType::Boolean, TEXT("Also save the level's loaded external actor and folder packages (default true)")),
		MCPParam::Optional(TEXT("commitDeletes"), EType::Boolean, TEXT("Save through the editor's dirty-package save, which deletes the packages of deleted World Partition actors (default false)")),
	}, MCPSpec::ContractExempt(TEXT("Saves the current level under the contract values; nothing it reads fails first")));
	Registry.RegisterHandler(TEXT("list_levels"), &ListLevels, {
		SpecCursor, SpecLimit,
	});
	Registry.RegisterHandler(TEXT("get_selected_actors"), &GetSelectedActors, {});
	Registry.RegisterHandler(TEXT("list_volumes"), &ListVolumes, {
		MCPParam::Optional(TEXT("volumeType"), EType::String, TEXT("Substring over the volume class name")),
		SpecCursor, SpecLimit,
	});
	Registry.RegisterHandler(TEXT("move_actor"), &MoveActor, {
		SpecActorLabel, SpecActorPath, SpecLocation, SpecRotation, SpecScale, SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("aim_actor_at"), &AimActorAt, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Optional(TEXT("targetPoint"), EType::Vec3, TEXT("World point to look at")),
		MCPParam::Optional(TEXT("targetActor"), EType::String, TEXT("Label of the actor to look at")),
		MCPParam::Optional(TEXT("targetActorPath"), EType::String, TEXT("Object path of the actor to look at")),
		MCPParam::Optional(TEXT("roll"), EType::Number, TEXT("Roll in degrees (default 0)")),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("nav_project_point"), &NavProjectPoint, {
		MCPParam::Required(TEXT("point"), EType::Vec3, TEXT("World point to project onto the navmesh")),
		MCPParam::Optional(TEXT("extent"), EType::Vec3, TEXT("Query extent (default 100, 100, 100)")),
		SpecWorld, SpecPieInstance,
	});
	const FMCPParamSpec SpecActorPaths = MCPParam::Optional(TEXT("actorPaths"), EType::Array, TEXT("Full actor object paths")).Items(EType::String);
	Registry.RegisterHandler(TEXT("select_actors"), &SelectActors, {
		SpecActorLabels, SpecActorPaths,
	}, MCPSpec::AtLeastOne({ { TEXT("actorLabels") }, { TEXT("actorPaths") } })
		.ContractExempt(TEXT("Deselects everything before it selects, so the contract's empty lists would clear the editor selection")));
	Registry.RegisterHandler(TEXT("spawn_light"), &SpawnLight, {
		MCPParam::Required(TEXT("lightType"), EType::String, TEXT("point | spot | directional | rect | sky")),
		SpecOnConflict, SpecLabel, SpecLocation, SpecRotation, SpecIntensity, SpecColor, SpecMobility,
		MCPParam::Optional(TEXT("attenuationRadius"), EType::Number, TEXT("Point, spot and rect lights only")),
	});
	Registry.RegisterHandler(TEXT("set_light_properties"), &SetLightProperties, {
		SpecActorLabel, SpecActorPath, SpecIntensity, SpecColor,
		MCPParam::Optional(TEXT("rotation"), EType::Rotator, TEXT("DirectionalLight sun angle")),
		SpecMobility,
		MCPParam::Optional(TEXT("recaptureSky"), EType::Boolean, TEXT("Recapture a SkyLight after the change")),
		MCPParam::Optional(TEXT("volumetricScatteringIntensity"), EType::Number, TEXT("Volumetric scattering intensity")),
		MCPParam::Optional(TEXT("sourceRadius"), EType::Number, TEXT("Point or spot light source radius")),
		MCPParam::Optional(TEXT("innerConeAngle"), EType::Number, TEXT("Spot light inner cone angle")),
		MCPParam::Optional(TEXT("outerConeAngle"), EType::Number, TEXT("Spot light outer cone angle")),
	});
	Registry.RegisterHandler(TEXT("spawn_volume"), &SpawnVolume, {
		MCPParam::Required(TEXT("volumeType"), EType::String, TEXT("Volume class, by short name or alias such as trigger, blocking, postprocess, navmesh")),
		SpecOnConflict, SpecLabel, SpecLocation,
		MCPParam::Optional(TEXT("extent"), EType::Vec3, TEXT("Half extent of the cube brush (default 100, 100, 100)")),
		MCPParam::Optional(TEXT("graphPath"), EType::String, TEXT("PCG graph for a PCGVolume")),
	});
	Registry.RegisterHandler(TEXT("add_component_to_actor"), &AddComponentToActor, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("componentClass"), EType::String, TEXT("Component class: short name or full path")),
		MCPParam::Required(TEXT("componentName"), EType::String, TEXT("Name of the new component")),
		SpecOnConflict,
	});
	Registry.RegisterHandler(TEXT("remove_component_from_actor"), &RemoveComponentFromActor, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("componentName"), EType::String, TEXT("Component to remove")),
	});
	Registry.RegisterHandler(TEXT("load_level"), &LoadLevel, {
		MCPParam::Required(TEXT("levelPath"), EType::String, TEXT("Map package to open")),
	}, MCPSpec::ContractExempt(TEXT("Ends any play session, collects garbage and swaps the open map before the contract path can fail")));
	Registry.RegisterHandler(TEXT("clear_level_script"), &ClearLevelScript, {
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Report what would be removed without removing it (default true)")),
		MCPParam::Optional(TEXT("save"), EType::Boolean, TEXT("Save only the current level after a successful clear and compile (default false)")),
	}, MCPSpec::ContractExempt(TEXT("The contract's dryRun=false clears the loaded Level Blueprint; nothing it reads fails first")));
	Registry.RegisterHandler(TEXT("set_component_property"), &SetComponentProperty, {
		SpecActorLabel, SpecActorPath, SpecComponentName, SpecPropertyName,
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("Value to write; null clears an object reference")),
		SpecWorld, SpecPieInstance,
	});
	const FMCPParamSpec SpecExactSceneComponent = MCPParam::Required(TEXT("componentName"), EType::String, TEXT("Exact SceneComponent instance name"));
	Registry.RegisterHandler(TEXT("nudge_component"), &NudgeComponent, {
		SpecActorLabel, SpecActorPath, SpecExactSceneComponent,
		MCPParam::Optional(TEXT("frame"), EType::String, TEXT("Frame for translation and axis rotation: world | actor (default) | parent | component")),
		MCPParam::Optional(TEXT("translationDelta"), EType::Object, TEXT("Frame-relative translation in centimetres")).WithFields({
			MCPParam::OptionalField(TEXT("forwardCm"), EType::Number, TEXT("Along the frame's forward axis")),
			MCPParam::OptionalField(TEXT("rightCm"), EType::Number, TEXT("Along the frame's right axis")),
			MCPParam::OptionalField(TEXT("upCm"), EType::Number, TEXT("Along the frame's up axis")),
		}),
		MCPParam::Optional(TEXT("axisRotation"), EType::Object, TEXT("Quaternion rotation about one frame axis; not with viewRotation")).WithFields({
			MCPParam::RequiredField(TEXT("axis"), EType::String, TEXT("forward | right | up")),
			MCPParam::RequiredField(TEXT("degrees"), EType::Number, TEXT("Signed rotation in degrees")),
		}),
		MCPParam::Optional(TEXT("viewRotation"), EType::Object, TEXT("Observer-relative rotation in the selected frame; not with axisRotation")).WithFields({
			MCPParam::RequiredField(TEXT("viewFrom"), EType::String, TEXT("front | back | right | left | above | below")),
			MCPParam::RequiredField(TEXT("direction"), EType::String, TEXT("clockwise | counterclockwise")),
			MCPParam::RequiredField(TEXT("degrees"), EType::Number, TEXT("Rotation in degrees, greater than zero")),
		}),
		MCPParam::Optional(TEXT("scaleMultiplier"), EType::Number, TEXT("Uniform relative-scale multiplier, greater than zero")),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Inspect and preview the requested transform without writing (default false)")),
		SpecWorld, SpecPieInstance,
	}, MCPSpec::ExactlyOne({ { TEXT("actorLabel") }, { TEXT("actorPath") } }));
	// nudge_component's rollback names this, so its own surface carries no restore key.
	Registry.RegisterHandler(TEXT("restore_component_relative_transform"), &RestoreComponentRelativeTransform, {
		SpecActorLabel, SpecActorPath, SpecExactSceneComponent,
		MCPParam::Required(TEXT("relativeTransform"), EType::Object, TEXT("The relative transform to put back, as nudge_component's rollback records it")).WithFields({
			MCPParam::RequiredField(TEXT("location"), EType::Vec3, TEXT("Relative location")),
			MCPParam::RequiredField(TEXT("quaternion"), EType::Object, TEXT("Relative rotation {x, y, z, w}; this is what is applied")),
			MCPParam::RequiredField(TEXT("scale"), EType::Vec3, TEXT("Relative scale")),
			MCPParam::OptionalField(TEXT("rotation"), EType::Rotator, TEXT("The same rotation as a rotator, for reading only")),
		}),
		SpecWorld, SpecPieInstance,
	}, MCPSpec::ExactlyOne({ { TEXT("actorLabel") }, { TEXT("actorPath") } }));
	Registry.RegisterHandler(TEXT("get_component_details"), &GetComponentDetails, {
		SpecActorLabel, SpecActorPath, SpecComponentName,
		MCPParam::Optional(TEXT("includeValues"), EType::Boolean, TEXT("Dump UPROPERTY values")),
		MCPParam::Optional(TEXT("propertyNames"), EType::Array, TEXT("Restrict includeValues to these properties")).Items(EType::String),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("set_actor_material"), &SetActorMaterial, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("materialPath"), EType::String, TEXT("Material asset path")),
		MCPParam::Optional(TEXT("slotIndex"), EType::Integer, TEXT("Material slot (default 0)")),
	});
	Registry.RegisterHandler(TEXT("set_volume_properties"), &SetVolumeProperties, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("properties"), EType::Object, TEXT("Property name to value")),
	});
	Registry.RegisterHandler(TEXT("get_world_settings"), &GetWorldSettings, {});
	Registry.RegisterHandler(TEXT("set_world_settings"), &SetWorldSettings, {
		MCPParam::Optional(TEXT("defaultGameMode"), EType::String, TEXT("GameMode class path or short name; None clears it")),
		MCPParam::Optional(TEXT("killZ"), EType::Number, TEXT("KillZ height")),
		MCPParam::Optional(TEXT("globalGravityZ"), EType::Number, TEXT("Global gravity Z")),
		MCPParam::Optional(TEXT("enableWorldBoundsChecks"), EType::Boolean, TEXT("Enable world bounds checks")),
	});
	Registry.RegisterHandler(TEXT("set_fog_properties"), &SetFogProperties, {
		SpecActorLabel, SpecActorPath, SpecWorld, SpecPieInstance,
		MCPParam::Optional(TEXT("fogDensity"), EType::Number, TEXT("Fog density")),
		MCPParam::Optional(TEXT("fogHeightFalloff"), EType::Number, TEXT("Fog height falloff")),
		MCPParam::Optional(TEXT("startDistance"), EType::Number, TEXT("Fog start distance")),
		MCPParam::Optional(TEXT("fogInscatteringColor"), EType::Object, TEXT("Inscattering colour {r, g, b} in 0-255")).Alias(TEXT("color")),
		MCPParam::Optional(TEXT("enableVolumetricFog"), EType::Boolean, TEXT("Enable volumetric fog")),
		MCPParam::Optional(TEXT("volumetricFogScatteringDistribution"), EType::Number, TEXT("Volumetric fog scattering distribution")),
		MCPParam::Optional(TEXT("volumetricFogExtinctionScale"), EType::Number, TEXT("Volumetric fog extinction scale")),
		MCPParam::Optional(TEXT("volumetricFogDistance"), EType::Number, TEXT("Volumetric fog distance")),
		MCPParam::Optional(TEXT("volumetricFogAlbedo"), EType::Object, TEXT("Volumetric fog albedo {r, g, b} in 0-255")),
	});
	Registry.RegisterHandler(TEXT("get_actors_by_class"), &GetActorsByClass, {
		MCPParam::Optional(TEXT("className"), EType::String, TEXT("Class name, /Script path or Blueprint class path; required without labelPrefix")),
		SpecLabelPrefix, SpecWorld, SpecPieInstance, SpecMatchSubclasses,
		MCPParam::Optional(TEXT("includeTransforms"), EType::Boolean, TEXT("Include each actor's location, rotation and scale (default true)")),
	});
	Registry.RegisterHandler(TEXT("get_actors_by_component_class"), &GetActorsByComponentClass, {
		MCPParam::Required(TEXT("componentClass"), EType::String, TEXT("Component class name, exact or substring")).Alias(TEXT("className")),
		SpecWorld, SpecPieInstance,
	});
	// Same budget as query_components, and for the same reason: this is a full
	// TActorIterator pass on the game thread. It additionally sorts every actor
	// by path name and sorts each actor's components, so it is the heavier of
	// the two whole-map scans and had no business inheriting the 30 second
	// default. Mirrored in src/bridge-timeouts.ts, which a parity test checks.
	Registry.RegisterHandlerWithTimeout(TEXT("summarize_static_mesh_usage"), &SummarizeStaticMeshUsage, 300.0f, {
		SpecWorld, SpecPieInstance,
		MCPParam::Optional(TEXT("maxResults"), EType::Integer, TEXT("Cap on result rows; full-scan totals are still reported")),
		MCPParam::Optional(TEXT("includeOccurrences"), EType::Boolean, TEXT("Include example actor and component occurrences per mesh")),
		MCPParam::Optional(TEXT("maxOccurrences"), EType::Integer, TEXT("Cap on occurrence examples per mesh")),
	});
	Registry.RegisterHandler(TEXT("count_actors_by_class"), &CountActorsByClass, {
		SpecWorld, SpecPieInstance,
		MCPParam::Optional(TEXT("topN"), EType::Integer, TEXT("Only the N most common classes")),
	});
	Registry.RegisterHandler(TEXT("get_runtime_virtual_texture_summary"), &GetRVTSummary, {
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("set_water_body_property"), &SetWaterBodyProperty, {
		SpecActorLabel, SpecActorPath, SpecPropertyName,
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("Value to write: string, number or boolean")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("rebuild_water_zone"), &RebuildWaterZone, 120.0f, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Optional(TEXT("zoneExtent"), EType::Any, TEXT("New WaterZone ZoneExtent in cm, {x, y} or [x, y]")),
		MCPParam::Optional(TEXT("tileSize"), EType::Number, TEXT("New WaterMesh TileSize in cm")),
		MCPParam::Optional(TEXT("maxPasses"), EType::Integer, TEXT("Rebuild passes before giving up on a stable QuadTreeResolution, 2..8 (default 4)")),
	});
	Registry.RegisterHandler(TEXT("get_water_state"), &GetWaterState, {
		SpecActorLabel, SpecActorPath,
	});
	Registry.RegisterHandler(TEXT("get_actor_bounds"), &GetActorBounds, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Optional(TEXT("onlyColliding"), EType::Boolean, TEXT("Only colliding components contribute to the bounds")),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("resolve_actor"), &ResolveActor, {
		MCPParam::Required(TEXT("internalName"), EType::String, TEXT("Internal UObject name, such as StaticMeshActor_141")),
	});
	Registry.RegisterHandler(TEXT("set_actor_property"), &SetActorProperty, {
		SpecActorLabel, SpecActorPath, SpecPropertyName,
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("Value to write")),
		MCPParam::Optional(TEXT("force"), EType::Boolean, TEXT("Bypass EditDefaultsOnly to write a per-instance override")),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("line_trace"), &LineTrace, {
		MCPParam::Optional(TEXT("start"), EType::Vec3, TEXT("Ray start")),
		MCPParam::Optional(TEXT("end"), EType::Vec3, TEXT("Ray end; pass end or direction and distance")),
		MCPParam::Optional(TEXT("direction"), EType::Vec3, TEXT("Ray direction, normalised internally")),
		MCPParam::Optional(TEXT("distance"), EType::Number, TEXT("Ray length when direction is given (default 200000)")),
		MCPParam::Optional(TEXT("traceComplex"), EType::Boolean, TEXT("Trace per-triangle collision (default false)")),
		MCPParam::Optional(TEXT("channel"), EType::String, TEXT("Collision channel (default Visibility)")),
		MCPParam::Optional(TEXT("ignoreActors"), EType::Array, TEXT("Actor labels to skip")).Items(EType::String),
		SpecWorld, SpecPieInstance,
	});
	Registry.RegisterHandler(TEXT("bulk_line_trace"), &BulkLineTrace, {
		MCPParam::Required(TEXT("traces"), EType::Array, TEXT("Line traces, 1 to 256, each read the way line_trace reads its parameters")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("start"), EType::Vec3, TEXT("Ray start")),
			MCPParam::OptionalField(TEXT("end"), EType::Vec3, TEXT("Ray end; pass end or direction and distance")),
			MCPParam::OptionalField(TEXT("direction"), EType::Vec3, TEXT("Ray direction, normalised internally")),
			MCPParam::OptionalField(TEXT("distance"), EType::Number, TEXT("Ray length when direction is given (default 200000)")),
			MCPParam::OptionalField(TEXT("traceComplex"), EType::Boolean, TEXT("Trace per-triangle collision (default false)")),
			MCPParam::OptionalField(TEXT("channel"), EType::String, TEXT("Collision channel (default Visibility)")),
			MCPParam::OptionalField(TEXT("ignoreActors"), EType::Array, TEXT("Actor labels to skip")).Items(EType::String),
		}),
		SpecWorld, SpecPieInstance,
	});
	// #453: per-actor motion snapshot for telemetry probes. Reads location,
	// rotation, velocity, angular velocity, scale, and ground state in one
	// call. Caller is expected to invoke at the desired sample interval.
	Registry.RegisterHandler(TEXT("read_actor_motion"), &ReadActorMotion, {
		SpecActorLabel, SpecActorLabels, SpecActorPath,
		MCPParam::Optional(TEXT("actorPaths"), EType::Array, TEXT("Full actor object paths")).Items(EType::String),
		MCPParam::Optional(TEXT("world"), EType::String, TEXT("World scope: auto (default, PIE when running) | editor | pie")),
		SpecPieInstance,
	});
	// #434: bulk-add transforms to a HISMC / ISMC component (Python crashes).
	const FMCPParamSpec SpecTransforms = MCPParam::Required(TEXT("transforms"), EType::Array, TEXT("Transforms {location, rotation?, scale?} to add")).Items(EType::Object);
	Registry.RegisterHandler(TEXT("add_hismc_instances"), &AddHismcInstances, {
		SpecActorLabel, SpecActorPath, SpecComponentName, SpecTransforms, SpecWorldSpace,
	});
	Registry.RegisterHandler(TEXT("add_instances"), &AddHismcInstances, {
		SpecActorLabel, SpecActorPath, SpecComponentName, SpecTransforms, SpecWorldSpace,
	});
	// #697: read/update/remove existing instances on an ISMC/HISMC.
	Registry.RegisterHandler(TEXT("get_instance_transforms"), &GetInstanceTransforms, {
		SpecActorLabel, SpecActorPath, SpecComponentName, SpecWorldSpace,
	});
	Registry.RegisterHandler(TEXT("update_instance_transform"), &UpdateInstanceTransform, {
		SpecActorLabel, SpecActorPath, SpecComponentName, SpecIndex, SpecWorldSpace, SpecLocation, SpecRotation, SpecScale,
	});
	Registry.RegisterHandler(TEXT("remove_instance"), &RemoveInstance, {
		SpecActorLabel, SpecActorPath, SpecComponentName, SpecIndex,
	});
	// Native bridge method only in this plugin-scoped change. It appears in
	// get_bridge_capabilities.actions and can be called directly over JSON-RPC
	// (or a UeMcpTask bridge.call). A first-class category action also requires a
	// server schema wrapper, which intentionally lives outside this plugin.
	Registry.RegisterHandlerWithTimeout(TEXT("snap_instances_to_surface"), &SnapInstancesToSurface, 300.0f, {
		SpecActorLabel, SpecActorPath, SpecComponentName,
		MCPParam::Optional(TEXT("instanceIndices"), EType::Array, TEXT("Instance indices to project; omit for every instance")).Items(EType::Integer),
		MCPParam::Optional(TEXT("maxInstances"), EType::Integer, TEXT("Cap on instances processed in one call")),
		MCPParam::Optional(TEXT("direction"), EType::Vec3, TEXT("Trace direction (default straight down)")),
		MCPParam::Optional(TEXT("traceStartOffset"), EType::Number, TEXT("Height above each instance to begin the trace")),
		MCPParam::Optional(TEXT("traceDistance"), EType::Number, TEXT("Maximum trace length")),
		MCPParam::Optional(TEXT("surfaceOffset"), EType::Number, TEXT("Offset along the surface normal after the hit")),
		MCPParam::Optional(TEXT("onMiss"), EType::String, TEXT("error (default, aborts the batch) | skip")),
		MCPParam::Optional(TEXT("surfaceActorClass"), EType::String, TEXT("Only accept hits on actors of this class")),
		MCPParam::Optional(TEXT("surfaceActorLabels"), EType::Array, TEXT("Only accept hits on actors with these labels")).Items(EType::String),
		MCPParam::Optional(TEXT("channel"), EType::String, TEXT("Collision channel (default Visibility)")),
		MCPParam::Optional(TEXT("traceComplex"), EType::Boolean, TEXT("Trace per-triangle collision (default false)")),
		SpecDryRun,
	});
	// #696: enable + force-build Nanite on a static mesh.
	Registry.RegisterHandler(TEXT("set_nanite_settings"), &SetNaniteSettings, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("StaticMesh asset path")).Alias(TEXT("meshPath")),
		MCPParam::Optional(TEXT("enabled"), EType::Boolean, TEXT("Enable Nanite (default true)")),
		MCPParam::Optional(TEXT("positionPrecision"), EType::Integer, TEXT("Nanite position precision")),
	});
	Registry.RegisterHandler(TEXT("get_nanite_info"), &GetNaniteInfo, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("StaticMesh asset path")).Alias(TEXT("meshPath")),
	});
	// #679/#677: spawn a SkeletalMeshActor for visual/deform verification. The
	// mesh load fails first under the contract values. skeletalMesh is nullable
	// only because the category shares the key with set_component_skeletal_mesh.
	const TArray<FMCPParamSpec> SpawnSkeletalSpec = {
		MCPParam::Optional(TEXT("skeletalMesh"), EType::String, TEXT("SkeletalMesh asset path to spawn; null is refused here")).Nullable(),
		MCPParam::Optional(TEXT("meshPath"), EType::String, TEXT("The older spelling of skeletalMesh")),
		SpecLabel, SpecOnConflict,
		MCPParam::Optional(TEXT("transform"), EType::Object, TEXT("Spawn transform; location, rotation and scale given on their own win over its parts")).WithFields({
			MCPParam::OptionalField(TEXT("location"), EType::Vec3, TEXT("World location")),
			MCPParam::OptionalField(TEXT("rotation"), EType::Rotator, TEXT("World rotation")),
			MCPParam::OptionalField(TEXT("scale"), EType::Vec3, TEXT("Actor scale")),
		}),
		SpecLocation, SpecRotation, SpecScale,
		MCPParam::Optional(TEXT("materials"), EType::Array, TEXT("Per-slot component material override paths; index is the slot, and a null or empty entry leaves that slot alone")),
		MCPParam::Optional(TEXT("animSequence"), EType::String, TEXT("Single-node preview animation")),
		MCPParam::Optional(TEXT("loop"), EType::Boolean, TEXT("Loop the preview animation (default true)")),
	};
	const FMCPSpecRules SpawnSkeletalRules = MCPSpec::ExactlyOne({ { TEXT("skeletalMesh") }, { TEXT("meshPath") } });
	Registry.RegisterHandler(TEXT("spawn_skeletal_mesh_actor"), &SpawnSkeletalMeshActor, SpawnSkeletalSpec, SpawnSkeletalRules);
	// Called once per selector by the contract test; the actor lookup fails first.
	Registry.RegisterHandler(TEXT("set_component_skeletal_mesh"), &SetComponentSkeletalMesh, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("skeletalMesh"), EType::String, TEXT("SkeletalMesh asset path, or null to clear the mesh")).Nullable(),
		MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Skinned mesh component (default: the first on the actor)")),
		SpecWorld, SpecPieInstance,
	}, MCPSpec::ExactlyOne({ { TEXT("actorLabel") }, { TEXT("actorPath") } }));
	// #666: add a material blendable to a PostProcessVolume.
	Registry.RegisterHandler(TEXT("add_post_process_blendable"), &AddPostProcessBlendable, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("materialPath"), EType::String, TEXT("Material to add as a blendable")).Alias(TEXT("material")),
		MCPParam::Optional(TEXT("weight"), EType::Number, TEXT("Blend weight (default 1)")),
	});
	// #950: LevelHandlers_PostProcess.cpp. The value half and the bOverride_ half
	// of FPostProcessSettings, written together.
	Registry.RegisterHandler(TEXT("set_post_process_settings"), &SetPostProcessSettings, {
		SpecActorLabel, SpecActorPath, SpecPostProcessComponent, SpecPostProcessProperty,
		MCPParam::Required(TEXT("settings"), EType::Object, TEXT("Setting name to value; each setting's bOverride flag is enabled too")),
		MCPParam::Optional(TEXT("enableOverrides"), EType::Boolean, TEXT("Enable each written setting's bOverride flag (default true)")),
	});
	Registry.RegisterHandler(TEXT("get_post_process_settings"), &GetPostProcessSettings, {
		SpecActorLabel, SpecActorPath, SpecPostProcessComponent, SpecPostProcessProperty,
		MCPParam::Optional(TEXT("onlyOverridden"), EType::Boolean, TEXT("Only settings whose bOverride flag is on")),
		MCPParam::Optional(TEXT("nameContains"), EType::String, TEXT("Substring over the setting name")),
		MCPParam::Optional(TEXT("names"), EType::Array, TEXT("Exact setting names to return")).Items(EType::String),
	});
	Registry.RegisterHandler(TEXT("set_fixed_exposure"), &SetFixedExposure, {
		SpecActorLabel, SpecActorPath, SpecPostProcessComponent, SpecPostProcessProperty,
		MCPParam::Required(TEXT("exposure"), EType::Number, TEXT("Fixed adaptation brightness, written to both min and max")).Alias(TEXT("brightness")),
		MCPParam::Optional(TEXT("bias"), EType::Number, TEXT("AutoExposureBias (exposure compensation)")),
	});
	// #637: export a selected actor's mesh to FBX + metadata sidecar.
	Registry.RegisterHandler(TEXT("export_actor_fbx"), &ExportActorFbx, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("outputPath"), EType::String, TEXT("Output .fbx path")).Alias(TEXT("filePath")),
	});
	Registry.RegisterHandler(TEXT("snap_actor_to_floor"), &SnapActorToFloor, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Optional(TEXT("floorOffset"), EType::Number, TEXT("Vertical offset added to the impact Z")),
		MCPParam::Optional(TEXT("maxDistance"), EType::Number, TEXT("Downward trace length (default 100000)")),
		SpecWorld, SpecPieInstance,
	});
	// The contract values match no actor, and the delete loop then has nothing to destroy.
	Registry.RegisterHandler(TEXT("delete_actors"), &DeleteActors, {
		SpecLabelPrefix, SpecLabelContains,
		MCPParam::Optional(TEXT("nameContains"), EType::String, TEXT("Case-insensitive substring over the actor's internal name")),
		MCPParam::Optional(TEXT("className"), EType::String, TEXT("Case-sensitive substring over the class name")),
		SpecSelectorTag,
		MCPParam::Optional(TEXT("classPathContains"), EType::String, TEXT("Case-insensitive substring of the generated class path")),
		MCPParam::Optional(TEXT("classPathContainsAny"), EType::Array, TEXT("Match when the class path contains any of these case-insensitive substrings")).Items(EType::String),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Report the matches without deleting them (default false)")),
	}, MCPSpec::AtLeastOne({
		{ TEXT("labelPrefix") }, { TEXT("labelContains") }, { TEXT("nameContains") }, { TEXT("className") },
		{ TEXT("tag") }, { TEXT("classPathContains") }, { TEXT("classPathContainsAny") },
	}));
	// Its onMissing check refuses the contract value before any map is loaded.
	Registry.RegisterHandlerWithTimeout(TEXT("delete_exact_labeled_actors_in_levels"), &DeleteExactLabeledActorsInLevels, 300.0f, {
		MCPParam::Required(TEXT("levels"), EType::Array, TEXT("Per-map targets, at most 16")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("levelPath"), EType::String, TEXT("Long package name of the .umap, such as /Game/Maps/Arena")),
			MCPParam::RequiredField(TEXT("actorLabels"), EType::Array, TEXT("Exact editor labels to delete in that map, at most 256, no duplicates")).Items(EType::String),
			MCPParam::OptionalField(TEXT("expectedClassPath"), EType::String, TEXT("Actor class every matched actor must be, such as /Script/Engine.StaticMeshActor")),
		}),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Preview without deleting (default TRUE)")),
		MCPParam::Optional(TEXT("onMissing"), EType::String, TEXT("error (default) | ignore")),
		MCPParam::Optional(TEXT("restoreOriginalLevel"), EType::Boolean, TEXT("Reopen the map that was open before the call (default true)")),
	});
	Registry.RegisterHandler(TEXT("set_actor_folder_path"), &SetActorFolderPath, {
		MCPParam::Required(TEXT("folderPath"), EType::String, TEXT("World Outliner folder; an empty string moves the actors to the root")),
		SpecActorLabels, SpecLabelPrefix,
		MCPParam::Optional(TEXT("className"), EType::String, TEXT("Case-sensitive substring over the class name")),
		SpecSelectorTag,
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Report the matches without moving them (default false)")),
		SpecTransactionLabel,
	}, MCPSpec::AtLeastOne({ { TEXT("actorLabels") }, { TEXT("labelPrefix") }, { TEXT("className") }, { TEXT("tag") } })
		.ContractExempt(TEXT("Opens an undo transaction and runs its write loop under the contract values, which match no actor; nothing it reads fails first")));
	const FMCPParamSpec SpecDescFilter = MCPParam::Optional(TEXT("filter"), EType::String, TEXT("Case-insensitive substring over label, name, class and path"));
	const FMCPParamSpec SpecDescClassName = MCPParam::Optional(TEXT("className"), EType::String, TEXT("Actor class filter"));
	const FMCPParamSpec SpecDescGuids = MCPParam::Optional(TEXT("guids"), EType::Array, TEXT("Exact actor GUIDs")).Items(EType::String);
	const FMCPParamSpec SpecDescBounds = MCPParam::Optional(TEXT("bounds"), EType::Object, TEXT("{min:{x,y,z}, max:{x,y,z}} intersection test"));
	const FMCPParamSpec SpecDescLoadedOnly = MCPParam::Optional(TEXT("loadedOnly"), EType::Boolean, TEXT("Only actors currently streamed in"));
	const FMCPParamSpec SpecDescUnloadedOnly = MCPParam::Optional(TEXT("unloadedOnly"), EType::Boolean, TEXT("Only actors on disk that are not streamed in"));
	Registry.RegisterHandler(TEXT("list_actor_descs"), &ListActorDescs, {
		SpecDescFilter, SpecDescClassName, SpecDescGuids, SpecDescBounds, SpecDescLoadedOnly, SpecDescUnloadedOnly,
		SpecCursor, SpecLimit,
	});
	// Its mode check refuses the contract value before anything is pinned.
	Registry.RegisterHandlerWithTimeout(TEXT("load_actor_descs"), &LoadActorDescs, 300.0f, {
		MCPParam::Optional(TEXT("mode"), EType::String, TEXT("pin (make resident, default) | unpin (release)")),
		SpecDescFilter, SpecDescClassName, SpecDescGuids, SpecDescBounds, SpecDescLoadedOnly, SpecDescUnloadedOnly,
		MCPParam::Optional(TEXT("maxActors"), EType::Integer, TEXT("Refuse to act on more than this many actors (default 256)")),
		SpecDryRun,
	});
	// #985: LevelHandlers_WorldPartitionSettings.cpp. The streaming knobs and
	// the runtime cell transformer stack live with the other World Partition
	// actions rather than in a category of their own.
	Registry.RegisterHandler(TEXT("get_world_partition_settings"), &GetWorldPartitionSettings, {});
	// A contract grid path names no grid and a zero cell size is refused, so nothing is written.
	Registry.RegisterHandler(TEXT("set_world_partition_settings"), &SetWorldPartitionSettings, {
		MCPParam::Optional(TEXT("cellSize"), EType::Number, TEXT("Streaming cell size in world centimetres")),
		MCPParam::Optional(TEXT("loadingRange"), EType::Number, TEXT("Streaming loading range in world centimetres")),
		MCPParam::Optional(TEXT("settings"), EType::Object, TEXT("Dotted path rooted at the world partition to value")),
		MCPParam::Optional(TEXT("grid"), EType::String, TEXT("Streaming grid for cellSize and loadingRange, by name; needed when the map has several")),
		MCPParam::Optional(TEXT("gridPath"), EType::String, TEXT("Streaming grid for cellSize and loadingRange, by the dotted path get_world_partition_settings reports")),
	}, MCPSpec::AtLeastOne({ { TEXT("cellSize") }, { TEXT("loadingRange") }, { TEXT("settings") } }));
	// The contract class resolves to nothing and is refused before the stack is touched.
	Registry.RegisterHandler(TEXT("add_runtime_cell_transformer"), &AddRuntimeCellTransformer, {
		MCPParam::Required(TEXT("transformerClass"), EType::String, TEXT("WorldPartitionRuntimeCellTransformer subclass: short name, Module.Class or /Script path")).Alias(TEXT("className")),
		MCPParam::Optional(TEXT("properties"), EType::Object, TEXT("Property name to value, applied to the new transformer instance")),
		MCPParam::Optional(TEXT("position"), EType::Integer, TEXT("Stack index to insert at; -1 (default) appends")),
		MCPParam::Optional(TEXT("skipIfPresent"), EType::Boolean, TEXT("Report an existing transformer of this class instead of adding a duplicate (default true)")),
	});
	// #985: bulk HLOD layer assignment. A whole-map selector, so it takes the
	// same 300 second budget as the other batch writes. Mirrored in
	// src/bridge-timeouts.ts, which a parity test checks.
	// The layer load fails first under the contract values, so the handler reads
	// its selector ahead; the contract test calls it once per selector.
	Registry.RegisterHandlerWithTimeout(TEXT("set_actor_hlod_layer"), &SetActorHLODLayer, 300.0f, {
		MCPParam::Required(TEXT("hlodLayer"), EType::String, TEXT("HLODLayer asset path, or null to clear the per-actor override")).Nullable(),
		SpecActorLabels, SpecLabelPrefix, SpecLabelContains, SpecSelectorTag,
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Actor class, resolved as a class or matched as a substring")),
		SpecFolderPath, SpecFolderPathPrefix, SpecMatchSubclasses,
		MCPParam::Optional(TEXT("enableAutoLODGeneration"), EType::Boolean, TEXT("Also set bEnableAutoLODGeneration on each matched actor")),
		SpecDryRun, SpecTransactionLabel,
	}, MCPSpec::AtLeastOne({
		{ TEXT("actorLabels") }, { TEXT("labelPrefix") }, { TEXT("labelContains") }, { TEXT("tag") },
		{ TEXT("classFilter") }, { TEXT("folderPath") }, { TEXT("folderPathPrefix") },
	}));
	Registry.RegisterHandler(TEXT("add_actor_tag"), &AddActorTag, {
		SpecActorLabel, SpecActorPath, SpecTag,
	});
	Registry.RegisterHandler(TEXT("remove_actor_tag"), &RemoveActorTag, {
		SpecActorLabel, SpecActorPath, SpecTag,
	});
	Registry.RegisterHandler(TEXT("set_actor_tags"), &SetActorTags, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("tags"), EType::Array, TEXT("The actor's complete tag list")).Items(EType::String),
	});
	Registry.RegisterHandler(TEXT("list_actor_tags"), &ListActorTags, {
		SpecActorLabel, SpecActorPath, SpecCursor, SpecLimit,
	});
	Registry.RegisterHandler(TEXT("attach_actor"), &AttachActor, {
		SpecChildLabel, SpecChildPath, SpecParentLabel, SpecParentPath, SpecAttachRule, SpecSocketName,
	});
	Registry.RegisterHandler(TEXT("detach_actor"), &DetachActor, {
		SpecChildLabel, SpecChildPath,
	});
	Registry.RegisterHandler(TEXT("attach_component"), &AttachComponent, {
		SpecChildLabel, SpecChildPath, SpecParentLabel, SpecParentPath,
		MCPParam::Optional(TEXT("childComponentName"), EType::String, TEXT("Child SceneComponent instance name; omitted selects the actor root")),
		MCPParam::Optional(TEXT("parentComponentName"), EType::String, TEXT("Parent SceneComponent instance name; omitted selects the actor root")),
		SpecAttachRule,
		MCPParam::Optional(TEXT("weldSimulatedBodies"), EType::Boolean, TEXT("Weld simulated bodies during attachment (default false)")),
		SpecSocketName,
	});
	Registry.RegisterHandler(TEXT("detach_component"), &DetachComponent, {
		SpecChildLabel, SpecChildPath,
		MCPParam::Optional(TEXT("childComponentName"), EType::String, TEXT("Child SceneComponent instance name; omitted selects the actor root")),
	});
	Registry.RegisterHandler(TEXT("set_actor_mobility"), &SetActorMobility, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("mobility"), EType::String, TEXT("static | stationary | movable")),
	});
	Registry.RegisterHandler(TEXT("get_current_edit_level"), &GetCurrentEditLevel, {});
	Registry.RegisterHandler(TEXT("set_current_edit_level"), &SetCurrentEditLevel, {
		MCPParam::Required(TEXT("levelName"), EType::String, TEXT("Loaded sub-level to make current")).Alias(TEXT("levelPath")),
	});
	Registry.RegisterHandler(TEXT("list_streaming_sublevels"), &ListStreamingSublevels, {});
	Registry.RegisterHandler(TEXT("add_streaming_sublevel"), &AddStreamingSublevel, {
		MCPParam::Required(TEXT("levelPath"), EType::String, TEXT("Map package to add as a streaming sub-level")),
		MCPParam::Optional(TEXT("streamingClass"), EType::String, TEXT("LevelStreamingDynamic (default) | LevelStreamingAlwaysLoaded")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("Sub-level offset")),
		MCPParam::Optional(TEXT("initiallyLoaded"), EType::Boolean, TEXT("Load the sub-level with the persistent level")),
		MCPParam::Optional(TEXT("initiallyVisible"), EType::Boolean, TEXT("Make the sub-level visible when loaded")),
	}, MCPSpec::ContractExempt(TEXT("Hands the contract path to AddLevelToWorld, which loads the package; nothing it reads fails first")));
	Registry.RegisterHandler(TEXT("remove_streaming_sublevel"), &RemoveStreamingSublevel, {
		SpecLevelName,
	});
	Registry.RegisterHandler(TEXT("set_streaming_sublevel_properties"), &SetStreamingSublevelProperties, {
		SpecLevelName,
		MCPParam::Optional(TEXT("initiallyLoaded"), EType::Boolean, TEXT("Load the sub-level with the persistent level")),
		MCPParam::Optional(TEXT("initiallyVisible"), EType::Boolean, TEXT("Make the sub-level visible when loaded")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("Sub-level offset")),
		MCPParam::Optional(TEXT("editorVisible"), EType::Boolean, TEXT("Editor viewport visibility")),
	});
	Registry.RegisterHandler(TEXT("spawn_grid"), &SpawnGrid, {
		MCPParam::Required(TEXT("staticMesh"), EType::String, TEXT("Static mesh to place")),
		MCPParam::Required(TEXT("min"), EType::Vec3, TEXT("Grid lower bound")),
		MCPParam::Required(TEXT("max"), EType::Vec3, TEXT("Grid upper bound")),
		MCPParam::Optional(TEXT("countX"), EType::Integer, TEXT("Actors along X (default 4)")),
		MCPParam::Optional(TEXT("countY"), EType::Integer, TEXT("Actors along Y (default 4)")),
		MCPParam::Optional(TEXT("countZ"), EType::Integer, TEXT("Actors along Z (default 1)")),
		MCPParam::Optional(TEXT("jitter"), EType::Number, TEXT("Per-axis location jitter")),
		MCPParam::Optional(TEXT("labelPrefix"), EType::String, TEXT("Label prefix for the spawned actors (default Grid)")),
	});
	// The contract selectors match no actor, which is refused before anything moves.
	Registry.RegisterHandler(TEXT("batch_translate"), &BatchTranslate, {
		MCPParam::Required(TEXT("offset"), EType::Vec3, TEXT("World-space offset added to each actor's location")),
		SpecActorLabels, SpecActorPaths, SpecSelectorTag,
	}, MCPSpec::AtLeastOne({ { TEXT("actorLabels") }, { TEXT("actorPaths") }, { TEXT("tag") } }));
	// The contract's empty actors list spawns nothing.
	Registry.RegisterHandler(TEXT("place_actors_batch"), &PlaceActorsBatch, {
		MCPParam::Required(TEXT("actors"), EType::Array, TEXT("StaticMeshActors to spawn; meshes load once per path")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("staticMesh"), EType::String, TEXT("Static mesh asset path")),
			MCPParam::OptionalField(TEXT("location"), EType::Vec3, TEXT("World location")),
			MCPParam::OptionalField(TEXT("rotation"), EType::Rotator, TEXT("World rotation")),
			MCPParam::OptionalField(TEXT("scale"), EType::Vec3, TEXT("World scale")),
			MCPParam::OptionalField(TEXT("label"), EType::String, TEXT("Actor label")),
		}),
	});
	// #910/#943/#912: the general editor-side component query. A whole-map
	// scan with a projection can take a while on a 4,000 actor level, so it
	// gets its own timeout rather than the 30 second default. The contract's
	// whereMode is refused before levelPath could open another map.
	Registry.RegisterHandlerWithTimeout(TEXT("query_components"), &QueryComponents, 300.0f, {
		MCPParam::Optional(TEXT("componentClass"), EType::String, TEXT("Component class, resolved as a class or matched as a substring")),
		MCPParam::Optional(TEXT("actorClass"), EType::String, TEXT("Owning actor class, resolved as a class or matched as a substring")),
		SpecMatchSubclasses,
		MCPParam::Optional(TEXT("componentNameContains"), EType::String, TEXT("Case-insensitive substring over the component instance name")),
		MCPParam::Optional(TEXT("actorLabelPrefix"), EType::String, TEXT("Case-sensitive prefix over the actor's editor label")),
		MCPParam::Optional(TEXT("actorLabelContains"), EType::String, TEXT("Case-insensitive substring over the actor's editor label")),
		MCPParam::Optional(TEXT("actorTag"), EType::String, TEXT("Actor must carry this tag")),
		SpecFolderPath, SpecFolderPathPrefix,
		MCPParam::Optional(TEXT("fields"), EType::Array, TEXT("Field groups: transform, bounds, localBounds, shadow, nanite, navigation, tick, materials, mesh, decal, health")).Items(EType::String),
		MCPParam::Optional(TEXT("propertyNames"), EType::Array, TEXT("Component UPROPERTY names projected under props.*, at most 32")).Items(EType::String),
		MCPParam::Optional(TEXT("where"), EType::Array, TEXT("Predicates evaluated in the editor, at most 24")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("field"), EType::String, TEXT("Dot path into the row, such as shadow.effectiveCastShadow or props.CullDistance")),
			MCPParam::OptionalField(TEXT("op"), EType::String, TEXT("eq (default) | ne | lt | lte | gt | gte | contains | notContains | startsWith | endsWith | in | notIn | exists | notExists | isNull | isNotNull | isTrue | isFalse")),
			MCPParam::OptionalField(TEXT("value"), EType::Any, TEXT("Value the operator compares against")),
		}),
		MCPParam::Optional(TEXT("whereMode"), EType::String, TEXT("all (default) | any")),
		MCPParam::Optional(TEXT("suspectOnly"), EType::Boolean, TEXT("Shorthand for where health.suspect isTrue")),
		MCPParam::Optional(TEXT("groupBy"), EType::String, TEXT("Dot path to group matches by; counts components, not instances")),
		MCPParam::Optional(TEXT("countBy"), EType::Array, TEXT("Dot paths to build value histograms for, at most 8")).Items(EType::String),
		MCPParam::Optional(TEXT("sampleLimit"), EType::Integer, TEXT("Sample labels per group (default 5, max 25)")),
		MCPParam::Optional(TEXT("countOnly"), EType::Boolean, TEXT("Return the aggregates without rows")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows returned (default 200, max 2000)")),
		MCPParam::Optional(TEXT("startIndex"), EType::Integer, TEXT("First row index; rows are sorted so the index is stable across calls")),
		MCPParam::Optional(TEXT("duplicateTransformTolerance"), EType::Number, TEXT("Centimetre bucket for duplicate-transform detection")),
		MCPParam::Optional(TEXT("levelPath"), EType::String, TEXT("Query another map: opened temporarily, refused while anything is dirty, and the open map restored")),
		SpecWorld, SpecPieInstance,
	});
	// #984/#941/#907/#987: level-wide writes driven by an editor-side selector.
	// Each can touch thousands of actors, so each gets its own timeout.
	Registry.RegisterHandlerWithTimeout(TEXT("batch_set_actor_properties"), &BatchSetActorProperties, 300.0f, {
		MCPParam::Required(TEXT("properties"), EType::Object, TEXT("Property name to value, dotted paths supported")),
		SpecActorLabels, SpecLabelPrefix, SpecLabelContains, SpecSelectorTag,
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Actor class, resolved as a class or matched as a substring")),
		SpecFolderPath, SpecFolderPathPrefix, SpecMatchSubclasses, SpecDryRun,
		MCPParam::Optional(TEXT("force"), EType::Boolean, TEXT("Bypass EditDefaultsOnly to write per-instance overrides")),
		SpecTransactionLabel,
	});
	const FMCPParamSpec SpecClassFilter = MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Actor class, resolved as a class or matched as a substring"));
	const TArray<TArray<FString>> SelectorBranches = {
		{ TEXT("actorLabels") }, { TEXT("labelPrefix") }, { TEXT("labelContains") }, { TEXT("tag") },
		{ TEXT("classFilter") }, { TEXT("folderPath") }, { TEXT("folderPathPrefix") },
	};
	// The contract selectors match no actor, so no transaction opens and nothing is written.
	Registry.RegisterHandlerWithTimeout(TEXT("bulk_set_component_property"), &BulkSetComponentProperty, 300.0f, {
		MCPParam::Required(TEXT("componentName"), EType::String, TEXT("Component name looked up on every matched actor")),
		SpecPropertyName,
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("Value to write; null clears an object reference")),
		SpecActorLabels, SpecLabelPrefix, SpecLabelContains, SpecSelectorTag, SpecClassFilter,
		SpecFolderPath, SpecFolderPathPrefix, SpecMatchSubclasses, SpecDryRun, SpecTransactionLabel,
	}, MCPSpec::AtLeastOne(SelectorBranches));
	Registry.RegisterHandlerWithTimeout(TEXT("remove_components_by_class"), &RemoveComponentsByClass, 300.0f, {
		MCPParam::Required(TEXT("componentClass"), EType::String, TEXT("Component class to remove")),
		MCPParam::Optional(TEXT("matchComponentSubclasses"), EType::Boolean, TEXT("Also match subclasses of componentClass (default true)")),
		MCPParam::Optional(TEXT("componentNameContains"), EType::String, TEXT("Case-insensitive substring over the component instance name")),
		SpecActorLabels, SpecLabelPrefix, SpecLabelContains, SpecSelectorTag,
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Restrict to actors of this class")).Alias(TEXT("actorClassFilter")),
		SpecFolderPath, SpecFolderPathPrefix, SpecMatchSubclasses,
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Report what would be removed without removing it (default TRUE)")),
		MCPParam::Optional(TEXT("save"), EType::Boolean, TEXT("Save the level after a committed removal (default false)")),
		SpecTransactionLabel,
	});
	// The contract class resolves to nothing and is refused before anything spawns.
	Registry.RegisterHandlerWithTimeout(TEXT("spawn_actors_batch"), &SpawnActorsBatch, 300.0f, {
		MCPParam::Required(TEXT("actorClass"), EType::String, TEXT("Actor class: short name, /Script path or Blueprint class path")),
		MCPParam::Optional(TEXT("instances"), EType::Array, TEXT("Explicit spawns")).Items(EType::Object).WithFields({
			MCPParam::OptionalField(TEXT("location"), EType::Vec3, TEXT("World location")),
			MCPParam::OptionalField(TEXT("rotation"), EType::Rotator, TEXT("World rotation")),
			MCPParam::OptionalField(TEXT("scale"), EType::Vec3, TEXT("Actor scale")),
			MCPParam::OptionalField(TEXT("label"), EType::String, TEXT("Actor label")),
			MCPParam::OptionalField(TEXT("properties"), EType::Object, TEXT("Property name to value for this spawn; wins over the shared properties")),
		}),
		MCPParam::Optional(TEXT("fromComponents"), EType::Object, TEXT("One spawn per matched component, placed from its bounds")).WithFields({
			MCPParam::OptionalField(TEXT("componentClass"), EType::String, TEXT("Component class the components must be")),
			MCPParam::OptionalField(TEXT("componentNameContains"), EType::String, TEXT("Case-insensitive substring over the component name")),
			MCPParam::OptionalField(TEXT("actorLabels"), EType::Array, TEXT("Exact actor editor labels")).Items(EType::String),
			MCPParam::OptionalField(TEXT("labelPrefix"), EType::String, TEXT("Case-sensitive prefix over the actor's editor label")),
			MCPParam::OptionalField(TEXT("labelContains"), EType::String, TEXT("Case-insensitive substring over the actor's editor label")),
			MCPParam::OptionalField(TEXT("tag"), EType::String, TEXT("Actor must carry this tag")),
			MCPParam::OptionalField(TEXT("classFilter"), EType::String, TEXT("Actor class, resolved as a class or matched as a substring")),
			MCPParam::OptionalField(TEXT("folderPath"), EType::String, TEXT("World Outliner folder, matched exactly")),
			MCPParam::OptionalField(TEXT("folderPathPrefix"), EType::String, TEXT("World Outliner folder prefix")),
			MCPParam::OptionalField(TEXT("matchSubclasses"), EType::Boolean, TEXT("Match subclasses of classFilter (default true)")),
			MCPParam::OptionalField(TEXT("space"), EType::String, TEXT("local (default, the component's own orientation) | world (axis-aligned bounds)")),
			MCPParam::OptionalField(TEXT("offset"), EType::Vec3, TEXT("Offset added to the computed point")),
			MCPParam::OptionalField(TEXT("extentFraction"), EType::Vec3, TEXT("Fraction of the box extent added per axis, such as {z: 0.72}")),
			MCPParam::OptionalField(TEXT("inheritRotation"), EType::Boolean, TEXT("Spawn with the component's rotation (default false)")),
		}),
		MCPParam::Optional(TEXT("alongSpline"), EType::Object, TEXT("Spawns scattered along a spline by distance")).WithFields({
			MCPParam::OptionalField(TEXT("actorLabel"), EType::String, TEXT("Spline actor label; pass actorLabel or actorPath")),
			MCPParam::OptionalField(TEXT("actorPath"), EType::String, TEXT("Spline actor object path")),
			MCPParam::OptionalField(TEXT("componentName"), EType::String, TEXT("Spline component, when the actor has several")),
			MCPParam::RequiredField(TEXT("spacing"), EType::Number, TEXT("Distance between spawns in cm, at least 1")),
			MCPParam::OptionalField(TEXT("startDistance"), EType::Number, TEXT("Distance along the spline to start at (default 0)")),
			MCPParam::OptionalField(TEXT("endDistance"), EType::Number, TEXT("Distance along the spline to stop at (default its length)")),
			MCPParam::OptionalField(TEXT("offset"), EType::Vec3, TEXT("Offset, in the spline's frame when aligning to it")),
			MCPParam::OptionalField(TEXT("alignToTangent"), EType::Boolean, TEXT("Rotate each spawn to the spline (default true)")),
		}),
		MCPParam::Optional(TEXT("properties"), EType::Object, TEXT("Property name to value applied to every spawn")),
		MCPParam::Optional(TEXT("labelPrefix"), EType::String, TEXT("Label prefix for the spawned actors")),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Return every computed transform without spawning")),
		MCPParam::Optional(TEXT("maxSpawn"), EType::Integer, TEXT("Refuse a plan larger than this (default 500, max 5000)")),
		SpecTransactionLabel,
	}, MCPSpec::ExactlyOne({ { TEXT("instances") }, { TEXT("fromComponents") }, { TEXT("alongSpline") } }));
	// #944/#915/#914: refresh state the editor is caching, and read the bounds
	// a caller needs to check the result. The contract world scope is refused
	// before any actor is touched.
	Registry.RegisterHandlerWithTimeout(TEXT("rerun_construction_scripts"), &RerunConstruction, 300.0f, {
		SpecActorLabels,
		MCPParam::Optional(TEXT("className"), EType::String, TEXT("Blueprint or native class, resolved as a class or matched as a substring")),
		SpecMatchSubclasses, SpecWorld, SpecPieInstance,
	}, MCPSpec::AtLeastOne({ { TEXT("actorLabels") }, { TEXT("className") } }));
	Registry.RegisterHandlerWithTimeout(TEXT("recreate_physics_state"), &RecreatePhysicsState, 300.0f, {
		SpecActorLabels, SpecLabelPrefix, SpecSelectorTag,
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Actor class the owners must be")),
		MCPParam::Optional(TEXT("componentClass"), EType::String, TEXT("Primitive component class to rebuild")),
		MCPParam::Optional(TEXT("componentNameContains"), EType::String, TEXT("Case-insensitive substring over the component instance name")),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("List what would be rebuilt without rebuilding it (default TRUE)")),
		MCPParam::Optional(TEXT("maxComponents"), EType::Integer, TEXT("Refuse to rebuild more than this many components (default 2000, max 20000)")),
		SpecWorld, SpecPieInstance,
	}, MCPSpec::AtLeastOne({
		{ TEXT("actorLabels") }, { TEXT("labelPrefix") }, { TEXT("tag") }, { TEXT("classFilter") },
		{ TEXT("componentClass") }, { TEXT("componentNameContains") },
	}));
	Registry.RegisterHandler(TEXT("test_component_overlap"), &TestComponentOverlap, {
		MCPParam::Optional(TEXT("actorLabelA"), EType::String, TEXT("First actor label; pass actorLabelA or actorPathA")),
		MCPParam::Optional(TEXT("actorPathA"), EType::String, TEXT("First actor object path")),
		MCPParam::Optional(TEXT("actorLabelB"), EType::String, TEXT("Second actor label; pass actorLabelB or actorPathB")),
		MCPParam::Optional(TEXT("actorPathB"), EType::String, TEXT("Second actor object path")),
		MCPParam::Optional(TEXT("componentNameA"), EType::String, TEXT("Component on actor A; omitted selects its root")),
		MCPParam::Optional(TEXT("componentNameB"), EType::String, TEXT("Component on actor B; omitted selects its root")),
		MCPParam::Optional(TEXT("method"), EType::String, TEXT("OBB (oriented, default) | AABB (axis-aligned world bounds)")),
		SpecWorld, SpecPieInstance,
	});
	// #911: BSP to StaticMesh. Generating meshes for hundreds of brushes takes
	// far longer than the default handler timeout. The contract class filter
	// resolves to no brush class and is refused before anything converts.
	Registry.RegisterHandlerWithTimeout(TEXT("convert_brushes_to_static_mesh"), &ConvertBrushesToStaticMesh, 600.0f, {
		SpecActorLabels,
		MCPParam::Optional(TEXT("folderPath"), EType::String, TEXT("World Outliner folder whose brushes are converted")),
		MCPParam::Optional(TEXT("recursiveFolder"), EType::Boolean, TEXT("Also take brushes in folders nested under folderPath (default true)")),
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Brush class to convert")),
		MCPParam::Optional(TEXT("exactClass"), EType::Boolean, TEXT("Require classFilter to be the exact class (default true)")),
		MCPParam::Optional(TEXT("destinationPath"), EType::String, TEXT("Content path the generated static meshes are written to (default /Game/Meshes/Converted)")),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Report the verdict per brush without converting (default TRUE)")),
		MCPParam::Optional(TEXT("allowSubtractive"), EType::Boolean, TEXT("Also convert subtractive brushes, which have no surface of their own")),
		MCPParam::Optional(TEXT("includeVolumes"), EType::Boolean, TEXT("Also convert volume brushes, which are collision rather than visible geometry")),
	}, MCPSpec::AtLeastOne({ { TEXT("actorLabels") }, { TEXT("folderPath") } }));
	// #946: component-level material overrides on placed actors. The contract
	// selectors match no actor and the contract material does not load.
	Registry.RegisterHandlerWithTimeout(TEXT("set_component_materials"), &SetComponentMaterials, 300.0f, {
		SpecActorLabels, SpecLabelPrefix, SpecLabelContains, SpecSelectorTag, SpecClassFilter,
		SpecFolderPath, SpecFolderPathPrefix, SpecMatchSubclasses,
		MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Mesh component (default: the actor's first mesh component)")),
		MCPParam::Optional(TEXT("materials"), EType::Array, TEXT("Per-slot material paths; index is the slot, and a null or empty entry clears that slot's override")),
		MCPParam::Optional(TEXT("material"), EType::String, TEXT("One material path applied to every slot")),
		MCPParam::Optional(TEXT("clearOverrides"), EType::Boolean, TEXT("true drops every component override so the mesh asset's own slots show through")),
		SpecDryRun, SpecTransactionLabel,
	}, MCPSpec::AtLeastOne(SelectorBranches)
		.ExactlyOne({ { TEXT("materials") }, { TEXT("material") }, { TEXT("clearOverrides") } }));
	// #956: a transient verification subject, and the two actions that keep it
	// from being left behind.
	Registry.RegisterHandler(TEXT("spawn_transient_actor"), &SpawnTransientActor, {
		SpecWorld, SpecPieInstance,
		MCPParam::Required(TEXT("actorClass"), EType::String, TEXT("Actor class: short name, /Script path or Blueprint class path")),
		SpecLocation, SpecRotation, SpecScale,
		MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label")),
		MCPParam::Optional(TEXT("hideFromOutliner"), EType::Boolean, TEXT("Keep the actor out of the World Outliner (default false)")),
		MCPParam::Optional(TEXT("initialize"), EType::String, TEXT("none | construction (default) | beginPlay")),
		MCPParam::Optional(TEXT("properties"), EType::Object, TEXT("Property name to value, applied before initialisation")),
	});
	Registry.RegisterHandler(TEXT("destroy_transient_actor"), &DestroyTransientActor, {
		SpecWorld, SpecPieInstance,
		MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Transient actor object path")),
		MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Transient actor label")),
		MCPParam::Optional(TEXT("all"), EType::Boolean, TEXT("Destroy every transient verification actor in the world")),
	});
	Registry.RegisterHandler(TEXT("list_transient_actors"), &ListTransientActors, {
		SpecWorld, SpecPieInstance, SpecCursor, SpecLimit,
	});
}

TSharedPtr<FJsonValue> FLevelHandlers::GetOutliner(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("classFilter"), TEXT("exactClass"), TEXT("nameFilter"), TEXT("folderPath"), TEXT("folderPathPrefix"),
		TEXT("editorHidden"), TEXT("includeStreaming"), TEXT("world"), TEXT("pieInstance"), TEXT("cursor"),
		TEXT("limit"),
	});

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
	const bool bHasEditorHiddenFilter = TryGetBoolParam(Params, TEXT("editorHidden"), bEditorHiddenFilterValue);

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
		TSharedPtr<FJsonObject> LocationObj = MCPVec3ToJsonObject(Location);
		ActorObj->SetObjectField(TEXT("location"), LocationObj);

		FRotator Rotation = Actor->GetActorRotation();
		TSharedPtr<FJsonObject> RotationObj = MCPRotatorToJsonObject(Rotation);
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
	MCPReadParamsAhead(Params, {
		TEXT("hidden"), TEXT("actorLabels"), TEXT("all"),
	});

	REQUIRE_EDITOR_WORLD(World);

	bool bHidden = false;
	if (!TryGetBoolParam(Params, TEXT("hidden"), bHidden))
	{
		return MCPError(TEXT("Missing 'hidden' parameter (true = hide in editor, false = show)"));
	}

	const bool bAll = OptionalBool(Params, TEXT("all"), false);

	TSet<FString> TargetLabels;
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("actorLabels"), LabelsArr) && LabelsArr)
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
	MCPReadParamsAhead(Params, {
		TEXT("actorClass"), TEXT("label"), TEXT("onConflict"), TEXT("location"), TEXT("rotation"), TEXT("scale"),
		TEXT("staticMesh"), TEXT("material"), TEXT("world"), TEXT("pieInstance"),
	});

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

	UClass* Class = MCPResolveClass(ActorClass);
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

	// #1119: SpawnActor does not run the editor's volume factory, so native
	// volumes otherwise have no brush, bounds or collision. Keep any brush
	// supplied by the class; only initialize a missing or polygonless one with
	// the native 200 cm cube. The builder can snap location and resets scale, so restore
	// the spawned transform before applying the caller's scale below.
	AVolume* BuiltVolume = nullptr;
	if (AVolume* Volume = Cast<AVolume>(NewActor);
		Volume && (!Volume->Brush || !Volume->Brush->Polys || Volume->Brush->Polys->Element.Num() == 0))
	{
		const FTransform VolumeTransform = Volume->GetActorTransform();
		UEMCP::BuildVolumeAsCube(World, Volume, FVector(100.0));
		Volume->SetActorTransform(VolumeTransform);
		BuiltVolume = Volume;
	}

	if (!Label.IsEmpty())
	{
		NewActor->SetActorLabel(Label);
	}

	if (HasParam(Params, TEXT("scale")))
	{
		NewActor->SetActorScale3D(OptionalVec3(Params, TEXT("scale"), FVector::OneVector));
	}

	// Nav bounds are captured when registered, so re-register them against
	// the final transform, not the unscaled cube the builder produced.
	if (AVolume* PlacedVolume = Cast<AVolume>(NewActor))
	{
		if (BuiltVolume) BuiltVolume->PostEditChange();
		UEMCP::NotifyVolumeBoundsChanged(PlacedVolume);
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"),
	});

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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("includeProperties"), TEXT("propertyName"), TEXT("world"),
		TEXT("pieInstance"),
	});

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
		World = GetEditorWorld();
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
	TSharedPtr<FJsonObject> LocationObj = MCPVec3ToJsonObject(Location);
	Result->SetObjectField(TEXT("location"), LocationObj);

	FRotator Rot = Actor->GetActorRotation();
	TSharedPtr<FJsonObject> RotObj = MCPRotatorToJsonObject(Rot);
	Result->SetObjectField(TEXT("rotation"), RotObj);

	FVector Scale = Actor->GetActorScale3D();
	TSharedPtr<FJsonObject> ScaleObj = MCPVec3ToJsonObject(Scale);
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("includeProperties"), TEXT("componentClass"), TEXT("componentName"),
		TEXT("world"), TEXT("pieInstance"),
	});

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
	// #1113: address one component by instance name (case-insensitive).
	const FString ComponentNameFilter = OptionalString(Params, TEXT("componentName"));

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	TArray<TSharedPtr<FJsonValue>> CompArr;
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;
		if (!PropertyFilter.IsEmpty() && !Comp->GetClass()->GetName().Contains(PropertyFilter, ESearchCase::IgnoreCase)) continue;
		if (!ComponentNameFilter.IsEmpty() && !Comp->GetName().Equals(ComponentNameFilter, ESearchCase::IgnoreCase)) continue;

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
			C->SetObjectField(TEXT("relativeLocation"), MCPVec3ToJsonObject(RelLoc));
			C->SetObjectField(TEXT("relativeRotation"), MCPRotatorToJsonObject(RelRot));
			C->SetObjectField(TEXT("relativeScale"), MCPVec3ToJsonObject(RelScale));

			// World transform
			C->SetObjectField(TEXT("worldLocation"), MCPVec3ToJsonObject(SC->GetComponentLocation()));
			C->SetObjectField(TEXT("worldRotation"), MCPRotatorToJsonObject(SC->GetComponentRotation()));
			C->SetObjectField(TEXT("worldScale"), MCPVec3ToJsonObject(SC->GetComponentScale()));

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
				C->SetObjectField(TEXT("boundsOrigin"), MCPVec3ToJsonObject(Bounds.Origin));
				C->SetObjectField(TEXT("boundsBoxExtent"), MCPVec3ToJsonObject(Bounds.BoxExtent));
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
	MCPReadParamsAhead(Params, {
		TEXT("targetLabel"), TEXT("targetPath"), TEXT("referenceLabel"), TEXT("referencePath"), TEXT("world"),
		TEXT("pieInstance"),
	});

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World '%s' not available"), *WorldScope));

	// #983: both ends take a path. Two duplicated labels would otherwise
	// produce a relative transform between whichever pair the actor iterator
	// reached first, which is exactly the number this action exists to trust.
	FMCPActorSelector TargetSel;
	TargetSel.LabelKey = TEXT("targetLabel");
	TargetSel.PathKey = TEXT("targetPath");
	// target and reference are spec aliases, renamed before this runs (#1057).
	TSharedPtr<FJsonValue> ActorErr;
	AActor* TargetActor = MCPResolveActor(World, Params, ActorErr, TargetSel);
	if (!TargetActor) return ActorErr;

	FMCPActorSelector ReferenceSel;
	ReferenceSel.LabelKey = TEXT("referenceLabel");
	ReferenceSel.PathKey = TEXT("referencePath");
	AActor* ReferenceActor = MCPResolveActor(World, Params, ActorErr, ReferenceSel);
	if (!ReferenceActor) return ActorErr;

	const FString TargetLabel = TargetActor->GetActorLabel();
	const FString ReferenceLabel = ReferenceActor->GetActorLabel();

	const FTransform Target = TargetActor->GetActorTransform();
	const FTransform Reference = ReferenceActor->GetActorTransform();
	const FTransform Relative = Target.GetRelativeTransform(Reference);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("targetLabel"), TargetLabel);
	Result->SetStringField(TEXT("targetPath"), TargetActor->GetPathName());
	Result->SetStringField(TEXT("referenceLabel"), ReferenceLabel);
	Result->SetStringField(TEXT("referencePath"), ReferenceActor->GetPathName());
	Result->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Relative.GetLocation()));
	Result->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Relative.GetRotation().Rotator()));
	Result->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Relative.GetScale3D()));
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
	MCPReadParamsAhead(Params, {
		TEXT("cursor"), TEXT("limit"),
	});

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
		TSharedPtr<FJsonObject> LocationObj = MCPVec3ToJsonObject(Location);
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("location"), TEXT("rotation"), TEXT("scale"), TEXT("world"),
		TEXT("pieInstance"),
	});

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

	if (HasParam(Params, TEXT("location")))
	{
		Actor->SetActorLocation(OptionalVec3(Params, TEXT("location"), Actor->GetActorLocation()));
	}
	if (HasParam(Params, TEXT("rotation")))
	{
		Actor->SetActorRotation(OptionalRotator(Params, TEXT("rotation"), Actor->GetActorRotation()));
	}
	if (HasParam(Params, TEXT("scale")))
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("targetPoint"), TEXT("targetActor"), TEXT("targetActorPath"),
		TEXT("roll"), TEXT("world"), TEXT("pieInstance"),
	});

	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	const FString ActorLabel = Actor->GetActorLabel();

	// Resolve the target point: an explicit target Vec3, or another actor's location.
	FVector TargetLocation;
	if (HasParam(Params, TEXT("targetActor")) || HasParam(Params, TEXT("targetActorPath")))
	{
		FMCPActorSelector TargetSel;
		TargetSel.LabelKey = TEXT("targetActor");
		TargetSel.PathKey = TEXT("targetActorPath");
		AActor* TargetActor = MCPResolveActor(World, Params, ActorErr, TargetSel);
		if (!TargetActor) return ActorErr;
		TargetLocation = TargetActor->GetActorLocation();
	}
	else if (HasParam(Params, TEXT("targetPoint")))
	{
		// targetPoint, not target: get_relative_transform takes target as a
		// label, and one category key has one type (#1057).
		TargetLocation = OptionalVec3(Params, TEXT("targetPoint"), FVector::ZeroVector);
	}
	else
	{
		return MCPError(TEXT("Supply 'targetPoint' (Vec3), 'targetActor' (label) or 'targetActorPath' (object path)"));
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
	MCPReadParamsAhead(Params, {
		TEXT("point"), TEXT("extent"), TEXT("world"), TEXT("pieInstance"),
	});

	if (!HasParam(Params, TEXT("point"))) return MCPError(TEXT("Missing 'point' (Vec3)"));
	const FVector Point = OptionalVec3(Params, TEXT("point"), FVector::ZeroVector);

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
	if (!Nav) return MCPError(TEXT("No navigation system in this world (add a NavMeshBoundsVolume and build navigation)"));

	const FVector Extent = HasParam(Params, TEXT("extent"))
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
	const bool bHasLabels = TryGetArrayParam(Params, TEXT("actorLabels"), ActorLabelsArray);
	if (!bHasLabels) ActorLabelsArray = &EmptySelection;
	if (!bHasLabels && !HasParam(Params, TEXT("actorPaths")))
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
	if (TryGetArrayParam(Params, TEXT("actorPaths"), ActorPathsArray))
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentClass"), TEXT("componentName"), TEXT("onConflict"),
	});

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
		CompClass = MCPResolveClass(ComponentClass);
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"),
	});

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
	// only reaches a short name through MCPResolveClass and a
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
	if (UWorld* PreviousWorld = GetEditorWorld())
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
		if (UWorld* OpenWorld = GetEditorWorld())
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
	UWorld* World = GetEditorWorld();
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

	UWorld* World = GetEditorWorld();
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

TSharedPtr<FJsonValue> FLevelHandlers::SetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("propertyName"), TEXT("value"),
		TEXT("world"), TEXT("pieInstance"),
	});

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

	const TSharedPtr<FJsonValue> ValueParam = TryGetParam(Params, TEXT("value"));
	const TSharedPtr<FJsonValue>* ValueField = ValueParam.IsValid() ? &ValueParam : nullptr;
	if (!ValueField || !(*ValueField).IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	// A skinned mesh's mesh pointer goes through the engine setter (#1099). The
	// property is owned by a component class, so its container is that component.
	if (MCPSkinnedAsset::IsMeshProperty(Prop))
	{
		USkinnedMeshComponent* SkinnedComp = Cast<USkinnedMeshComponent>(static_cast<UObject*>(CurrentContainer));
		if (!SkinnedComp)
		{
			return MCPError(TEXT("The skinned mesh component could not be resolved. Use level(set_component_skeletal_mesh)."));
		}
		FString PreviousMesh;
		FString MeshErr;
		if (!MCPSkinnedAsset::AssignFromJson(SkinnedComp, *ValueField, PreviousMesh, MeshErr))
		{
			return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *MeshErr));
		}
		SkinnedComp->MarkPackageDirty();

		auto MeshResult = MCPSuccess();
		MCPSetUpdated(MeshResult);
		MeshResult->SetStringField(TEXT("actorLabel"), ActorLabel);
		MeshResult->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
		MeshResult->SetStringField(TEXT("componentClass"), SkinnedComp->GetClass()->GetName());
		MeshResult->SetStringField(TEXT("propertyName"), PropertyName);
		MCPSkinnedAsset::Report(MeshResult, SkinnedComp, PreviousMesh);

		TSharedPtr<FJsonObject> MeshPayload = MakeShared<FJsonObject>();
		MeshPayload->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
		MeshPayload->SetStringField(TEXT("actorLabel"), ActorLabel);
		if (!ComponentName.IsEmpty()) MeshPayload->SetStringField(TEXT("componentName"), ComponentName);
		MeshPayload->SetStringField(TEXT("propertyName"), PropertyName);
		MeshPayload->SetStringField(TEXT("value"), PreviousMesh.IsEmpty() ? FString(TEXT("None")) : PreviousMesh);
		MCPSetRollback(MeshResult, TEXT("set_component_property"), MeshPayload);
		return MCPResult(MeshResult);
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("includeValues"), TEXT("propertyNames"),
		TEXT("world"), TEXT("pieInstance"),
	});

	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	// #584: optionally dump arbitrary UPROPERTY values (custom fields,
	// CharacterMovement MaxWalkSpeed, etc.). world lets this read a PIE
	// actor's live component instance too.
	const bool bIncludeValues = OptionalBool(Params, TEXT("includeValues"), false);
	const TArray<TSharedPtr<FJsonValue>>* PropNamesArr = nullptr;
	TryGetArrayParam(Params, TEXT("propertyNames"), PropNamesArr);
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

			Obj->SetObjectField(TEXT("relativeLocation"), MCPVec3ToJsonObject(RelLoc));
			Obj->SetObjectField(TEXT("relativeRotation"), MCPRotatorToJsonObject(RelRot));
			Obj->SetObjectField(TEXT("relativeScale3D"), MCPVec3ToJsonObject(RelScale));
			Obj->SetObjectField(TEXT("worldLocation"), MCPVec3ToJsonObject(World.GetLocation()));
			Obj->SetObjectField(TEXT("worldRotation"), MCPRotatorToJsonObject(World.Rotator()));
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
	MCPReadParamsAhead(Params, {
		TEXT("defaultGameMode"), TEXT("killZ"), TEXT("globalGravityZ"), TEXT("enableWorldBoundsChecks"),
	});

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
	if (TryGetStringParam(Params, TEXT("defaultGameMode"), GameModeStr))
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
				GMClass = MCPResolveClass(GameModeStr);
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
	if (TryGetNumberParam(Params, TEXT("killZ"), KillZ))
	{
		Settings->KillZ = KillZ;
		Changes.Add(MakeShared<FJsonValueString>(TEXT("killZ")));
		PrevPayload->SetNumberField(TEXT("killZ"), PrevKillZ);
	}

	double GravityZ;
	if (TryGetNumberParam(Params, TEXT("globalGravityZ"), GravityZ))
	{
		Settings->GlobalGravityZ = GravityZ;
		Changes.Add(MakeShared<FJsonValueString>(TEXT("globalGravityZ")));
		PrevPayload->SetNumberField(TEXT("globalGravityZ"), PrevGravityZ);
	}

	bool bBoundsChecks;
	if (TryGetBoolParam(Params, TEXT("enableWorldBoundsChecks"), bBoundsChecks))
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("materialPath"), TEXT("slotIndex"),
	});

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
	MCPReadParamsAhead(Params, {
		TEXT("className"), TEXT("labelPrefix"), TEXT("world"), TEXT("pieInstance"), TEXT("matchSubclasses"),
		TEXT("includeTransforms"),
	});

	// #1113: labelPrefix is a case-sensitive prefix over the editor label, and
	// with it className may be omitted to match every actor class.
	const FString LabelPrefix = OptionalString(Params, TEXT("labelPrefix"));
	FString ClassName = OptionalString(Params, TEXT("className"));
	if (ClassName.IsEmpty() && LabelPrefix.IsEmpty())
	{
		return MCPError(TEXT("Pass className, labelPrefix, or both"));
	}
	if (ClassName.IsEmpty()) ClassName = TEXT("Actor");

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
		TargetClass = MCPResolveClass(ClassName);
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
		if (!LabelPrefix.IsEmpty() && !A->GetActorLabel().StartsWith(LabelPrefix, ESearchCase::CaseSensitive)) continue;
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
	MCPReadParamsAhead(Params, {
		TEXT("componentClass"), TEXT("world"), TEXT("pieInstance"),
	});

	FString ComponentClass;
	// className is a spec alias, renamed to componentClass before this runs (#1057).
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClass)) return Err;

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
	MCPReadParamsAhead(Params, {
		TEXT("world"), TEXT("pieInstance"), TEXT("topN"),
	});

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
	MCPReadParamsAhead(Params, {
		TEXT("world"), TEXT("pieInstance"),
	});

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
		TSharedPtr<FJsonObject> LocObj = MCPVec3ToJsonObject(Loc);
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

// ─── #188 get_actor_bounds ──────────────────────────────────────────
// Returns the axis-aligned bounding box (origin + extent) for an actor
// named by its editor label or its object path.
TSharedPtr<FJsonValue> FLevelHandlers::GetActorBounds(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("onlyColliding"), TEXT("world"), TEXT("pieInstance"),
	});

	FString ActorLabel;

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

	TSharedPtr<FJsonObject> OriginObj = MCPVec3ToJsonObject(Origin);

	TSharedPtr<FJsonObject> ExtentObj = MCPVec3ToJsonObject(Extent);

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
	MCPReadParamsAhead(Params, {
		TEXT("internalName"),
	});

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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("propertyName"), TEXT("value"), TEXT("force"), TEXT("world"),
		TEXT("pieInstance"),
	});

	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	const TSharedPtr<FJsonValue> ValueParam = TryGetParam(Params, TEXT("value"));
	const TSharedPtr<FJsonValue>* ValueField = ValueParam.IsValid() ? &ValueParam : nullptr;
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
		&& !HasParam(Params, TEXT("actorPath"));
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

	// A skinned mesh's mesh pointer goes through the engine setter (#1099). The
	// property is owned by a component class, so its container is that component.
	if (MCPSkinnedAsset::IsMeshProperty(Prop))
	{
		USkinnedMeshComponent* SkinnedComp = Cast<USkinnedMeshComponent>(static_cast<UObject*>(CurrentContainer));
		if (!SkinnedComp)
		{
			return MCPError(TEXT("The skinned mesh component could not be resolved. Use level(set_component_skeletal_mesh)."));
		}
		TargetActor->Modify();
		FString PreviousMesh;
		FString MeshErr;
		if (!MCPSkinnedAsset::AssignFromJson(SkinnedComp, *ValueField, PreviousMesh, MeshErr))
		{
			return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *MeshErr));
		}
		TargetActor->MarkPackageDirty();

		auto MeshResult = MCPSuccess();
		MCPSetUpdated(MeshResult);
		MeshResult->SetStringField(TEXT("actorLabel"), ActorLabel);
		MeshResult->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
		MeshResult->SetStringField(TEXT("propertyName"), PropertyName);
		MCPSkinnedAsset::Report(MeshResult, SkinnedComp, PreviousMesh);

		TSharedPtr<FJsonObject> MeshPayload = MakeShared<FJsonObject>();
		MeshPayload->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
		MeshPayload->SetStringField(TEXT("actorLabel"), ActorLabel);
		MeshPayload->SetStringField(TEXT("propertyName"), PropertyName);
		MeshPayload->SetStringField(TEXT("value"), PreviousMesh.IsEmpty() ? FString(TEXT("None")) : PreviousMesh);
		MCPSetRollback(MeshResult, TEXT("set_actor_property"), MeshPayload);
		return MCPResult(MeshResult);
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorLabels"), TEXT("actorPath"), TEXT("actorPaths"), TEXT("world"),
		TEXT("pieInstance"),
	});

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
	if (TryGetStringParam(Params, TEXT("actorLabel"), Single) && !Single.IsEmpty())
	{
		Labels.Add(Single);
	}
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("actorLabels"), LabelsArr) && LabelsArr)
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
	if (TryGetStringParam(Params, TEXT("actorPath"), SinglePath) && !SinglePath.IsEmpty())
	{
		Paths.Add(SinglePath);
	}
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("actorPaths"), PathsArr) && PathsArr)
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
		S->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Actor->GetActorLocation()));
		S->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Actor->GetActorRotation()));
		S->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Actor->GetActorScale3D()));
		S->SetObjectField(TEXT("velocity"), MCPVec3ToJsonObject(Actor->GetVelocity()));

		// Physics: drill into the root primitive for angular velocity + grounded.
		if (UPrimitiveComponent* Prim = Actor->FindComponentByClass<UPrimitiveComponent>())
		{
			if (Prim->IsSimulatingPhysics())
			{
				S->SetBoolField(TEXT("simulatingPhysics"), true);
				S->SetObjectField(TEXT("angularVelocity"), MCPVec3ToJsonObject(Prim->GetPhysicsAngularVelocityInDegrees()));
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

// #696: enable + force-build Nanite on a UStaticMesh asset.
TSharedPtr<FJsonValue> FLevelHandlers::SetNaniteSettings(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("assetPath"), TEXT("enabled"), TEXT("positionPrecision"),
	});

#if WITH_EDITOR
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err; // meshPath is a spec alias (#1057).
	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	const bool bEnabled = OptionalBool(Params, TEXT("enabled"), true);
	// Use the accessor pair (GetNaniteSettings/SetNaniteSettings) - direct
	// member access to NaniteSettings is deprecated in 5.7+.
	FMeshNaniteSettings Settings = MCPGetNaniteSettings(Mesh);
	const bool bPreviousEnabled = Settings.bEnabled != 0;
	const int32 PreviousPositionPrecision = Settings.PositionPrecision;
	Settings.bEnabled = bEnabled;
	if (HasParam(Params, TEXT("positionPrecision")))
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
	FString SaveError;
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
		bSaved = SaveAssetPackageChecked(Mesh, SaveError);
	}

	const bool bNowEnabled = MCPGetNaniteSettings(Mesh).bEnabled != 0;
	const int32 NowPositionPrecision = MCPGetNaniteSettings(Mesh).PositionPrecision;

	auto Result = MCPSuccess();
	if (bSettingsChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bSettingsChanged);
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Result->SetBoolField(TEXT("naniteEnabled"), bNowEnabled);
	Result->SetNumberField(TEXT("positionPrecision"), NowPositionPrecision);
	if (bSettingsChanged) MCPNoteSaveOutcome(Result, Mesh->GetPathName(), bSaved, SaveError);
	else Result->SetBoolField(TEXT("saved"), false);
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
	MCPReadParamsAhead(Params, {
		TEXT("assetPath"),
	});

	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err; // meshPath is a spec alias (#1057).
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
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("outputPath"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err; // filePath is a spec alias (#1057).

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
	if (!HasParam(Params, TEXT("folderPath")))
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
	if (TryGetArrayParam(Params, TEXT("actorLabels"), LabelValues) && LabelValues)
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
	if (Params.IsValid() && TryGetArrayParam(Params, TEXT("classPathContainsAny"), ClassPathAny) && ClassPathAny)
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

// #205: set USceneComponent::Mobility on the actor's root component.
TSharedPtr<FJsonValue> FLevelHandlers::SetActorMobility(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("mobility"),
	});

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
