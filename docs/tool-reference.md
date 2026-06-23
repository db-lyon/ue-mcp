# Tool Reference

UE-MCP exposes **<!-- count:tools -->21<!-- /count --> category tools** covering **<!-- count:actions -->569+<!-- /count --> actions**, plus a `flow` tool for running multi-step YAML workflows. Every category tool takes an `action` parameter that selects the operation, plus action-specific parameters.

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
| `write_source_file` | Write a .h/.cpp/.inl into a named module's Public/Private folder (resolves the module dir for you, including plugin modules under Plugins/*/Source/ that write_cpp_file refuses). After a new file, build_project + restart; after a body edit, live_coding_compile. Params: `module (module name, default the project's primary module), visibility (Public\\|Private, default Private), fileName, content` |
| `read_source_file` | Read a .h/.cpp/.inl from a named module's folder (companion to write_source_file; resolves plugin modules too). With no visibility it tries Public then Private then the module root. Params: `module, visibility?, fileName` |
| `add_module_dependency` | Add a module to a target module's Build.cs dependency array. Params: `moduleName (the Build.cs to edit - must exist in the project), dependency (module name to add, e.g. 'UMG'), access? ('public'\\|'private', default 'private')` |
| `add_cpp_member` | Append a UPROPERTY/UFUNCTION declaration to an existing UCLASS header inside the access specifier you choose. Idempotent: if a declaration containing the same memberName is already present, returns existed:true. Params: `headerPath (relative to Source/ or absolute), declaration (full multi-line UPROPERTY(...) / UFUNCTION(...) block plus its single-line member or function signature), memberName (the identifier the declaration introduces - used for idempotency), access? ('public'\\|'protected'\\|'private', default 'public')` |

---

## asset

*Asset management: list, search, read, CRUD, import meshes/textures, datatables, and curvetables.*

| Action | Description |
|--------|-------------|
| `list` | List assets via the AssetRegistry (sees /Game and every mounted plugin root). Params: `directory? (default /Game), classFilter?, recursive? (default true), maxResults? (default 2000)` |
| `search` | Search by name/class/path. Params: `query, directory?, maxResults?, searchAll?` |
| `read` | Read asset via reflection. Params: `assetPath` |
| `read_properties` | Read asset properties with values. propertyName accepts dotted/indexed paths into nested structs, array elements, and instanced subobjects (e.g. `Config.Traits[1].Params.Field`); landing on an array of subobjects also lists each element's index+class (#527). Params: `assetPath, propertyName?, includeValues?` |
| `duplicate` | Duplicate asset. Params: `sourcePath, destinationPath` |
| `rename` | Rename asset. Params: `assetPath, newName (or sourcePath, destinationPath), force?` |
| `bulk_rename` | Batched rename using IAssetTools::RenameAssets - single transaction with one redirector-fixup pass (matches Content Browser drag). Use this over looped rename for scene-referenced assets. World assets are rejected (status=rejected_world); use rename_asset which handles WP externals atomically (#409). Params: `renames[] where each entry is {sourcePath, destinationPath} OR {assetPath, newName}` |
| `move` | Move asset. Params: `sourcePath, destinationPath` |
| `delete` | Delete asset. On failure returns reason (open_in_editor / has_referencers / unknown) plus referencer list. Pass force=true to auto-close any open asset editors before deleting (#278). Params: `assetPath, force?` |
| `delete_batch` | Batch-delete assets. Per-path status (deleted/absent/failed) plus reason+referencers on failed entries (#278). Params: `assetPaths[], force?` |
| `create_data_asset` | Create UDataAsset instance of custom class. Params: `name, className (/Script/Module.ClassName or loaded name), packagePath?, properties? (key/value map)` |
| `save` | Save asset(s). Params: `assetPath?` |
| `save_all_dirty` | Flush every dirty package to disk in one call. End-of-workflow shortcut after bulk import/edit. Params: `saveMapPackages? (default true), saveContentPackages? (default true)` |
| `set_mesh_material` | Assign material to static mesh slot. Params: `assetPath, materialPath, slotIndex?` |
| `recenter_pivot` | Move static mesh pivot to geometry center. Params: `assetPath OR assetPaths` |
| `import_static_mesh` | Import from FBX/OBJ. Params: `filePath, name?, packagePath?, combineMeshes?, importMaterials?, importTextures?, generateLightmapUVs?` |
| `import_skeletal_mesh` | Import skeletal mesh from FBX. Params: `filePath, name?, packagePath?, skeletonPath?, importMaterials?, importTextures?` |
| `import_animation` | Import anim from FBX. Params: `filePath, name?, packagePath?, skeletonPath` |
| `import_texture` | Import image. Params: `filePath, name?, packagePath?` |
| `import_texture_batch` | Import many textures in one call - the loop stays inside the editor (no per-file bridge round-trip), so this finishes far faster than N import_texture calls. Per-item result records mirror import_texture. Params: `items[]: [{filePath, packagePath?, name?, replaceExisting?}], packagePath? (default for items that don't set it), save? (default true), automated? (default true)` |
| `reimport` | Reimport asset from source file. Params: `assetPath, filePath?` |
| `read_datatable` | Read DataTable rows. Params: `assetPath, rowFilter?` |
| `create_datatable` | Create DataTable. Params: `name, packagePath?, rowStruct` |
| `reimport_datatable` | Reimport DataTable from JSON. Params: `assetPath, jsonPath?, jsonString?` |
| `set_datatable_row` | Append or overwrite a single DataTable row. Params: `assetPath, rowName, row (object with row-struct fields - partial updates merge with the existing row)` |
| `add_datatable_row` | Alias for set_datatable_row (#437) |
| `update_datatable_row` | Alias for set_datatable_row; partial update merges with existing row (#437) |
| `remove_datatable_row` | Remove a single DataTable row. Idempotent (alreadyDeleted=true if missing). Params: `assetPath, rowName (#437)` |
| `get_datatable_row` | Read one DataTable row's fields without dumping the whole table. Params: `assetPath, rowName (#535)` |
| `set_datatable_cell` | Write a single field on a single existing row (merges, leaves other cells untouched). Errors if the row doesn't exist. Params: `assetPath, rowName, fieldName, value (#535)` |
| `rename_datatable_row` | Rename a row key, preserving its values. Params: `assetPath, oldName, newName (#535)` |
| `fill_datatable_from_json` | Bulk-upsert rows from a {rowName: {field: value}} object without touching unrelated rows (non-destructive, unlike reimport_datatable). Params: `assetPath, rows (object) or jsonString (#535)` |
| `create_curvetable` | Create CurveTable asset. Params: `name, packagePath?, onConflict?` |
| `read_curvetable` | Read CurveTable rows and keys. Params: `assetPath, rowFilter?` |
| `list_curvetable_rows` | Alias for read_curvetable. Params: `assetPath, rowFilter?` |
| `import_curvetable` | Import CurveTable from JSON/CSV string or file. Params: `assetPath, jsonString?, csvString?, filePath?, format?, interpMode?` |
| `add_curvetable_row` | Add CurveTable row. Params: `assetPath, rowName, curveType? ('simple'|'rich'), interpMode?` |
| `remove_curvetable_row` | Remove CurveTable row. Idempotent if missing. Params: `assetPath, rowName` |
| `rename_curvetable_row` | Rename CurveTable row. Params: `assetPath, oldName, newName` |
| `get_curvetable_keys` | Read keys from one CurveTable row. Params: `assetPath, rowName` |
| `set_curvetable_keys` | Replace keys on one CurveTable row. Params: `assetPath, rowName, keys:[{time,value,interpMode?,arriveTangent?,leaveTangent?}]` |
| `add_curvetable_key` | Add or update one key on a CurveTable row. Params: `assetPath, rowName, time, value, interpMode?, keyTimeTolerance?` |
| `list_textures` | List textures. Params: `directory?, recursive?` |
| `get_texture_info` | Get texture details. Params: `assetPath` |
| `set_texture_settings` | Set texture settings. Params: `assetPath, settings (object with compressionSettings?, lodGroup?, sRGB?, neverStream?)` |
| `create_stringtable` | Create a StringTable asset. Params: `name, packagePath?, namespace?, onConflict?` |
| `read_stringtable` | Read StringTable entries and keys. Params: `assetPath, keyFilter?` |
| `list_stringtable_keys` | List StringTable keys. Params: `assetPath, keyFilter?` |
| `get_stringtable_entry` | Read one StringTable entry. Params: `assetPath, key` |
| `set_stringtable_entry` | Create or update one StringTable entry. Params: `assetPath, key, sourceString (or value)` |
| `remove_stringtable_entry` | Remove one StringTable entry. Idempotent (alreadyDeleted=true if missing). Params: `assetPath, key` |
| `import_stringtable` | Import StringTable entries from CSV. Params: `assetPath, filePath (or csvPath)` |
| `add_input_mapping` | Append an Enhanced Input key mapping to an InputMappingContext (InputAction + key by name string e.g. 'Mouse2D','LeftMouseButton'). Idempotent on (action,key). For modifiers/triggers use gameplay(set_mapping_modifiers). Same as gameplay(add_imc_mapping) (#525). Params: `mappingContext (IMC path), inputAction (IA path), key` |
| `remove_input_mapping` | Remove an IMC key mapping. Same as gameplay(remove_imc_mapping) (#525). Params: `mappingContext (IMC path), mappingIndex? \\| (inputAction? + key?)` |
| `list_input_mappings` | List an IMC's key->action bindings with triggers/modifiers. Same as gameplay(read_imc) (#525). Params: `mappingContext (IMC path)` |
| `add_socket` | Add socket to StaticMesh or SkeletalMesh. Idempotent on socket name; pass onConflict='update' to overwrite an existing socket's transform with the supplied relativeLocation/relativeRotation/relativeScale (#412). Params: `assetPath, socketName, boneName? (SkeletalMesh only, default 'root'), relativeLocation?, relativeRotation?, relativeScale?, onConflict? (skip\\\|update\\\|error, default skip)` |
| `remove_socket` | Remove socket by name. Params: `assetPath, socketName` |
| `list_sockets` | List sockets on a mesh (StaticMesh or SkeletalMesh). Params: `assetPath` |
| `set_socket_transform` | Update an existing socket's relative transform on StaticMesh or SkeletalMesh. Pass any subset of relativeLocation/relativeRotation/relativeScale; omitted fields stay at their current values. Errors if the socket does not exist (use add_socket to create). Common after FBX import when SOCKET_* empties land with scale=(100,100,100) (#412). Params: `assetPath, socketName, relativeLocation?, relativeRotation?, relativeScale?` |
| `set_property` | Set a UPROPERTY on any loaded asset (Material, DataAsset, DataTable, SubsurfaceProfile, etc.) using a dotted path. Walks nested structs, array elements by index, and instanced subobjects internally - no more read-modify-write copies (e.g. `settings.mean_free_path_distance` on a UMaterial, or `Config.Traits[1].Params.Field` on a config asset #527). Value goes through MCPJsonProperty::SetJsonOnProperty so JSON null clears object refs, structs accept {x,y,z}, arrays/maps round-trip. Params: `assetPath, propertyName (dotted path), value (#420)` |
| `set_texture_settings_by_type` | Apply the canonical (compressionSettings, sRGB, LOD group) combo to every texture in each group: normal -> Normalmap, grayscale -> Grayscale, baseColor -> Default sRGB, hdr -> HDR. Params: `groups (object: {normal?:[paths], grayscale?:[paths], baseColor?:[paths], hdr?:[paths]}) (#421)` |
| `create_interchange_pipeline` | One-call factory for a UInterchangeGenericAssetsPipeline asset with the 15-property mesh-import boilerplate already applied (RecomputeNormals=false, MikkTSpace=true, HighPrecisionTangents=true, BuildNanite=false, CreatePhysicsAsset=false, etc.). Params: `assetPath OR (name + packagePath?), meshType? (skeletal default \\| static), options? (dotted-path overrides on the resulting pipeline e.g. {'MeshPipeline.bBuildNanite': true}), onConflict? (#421)` |
| `reload_package` | Force reload an asset package from disk. Params: `assetPath` |
| `health_check` | Diagnose stuck-unloadable asset. Returns onDisk/inRegistry/isLoaded/canLoad/isStuck flags so an agent can detect the half-shutdown state where load returns null but the file exists (#279). Params: `assetPath` |
| `force_reload` | Aggressive reload that resets package loaders + GCs + LoadObject. Recovers from the half-shutdown state without an editor restart (#279). Closes any open editors first. Params: `assetPath` |
| `export` | Export asset to disk file (Texture2D → PNG, StaticMesh → FBX, etc.). Params: `assetPath, outputPath` |
| `search_fts` | Ranked asset search (token-scored over name/class/path). Params: `query, maxResults?, classFilter?` |
| `reindex_fts` | Rebuild the SQLite FTS5 asset index. Params: `directory?` |
| `get_referencers` | Reverse dependency lookup. Params: `packages[] OR packagePath (#150)` |
| `set_sk_material_slots` | Set materials on a USkeletalMesh by slot name or slotIndex (bypasses the blueprint override-materials path that UE's ICH silently reverts). Params: `assetPath, slots[{slotName?\\|slotIndex?, materialPath}]` |
| `diagnose_registry` | Scan a content path and compare disk vs AssetRegistry (including in-memory pending-kill entries). Returns onDiskCount, inMemoryIncludedCount, ghostCount and paths. Params: `path, recursive? (default true), reconcile? (forceRescan=true)` |
| `get_mesh_bounds` | Get StaticMesh OR SkeletalMesh bounding box. Params: `assetPath` |
| `get_mesh_info` | One-call mesh QA: bounds + material slots + skeleton + LOD/vertex counts. Works for both UStaticMesh and USkeletalMesh. Params: `assetPath` |
| `read_import_sources` | Read AssetImportData source filenames on an imported asset (StaticMesh, SkeletalMesh, Texture, Animation, etc.). Returns sources[] of {relativeFilename, absolutePath, timestamp, fileHash, displayLabelName}. Params: `assetPath (#270)` |
| `get_mesh_collision` | Inspect StaticMesh collision setup. Params: `assetPath` |
| `move_folder` | Move/rename entire content folder with redirector fixup in one transaction. Params: `sourcePath, destinationPath (#192)` |
| `create_folder` | Create empty content browser folder(s). Params: `path OR paths[] (e.g. /Game/Foo, /Game/Bar/Baz)` |
| `delete_folder` | Delete content browser folder(s) - counterpart to delete_asset, which leaves the parent directory entry behind as an orphan. Empty folders only by default; pass force=true to also delete any assets still inside (Content Browser 'Delete folder' equivalent). Per-path status (deleted/absent/failed) with reason (invalid_path/protected_path/not_empty/delete_failed) and a sample of contained assets on not_empty entries. Params: `path OR paths[], force?` |
| `set_mesh_nav` | Set StaticMesh nav contribution. Params: `assetPath, bHasNavigationData?, clearNavCollision? (#167)` |

---

## blueprint

*Blueprint reading, authoring, and compilation. Covers variables, functions, graphs, nodes, components, interfaces, and event dispatchers.*

| Action | Description |
|--------|-------------|
| `read` | Read BP structure incl. SCS components AND inherited native components from the CDO (CharacterMesh0, CharMoveComp, etc.). Params: `assetPath, includeComponentProperties? (dump UPROPERTY name/type/value per component template; off by default) (#353/#370)` |
| `list_variables` | List variables. Params: `assetPath` |
| `list_functions` | List functions/graphs. Params: `assetPath` |
| `read_graph` | Read graph nodes. Supports pagination and file dumps. Params: `assetPath, graphName, offset?, limit?, includePins?, includeDefaults?, includeComments?, dumpToFile?, outputPath?` |
| `read_graph_summary` | Lightweight graph summary (nodes+edges only, ~10KB). Params: `assetPath, graphName?` |
| `get_execution_flow` | Trace exec pins from an entry point. Params: `assetPath, graphName?, entryPoint?` |
| `get_dependencies` | Forward (classes/functions/assets) or reverse (referencers) deps. Params: `assetPath, reverse?` |
| `create` | Create Blueprint. Params: `assetPath, parentClass?` |
| `add_variable` | Add variable. Params: `assetPath, name, varType` |
| `set_variable_properties` | Edit variable properties. Params: `assetPath, name, instanceEditable?, blueprintReadOnly?, category?, tooltip?, replicationType?, exposeOnSpawn?` |
| `create_function` | Create function. Params: `assetPath, functionName` |
| `delete_function` | Delete function. Params: `assetPath, functionName` |
| `rename_function` | Rename function. Params: `assetPath, oldName, newName` |
| `add_node` | Add graph node. For a CallFunction node bound to a custom C++ UFUNCTION, pass nodeParams {functionName, className (or targetClass) = /Script/Module.Class}; the function also resolves against the BP's own component classes and an unambiguous loaded BlueprintCallable function, producing a bound node with pins instead of a stub (#546). Params: `assetPath, graphName?, nodeClass, nodeParams?` |
| `delete_node` | Delete node. Params: `assetPath, graphName, nodeName` |
| `set_node_property` | Set node pin default or struct property. Params: `assetPath, graphName, nodeName, propertyName, value` |
| `connect_pins` | Wire nodes. Params: `sourceNode, sourcePin, targetNode, targetPin, assetPath, graphName?` |
| `add_component` | Add BP component. componentClass accepts short names (e.g. 'ChildActorComponent') or full paths. For a ChildActorComponent, pass childActorClass to set its ChildActorClass in the same call (a Blueprint path with or without _C, or a C++ class) (#526). Params: `assetPath, componentClass, componentName?, parentComponent? (SCS parent for hierarchy - #115), childActorClass?` |
| `remove_component` | Remove SCS component. Params: `assetPath, componentName` |
| `set_component_property` | Set property on SCS or inherited component. Inherited components go through the child BP's InheritableComponentHandler override template so the parent stays untouched. Pass value=null to clear a TObjectPtr/SoftObject/WeakObject/UClass/Interface reference (e.g. clear AnimClass on CharacterMesh0) (#420). Params: `assetPath, componentName, propertyName, value` |
| `set_component_override_materials` | Write OverrideMaterials on a mesh-component template (StaticMeshComponent / SkeletalMeshComponent / any UMeshComponent). Pass materialPaths as a string[] of material asset paths (empty array clears). Avoids any TArray<UObject*> coercion on the generic set_component_property path (#442). Params: `assetPath, componentName, materialPaths` |
| `add_timeline_track` | Add a track to a Blueprint timeline. Creates the UTimelineTemplate if missing, builds the matching curve asset (float/vector/color/event), applies keyframes, bumps TimelineLength to cover the last key, then recompiles so K2Node_Timeline regenerates its output pins. Params: `assetPath, timelineName, trackName, trackType ('float'\\|'vector'\\|'color'\\|'event'), keyframes ([{time, value}])` |
| `set_capsule_size` | Call UCapsuleComponent::SetCapsuleSize on a CapsuleComponent template (CharacterMovement-friendly path; raw property writes leave the visualizer stale). Pass either or both of halfHeight/radius. Returns the new + previous values. Params: `assetPath, componentName, halfHeight?, radius? (#419)` |
| `get_component_property` | Read a single property value from an SCS or inherited component template. Returns the ICH override value for child BPs if one exists. Params: `assetPath, componentName, propertyName` |
| `set_class_default` | Set UPROPERTY on Blueprint CDO. Pass value=null to clear an object/class/interface reference (#420). Params: `assetPath, propertyName, value` |
| `delete_variable` | Delete a member variable. Params: `assetPath, name` |
| `add_function_parameter` | Add input or output parameter to a function. Params: `assetPath, functionName, parameterName, parameterType?, isOutput?` |
| `set_variable_default` | Set default value on a BP variable. Params: `assetPath, name, value` |
| `compile` | Compile Blueprint. Params: `assetPath` |
| `list_node_types` | List node types. Params: `category?, includeFunctions?` |
| `search_node_types` | Search nodes. Params: `query` |
| `create_interface` | Create BP Interface. Params: `assetPath` |
| `add_interface` | Implement interface. Params: `blueprintPath, interfacePath` |
| `list_graphs` | List all graphs in a blueprint. Params: `assetPath` |
| `add_event_dispatcher` | Add event dispatcher (multicast delegate variable + signature graph + UFunction). Without parameters, broadcasters fire void(). With parameters, the signature graph gets typed user pins so K2Node_CallDelegate compiles cleanly (#276). Params: `blueprintPath, name, parameters?: [{name, type}] where type is bool/int/float/string/name/text/vector/rotator/transform/object:/Script/Module.Class/struct:/Script/Module.Struct` |
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
| `compile_all` | Batch compile + save Blueprints. Params: `assetPaths[], save? (default true)` |
| `cleanup_graph` | Remove orphan/corrupted nodes (no class, blank title+no pins, missing target UFunction). Params: `assetPath, graphName? (default: every graph) (#285)` |
| `connect_pins_batch` | Apply many pin connections in one call (single compile + save). Params: `assetPath, graphName?, connections[]: [{sourceNode, sourcePin, targetNode, targetPin}] (#267)` |
| `set_node_position` | Move a graph node to (posX, posY). Params: `assetPath, graphName?, nodeId, posX, posY (#277)` |
| `auto_layout` | Topological layered layout for a graph. Eliminates the (0,0) stack from programmatic add_node. Params: `assetPath, graphName?, columnGap? (default 360), rowGap? (default 200) (#277)` |

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
| `remove_component` | Remove instance component from a level actor by name. Idempotent: returns alreadyDeleted=true if no matching component exists. Params: `actorLabel, componentName (#426)` |
| `set_component_property` | Set component prop. Pass value=null to clear a TObjectPtr/SoftObject/WeakObject/UClass/Interface reference (#420). Resolves inherited/SCS components on placed Blueprint instances case-insensitively, and refreshes the scene transform after RelativeLocation/RelativeRotation/RelativeScale3D writes (#539). Params: `actorLabel, componentName, propertyName, value` |
| `get_component_details` | Read a placed actor's component transforms. With componentName returns that component's relative+world location/rotation/scale, class, and attach parent; without it lists every component with its transform. Resolves SCS components on Blueprint instances (#539). Params: `actorLabel, componentName?` |
| `get_current` | Get current level name and path |
| `load` | Load level. Params: `levelPath` |
| `save` | Save current level |
| `list` | List levels. Params: `directory?, recursive?` |
| `create` | Create new level. Params: `levelPath?, templateLevel?` |
| `spawn_volume` | Place volume. Params: `volumeType, location?, extent?, label?` |
| `list_volumes` | List volumes. Params: `volumeType?` |
| `set_volume_properties` | Edit volume. Params: `actorLabel, properties` |
| `spawn_light` | Place light. Params: `lightType (point\\|spot\\|directional\\|rect\\|sky), location?, rotation?, intensity?, color? ({r,g,b} 0-255), mobility? (static\\|stationary\\|movable; default movable so the light renders without a build), label? (#331/#310)` |
| `set_light_properties` | Edit light. Params: `actorLabel, intensity?, color?, rotation? (DirectionalLight sun angle), mobility? (static\\|stationary\\|movable), recaptureSky?, temperature?, castShadows?, attenuationRadius?` |
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
| `get_component_tree` | Deep component-tree dump for an actor. Returns per-component: name, class, attachParent, attachSocket, mobility, visibility, relative+world transforms, tags. For PrimitiveComponents adds collisionProfile/collisionEnabled/bounds/castShadow. For StaticMeshComponent adds staticMesh + materials[]. For SkeletalMeshComponent adds skeletalMesh + skeleton + materials[]. Params: `actorLabel \\| actorPath, world? (editor\\|pie), componentClass? (substring filter), includeProperties? (dump UPROPERTY name/type/value per component) (#240/#241/#302/#320/#370/#353)` |
| `get_relative_transform` | Compute target's transform in reference's local space (location/rotation/scale). Common dungeon/calibration workflow. Params: `target (actor label), reference (actor label), world? (#386/#387)` |
| `resolve_actor` | Resolve internal/runtime actor name to editor label. Params: `internalName (e.g. StaticMeshActor_141)` |
| `set_actor_property` | Set per-instance UPROPERTY on a level actor. Params: `actorLabel ('WorldSettings' targets the world settings actor), propertyName (dotted paths like 'Foo.Bar' supported), value (string/number/bool/object/array; a label string resolves to an AActor* ref, and a JSON array of labels populates a TArray of actor refs #538), force? (bypass EditDefaultsOnly), world? (editor\\|pie) (#202/#230)` |
| `read_actor_motion` | Snapshot motion telemetry for one or many actors: location, rotation, velocity, scale, angularVelocity (when simulating physics), grounded + distanceToGround (downward 200u trace). Defaults to the PIE world with editor fallback. Loop at your sample interval for long telemetry probes. Params: `actorLabel? OR actorLabels (string[]), world? ('pie'\\|'editor') (#453)` |
| `add_hismc_instances` | Bulk-add transforms to a HISMC / ISMC component on an actor (Python add_instance crashes on UE 5.7; this is the C++ path). Params: `actorLabel, componentName? (default: first ISMC/HISMC found), transforms ([{location: {x,y,z}, rotation?: {pitch,yaw,roll}, scale?: {x,y,z}}]), worldSpace? (default true) (#434)` |
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
| `place_actors_batch` | Bulk-spawn StaticMeshActors with per-instance mesh + transform. Params: `actors[]: [{staticMesh, location?, rotation?, scale?, label?}]` |
| `line_trace` | Line trace in the editor world. Returns hit + actorLabel/actorClass/componentName/componentClass/location/impactPoint/normal/distance/faceIndex/boneName/physicalMaterial. Params: `start (Vec3), end? (Vec3) OR direction? (Vec3) + distance? (default 200000), ignoreActors? (array of labels) (#420)` |
| `snap_actor_to_floor` | Snap an actor's bounds-bottom to the first downward line-trace hit. Equivalent of the End-key shortcut, works on arbitrary geometry (not just Landscape). Params: `actorLabel, floorOffset? (added to impact Z, default 0), maxDistance? (default 100000) (#419)` |

---

## material

*Materials: create, read, parameters, shading, textures, and graph authoring (expression nodes, connections).*

| Action | Description |
|--------|-------------|
| `read` | Read material structure. Params: `assetPath` |
| `list_parameters` | List overridable parameters. Params: `assetPath` |
| `set_parameter` | Set parameter on MaterialInstance. Params: `assetPath, parameterName, parameterType, value` |
| `read_instance` | Read a MaterialInstanceConstant parent and override summary. Params: `assetPath` |
| `set_instance_parent` | Set a MaterialInstanceConstant parent. Params: `assetPath, newParentPath (or parentPath)` |
| `clear_instance_parameters` | Clear all MaterialInstanceConstant parameter overrides. Params: `assetPath` |
| `list_static_switches` | List static switch parameters on a Material or MaterialInstance. Params: `assetPath` |
| `set_static_switch` | Set a MaterialInstanceConstant static switch parameter. Params: `assetPath, parameterName, value, association?, parameterIndex?` |
| `set_expression_value` | Set value on expression node. Params: `materialPath, expressionIndex, value` |
| `disconnect_property` | Disconnect a material property input. Params: `materialPath, property` |
| `create_instance` | Create material instance. Params: `parentPath, name?, packagePath?` |
| `create` | Create material. Params: `name, packagePath?` |
| `create_function` | Create a MaterialFunction asset. Params: `name, packagePath? (default /Game/Materials/Functions), description? (#463)` |
| `add_function_expression` | Add an expression node to a MaterialFunction graph. Params: `functionPath, expressionType (e.g. Constant3Vector, FunctionInput, FunctionOutput, If), positionX?, positionY?, inputName? (for FunctionInput), inputType? (Scalar\\|Vector2\\|Vector3\\|Vector4\\|Texture2D\\|TextureCube\\|StaticBool\\|MaterialAttributes), outputName? (for FunctionOutput) (#463)` |
| `connect_function_expressions` | Wire two expressions inside a MaterialFunction. Params: `functionPath, sourceExpression (name or index), sourceOutput?, targetExpression (name or index), targetInput? (#463)` |
| `list_function_expressions` | List expression nodes inside a MaterialFunction. Params: `functionPath (#463)` |
| `create_simple` | Single-call simple material. Params: `name, packagePath?, baseColor? ({r,g,b}), metallic?, specular?, roughness?, emissive?, usages?[] (e.g. InstancedStaticMeshes, Nanite, NiagaraSprites)` |
| `set_usage` | Set EMaterialUsage flag(s) on a material. Params: `assetPath, usage OR usages[], enabled? (default true)` |
| `set_shading_model` | Set shading model. Params: `assetPath, shadingModel` |
| `set_blend_mode` | Set blend mode. Params: `assetPath, blendMode` |
| `set_domain` | Set material domain. Params: `assetPath, materialDomain (Surface \\| DeferredDecal \\| LightFunction \\| Volume \\| PostProcess \\| UI \\| RuntimeVirtualTexture)` |
| `set_base_color` | Set base color. Params: `assetPath, color` |
| `connect_texture` | Connect texture to property. Params: `materialPath, texturePath, property` |
| `add_expression` | Add expression node. Params: `materialPath, expressionType, name?, parameterName?, group?, sortPriority?, defaultValue? (scalar number or {r,g,b,a} for vector params), value? (number for Constant, {r,g,b} for Constant3Vector, {x,y} for Constant2Vector), channels? ({r,g,b,a} bools for ComponentMask), positionX?, positionY? (#318)` |
| `connect_expressions` | Wire two expressions. Params: `materialPath, sourceExpression, sourceOutput?, targetExpression, targetInput?` |
| `connect_to_property` | Wire expression to material output. Params: `materialPath, expressionName, outputName?, property` |
| `list_expressions` | List expression nodes. Params: `materialPath` |
| `delete_expression` | Remove expression. Params: `materialPath, expressionName` |
| `list_expression_types` | List available expression types |
| `recompile` | Recompile material. Pass recompileChildren=true to cascade to every MaterialInstanceConstant whose parent chain reaches this material (#421). Params: `materialPath, recompileChildren?` |
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
| `scan_animation_tracks` | Scan AnimSequence bone-track counts. Params: `directory?, recursive?, assetPaths?, skeletonPath?, targetTrackCount?, includeTrackNames?` |
| `read_blendspace` | Read blendspace. Params: `assetPath` |
| `add_blend_sample` | Append a sample to a BlendSpace. Params: `assetPath, animation (AnimSequence path), position {x,y} (or flat x,y) (#248)` |
| `set_blend_sample` | Move an existing BlendSpace sample or swap its animation. Params: `assetPath, sampleIndex, position? {x,y} (or flat x,y), animation? (#272)` |
| `list` | List anim assets. Params: `directory?, recursive?` |
| `create_montage` | Create montage. Params: `animSequencePath, name?, packagePath?` |
| `create_anim_blueprint` | Create AnimBP. Params: `skeletonPath, name?, packagePath?, parentClass?` |
| `create_blendspace` | Create blendspace (2D). Params: `skeletonPath, name?, packagePath?, axisHorizontal?, axisVertical?` |
| `create_blendspace_1d` | Create BlendSpace1D. Params: `skeletonPath, name?, packagePath?, axisName? (default Speed), axisMin?, axisMax?, gridNum? (#459)` |
| `populate_blendspace` | One-call axis params + samples authoring for BlendSpace 1D/2D. Params: `assetPath, axis? ({name?, min?, max?, gridNum?}) for axis 0, blendspaceAxes? (per-axis array), axisHorizontal?/axisVertical? + horizontalMin/horizontalMax/verticalMin/verticalMax/gridNumHorizontal/gridNumVertical (back-compat), samples ([{animationPath, x, y?}]), clearExisting? (default true) (#459)` |
| `add_notify` | Add notify. For PlayMontageNotify the notifyName is also written onto the spawned notify object so OnPlayMontageNotifyBegin broadcasts it (not 'None'), and montage branching-point markers refresh (#528). Params: `assetPath, notifyName, triggerTime, notifyClass?` |
| `remove_notify` | Remove notify(s) by name and/or class. Pass at least one of notifyName/notifyClass; both filters AND. Idempotent: alreadyDeleted=true if no match. Params: `assetPath, notifyName?, notifyClass? (#471)` |
| `get_skeleton_info` | Read skeleton. Params: `assetPath` |
| `list_sockets` | List sockets. Params: `assetPath` |
| `list_skeletal_meshes` | List skeletal meshes. Params: `directory?, recursive?` |
| `get_physics_asset` | Read physics asset. Params: `assetPath` |
| `create_sequence` | Create blank AnimSequence. Params: `name, skeletonPath, packagePath?, numFrames?, frameRate?` |
| `set_bone_keyframes` | Set bone transform keyframes. Params: `assetPath, boneName, keyframes` |
| `bake_keyframes_batch` | Bake per-bone keyframe arrays for many bones into an AnimSequence in one call. Auto-creates each bone track first (set_bone_keyframes silently leaves a T-pose if the track is missing), wraps the batch in one transaction, and raises if any bone fails instead of reporting hollow success (#540). Params: `assetPath, tracks ([{bone, keyframes:[{location,rotation{x,y,z,w},scale?}]}]), save? (default true)` |
| `get_bone_transforms` | Read reference pose transforms. Params: `skeletonPath, boneNames?, space? ('local' default, or 'component' for composed parent-chain transforms - retarget-chain / anatomical-scale work) (#245)` |
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
| `create_ik_retargeter` | Create IKRetargeter asset and (default) initialize the UE 5.7 ops stack: assigns sourceRig+targetRig to all ops, runs AutoMapChains. Returns chainsMapped count. Params: `name, packagePath?, sourceRig?, targetRig?, autoMapChains? (default true) (#246)` |
| `read_ik_retargeter` | Read IKRetargeter: source/target rigs and chain mappings. Params: `assetPath (#246)` |
| `set_anim_blueprint_skeleton` | Set target skeleton on AnimBP. Params: `assetPath, skeletonPath` |
| `read_bone_track` | Read bone transform samples from AnimSequence. Params: `assetPath, boneName, frames?: [int]` |
| `create_pose_search_database` | Create a PoseSearchDatabase asset (motion matching). Params: `name, packagePath?, schemaPath?` |
| `set_pose_search_schema` | Set the Schema on an existing PoseSearchDatabase. Params: `assetPath, schemaPath` |
| `add_pose_search_sequence` | Append an AnimSequence/AnimComposite/AnimMontage/BlendSpace to a PoseSearchDatabase. Params: `assetPath, sequencePath` |
| `build_pose_search_index` | Build (or rebuild) the search index. Params: `assetPath, wait? (default true)` |
| `read_pose_search_database` | Inspect a PoseSearchDatabase: schema, animation entries, cost biases, tags. Params: `assetPath` |
| `set_sequence_properties` | Batch-set properties on AnimSequence assets. If a path is a Montage and resolveFromMontages is true (default), resolves to its first AnimSequence. Params: `assetPaths[], properties{enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?}, resolveFromMontages?` |
| `bake_root_motion_from_bone` | Bake delta translation from a source bone (e.g. pelvis) onto the root bone across the whole sequence; compensates the source bone so world-space position is unchanged. Params: `assetPath, sourceBone, rootBone? (default 'root'), axes? (default ['x','y']), interpolation? ('linear'\\|'per_frame', default 'linear')` |
| `get_bone_transform` | Read a bone or socket transform on a live actor's SkeletalMeshComponent. Wraps GetBoneTransform / GetSocketTransform. Params: `actorLabel, boneName (or socket name), componentName? (default: CharacterMesh0 / Mesh / first SK component), world? (auto\\|pie\\|game\\|editor, default auto), space? (world\\|component\\|local, default world)` |
| `list_bones` | List bones in a live actor's SkeletalMeshComponent ref skeleton (name, index, parent). Params: `actorLabel, componentName?, world? (auto\\|pie\\|game\\|editor, default auto) (#420)` |
| `rebind_leader_pose` | Re-bind every secondary SkeletalMeshComponent on an actor to a body component (default CharacterMesh0 / Mesh). One-call fix for the 'character explodes after rotating the actor' failure mode. Params: `actorLabel, bodyComponent? (#419)` |
| `preview_animation` | Toggle bUpdateAnimationInEditor + VisibilityBasedAnimTickOption=AlwaysTickPoseAndRefreshBones on every SkeletalMeshComponent of an actor. Bypasses the 'cannot be edited on templates' guard for level instances. Params: `actorLabel, enabled (#419/#420)` |

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
| `set_material` | Set landscape material. Params: `materialPath` |
| `add_layer_info` | Register paint layer (creates LayerInfo asset + binds to active landscape). Params: `layerName, packagePath?, weightBlended?` |
| `create_layer_info` | Standalone LayerInfo asset creation - no landscape required. Params: `layerName, name? (default LI_<layerName>), packagePath? (default /Game/Landscape/LayerInfos), physMaterial? (asset path), hardness? (#251)` |
| `create` | Spawn a new ALandscape with a flat heightmap. Defaults match the Editor's Landscape Mode 'create new' (8x8 components, 63 quads/subsection, 2 subsections/component = 1016x1016 quads). Params: `location? (Vec3), scale? (Vec3, default 100,100,100), componentCountX? (default 8), componentCountY? (default 8), subsectionSizeQuads? (one of 7\\|15\\|31\\|63\\|127\\|255, default 63), numSubsections? (1\\|2, default 2), heightOffset? (uint16, default 32768 = mid-elevation), label? (#303)` |
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
| `disconnect_nodes` | Remove a wired edge between two PCG nodes. Params: `assetPath, sourceNode, targetNode, sourcePin? (default: any), targetPin? (default: any)` |
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
| `create_type` | Create foliage type from mesh. Params: `meshPath, name?, packagePath?` |
| `set_settings` | Modify type settings. Params: `foliageTypeName, settings` |

---

## niagara

*Niagara VFX: systems, emitters, spawning, parameters, and graph authoring.*

| Action | Description |
|--------|-------------|
| `list` | List Niagara assets. Params: `directory?, recursive?` |
| `get_info` | Inspect system. Params: `assetPath` |
| `spawn` | Spawn VFX as a transient component (GC's before offscreen capture). For a findable preview use spawn_actor. Params: `systemPath, location, rotation?, label?` |
| `spawn_actor` | Spawn a PERSISTENT, labeled NiagaraActor in the editor world (findable, re-activatable, survives capture - unlike spawn). Assigns the system and activates. Params: `systemPath, location?, rotation?, label?, activate? (default true) (#537)` |
| `reactivate` | Reset + reactivate the NiagaraComponent on a placed actor (replay a burst before capturing). Params: `actorLabel (#537)` |
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
| `get_details` | Inspect widget (curated subset). Params: `assetPath, widgetName` |
| `get_properties` | Full reflected property dump for a widget - every UPROPERTY (RenderOpacity, Visibility, ColorAndOpacity, Border padding/colors, Image brush TintColor/ImageSize, fonts, etc.) plus the slot block, for diagnosing visual bugs get_details omits. Pass includeSubtree to also dump children (#547). Params: `assetPath, widgetName, includeSubtree?` |
| `list_bindings` | List designer property bindings on a WidgetBlueprint (the UE 5.7 Python API keeps them protected). Returns {widgetName, propertyName, functionName, bindingType}. Optional filterWidgetName/filterProperty (#530). Params: `assetPath, filterWidgetName?, filterProperty?` |
| `clear_binding` | Remove designer binding(s) matching widgetName (and optional propertyName) from a WidgetBlueprint without opening the editor. Idempotent (#530). Params: `assetPath, widgetName, propertyName?` |
| `set_property` | Set widget property. Slot struct props take UE struct text that persists every field - `Slot.Size`=`(Value=1.0,SizeRule=Fill)`, `Slot.Padding`=`(Left=8,Top=8,Right=8,Bottom=8)` - or a nested field path like `Slot.Size.Value` / `Slot.Padding.Left`; an invalid value errors instead of silently writing 0 (#532). Params: `assetPath, widgetName, propertyName, value` |
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
| `set_root` | Replace WBP root with an existing widget by name (#365). Params: `assetPath, widgetName` |
| `wrap_root` | Wrap the current root in a new panel widget (UMG 'Wrap With'). Params: `assetPath, wrapperClass (must be a UPanelWidget subclass), wrapperName? (#365)` |
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
| `build_project` | Build the project's C++ code using Unreal Build Tool. Editor should be stopped first |
| `execute_command` | Run console command. Params: `command` |
| `execute_python` | Run Python in editor. Params: `code` |
| `run_python_file` | Run a Python file from disk with __file__/__name__ populated (#142). Params: `filePath, args?` |
| `set_property` | Set UObject property. Supports dotted/indexed paths into structs, arrays, and subobjects. Params: `objectPath, propertyName, value` |
| `get_property` | Read a UObject property as structured JSON plus UE export text. Supports dotted/indexed paths into structs, arrays, and subobjects. Params: `objectPath, propertyName` |
| `describe_object` | Describe a UObject, resolving Blueprint/class paths to their CDOs and optionally returning reflected property metadata/values. Params: `objectPath, includeProperties?, includeValues?, propertyNames?` |
| `play_in_editor` | PIE control. Params: `pieAction (start\\|stop\\|status), waitForAssetRegistry? (start only; default true - block until the AssetRegistry initial scan completes before requesting PIE, otherwise PIE silently no-ops on cold editor starts), assetRegistryTimeoutSeconds? (default 180) (#406)` |
| `get_runtime_value` | Read PIE actor property. Params: `actorLabel, propertyName (supports dotted paths: component.field or component.struct.field for nested reads on component subobjects, #344/#381)` |
| `get_pie_pawn` | Resolve the controlled pawn in the active PIE world. Params: `playerIndex? (default 0)` |
| `invoke_function` | Call a BlueprintCallable / Exec UFUNCTION on a target actor or one of its components. Params: `actorLabel, functionName, component? (component subobject name; redirects target from the actor to that component, #382), args? (object), actorArgs? (object mapping UObject* parameter name to actor label, resolved against live actors in the active world; #383), world? (editor\\|pie)` |
| `invoke_static_function` | Call a static UFUNCTION on a UBlueprintFunctionLibrary (no actor instance). invoke_function needs an actor/component target; this targets the library class CDO instead, so it reaches static *_BlueprintOnly libraries (Voxel sculpt/query/stamp), GeometryScript, Kismet math, any function library. Params: `className (short name or /Script/Module.Class path), functionName, args? (name -> JSON value, same marshalling as invoke_function), actorArgs? (name -> actor label for UObject* params that are actors, e.g. the sculpt actor), worldContextParam? (name of a UObject* param to fill with the editor/PIE world; auto-detected for params named WorldContextObject), world? (editor\\|pie)` |
| `list_function_libraries` | Enumerate UBlueprintFunctionLibrary subclasses on this build. Filter by name (case-insensitive substring, e.g. 'GeometryScript' / 'Kismet' / 'Animation'). Returns name, module, and (by default) every static BlueprintCallable function on the library with its tooltip. Use to discover what's available for editor.invoke_function (#455). Params: `pattern?, includeFunctions?` |
| `set_pie_time_scale` | Fast-forward PIE game time. Params: `factor (>0)` |
| `hot_reload` | Hot reload C++ |
| `undo` | Undo last transaction |
| `redo` | Redo last transaction |
| `get_perf_stats` | Editor performance stats |
| `run_stat` | Run stat command. Params: `command` |
| `set_scalability` | Set quality. Params: `level` |
| `capture_screenshot` | Screenshot. Params: `filename?, resolution?, target? (auto\\|pie\\|editor; auto routes to PIE viewport when PIE is running) (#226)` |
| `capture_scene_png` | Headless PNG screenshot via SceneCapture2D (works unfocused, guaranteed RGBA8 LDR). Params: `outputPath, location?, rotation?, width? (default 1280), height? (default 720), fov? (default 90) (#148)` |
| `set_realtime` | Toggle realtime update on the level editor viewports so the editor-world sim (Niagara, anims) ticks - otherwise capture_scene_png renders an unticked, empty sim. Params: `enabled (default true) (#537)` |
| `get_viewport` | Get viewport camera |
| `hit_test_viewport_pixel` | Ray-cast from a screen pixel through the active editor viewport and return the first hit. Builds the ray from the live viewport's projection matrix (no FOV/aspect guessing). Returns hit + actorLabel/actorClass/componentName/componentClass/materialPath/location/impactPoint/normal/distance/faceIndex/boneName/physicalMaterial. Params: `x, y (pixel coords), width? height? (override viewport size when picking from a different-resolution screenshot), maxDistance? (default 200000), ignoreActors? (array of actor labels) (#418)` |
| `get_runtime_values` | Bulk runtime read across the active world. For each actor/component matching classFilter, resolves every path against the (actor\|component) root and returns rows of {actorLabel, actorClass, componentName?, componentClass?, values, errors?}. Paths support property hops, sub-object hops, and zero-arg BlueprintCallable getter calls at any segment (e.g. 'PowerConnector.GetRequired' reaches a UFUNCTION on a UObject sub-object). classFilter matches actor class OR component class - omit to match everything. World defaults to PIE if running, else editor. Params: `classFilter?, paths[], world? (editor\\|pie) (#414)` |
| `set_viewport` | Set viewport camera. Params: `location?, rotation?` |
| `focus_on_actor` | Focus on actor. Params: `actorLabel` |
| `create_sequence` | Create Level Sequence. Params: `name, packagePath?` |
| `get_sequence_info` | Read sequence. Params: `assetPath, includeSectionDetails? (attach sockets, first transform key values per track)` |
| `add_sequence_track` | Add an empty track. Params: `assetPath, trackType, actorLabel?` |
| `add_sequence_section` | Add a section to a track (creating the track if needed), set its start/end in seconds, and for a CameraCut track bind it to a camera. Returns the section index + channel names to key. Params: `sequencePath, trackType (Transform\\|Float\\|Fade\\|CameraCut\\|Audio\\|Event\\|SkeletalAnimation), actorLabel? (binding scope), startSeconds?, endSeconds?, cameraActorLabel? (#548)` |
| `set_sequence_keyframes` | Add keyframes to a section channel. Transform channels: Location.X/Y/Z, Rotation.X/Y/Z (or friendly x/y/z, yaw/pitch/roll); Fade/Float: the float channel. Params: `sequencePath, trackType, actorLabel?, sectionIndex? (default 0), channel, keyframes ([{seconds, value}]), interpolation? (cubic\\|linear) (#548)` |
| `set_sequence_playback_range` | Set a Level Sequence's playback range in seconds. Params: `sequencePath, startSeconds, endSeconds (#548)` |
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
| `save_dirty` | Flush every dirty package and return a per-package saved/failed map. Use after multi-step CDO/component edits when set_class_default leaves the asset dirty without persisting (#378). Params: `includeMaps? (default true), includeContent? (default true)` |
| `configure_pie` | Set ULevelEditorPlaySettings - multi-client PIE, net mode, single-process flag. Params: `numClients?, netMode? (standalone\\|listen\\|client), runUnderOneProcess?, launchSeparateServer? (#384)` |
| `get_pie_config` | Read current ULevelEditorPlaySettings (numClients, netMode, single-process, separate-server) (#384) |
| `list_dirty_packages` | Enumerate currently-dirty content + map packages (#340) |

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
| `create_enum` | Create UUserDefinedEnum asset, optionally seeded with entries. Params: `name, packagePath?, entries?: (string\\|{name, displayName?})[], onConflict? (#274)` |
| `set_enum_entries` | Replace entries on an existing UUserDefinedEnum. Params: `assetPath, entries[] (#274)` |

---

## gameplay

*Gameplay systems: physics, collision, navigation, input, behavior trees, AI (EQS, perception, State Trees, Smart Objects), game framework.*

| Action | Description |
|--------|-------------|
| `set_collision_profile` | Set collision preset. Params: `actorLabel, profileName` |
| `set_simulate_physics` | Toggle physics. Params: `actorLabel, simulate` |
| `set_collision_enabled` | Set collision mode. Params: `actorLabel, collisionEnabled` |
| `set_collision` | Unified collision authoring for a placed actor (actorLabel) or a Blueprint component template (assetPath+componentName). Apply any of: collisionProfile, collisionEnabled (NoCollision\|QueryOnly\|PhysicsOnly\|QueryAndPhysics), objectType (channel name), responseToAllChannels (Block\|Overlap\|Ignore), responses ({channel: Block\|Overlap\|Ignore}). Profile is applied first, then overrides. componentName optional for actors (defaults to all primitive components), required for Blueprint templates (#545) |
| `set_physics_properties` | Set mass/damping/gravity. Params: `actorLabel, mass?, linearDamping?, angularDamping?, enableGravity?` |
| `rebuild_navigation` | Rebuild navmesh |
| `find_nav_path` | Synchronous nav-path query between two world points. Returns valid/partial/length plus the polyline. The standard 'why doesn't my AI move?' diagnostic. Params: `start (Vec3), end (Vec3), pathfindingContext? (actorLabel - uses its agent + filter) (#424)` |
| `list_nav_invokers` | Enumerate actors carrying a NavigationInvokerComponent + their tile generation/removal radii. Diagnoses 'no navmesh in this region' caused by missing or mis-sized invokers (#424) |
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
| `remove_imc_mapping` | Remove an IMC mapping. Params: `imcPath, mappingIndex? \\| (inputActionPath? + key?) (#158)` |
| `set_imc_mapping_key` | Rebind an IMC mapping to a new key. Params: `imcPath, newKey, mappingIndex? \\| key? \\| inputActionPath? (#158)` |
| `set_imc_mapping_action` | Retarget an IMC mapping to a different InputAction. Params: `imcPath, newInputActionPath, mappingIndex? \\| key? \\| inputActionPath? (#158)` |
| `list_behavior_trees` | List behavior trees. Params: `directory?, recursive?` |
| `get_behavior_tree_info` | Inspect behavior tree (top-level + blackboard). Params: `assetPath` |
| `read_behavior_tree_graph` | Walk BT tree: composites, tasks, decorators, services with blackboard keys. Params: `assetPath` |
| `create_blackboard` | Create Blackboard. Params: `name, packagePath?` |
| `add_blackboard_key` | Add a typed key to a Blackboard asset. Params: `blackboardPath, keyName, keyType (Bool\\|Int\\|Float\\|String\\|Name\\|Vector\\|Rotator\\|Object\\|Class\\|Enum), baseClass? (for Object/Class types; e.g. /Script/Engine.Actor) (#250)` |
| `remove_blackboard_key` | Remove a key from a Blackboard asset by name. Idempotent. Params: `blackboardPath, keyName (#469)` |
| `set_blackboard_parent` | Set Parent on a BlackboardData asset (canonical UE child-of-parent pattern). Pass parentPath="None" or omit to clear. autoPruneDuplicateKeys (default true) removes own-keys that the parent chain already defines so the BT compiler accepts the child (#469) |
| `read_blackboard` | Read a Blackboard asset: parent path, ownKeys, inheritedKeys (walks the parent chain). Params: `blackboardPath (#469)` |
| `list_bt_node_classes` | Enumerate every concrete BehaviorTree node class on this build (composites, tasks, decorators, services). Filter by kind to narrow. Useful for discovering plugin-supplied decorator/task classes without grepping engine + plugin source. Params: `kind? ('composite'\\|'task'\\|'decorator'\\|'service') (#494)` |
| `set_behavior_tree_blackboard` | Rebind a BehaviorTree asset's BlackboardAsset reference. Params: `behaviorTreePath, blackboardPath` |
| `create_behavior_tree` | Create behavior tree. Params: `name, packagePath?, blackboardPath?` |
| `create_eqs_query` | Create EQS query. Params: `name, packagePath?` |
| `list_eqs_queries` | List EQS queries. Params: `directory?` |
| `add_perception` | Add AIPerceptionComponent. Params: `blueprintPath, senses?` |
| `configure_sense` | Add + configure an AI perception sense config on the blueprint's AIPerceptionComponent. Params: `blueprintPath, senseType (Sight\\|Hearing\\|Damage\\|Touch\\|Team\\|Prediction\\|Blueprint), settings? ({SightRadius: ...}), componentName?` |
| `create_state_tree` | Create StateTree. Params: `name, packagePath?` |
| `list_state_trees` | List StateTrees. Params: `directory?` |
| `add_state_tree_component` | Add StateTreeComponent. Params: `blueprintPath` |
| `create_smart_object_def` | Create SmartObjectDefinition. Params: `name, packagePath?` |
| `add_smart_object_component` | Add SmartObjectComponent. Params: `blueprintPath` |
| `add_smart_object_slot` | Append a FSmartObjectSlotDefinition to a SmartObjectDefinition's Slots array. Params: `assetPath, name?, offset? ({x,y,z}), rotation? ({pitch,yaw,roll}), tags? (array)` |
| `set_smart_object_slot` | Mutate an existing slot's offset/rotation/tags. Params: `assetPath, slotIndex, offset? ({x,y,z}), rotation? ({pitch,yaw,roll}), tags? (array)` |
| `remove_smart_object_slot` | Remove a slot by index. Idempotent: out-of-range returns alreadyDeleted=true. Params: `assetPath, slotIndex (#416)` |
| `list_smart_object_slots` | List slots on a SmartObjectDefinition with index, offset, rotation, and raw text. Params: `assetPath (#416)` |
| `add_smart_object_slot_behavior` | Attach a behavior definition (UBehaviorDefinition asset or class) to a slot's BehaviorDefinitions array. Pass instanceProperties to seed UPROPERTYs on a freshly-spawned class-instance. Params: `assetPath, slotIndex, behaviorClass (asset path or class path), instanceProperties? (#416)` |
| `create_game_mode` | Create GameMode BP. Params: `name, packagePath?, parentClass?, defaults?` |
| `create_game_state` | Create GameState BP. Params: `name, packagePath?, parentClass?` |
| `create_player_controller` | Create PlayerController BP. Params: `name, packagePath?, parentClass?` |
| `create_player_state` | Create PlayerState BP. Params: `name, packagePath?` |
| `create_hud` | Create HUD BP. Params: `name, packagePath?` |
| `set_world_game_mode` | Set level GameMode override. Params: `gameModeClass (or legacy gameModePath)` |
| `get_framework_info` | Get level framework classes |
| `get_navmesh_details` | Read RecastNavMesh generation params (cellSize, agentHeight, maxStepHeight, etc.) (#163) |

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
| `set_asc_defaults` | Wire an AttributeSet onto a Blueprint's ASC component (DefaultStartingData) so attributes exist at runtime. Params: `blueprintPath, attributeSet (content path or class name), componentName?, initDataTable? (starting values)` |
| `apply_effect` | Apply a GameplayEffect to a live actor's ASC (agnostic stat/damage stimulus - uses the game's own effect). Params: `actorLabel, effectClass (content path or class name), level?, setByCaller? ({tag-or-name: magnitude}), world? (auto\\|pie\\|editor, default auto)` |
| `set_attribute` | Set a gameplay attribute's base value on a live actor's ASC (recalculates CurrentValue through the aggregator). Params: `actorLabel, attribute (Health \\| SetName.Health), value, world?` |
| `get_attribute` | Read gameplay attribute base + current values on a live actor's ASC. Omit attribute to list all. Params: `actorLabel, attribute?, world?` |
| `init_asc` | Initialize a live actor's ASC (InitAbilityActorInfo) and optionally instantiate an AttributeSet so attributes are live - the runtime setup step for testing a bridge-authored GAS actor. Params: `actorLabel, attributeSet? (content path or class name), world?` |

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
| `cleanup` | Remove demo assets and actors. Switches editor to /Game/MCP_Home before deleting so the editor is never left on an Untitled map |
| `go_home` | Switch the editor to /Game/MCP_Home (creating it on first use). Use this before any operation that would leave the editor on an Untitled map |

---

## feedback

*Submit feedback to improve ue-mcp when native tools fall short and execute_python was used as a workaround.*

| Action | Description |
|--------|-------------|
| `submit` | Submit feedback about a tool gap. Blocks on an MCP elicitation prompt that asks the USER (not the agent) to approve or decline the exact payload before anything is posted to GitHub |

---

## statetree

*StateTree asset editing: read, modify states/tasks/conditions/transitions/bindings/evaluators/global tasks/colors/state parameters/root parameters, compile and validate.*

| Action | Description |
|--------|-------------|
| `read` | Full dump of a StateTree asset: state hierarchy (with description, tag, customTickRate, color), tasks, conditions, transitions, evaluators, global tasks, root params, bindings. Params: `assetPath` |
| `list_states` | List all states with IDs and paths. Params: `assetPath` |
| `add_state` | Add child state. Params: `assetPath, stateId? (parent GUID, omit for root), name, stateType? (State\\|Group\\|LinkedAsset\\|Subtree), selectionBehavior?, insertIndex?` |
| `remove_state` | Remove a state by ID. Params: `assetPath, stateId` |
| `set_state_property` | Set a property on a state. Params: `assetPath, stateId, propertyName (name\\|type\\|selectionBehavior\\|bEnabled\\|weight\\|linkedAsset\\|description\\|tag\\|customTickRate\\|color), value` |
| `clear_state_nodes` | Remove all tasks/conditions/transitions from a state. Params: `assetPath, stateId` |
| `add_task` | Add a task to a state. Params: `assetPath, stateId, structType (C++ struct name e.g. FMyStateTreeTask or an engine-shipped task like FStateTreeRunParallelStateTreesTask), instanceProperties?` |
| `add_enter_condition` | Add an enter condition to a state. Params: `assetPath, stateId, structType (C++ struct name), instanceProperties?, operand? (And\\|Or)` |
| `remove_enter_condition` | Remove an enter condition by index. Params: `assetPath, stateId, conditionIndex` |
| `remove_task` | Remove a task by index. Params: `assetPath, stateId, taskIndex` |
| `set_task_instance_property` | Set a property on a task's instance data. Params: `assetPath, stateId, taskIndex, propertyName, value` |
| `set_task_property` | Set a property on the task's node struct (FStateTreeTaskBase-level: bConsideredForCompletion, bTaskEnabled, bShouldStateChangeOnReselect). Distinct from set_task_instance_property which targets instance data. Params: `assetPath, stateId, taskIndex, propertyName, value (string-encoded e.g. 'true'/'false')` |
| `add_transition` | Add a transition to a state. Params: `assetPath, stateId, trigger (OnStateCompleted\\|OnStateSucceeded\\|OnStateFailed\\|OnTick\\|OnEvent; combine with \\| e.g. OnStateSucceeded\\|OnStateFailed), transitionType (GotoState\\|NextState\\|Succeeded\\|Failed), eventTag?, targetStateId?, targetStatePath?, priority? (Low\\|Normal\\|Medium\\|High\\|Critical), delayDuration?, bDelayTransition?` |
| `add_transition_condition` | Add a condition to an existing transition. Params: `assetPath, stateId, transitionIndex, structType, instanceProperties?, operand?` |
| `remove_transition` | Remove a transition by index. Params: `assetPath, stateId, transitionIndex` |
| `add_binding` | Add a property binding. Params: `assetPath, sourceStructId, sourcePath, targetStructId, targetPath` |
| `remove_binding` | Remove a property binding. Params: `assetPath, targetStructId, targetPath` |
| `list_bindings` | List all property bindings. Params: `assetPath, structId? (filter)` |
| `add_evaluator` | Add an evaluator to the StateTree (tree-level). Params: `assetPath, structType (must derive from FStateTreeEvaluatorBase), instanceProperties?` |
| `remove_evaluator` | Remove an evaluator by node ID. Params: `assetPath, nodeId` |
| `set_evaluator_instance_property` | Set a property on an evaluator's instance data. Params: `assetPath, nodeId, propertyName, value` |
| `set_evaluator_property` | Set a property on the evaluator's node struct (FStateTreeEvaluatorBase-level). Params: `assetPath, nodeId, propertyName, value` |
| `add_global_task` | Add a global task to the StateTree (tree-level). Params: `assetPath, structType (must derive from FStateTreeTaskBase), instanceProperties?` |
| `remove_global_task` | Remove a global task by node ID. Params: `assetPath, nodeId` |
| `set_global_task_instance_property` | Set a property on a global task's instance data. Params: `assetPath, nodeId, propertyName, value` |
| `set_global_task_property` | Set a property on a global task's node struct (FStateTreeTaskBase-level). Params: `assetPath, nodeId, propertyName, value` |
| `list_colors` | List all color palette entries for a StateTree. Params: `assetPath` |
| `add_color` | Add a new color to the StateTree palette. Params: `assetPath, displayName, color? (FLinearColor string e.g. '(R=1.0,G=0.0,B=0.0,A=1.0)')` |
| `list_state_parameters` | List parameters defined on a state. Params: `assetPath, stateId` |
| `add_state_parameter` | Add a parameter to a state's property bag. Rejects fixed-layout (linked) states. Params: `assetPath, stateId, paramName, paramType (Bool\\|Int32\\|Int64\\|Float\\|Double\\|Name\\|String\\|Text)` |
| `remove_state_parameter` | Remove a parameter from a state's property bag by name. Rejects fixed-layout (linked) states. Params: `assetPath, stateId, paramName` |
| `set_state_parameter` | Set the value of an existing state parameter. On fixed-layout states, also marks the parameter as overridden. Params: `assetPath, stateId, paramName, value` |
| `set_root_parameters` | Define root parameters (property bag). Params: `assetPath, parameters[] ({name, type}) where type is float\\|int32\\|bool\\|string\\|name\\|double` |
| `compile` | Compile a StateTree asset. Returns success, errors[], warnings[]. Params: `assetPath` |
| `validate` | Validate a StateTree asset without compiling. Params: `assetPath` |

---

## plugins

*Introspect npm-distributed plugins that contribute actions into other categories. Read-only.*

| Action | Description |
|--------|-------------|
| `list` | Every plugin loaded from ue-mcp.yml: name, version, prefix, status, and injected actions |
| `describe` | Full detail for one plugin including knowledge files and flows. Params: `name` |
