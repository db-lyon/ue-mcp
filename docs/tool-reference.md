# Tool Reference

UE-MCP exposes **<!-- count:tools -->19<!-- /count --> category tools** covering **<!-- count:actions -->438+<!-- /count --> actions**, plus a `flow` tool for running multi-step YAML workflows. Every category tool takes an `action` parameter that selects the operation, plus action-specific parameters.

!!! tip "First call in any session"
    Start with `project(action="get_status")` to check the connection, then `level(action="get_outliner")` or `asset(action="list")` to explore.

!!! info "How to read this page"
    Each row lists a single action and its key parameters. Optional params are marked with `?`. For full schemas (types, descriptions, defaults), every action also surfaces its description through the MCP schema - your AI client can introspect them at runtime.

---

## project

*Project status, config INI files, and C++ source inspection.*

| Action | Description |
|--------|-------------|
| `get_status` | Check server mode and editor connection |
| `set_project` | Switch project. Params: `projectPath` |
| `get_info` | Read .uproject file details |
| `read_config` | Read INI config. Params: `configName (e.g. 'Engine', 'Game')` |
| `search_config` | Search INI files. Params: `query` |
| `list_config_tags` | Extract gameplay tags from config |
| `read_cpp_header` | Parse a .h file. Params: `headerPath` |
| `read_module` | Read module source. Params: `moduleName` |
| `list_modules` | List C++ modules |
| `search_cpp` | Search .h/.cpp files. Params: `query, directory?` |
| `read_engine_header` | Parse a .h file from the engine source tree. Params: `headerPath (relative to Engine/Source, or absolute)` |
| `find_engine_symbol` | Grep engine headers for a symbol. Params: `symbol, maxResults?` |
| `list_engine_modules` | List modules in Engine/Source/Runtime |
| `search_engine_cpp` | Search engine .h/.cpp/.inl files across Runtime/Editor/Developer/Plugins. Params: `query, tree? (Runtime\\|Editor\\|Developer\\|Plugins\\|all - default Runtime), subdirectory?, maxResults? (default 500)` |
| `set_config` | Write to INI. Params: `configName, section, key, value` |
| `build` | Build C++ project. Params: `configuration?, platform?, clean?` |
| `generate_project_files` | Generate IDE project files (Visual Studio, Xcode, etc.) |
| `create_cpp_class` | Create a new native UCLASS in a project module. Uses the same engine template path as File → New C++ Class. Writes .h + .cpp; returns both paths plus needsEditorRestart (true unless Live Coding successfully hot-reloaded). Params: `className (no prefix), parentClass? (default UObject; accepts short names like 'Actor' or /Script/<Module>.<Class> paths), moduleName? (default: first project module, use list_project_modules to pick), classDomain? ('public'\\|'private'\\|'classes', default public), subPath?` |
| `list_project_modules` | List native modules in the current project (name, host type, source path). Feed moduleName from here into create_cpp_class |
| `live_coding_compile` | Trigger a Live Coding compile (Windows only). Hot-patches method bodies of existing UCLASSes without editor restart - the fast inner loop for UFUNCTION implementations. Does NOT reliably register brand-new UCLASSes; use build_project + editor restart for those. Params: `wait? (default false - fire and return 'in_progress')` |
| `live_coding_status` | Report Live Coding availability/state (available, started, enabledForSession, compiling). Helps choose between live_coding_compile and build_project |
| `write_cpp_file` | Write a .h / .cpp / .inl file under the project's Source/ tree. Used to append UPROPERTYs/UFUNCTIONs or method bodies after create_cpp_class. Writes are scoped to Source/ for safety. Params: `path (relative to Source/ or absolute within Source/), content (full file contents)` |
| `read_cpp_source` | Read a .cpp file from the project Source/ tree. Companion to read_cpp_header for round-trip edits. Params: `sourcePath (relative to Source/ or absolute)` |
| `add_module_dependency` | Add a module to a target module's Build.cs dependency array. Params: `moduleName (the Build.cs to edit - must exist in the project), dependency (module name to add, e.g. 'UMG'), access? ('public'\\|'private', default 'private')` |

---

## asset

*Asset management: list, search, read, CRUD, import meshes/textures, datatables.*

| Action | Description |
|--------|-------------|
| `list` | List assets in directory. Params: `directory?, typeFilter?, recursive?` |
| `search` | Search by name/class/path. Params: `query, directory?, maxResults?, searchAll?` |
| `read` | Read asset via reflection. Params: `assetPath` |
| `read_properties` | Read asset properties with values. Params: `assetPath, propertyName?, includeValues?` |
| `duplicate` | Duplicate asset. Params: `sourcePath, destinationPath` |
| `rename` | Rename asset. Params: `assetPath, newName` |
| `bulk_rename` | Batched rename using IAssetTools::RenameAssets - single transaction with one redirector-fixup pass (matches Content Browser drag). Use this over looped rename for scene-referenced assets. Params: `renames[] where each entry is {sourcePath, destinationPath} OR {assetPath, newName}` |
| `move` | Move asset. Params: `sourcePath, destinationPath` |
| `delete` | Delete asset. Params: `assetPath` |
| `delete_batch` | Batch-delete assets. Params: `assetPaths[]` |
| `create_data_asset` | Create UDataAsset instance of custom class. Params: `name, className (/Script/Module.ClassName or loaded name), packagePath?, properties? (key/value map)` |
| `save` | Save asset(s). Params: `assetPath?` |
| `set_mesh_material` | Assign material to static mesh slot. Params: `assetPath, materialPath, slotIndex?` |
| `recenter_pivot` | Move static mesh pivot to geometry center. Params: `assetPath OR assetPaths` |
| `import_static_mesh` | Import from FBX/OBJ. Params: `filePath, name?, packagePath?, combineMeshes?, importMaterials?, importTextures?, generateLightmapUVs?` |
| `import_skeletal_mesh` | Import skeletal mesh from FBX. Params: `filePath, name?, packagePath?, skeletonPath?, importMaterials?, importTextures?` |
| `import_animation` | Import anim from FBX. Params: `filePath, name?, packagePath?, skeletonPath` |
| `import_texture` | Import image. Params: `filePath, name?, packagePath?` |
| `reimport` | Reimport asset from source file. Params: `assetPath, filePath?` |
| `read_datatable` | Read DataTable rows. Params: `assetPath, rowFilter?` |
| `create_datatable` | Create DataTable. Params: `name, packagePath?, rowStruct` |
| `reimport_datatable` | Reimport DataTable from JSON. Params: `assetPath, jsonPath?, jsonString?` |
| `list_textures` | List textures. Params: `directory?, recursive?` |
| `get_texture_info` | Get texture details. Params: `assetPath` |
| `set_texture_settings` | Set texture settings. Params: `assetPath, settings (object with compressionSettings?, lodGroup?, sRGB?, neverStream?)` |
| `add_socket` | Add socket to StaticMesh or SkeletalMesh. Params: `assetPath, socketName, boneName?, relativeLocation?, relativeRotation?, relativeScale?` |
| `remove_socket` | Remove socket by name. Params: `assetPath, socketName` |
| `list_sockets` | List sockets on a mesh. Params: `assetPath` |
| `reload_package` | Force reload an asset package from disk. Params: `assetPath` |
| `export` | Export asset to disk file (Texture2D → PNG, StaticMesh → FBX, etc.). Params: `assetPath, outputPath` |
| `search_fts` | Ranked asset search (token-scored over name/class/path). Params: `query, maxResults?, classFilter?` |
| `reindex_fts` | Rebuild the SQLite FTS5 asset index. Params: `directory?` |
| `get_referencers` | Reverse dependency lookup. Params: `packages[] OR packagePath (#150)` |
| `set_sk_material_slots` | Set materials on a USkeletalMesh by slot name or slotIndex (bypasses the blueprint override-materials path that UE's ICH silently reverts). Params: `assetPath, slots[{slotName?\\|slotIndex?, materialPath}]` |
| `diagnose_registry` | Scan a content path and compare disk vs AssetRegistry (including in-memory pending-kill entries). Returns onDiskCount, inMemoryIncludedCount, ghostCount and paths. Params: `path, recursive? (default true), reconcile? (forceRescan=true)` |
| `get_mesh_bounds` | Get StaticMesh bounding box. Params: `assetPath` |
| `get_mesh_collision` | Inspect StaticMesh collision setup. Params: `assetPath` |
| `move_folder` | Move/rename entire content folder with redirector fixup in one transaction. Params: `sourcePath, destinationPath (#192)` |
| `create_folder` | Create empty content browser folder(s). Params: `path OR paths[] (e.g. /Game/Foo, /Game/Bar/Baz)` |
| `set_mesh_nav` | Set StaticMesh nav contribution. Params: `assetPath, bHasNavigationData?, clearNavCollision? (#167)` |

---

## blueprint

*Blueprint reading, authoring, and compilation. Covers variables, functions, graphs, nodes, components, interfaces, and event dispatchers.*

| Action | Description |
|--------|-------------|
| `read` | Read BP structure incl. SCS components. Params: `assetPath` |
| `list_variables` | List variables. Params: `assetPath` |
| `list_functions` | List functions/graphs. Params: `assetPath` |
| `read_graph` | Read graph nodes. Params: `assetPath, graphName` |
| `read_graph_summary` | Lightweight graph summary (nodes+edges only, ~10KB). Params: `assetPath, graphName?` |
| `get_execution_flow` | Trace exec pins from an entry point. Params: `assetPath, graphName?, entryPoint?` |
| `get_dependencies` | Forward (classes/functions/assets) or reverse (referencers) deps. Params: `assetPath, reverse?` |
| `create` | Create Blueprint. Params: `assetPath, parentClass?` |
| `add_variable` | Add variable. Params: `assetPath, name, varType` |
| `set_variable_properties` | Edit variable properties. Params: `assetPath, name, instanceEditable?, blueprintReadOnly?, category?, tooltip?, replicationType?, exposeOnSpawn?` |
| `create_function` | Create function. Params: `assetPath, functionName` |
| `delete_function` | Delete function. Params: `assetPath, functionName` |
| `rename_function` | Rename function. Params: `assetPath, oldName, newName` |
| `add_node` | Add graph node. Params: `assetPath, graphName?, nodeClass, nodeParams?` |
| `delete_node` | Delete node. Params: `assetPath, graphName, nodeName` |
| `set_node_property` | Set node pin default or struct property. Params: `assetPath, graphName, nodeName, propertyName, value` |
| `connect_pins` | Wire nodes. Params: `sourceNode, sourcePin, targetNode, targetPin, assetPath, graphName?` |
| `add_component` | Add BP component. Params: `assetPath, componentClass, componentName?, parentComponent? (SCS parent for hierarchy - #115)` |
| `remove_component` | Remove SCS component. Params: `assetPath, componentName` |
| `set_component_property` | Set property on SCS or inherited component. Inherited components go through the child BP's InheritableComponentHandler override template so the parent stays untouched. Params: `assetPath, componentName, propertyName, value` |
| `get_component_property` | Read a single property value from an SCS or inherited component template. Returns the ICH override value for child BPs if one exists. Params: `assetPath, componentName, propertyName` |
| `set_class_default` | Set UPROPERTY on Blueprint CDO. Params: `assetPath, propertyName, value` |
| `delete_variable` | Delete a member variable. Params: `assetPath, name` |
| `add_function_parameter` | Add input or output parameter to a function. Params: `assetPath, functionName, parameterName, parameterType?, isOutput?` |
| `set_variable_default` | Set default value on a BP variable. Params: `assetPath, name, value` |
| `compile` | Compile Blueprint. Params: `assetPath` |
| `list_node_types` | List node types. Params: `category?, includeFunctions?` |
| `search_node_types` | Search nodes. Params: `query` |
| `create_interface` | Create BP Interface. Params: `assetPath` |
| `add_interface` | Implement interface. Params: `blueprintPath, interfacePath` |
| `list_graphs` | List all graphs in a blueprint. Params: `assetPath` |
| `add_event_dispatcher` | Add event dispatcher. Params: `blueprintPath, name` |
| `duplicate` | Duplicate blueprint asset. Params: `sourcePath, destinationPath` |
| `add_local_variable` | Add function-scope local variable. Params: `assetPath, functionName, name, varType?` |
| `list_local_variables` | List local variables in a function. Params: `assetPath, functionName` |
| `validate` | Validate blueprint without saving (compile + collect diagnostics). Params: `assetPath` |
| `read_component_properties` | Dump ALL UPROPERTYs on a BP component template incl. array contents (#105). Params: `assetPath, componentName` |
| `read_node_property` | Read a node pin default OR a reflected node property for verification (#102). Params: `assetPath, graphName?, nodeName, propertyName` |
| `reparent_component` | Reparent an SCS component under a new parent (#115). Params: `assetPath, componentName, newParent` |
| `reparent` | Change a Blueprint's ParentClass and recompile (#138). Params: `assetPath, parentClass (short name or full path)` |
| `set_actor_tick_settings` | Set actor CDO tick settings (#116). Params: `assetPath, bCanEverTick?, bStartWithTickEnabled?, TickInterval?` |
| `export_nodes_t3d` | Export graph nodes as T3D text (Ctrl+C equivalent) for bulk round-trip (#130). Params: `assetPath, graphName?, nodeIds? (omit = whole graph)` |
| `import_nodes_t3d` | Paste a T3D node blob into a graph (Ctrl+V equivalent) for bulk authoring (#130). Params: `assetPath, graphName?, t3d, posX?, posY?` |
| `set_cdo_property` | Set UPROPERTY on any C++ class CDO (not just Blueprints). Params: `className, propertyName, value (#182/#183)` |
| `get_cdo_properties` | Read UPROPERTY values from any C++ class CDO. Params: `className, propertyNames? (#183)` |
| `run_construction_script` | Spawn temp actor, run construction script, return generated components and transforms. Params: `assetPath, location? (#195)` |

---

## level

*Level actors, selection, components, level management, volumes, lights, and splines.*

| Action | Description |
|--------|-------------|
| `get_outliner` | List actors. Params: `classFilter?, nameFilter?, world? (editor\\|pie\\|auto), limit?` |
| `place_actor` | Spawn actor. Params: `actorClass, label?, location?, rotation?, scale?, staticMesh?, material?` |
| `delete_actor` | Remove actor. Params: `actorLabel` |
| `get_actor_details` | Inspect actor. Params: `actorLabel OR actorPath, includeProperties?, propertyName?, world? (editor\\|pie)` |
| `move_actor` | Transform actor. Params: `actorLabel, location?, rotation?, scale?` |
| `select` | Select actors. Params: `actorLabels[]` |
| `get_selected` | Get selection |
| `add_component` | Add component to actor. Params: `actorLabel, componentClass, componentName?` |
| `set_component_property` | Set component prop. Params: `actorLabel, componentName, propertyName, value` |
| `get_current` | Get current level name and path |
| `load` | Load level. Params: `levelPath` |
| `save` | Save current level |
| `list` | List levels. Params: `directory?, recursive?` |
| `create` | Create new level. Params: `levelPath?, templateLevel?` |
| `spawn_volume` | Place volume. Params: `volumeType, location?, extent?, label?` |
| `list_volumes` | List volumes. Params: `volumeType?` |
| `set_volume_properties` | Edit volume. Params: `actorLabel, properties` |
| `spawn_light` | Place light. Params: `lightType, location?, rotation?, intensity?, color?, label?` |
| `set_light_properties` | Edit light. Params: `actorLabel, intensity?, color?, rotation? (DirectionalLight sun angle), recaptureSky?, temperature?, castShadows?, attenuationRadius?` |
| `set_fog_properties` | Edit ExponentialHeightFog. Params: `actorLabel?, fogDensity?, fogHeightFalloff?, startDistance?, fogInscatteringColor?` |
| `get_actors_by_class` | List actors by class name. Params: `className, world? (editor\\|pie)` |
| `count_actors_by_class` | Histogram of actor classes in the level (sorted desc). Params: `world? (editor\\|pie), topN? (#146)` |
| `get_runtime_virtual_texture_summary` | List RuntimeVirtualTextureVolume actors + their bound VirtualTexture assets (#150) |
| `set_water_body_property` | Set a property on an actor's WaterBodyComponent (ShapeDilation, WaterLevel, etc.). Params: `actorLabel, propertyName, value` |
| `build_lighting` | Build lights. Params: `quality?` |
| `get_spline_info` | Read spline. Params: `actorLabel` |
| `set_spline_points` | Set spline points. Params: `actorLabel, points[], closedLoop?` |
| `set_actor_material` | Set material on actor. Params: `actorLabel, materialPath, slotIndex?` |
| `get_world_settings` | Read world settings (GameMode, KillZ, gravity, etc.) |
| `set_world_settings` | Set world settings. Params: `defaultGameMode?, killZ?, globalGravityZ?, enableWorldBoundsChecks?` |
| `get_actor_bounds` | Get actor AABB. Params: `actorLabel` |
| `resolve_actor` | Resolve internal/runtime actor name to editor label. Params: `internalName (e.g. StaticMeshActor_141)` |
| `set_actor_property` | Set per-instance UPROPERTY on a level actor. Params: `actorLabel ('WorldSettings' targets the world settings actor), propertyName (dotted paths like 'Foo.Bar' supported), value (string/number/bool/object/array; an actor label resolves to AActor* refs), force? (bypass EditDefaultsOnly), world? (editor\\|pie) (#202/#230)` |
| `delete_actors` | Bulk-delete actors. Params: `at least one of labelPrefix, className, tag; dryRun? to preview` |
| `add_actor_tag` | Append a tag to an actor's Tags array. Params: `actorLabel, tag (#219)` |
| `remove_actor_tag` | Remove a tag from an actor's Tags array. Params: `actorLabel, tag (#219)` |
| `set_actor_tags` | Replace an actor's Tags array. Params: `actorLabel, tags[] (#219)` |
| `list_actor_tags` | List an actor's Tags. Params: `actorLabel (#219)` |
| `attach_actor` | Attach actor as child. Params: `childLabel, parentLabel, attachRule? (KeepWorld\\|KeepRelative\\|SnapToTarget; default KeepWorld), socketName? (#205)` |
| `detach_actor` | Detach actor from parent. Params: `childLabel (#205)` |
| `set_actor_mobility` | Set actor root component Mobility. Params: `actorLabel, mobility (static\\|stationary\\|movable) (#205)` |
| `get_current_edit_level` | Read the active edit-target sub-level (#204) |
| `set_current_edit_level` | Set the active edit-target sub-level so subsequent spawns land in it. Params: `levelName (e.g. SubLevel_A) (#204)` |
| `list_streaming_sublevels` | List streaming sub-levels with transform + initially-loaded/visible flags (#206) |
| `add_streaming_sublevel` | Add a streaming sub-level. Params: `levelPath, streamingClass? (LevelStreamingDynamic\\|LevelStreamingAlwaysLoaded), location?, initiallyLoaded?, initiallyVisible? (#206)` |
| `remove_streaming_sublevel` | Remove a streaming sub-level. Params: `levelName \\| levelPath (#206)` |
| `set_streaming_sublevel_properties` | Update sub-level transform/visibility flags. Params: `levelName \\| levelPath, location?, initiallyLoaded?, initiallyVisible?, editorVisible? (#206)` |
| `spawn_grid` | Batch-spawn StaticMeshActors on a grid. Params: `staticMesh, min, max (Vec3 bounds), countX?, countY?, countZ?, jitter?, labelPrefix? (#203)` |
| `batch_translate` | Translate a set of actors by an offset. Params: `offset (Vec3), actorLabels[] OR tag (#203)` |

---

## material

*Materials: create, read, parameters, shading, textures, and graph authoring (expression nodes, connections).*

| Action | Description |
|--------|-------------|
| `read` | Read material structure. Params: `assetPath` |
| `list_parameters` | List overridable parameters. Params: `assetPath` |
| `set_parameter` | Set parameter on MaterialInstance. Params: `assetPath, parameterName, parameterType, value` |
| `set_expression_value` | Set value on expression node. Params: `materialPath, expressionIndex, value` |
| `disconnect_property` | Disconnect a material property input. Params: `materialPath, property` |
| `create_instance` | Create material instance. Params: `parentPath, name?, packagePath?` |
| `create` | Create material. Params: `name, packagePath?` |
| `create_simple` | Single-call simple material. Params: `name, packagePath?, baseColor? ({r,g,b}), metallic?, specular?, roughness?, emissive?, usages?[] (e.g. InstancedStaticMeshes, Nanite, NiagaraSprites)` |
| `set_usage` | Set EMaterialUsage flag(s) on a material. Params: `assetPath, usage OR usages[], enabled? (default true)` |
| `set_shading_model` | Set shading model. Params: `assetPath, shadingModel` |
| `set_blend_mode` | Set blend mode. Params: `assetPath, blendMode` |
| `set_base_color` | Set base color. Params: `assetPath, color` |
| `connect_texture` | Connect texture to property. Params: `materialPath, texturePath, property` |
| `add_expression` | Add expression node. Params: `materialPath, expressionType, name?, parameterName?` |
| `connect_expressions` | Wire two expressions. Params: `materialPath, sourceExpression, sourceOutput?, targetExpression, targetInput?` |
| `connect_to_property` | Wire expression to material output. Params: `materialPath, expressionName, outputName?, property` |
| `list_expressions` | List expression nodes. Params: `materialPath` |
| `delete_expression` | Remove expression. Params: `materialPath, expressionName` |
| `list_expression_types` | List available expression types |
| `recompile` | Recompile material. Params: `materialPath` |
| `duplicate` | Duplicate material asset. Params: `sourcePath, destinationPath` |
| `validate` | Validate material graph - find orphans, broken refs. Params: `assetPath` |
| `get_shader_stats` | Shader compile stats, sampler+param counts. Params: `assetPath` |
| `export_graph` | Export material graph as JSON. Params: `assetPath` |
| `import_graph` | Rebuild graph from JSON. Params: `assetPath, nodes, propertyConnections?` |
| `build_graph` | Build graph from spec. Params: `assetPath, nodes, propertyConnections?` |
| `render_preview` | Render preview PNG. Params: `assetPath, outputPath, width?, height?` |
| `begin_transaction` | Begin undo transaction. Params: `label?` |
| `end_transaction` | End undo transaction |

---

## animation

*Animation assets, skeletons, montages, blendspaces, anim blueprints, physics assets.*

| Action | Description |
|--------|-------------|
| `read_anim_blueprint` | Read AnimBP structure. Params: `assetPath` |
| `read_montage` | Read montage. Params: `assetPath` |
| `read_sequence` | Read anim sequence. Params: `assetPath` |
| `read_blendspace` | Read blendspace. Params: `assetPath` |
| `list` | List anim assets. Params: `directory?, recursive?` |
| `create_montage` | Create montage. Params: `animSequencePath, name?, packagePath?` |
| `create_anim_blueprint` | Create AnimBP. Params: `skeletonPath, name?, packagePath?, parentClass?` |
| `create_blendspace` | Create blendspace. Params: `skeletonPath, name?, packagePath?, axisHorizontal?, axisVertical?` |
| `add_notify` | Add notify. Params: `assetPath, notifyName, triggerTime, notifyClass?` |
| `get_skeleton_info` | Read skeleton. Params: `assetPath` |
| `list_sockets` | List sockets. Params: `assetPath` |
| `list_skeletal_meshes` | List skeletal meshes. Params: `directory?, recursive?` |
| `get_physics_asset` | Read physics asset. Params: `assetPath` |
| `create_sequence` | Create blank AnimSequence. Params: `name, skeletonPath, packagePath?, numFrames?, frameRate?` |
| `set_bone_keyframes` | Set bone transform keyframes. Params: `assetPath, boneName, keyframes` |
| `get_bone_transforms` | Read reference pose transforms. Params: `skeletonPath, boneNames?` |
| `set_montage_sequence` | Replace animation sequence in a montage. Params: `assetPath, animSequencePath, slotIndex?` |
| `set_montage_properties` | Set montage properties. Params: `assetPath, sequenceLength?, rateScale?, blendIn?, blendOut?` |
| `create_state_machine` | Create state machine in AnimBP. Params: `assetPath, name?, graphName?` |
| `add_state` | Add state to a state machine. Params: `assetPath, stateMachineName, stateName` |
| `add_transition` | Add directed transition between states. Params: `assetPath, stateMachineName, fromState, toState` |
| `set_state_animation` | Assign anim asset to state. Params: `assetPath, stateMachineName, stateName, animAssetPath` |
| `set_transition_blend` | Set blend type/duration on transition. Params: `assetPath, stateMachineName, fromState, toState, blendDuration?, blendLogic?` |
| `read_state_machine` | Read state machine topology. Params: `assetPath, stateMachineName` |
| `read_anim_graph` | Read AnimBP AnimGraph nodes with properties & pins. Params: `assetPath, graphName?` |
| `add_curve` | Add float curve to AnimSequence. Params: `assetPath, curveName, curveType?` |
| `set_montage_slot` | Set slot name on a montage track. Params: `assetPath, slotName, trackIndex?` |
| `add_montage_section` | Add composite section to montage. Params: `assetPath, sectionName, startTime?, linkedSection?` |
| `create_ik_rig` | Create IKRigDefinition asset, optionally with retargetRoot + chains[]. Params: `name, skeletalMeshPath, packagePath?, retargetRoot?, chains?: [{name, startBone, endBone, goal?}]` |
| `read_ik_rig` | Read IK Rig chains, solvers, skeleton. Params: `assetPath` |
| `list_control_rig_variables` | List ControlRig variables and hierarchy. Params: `assetPath` |
| `set_root_motion` | Set root motion settings on AnimSequence. Params: `assetPath, enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?` |
| `add_virtual_bone` | Add virtual bone. Params: `skeletonPath, sourceBone, targetBone` |
| `remove_virtual_bone` | Remove virtual bone. Params: `skeletonPath, virtualBoneName` |
| `create_composite` | Create AnimComposite. Params: `name, skeletonPath, packagePath?` |
| `list_modifiers` | List applied animation modifiers. Params: `assetPath` |
| `create_ik_retargeter` | Create IKRetargeter asset. Params: `name, packagePath?, sourceRig?, targetRig?` |
| `set_anim_blueprint_skeleton` | Set target skeleton on AnimBP. Params: `assetPath, skeletonPath` |
| `read_bone_track` | Read bone transform samples from AnimSequence. Params: `assetPath, boneName, frames?: [int]` |
| `create_pose_search_database` | Create a PoseSearchDatabase asset (motion matching). Params: `name, packagePath?, schemaPath?` |
| `set_pose_search_schema` | Set the Schema on an existing PoseSearchDatabase. Params: `assetPath, schemaPath` |
| `add_pose_search_sequence` | Append an AnimSequence/AnimComposite/AnimMontage/BlendSpace to a PoseSearchDatabase. Params: `assetPath, sequencePath` |
| `build_pose_search_index` | Build (or rebuild) the search index. Params: `assetPath, wait? (default true)` |
| `read_pose_search_database` | Inspect a PoseSearchDatabase: schema, animation entries, cost biases, tags. Params: `assetPath` |
| `set_sequence_properties` | Batch-set properties on AnimSequence assets. If a path is a Montage and resolveFromMontages is true (default), resolves to its first AnimSequence. Params: `assetPaths[], properties{enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?}, resolveFromMontages?` |
| `bake_root_motion_from_bone` | Bake delta translation from a source bone (e.g. pelvis) onto the root bone across the whole sequence; compensates the source bone so world-space position is unchanged. Params: `assetPath, sourceBone, rootBone? (default 'root'), axes? (default ['x','y']), interpolation? ('linear'\\|'per_frame', default 'linear')` |

---

## landscape

*Landscape terrain: info, layers, sculpting, painting, materials, heightmap import.*

| Action | Description |
|--------|-------------|
| `get_info` | Get landscape setup |
| `list_layers` | List paint layers |
| `sample` | Sample height/layers. Params: `x, y` |
| `list_splines` | Read landscape splines |
| `get_component` | Inspect component. Params: `componentIndex` |
| `sculpt` | Sculpt heightmap. Params: `x, y, radius, strength, falloff?` |
| `paint_layer` | Paint weight layer. Params: `layerName, x, y, radius, strength?` |
| `set_material` | Set landscape material. Params: `materialPath` |
| `add_layer_info` | Register paint layer. Params: `layerName` |
| `import_heightmap` | Import heightmap file. Params: `filePath` |
| `get_material_usage_summary` | Per-proxy summary: landscape/hole material paths + component/grass/nanite counts (#150) |

---

## pcg

*Procedural Content Generation: graphs, nodes, connections, execution, volumes.*

| Action | Description |
|--------|-------------|
| `list_graphs` | List PCG graphs. Params: `directory?, recursive?` |
| `read_graph` | Read graph structure. Params: `assetPath` |
| `read_node_settings` | Read node settings. Params: `assetPath, nodeName` |
| `get_components` | List PCG components in level |
| `get_component_details` | Inspect PCG component. Params: `actorLabel` |
| `create_graph` | Create graph. Params: `name, packagePath?` |
| `add_node` | Add node. Params: `assetPath, nodeType, nodeName?` |
| `connect_nodes` | Wire nodes. Params: `assetPath, sourceNode, sourcePin, targetNode, targetPin` |
| `set_node_settings` | Set node params. Params: `assetPath, nodeName, settings` |
| `set_static_mesh_spawner_meshes` | Populate weighted MeshEntries on a PCGStaticMeshSpawner node (#145). Params: `assetPath, nodeName, entries=[{mesh, weight?}], replace? (default true)` |
| `remove_node` | Remove node. Params: `assetPath, nodeName` |
| `execute` | Regenerate PCG. Params: `actorLabel` |
| `force_regenerate` | Force a stuck PCG component to regenerate (clears graph ref, re-sets, cleanup+generate). Params: `actorLabel (#146)` |
| `cleanup` | Cleanup a PCG component (remove spawned content). Params: `actorLabel, removeComponents? (default true) (#146)` |
| `toggle_graph` | Toggle a PCG component's graph assignment to force reinit (no generate). Params: `actorLabel, graphPath? (#146)` |
| `add_volume` | Place PCG volume. Params: `graphPath, location?, extent?` |
| `import_graph` | Bulk-author a PCG graph from JSON. Params: `assetPath, nodes=[{name,class,posX?,posY?,settings?}], connections=[{from,fromPin?,to,toPin?}], replace? (default false)` |
| `export_graph` | Export a PCG graph as JSON. Params: `assetPath, includeSettings? (default true)` |

---

## foliage

*Foliage painting, types, sampling, and settings.*

| Action | Description |
|--------|-------------|
| `list_types` | List foliage types in level |
| `get_settings` | Read foliage type settings. Params: `foliageTypeName` |
| `sample` | Query instances in region. Params: `center, radius, foliageType?` |
| `paint` | Add foliage. Params: `foliageType, center, radius, count?, density?` |
| `erase` | Remove foliage. Params: `center, radius, foliageType?` |
| `create_type` | Create foliage type from mesh. Params: `meshPath, name?, packagePath?` |
| `set_settings` | Modify type settings. Params: `foliageTypeName, settings` |

---

## niagara

*Niagara VFX: systems, emitters, spawning, parameters, and graph authoring.*

| Action | Description |
|--------|-------------|
| `list` | List Niagara assets. Params: `directory?, recursive?` |
| `get_info` | Inspect system. Params: `assetPath` |
| `spawn` | Spawn VFX. Params: `systemPath, location, rotation?, label?` |
| `set_parameter` | Set parameter. Params: `actorLabel, parameterName, value, parameterType?` |
| `create` | Create system. Params: `name, packagePath?` |
| `create_emitter` | Create Niagara emitter. Params: `name, packagePath?, templatePath?` |
| `add_emitter` | Add emitter to system. Params: `systemPath, emitterPath` |
| `list_emitters` | List emitters in system. Params: `systemPath` |
| `set_emitter_property` | Set emitter property. Params: `systemPath, emitterName?, propertyName, value` |
| `list_modules` | List Niagara modules. Params: `directory?` |
| `get_emitter_info` | Inspect emitter. Params: `assetPath` |
| `list_renderers` | List renderers on an emitter. Params: `systemPath, emitterName?, emitterIndex?` |
| `add_renderer` | Add renderer (sprite/mesh/ribbon or full class). Params: `systemPath, rendererType, emitterName?, emitterIndex?` |
| `remove_renderer` | Remove renderer by index. Params: `systemPath, rendererIndex, emitterName?, emitterIndex?` |
| `set_renderer_property` | Set renderer bool/number/string property. Params: `systemPath, rendererIndex, propertyName, value, emitterName?, emitterIndex?` |
| `inspect_data_interfaces` | List user-scope data interfaces. Params: `systemPath` |
| `create_system_from_spec` | Declaratively create a system + emitters. Params: `name, packagePath?, emitters?:[{path}]` |
| `get_compiled_hlsl` | Read GPU compute script info for an emitter. Params: `systemPath, emitterName?, emitterIndex?` |
| `list_system_parameters` | List user-exposed system parameters. Params: `systemPath` |
| `list_module_inputs` | List modules + their input pins for an emitter. Params: `systemPath, emitterName?, emitterIndex?, stackContext? (ParticleSpawn\\|ParticleUpdate\\|EmitterSpawn\\|EmitterUpdate\\|all - default all)` |
| `set_module_input` | Set literal default on a module input pin. Params: `systemPath, moduleName, inputName, value, emitterName?, emitterIndex?, stackContext?` |
| `list_static_switches` | List static switch inputs on a module. Params: `systemPath, moduleName, emitterName?, emitterIndex?, stackContext?` |
| `set_static_switch` | Set static switch value on a module's function call node. Params: `systemPath, moduleName, switchName, value, emitterName?, emitterIndex?, stackContext?` |
| `create_module_from_hlsl` | Create a NiagaraScript module backed by a custom HLSL node. Params: `name, hlsl, packagePath?, inputs?:[{name,type}], outputs?:[{name,type}]` |
| `create_scratch_module` | Create empty Niagara scratch module. Params: `name, packagePath?, inputs?:[{name,type}], outputs?:[{name,type}] (#185)` |
| `batch` | Run a sequence of niagara operations against the bridge in order. Fails fast on the first error (returns results up to that point + error). Params: `ops:[{action, params}] where action is any niagara subaction listed above` |

---

## audio

*Audio: sound assets, playback, ambient sounds, SoundCues, MetaSounds.*

| Action | Description |
|--------|-------------|
| `list` | List sound assets. Params: `directory?, recursive?` |
| `play_at_location` | Play sound. Params: `soundPath, location, volumeMultiplier?, pitchMultiplier?` |
| `spawn_ambient` | Place ambient sound. Params: `soundPath, location, label?` |
| `create_cue` | Create SoundCue. Params: `name, packagePath?, soundWavePath?` |
| `create_metasound` | Create MetaSoundSource. Params: `name, packagePath?` |

---

## widget

*UMG Widget Blueprints, Editor Utility Widgets, and Editor Utility Blueprints.*

| Action | Description |
|--------|-------------|
| `read_tree` | Read widget hierarchy. Params: `assetPath` |
| `get_details` | Inspect widget. Params: `assetPath, widgetName` |
| `set_property` | Set widget property. Params: `assetPath, widgetName, propertyName, value` |
| `list` | List Widget BPs. Params: `directory?, recursive?` |
| `read_animations` | Read UMG animations. Params: `assetPath` |
| `create` | Create Widget BP. Params: `name, packagePath?, parentClass?` |
| `create_utility_widget` | Create editor utility widget. Params: `name, packagePath?` |
| `run_utility_widget` | Open editor utility widget. Params: `assetPath` |
| `create_utility_blueprint` | Create editor utility blueprint. Params: `name, packagePath?` |
| `run_utility_blueprint` | Run editor utility blueprint. Params: `assetPath` |
| `add_widget` | Add widget to widget tree. Params: `assetPath, widgetClass, widgetName?, parentWidgetName?` |
| `remove_widget` | Remove widget from tree. Params: `assetPath, widgetName` |
| `move_widget` | Reparent widget. Params: `assetPath, widgetName, newParentWidgetName` |
| `list_classes` | List available widget classes |
| `list_runtime` | (#160) List live UUserWidget instances in the PIE world. Params: `classFilter?, namePrefix?, viewportOnly?` |
| `get_runtime` | (#160) Inspect a live PIE widget tree with text/visibility/brush/percent values. Params: `widgetName? \\| className?, childName?, maxDepth?` |
| `get_runtime_delegates` | (#161) Read delegate binding state on a live PIE widget. Params: `widgetName, className?` |

---

## editor

*Editor commands, Python execution, PIE, undo/redo, hot reload, viewport, performance, sequencer, build pipeline, logs, editor control.*

| Action | Description |
|--------|-------------|
| `start_editor` | Launch Unreal Editor with the current project and reconnect bridge |
| `stop_editor` | Close Unreal Editor gracefully |
| `restart_editor` | Stop then start the editor |
| `execute_command` | Run console command. Params: `command` |
| `execute_python` | Run Python in editor. Params: `code` |
| `run_python_file` | Run a Python file from disk with __file__/__name__ populated (#142). Params: `filePath, args?` |
| `set_property` | Set UObject property. Params: `objectPath, propertyName, value` |
| `play_in_editor` | PIE control. Params: `pieAction (start\\|stop\\|status)` |
| `get_runtime_value` | Read PIE actor value. Params: `actorLabel, propertyName` |
| `get_pie_pawn` | Resolve the controlled pawn in the active PIE world. Params: `playerIndex? (default 0)` |
| `invoke_function` | Call a BlueprintCallable / Exec UFUNCTION on a target actor. Params: `actorLabel, functionName, args? (object), world? (editor\\|pie)` |
| `set_pie_time_scale` | Fast-forward PIE game time. Params: `factor (>0)` |
| `hot_reload` | Hot reload C++ |
| `undo` | Undo last transaction |
| `redo` | Redo last transaction |
| `get_perf_stats` | Editor performance stats |
| `run_stat` | Run stat command. Params: `command` |
| `set_scalability` | Set quality. Params: `level` |
| `capture_screenshot` | Screenshot. Params: `filename?, resolution?, target? (auto\\|pie\\|editor; auto routes to PIE viewport when PIE is running) (#226)` |
| `capture_scene_png` | Headless PNG screenshot via SceneCapture2D (works unfocused, guaranteed RGBA8 LDR). Params: `outputPath, location?, rotation?, width? (default 1280), height? (default 720), fov? (default 90) (#148)` |
| `get_viewport` | Get viewport camera |
| `set_viewport` | Set viewport camera. Params: `location?, rotation?` |
| `focus_on_actor` | Focus on actor. Params: `actorLabel` |
| `create_sequence` | Create Level Sequence. Params: `name, packagePath?` |
| `get_sequence_info` | Read sequence. Params: `assetPath, includeSectionDetails? (attach sockets, first transform key values per track)` |
| `add_sequence_track` | Add track. Params: `assetPath, trackType, actorLabel?` |
| `play_sequence` | Play/stop/pause sequence. Params: `assetPath, sequenceAction` |
| `build_all` | Build all (geometry, lighting, paths, HLOD) |
| `build_geometry` | Rebuild BSP geometry |
| `build_hlod` | Build HLODs |
| `validate_assets` | Run data validation. Params: `directory?` |
| `get_build_status` | Get build/map status |
| `cook_content` | Cook content. Params: `platform?` |
| `get_log` | Read output log. Params: `maxLines?, filter?, category?` |
| `search_log` | Search log. Params: `query` |
| `get_message_log` | Read message log. Params: `logName?` |
| `list_crashes` | List crash reports |
| `get_crash_info` | Get crash details. Params: `crashFolder` |
| `check_for_crashes` | Check for recent crashes |
| `set_dialog_policy` | Auto-respond to dialogs matching a pattern. Params: `pattern, response` |
| `clear_dialog_policy` | Clear dialog policies. Params: `pattern?` |
| `get_dialog_policy` | Get current dialog policies |
| `list_dialogs` | List active modal dialogs |
| `respond_to_dialog` | Click a button on the active modal dialog. Params: `buttonIndex?, buttonLabel?` |
| `open_asset` | Open asset in its editor. Params: `assetPath` |
| `reload_bridge` | Hot-reload Python bridge handlers from disk |

---

## reflection

*UE reflection: classes, structs, enums, gameplay tags.*

| Action | Description |
|--------|-------------|
| `reflect_class` | Reflect UClass. Params: `className, includeInherited?` |
| `reflect_struct` | Reflect UScriptStruct. Params: `structName` |
| `reflect_enum` | Reflect UEnum. Params: `enumName` |
| `list_classes` | List classes. Params: `parentFilter?, limit?` |
| `list_tags` | List gameplay tags. Params: `filter?` |
| `create_tag` | Create gameplay tag. Params: `tag, comment?` |

---

## gameplay

*Gameplay systems: physics, collision, navigation, input, behavior trees, AI (EQS, perception, State Trees, Smart Objects), game framework.*

| Action | Description |
|--------|-------------|
| `set_collision_profile` | Set collision preset. Params: `actorLabel, profileName` |
| `set_simulate_physics` | Toggle physics. Params: `actorLabel, simulate` |
| `set_collision_enabled` | Set collision mode. Params: `actorLabel, collisionEnabled` |
| `set_physics_properties` | Set mass/damping/gravity. Params: `actorLabel, mass?, linearDamping?, angularDamping?, enableGravity?` |
| `rebuild_navigation` | Rebuild navmesh |
| `get_navmesh_info` | Query nav system |
| `project_to_nav` | Project point to navmesh. Params: `location, extent?` |
| `spawn_nav_modifier` | Place nav modifier. Params: `location, extent?, areaClass?` |
| `create_input_action` | Create InputAction. Params: `name, packagePath?, valueType?` |
| `create_input_mapping` | Create InputMappingContext. Params: `name, packagePath?` |
| `list_input_assets` | List input assets. Params: `directory?, recursive?` |
| `read_imc` | Read InputMappingContext mappings. Params: `imcPath` |
| `list_input_mappings` | Alias for read_imc. List key→action bindings with triggers/modifiers. Params: `imcPath` |
| `add_imc_mapping` | Add key mapping to IMC. Params: `imcPath, inputActionPath, key` |
| `set_mapping_modifiers` | Set modifiers/triggers on an IMC mapping. Params: `imcPath, mappingIndex?, modifiers?, triggers?` |
| `remove_imc_mapping` | (#158) Remove an IMC mapping. Params: `imcPath, mappingIndex? \\| (inputActionPath? + key?)` |
| `set_imc_mapping_key` | (#158) Rebind an IMC mapping to a new key. Params: `imcPath, newKey, mappingIndex? \\| key? \\| inputActionPath?` |
| `set_imc_mapping_action` | (#158) Retarget an IMC mapping to a different InputAction. Params: `imcPath, newInputActionPath, mappingIndex? \\| key? \\| inputActionPath?` |
| `list_behavior_trees` | List behavior trees. Params: `directory?, recursive?` |
| `get_behavior_tree_info` | Inspect behavior tree (top-level + blackboard). Params: `assetPath` |
| `read_behavior_tree_graph` | Walk BT tree: composites, tasks, decorators, services with blackboard keys. Params: `assetPath` |
| `create_blackboard` | Create Blackboard. Params: `name, packagePath?` |
| `create_behavior_tree` | Create behavior tree. Params: `name, packagePath?, blackboardPath?` |
| `create_eqs_query` | Create EQS query. Params: `name, packagePath?` |
| `list_eqs_queries` | List EQS queries. Params: `directory?` |
| `add_perception` | Add AIPerceptionComponent. Params: `blueprintPath, senses?` |
| `configure_sense` | Configure perception sense. Params: `blueprintPath, senseType, settings?` |
| `create_state_tree` | Create StateTree. Params: `name, packagePath?` |
| `list_state_trees` | List StateTrees. Params: `directory?` |
| `add_state_tree_component` | Add StateTreeComponent. Params: `blueprintPath` |
| `create_smart_object_def` | Create SmartObjectDefinition. Params: `name, packagePath?` |
| `add_smart_object_component` | Add SmartObjectComponent. Params: `blueprintPath` |
| `inspect_pie` | Inspect PIE runtime. Params: `actorLabel?` |
| `get_pie_anim_state` | Get PIE anim instance state. Params: `actorLabel` |
| `get_pie_anim_properties` | Read arbitrary UPROPERTY values on a PIE actor's AnimInstance (#139). Params: `actorLabel, propertyNames? (omit = dump all)` |
| `get_pie_subsystem_state` | Read UPROPERTY values on a running subsystem in PIE (#139). Params: `subsystemClass, scope? (game\\|world\\|engine\\|localplayer), propertyNames?` |
| `create_game_mode` | Create GameMode BP. Params: `name, packagePath?, parentClass?, defaults?` |
| `create_game_state` | Create GameState BP. Params: `name, packagePath?, parentClass?` |
| `create_player_controller` | Create PlayerController BP. Params: `name, packagePath?, parentClass?` |
| `create_player_state` | Create PlayerState BP. Params: `name, packagePath?` |
| `create_hud` | Create HUD BP. Params: `name, packagePath?` |
| `set_world_game_mode` | Set level GameMode override. Params: `gameModePath` |
| `get_framework_info` | Get level framework classes |
| `get_navmesh_details` | Read RecastNavMesh generation params (cellSize, agentHeight, maxStepHeight, etc.) (#163) |
| `apply_damage_in_pie` | Apply damage to PIE actor. Params: `actorLabel, baseDamage?, damageTypeClass? (#186)` |

---

## gas

*Gameplay Ability System: abilities, effects, attribute sets, cues.*

| Action | Description |
|--------|-------------|
| `add_asc` | Add AbilitySystemComponent. Params: `blueprintPath, componentName?` |
| `create_attribute_set` | Create AttributeSet BP. Params: `name, packagePath?` |
| `add_attribute` | Add attribute to set. Params: `attributeSetPath, attributeName, defaultValue?` |
| `create_ability` | Create GameplayAbility BP. Params: `name, packagePath?, parentClass?` |
| `set_ability_tags` | Set tags on ability. Params: `abilityPath, ability_tags?, cancel_abilities_with_tag?, activation_required_tags?, activation_blocked_tags?` |
| `create_effect` | Create GameplayEffect BP. Params: `name, packagePath?, durationPolicy?` |
| `set_effect_modifier` | Add modifier. Params: `effectPath, attribute, operation?, magnitude?` |
| `create_cue` | Create GameplayCue. Params: `name, packagePath?, cueType?` |
| `get_info` | Inspect GAS setup. Params: `blueprintPath` |

---

## networking

*Networking and replication: actor replication, property replication, net relevancy, dormancy.*

| Action | Description |
|--------|-------------|
| `set_replicates` | Enable actor replication. Params: `blueprintPath, replicates?` |
| `set_property_replicated` | Mark variable as replicated. Params: `blueprintPath, propertyName, replicated?, replicationCondition?, repNotify?` |
| `configure_net_frequency` | Set update frequency. Params: `blueprintPath, netUpdateFrequency?, minNetUpdateFrequency?` |
| `set_dormancy` | Set net dormancy. Params: `blueprintPath, dormancy` |
| `set_net_load_on_client` | Control client loading. Params: `blueprintPath, loadOnClient?` |
| `set_always_relevant` | Always network relevant. Params: `blueprintPath, alwaysRelevant?` |
| `set_only_relevant_to_owner` | Only relevant to owner. Params: `blueprintPath, onlyRelevantToOwner?` |
| `configure_cull_distance` | Net cull distance. Params: `blueprintPath, netCullDistanceSquared?` |
| `set_priority` | Net priority. Params: `blueprintPath, netPriority?` |
| `set_replicate_movement` | Replicate movement. Params: `blueprintPath, replicateMovement?` |
| `get_info` | Get networking info. Params: `blueprintPath` |

---

## demo

*Neon Shrine demo scene builder and cleanup.*

| Action | Description |
|--------|-------------|
| `step` | Execute demo step. Params: `stepIndex?` |
| `cleanup` | Remove demo assets and actors |

---

## feedback

*Submit feedback to improve ue-mcp when native tools fall short and execute_python was used as a workaround.*

| Action | Description |
|--------|-------------|
| `submit` | Submit feedback about a tool gap. Params: `title, summary, pythonWorkaround?, idealTool?` |
