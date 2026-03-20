# Tool Reference

UE-MCP exposes **19 category tools** covering **300+ actions**. Every tool takes an `action` parameter that selects the operation, plus action-specific parameters.

!!! tip
    Start with `project(action="get_status")` to check the connection, then `level(action="get_outliner")` or `asset(action="list")` to explore.

---

## project — Project Status, Config INI, C++ Source

| Action | Description | Key Params |
|--------|-------------|------------|
| `get_status` | Server mode and editor connection status | |
| `set_project` | Switch to a different UE project (auto-deploys bridge) | `projectPath` |
| `get_info` | Read `.uproject` file details | |
| `read_config` | Parse an INI config file | `configName` |
| `search_config` | Search all INI files for a string | `query` |
| `list_config_tags` | Extract gameplay tags from config | |
| `set_config` | Write to an INI file | `configName`, `section`, `key`, `value` |
| `read_cpp_header` | Parse a `.h` file for UCLASS/USTRUCT/UENUM/UPROPERTY/UFUNCTION | `headerPath` |
| `read_module` | Read C++ module structure and Build.cs | `moduleName` |
| `list_modules` | List all C++ modules in Source/ | |
| `search_cpp` | Search `.h`/`.cpp` files for text | `query`, `directory?` |

!!! note
    `read_config`, `read_cpp_header`, `list_modules`, and `search_cpp` work without the editor (filesystem only).

---

## asset — Asset Management, Import, DataTables, Textures

| Action | Description | Key Params |
|--------|-------------|------------|
| `list` | List assets in a directory | `directory?`, `typeFilter?`, `recursive?` |
| `search` | Search by name/class/path (supports wildcards) | `query`, `directory?`, `maxResults?` |
| `read` | Read asset via reflection | `assetPath` |
| `read_properties` | Read specific properties of an asset | `assetPath`, `exportName?`, `propertyName?` |
| `duplicate` | Duplicate an asset | `sourcePath`, `destinationPath` |
| `rename` | Rename an asset | `assetPath`, `newName` |
| `move` | Move an asset to a new path | `sourcePath`, `destinationPath` |
| `delete` | Delete an asset | `assetPath` |
| `save` | Save asset(s) to disk | `assetPath?` (omit to save all) |
| `import_static_mesh` | Import static mesh from FBX/OBJ | `filePath`, `name?`, `packagePath?` |
| `import_skeletal_mesh` | Import skeletal mesh from FBX | `filePath`, `skeletonPath?` |
| `import_animation` | Import animation from FBX | `filePath`, `skeletonPath?` |
| `import_texture` | Import texture from PNG/TGA/EXR/HDR | `filePath` |
| `read_datatable` | Read DataTable rows | `assetPath`, `rowFilter?` |
| `create_datatable` | Create a DataTable | `name`, `rowStruct` |
| `reimport_datatable` | Reimport DataTable from JSON | `assetPath`, `jsonPath?`, `jsonString?` |
| `list_textures` | List texture assets | `directory?`, `recursive?` |
| `get_texture_info` | Get texture details (size, format, etc.) | `assetPath` |
| `set_texture_settings` | Set texture properties | `assetPath`, `settings` |

!!! note
    `search` auto-searches all configured content roots (see [Configuration](configuration.md)).

---

## blueprint — Reading, Authoring, Compilation

| Action | Description | Key Params |
|--------|-------------|------------|
| `read` | Read full Blueprint structure | `assetPath` |
| `list_variables` | List variables with types and flags | `assetPath` |
| `list_functions` | List functions and graphs | `assetPath` |
| `read_graph` | Read graph nodes and connections | `assetPath`, `graphName` |
| `create` | Create a new Blueprint | `assetPath`, `parentClass?` |
| `add_variable` | Add a variable | `assetPath`, `name`, `varType` |
| `set_variable_properties` | Edit variable flags | `assetPath`, `name`, `instanceEditable?`, `category?` |
| `create_function` | Create a function graph | `assetPath`, `functionName` |
| `delete_function` | Delete a function | `assetPath`, `functionName` |
| `rename_function` | Rename a function | `assetPath`, `oldName`, `newName` |
| `add_node` | Add a graph node | `assetPath`, `nodeClass`, `graphName?` |
| `delete_node` | Remove a node | `assetPath`, `graphName`, `nodeName` |
| `set_node_property` | Set a node property | `assetPath`, `graphName`, `nodeName`, `propertyName`, `value` |
| `connect_pins` | Wire two node pins together | `assetPath`, `sourceNode`, `sourcePin`, `targetNode`, `targetPin` |
| `add_component` | Add a component to the BP | `assetPath`, `componentClass` |
| `compile` | Compile the Blueprint | `assetPath` |
| `list_node_types` | List available node types by category | `category?` |
| `search_node_types` | Search for node types | `query` |
| `create_interface` | Create a Blueprint Interface | `assetPath` |
| `add_interface` | Implement an interface on a BP | `blueprintPath`, `interfacePath` |
| `add_event_dispatcher` | Add an event dispatcher | `blueprintPath`, `name` |

!!! tip "Graph Scripting Workflow"
    `search_node_types` → `add_node` → `connect_pins` is the standard flow for graph scripting.

---

## level — Actors, Selection, Components, Volumes, Lights, Splines

| Action | Description | Key Params |
|--------|-------------|------------|
| `get_outliner` | List all actors in the level | `classFilter?`, `nameFilter?` |
| `place_actor` | Spawn an actor | `actorClass`, `location?`, `rotation?`, `label?` |
| `delete_actor` | Remove an actor | `actorLabel` |
| `get_actor_details` | Inspect an actor's properties | `actorLabel` |
| `move_actor` | Transform an actor | `actorLabel`, `location?`, `rotation?`, `scale?` |
| `select` | Select actors in the editor | `actorLabels[]` |
| `get_selected` | Get current selection | |
| `add_component` | Add a component to an actor | `actorLabel`, `componentClass` |
| `set_component_property` | Set a component property | `actorLabel`, `componentName`, `propertyName`, `value` |
| `get_current` | Get current level info | |
| `load` | Open a level | `levelPath` |
| `save` | Save the current level | |
| `list` | List map assets | |
| `create` | Create an empty level | |
| `spawn_volume` | Place a volume | `volumeType`, `location?`, `extent?` |
| `list_volumes` | List volumes in the level | `volumeType?` |
| `set_volume_properties` | Edit volume properties | `actorLabel`, `properties` |
| `spawn_light` | Place a light | `lightType`, `location?`, `intensity?`, `color?` |
| `set_light_properties` | Edit light properties | `actorLabel`, `intensity?`, `color?`, `temperature?` |
| `build_lighting` | Build lighting | `quality?` |
| `get_spline_info` | Read spline component data | `actorLabel` |
| `set_spline_points` | Set spline points | `actorLabel`, `points[]`, `closedLoop?` |

---

## material — Materials, Instances, Shading, Graph Authoring

| Action | Description | Key Params |
|--------|-------------|------------|
| `read` | Read material structure | `assetPath` |
| `list_parameters` | List overridable parameters | `assetPath` |
| `set_parameter` | Set scalar/vector/texture parameter | `assetPath`, `parameterName`, `value` |
| `create_instance` | Create a material instance | `parentPath`, `name?` |
| `create` | Create a new material | `name`, `packagePath?` |
| `set_shading_model` | Set shading model | `assetPath`, `shadingModel` |
| `set_base_color` | Set base color | `assetPath`, `color {r,g,b}` |
| `connect_texture` | Wire a texture to a material property | `materialPath`, `texturePath`, `property` |
| `add_expression` | Add a material expression node | `assetPath`, `expressionType`, `name?` |
| `connect_expressions` | Connect two expression nodes | `assetPath`, `sourceExpression`, `sourceOutput`, `destExpression`, `destInput` |
| `connect_to_property` | Connect expression to a material property | `assetPath`, `expressionName`, `outputIndex?`, `property` |
| `list_expressions` | List expressions in a material | `assetPath` |
| `delete_expression` | Delete an expression node | `assetPath`, `expressionName` |
| `list_expression_types` | List available expression types | `filter?` |
| `recompile` | Recompile the material | `assetPath` |

---

## animation — Anim Assets, Skeletons, Montages, Blendspaces

| Action | Description | Key Params |
|--------|-------------|------------|
| `read_anim_blueprint` | Read AnimBP structure | `assetPath` |
| `read_montage` | Read montage sections and notifies | `assetPath` |
| `read_sequence` | Read animation sequence | `assetPath` |
| `read_blendspace` | Read blendspace samples | `assetPath` |
| `list` | List animation assets | `directory?`, `recursive?` |
| `create_montage` | Create montage from a sequence | `animSequencePath` |
| `create_anim_blueprint` | Create an AnimBP | `skeletonPath` |
| `create_blendspace` | Create a blendspace | `skeletonPath` |
| `add_notify` | Add a notify event | `assetPath`, `notifyName`, `triggerTime` |
| `get_skeleton_info` | Read skeleton bone hierarchy | `assetPath` |
| `list_sockets` | List skeleton sockets | `assetPath` |
| `list_skeletal_meshes` | List skeletal meshes in project | `directory?` |
| `get_physics_asset` | Read physics asset bodies | `assetPath` |
| `create_sequence` | Create blank AnimSequence | `name`, `skeletonPath`, `packagePath?`, `numFrames?`, `frameRate?` |
| `set_bone_keyframes` | Set bone transform keyframes | `assetPath`, `boneName`, `keyframes` |
| `get_bone_transforms` | Read reference pose transforms | `skeletonPath`, `boneNames?` |
| `set_montage_sequence` | Replace animation in a montage (auto-updates duration) | `assetPath`, `animSequencePath`, `slotIndex?` |
| `set_montage_properties` | Set montage duration, rate, and blending | `assetPath`, `sequenceLength?`, `rateScale?`, `blendIn?`, `blendOut?` |

!!! note
    Most animation tools need a skeleton path — use `list_skeletal_meshes` to find it.

---

## landscape — Terrain Sculpting, Painting, Layers

| Action | Description | Key Params |
|--------|-------------|------------|
| `get_info` | Get landscape setup (size, scale, layers) | |
| `list_layers` | List paint layers | |
| `sample` | Sample height and layer weights at a point | `x`, `y` |
| `list_splines` | Read landscape splines | |
| `get_component` | Inspect a landscape component | `componentIndex` |
| `sculpt` | Sculpt the heightmap | `x`, `y`, `radius`, `strength` |
| `paint_layer` | Paint a weight layer | `layerName`, `x`, `y`, `radius` |
| `set_material` | Set the landscape material | `materialPath` |
| `add_layer_info` | Register a paint layer | `layerName` |
| `import_heightmap` | Import a heightmap file | `filePath` |

---

## pcg — Procedural Content Generation

| Action | Description | Key Params |
|--------|-------------|------------|
| `list_graphs` | List PCG graph assets | `directory?` |
| `read_graph` | Read graph structure (nodes and edges) | `assetPath` |
| `read_node_settings` | Read a node's settings | `assetPath`, `nodeName` |
| `get_components` | List PCG components in the level | |
| `get_component_details` | Inspect a PCG component | `actorLabel` |
| `create_graph` | Create a new PCG graph | `name` |
| `add_node` | Add a node to the graph | `assetPath`, `nodeType` |
| `connect_nodes` | Wire two nodes | `assetPath`, `sourceNode`, `sourcePin`, `targetNode`, `targetPin` |
| `set_node_settings` | Set node parameters | `assetPath`, `nodeName`, `settings` |
| `remove_node` | Remove a node | `assetPath`, `nodeName` |
| `execute` | Regenerate PCG output | `actorLabel` |
| `add_volume` | Place a PCG volume in the level | `graphPath`, `location?`, `extent?` |

---

## foliage — Foliage Painting and Types

| Action | Description | Key Params |
|--------|-------------|------------|
| `list_types` | List foliage types in the level | |
| `get_settings` | Read foliage type settings | `foliageTypeName` |
| `sample` | Query foliage instances in a region | `center {x,y,z}`, `radius` |
| `paint` | Add foliage instances | `foliageType`, `center`, `radius` |
| `erase` | Remove foliage instances | `center`, `radius` |
| `create_type` | Create a foliage type from a StaticMesh | `meshPath` |
| `set_settings` | Modify foliage type settings | `foliageTypeName`, `settings` |

---

## niagara — VFX Systems and Graph Authoring

| Action | Description | Key Params |
|--------|-------------|------------|
| `list` | List Niagara assets | `directory?` |
| `get_info` | Inspect a Niagara system | `assetPath` |
| `spawn` | Spawn VFX at a location | `systemPath`, `location` |
| `set_parameter` | Set parameter on a spawned component | `actorLabel`, `parameterName`, `value` |
| `create` | Create a new Niagara System | `name` |
| `create_emitter` | Create a Niagara Emitter | `name` |
| `add_emitter` | Add an emitter to a system | `systemPath`, `emitterPath` |
| `list_emitters` | List emitters in a system | `assetPath` |
| `set_emitter_property` | Set emitter property | `assetPath`, `emitterName`, `propertyName`, `value` |
| `list_modules` | List modules in an emitter | `assetPath`, `emitterName` |
| `get_emitter_info` | Get detailed emitter info | `assetPath`, `emitterName` |

---

## audio — Sound Assets and Playback

| Action | Description | Key Params |
|--------|-------------|------------|
| `list` | List sound assets | `directory?` |
| `play_at_location` | Play a sound at a world location | `soundPath`, `location` |
| `spawn_ambient` | Place an ambient sound actor | `soundPath`, `location` |
| `create_cue` | Create a SoundCue | `name`, `soundWavePath?` |
| `create_metasound` | Create a MetaSoundSource | `name` |

---

## widget — UMG Widgets and Editor Utilities

| Action | Description | Key Params |
|--------|-------------|------------|
| `read_tree` | Read widget hierarchy | `assetPath` |
| `get_details` | Inspect a specific widget | `assetPath`, `widgetName` |
| `set_property` | Set a widget property | `assetPath`, `widgetName`, `propertyName`, `value` |
| `list` | List Widget Blueprints | `directory?` |
| `read_animations` | Read UMG animations | `assetPath` |
| `create` | Create a Widget Blueprint | `name` |
| `create_utility_widget` | Create an Editor Utility Widget | `name` |
| `run_utility_widget` | Open an Editor Utility Widget panel | `assetPath` |
| `create_utility_blueprint` | Create an Editor Utility Blueprint | `name` |
| `run_utility_blueprint` | Run an Editor Utility Blueprint | `assetPath` |

---

## editor — Console, Python, PIE, Viewport, Sequencer, Build, Logs

| Action | Description | Key Params |
|--------|-------------|------------|
| `execute_command` | Run a console command | `command` |
| `execute_python` | Run Python in the editor (escape hatch) | `code` |
| `set_property` | Set a UObject property by path | `objectPath`, `propertyName`, `value` |
| `play_in_editor` | PIE control | `pieAction` (start/stop/status) |
| `get_runtime_value` | Read a PIE actor property value | `actorLabel`, `propertyName` |
| `hot_reload` | Trigger Live Coding hot reload | |
| `undo` | Undo last editor transaction | |
| `redo` | Redo | |
| `get_perf_stats` | Get editor performance stats | |
| `run_stat` | Toggle stat overlay | `command` |
| `set_scalability` | Set quality/scalability level | `level` |
| `capture_screenshot` | Capture a viewport screenshot | `filename?`, `resolution?` |
| `get_viewport` | Get viewport camera transform | |
| `set_viewport` | Set viewport camera transform | `location?`, `rotation?` |
| `focus_on_actor` | Focus viewport on an actor | `actorLabel` |
| `create_sequence` | Create a Level Sequence | `name` |
| `get_sequence_info` | Read sequence tracks and keyframes | `assetPath` |
| `add_sequence_track` | Add a track to a sequence | `assetPath`, `trackType`, `actorLabel?` |
| `play_sequence` | Play/stop/pause a sequence | `assetPath`, `sequenceAction` |
| `build_all` | Run full build (geometry + lighting + navigation) | |
| `build_geometry` | Build geometry only | |
| `build_hlod` | Build HLODs | |
| `validate_assets` | Run data validation | |
| `get_build_status` | Check build status | |
| `cook_content` | Cook content for target platform | |
| `get_log` | Get output log entries | `category?` |
| `search_log` | Search log entries | `query` |
| `get_message_log` | Get message log | |
| `set_dialog_policy` | Auto-respond to modal dialogs | `dialogTitle`, `response` |
| `clear_dialog_policy` | Clear dialog auto-response | `dialogTitle?` |
| `get_dialog_policy` | Get current dialog policies | |
| `list_dialogs` | List pending modal dialogs | |
| `respond_to_dialog` | Respond to a specific dialog | `dialogId`, `response` |

!!! tip
    - `execute_python` is the escape hatch for anything not covered by dedicated tools.
    - `hot_reload` triggers Live Coding — no editor restart needed.
    - `get_log(category="LogMCPBridge")` shows bridge-specific logs.
    - Dialog management lets you suppress "Are you sure?" prompts during batch operations.

---

## reflection — UE Class/Struct/Enum Reflection, Gameplay Tags

| Action | Description | Key Params |
|--------|-------------|------------|
| `reflect_class` | Reflect a UClass (properties, functions, metadata) | `className`, `includeInherited?` |
| `reflect_struct` | Reflect a UScriptStruct | `structName` |
| `reflect_enum` | Reflect a UEnum | `enumName` |
| `list_classes` | List registered classes | `parentFilter?`, `limit?` |
| `list_tags` | List gameplay tags | `filter?` |
| `create_tag` | Create a gameplay tag | `tag`, `comment?` |

!!! tip
    Use `reflect_class` to understand any UE class's properties before working with it.

---

## gameplay — Physics, Collision, Navigation, Input, AI, Game Framework

| Action | Description | Key Params |
|--------|-------------|------------|
| `set_collision_profile` | Set collision preset on an actor | `actorLabel`, `profileName` |
| `set_simulate_physics` | Toggle physics simulation | `actorLabel`, `simulate` |
| `set_collision_enabled` | Set collision mode | `actorLabel`, `collisionEnabled` |
| `set_physics_properties` | Set mass, damping, gravity | `actorLabel`, `mass?`, `linearDamping?` |
| `rebuild_navigation` | Rebuild the navigation mesh | |
| `get_navmesh_info` | Query navigation system info | |
| `project_to_nav` | Project a point onto the navmesh | `location` |
| `spawn_nav_modifier` | Place a nav modifier volume | `location`, `extent?`, `areaClass?` |
| `create_input_action` | Create an Enhanced Input Action | `name`, `valueType?` |
| `create_input_mapping` | Create an Input Mapping Context | `name` |
| `list_input_assets` | List input-related assets | `directory?` |
| `list_behavior_trees` | List Behavior Trees and Blackboards | `directory?` |
| `get_behavior_tree_info` | Inspect a Behavior Tree | `assetPath` |
| `create_blackboard` | Create a Blackboard Data asset | `name` |
| `create_behavior_tree` | Create a Behavior Tree | `name`, `blackboardPath?` |
| `create_eqs_query` | Create an EQS query | `name` |
| `list_eqs_queries` | List EQS queries | |
| `add_perception` | Add AI Perception component | `blueprintPath` |
| `configure_sense` | Configure a perception sense | `blueprintPath`, `senseClass`, `settings` |
| `create_state_tree` | Create a State Tree | `name` |
| `list_state_trees` | List State Trees | |
| `add_state_tree_component` | Add State Tree component to an actor | `actorLabel` |
| `create_smart_object_def` | Create a Smart Object Definition | `name` |
| `add_smart_object_component` | Add Smart Object component | `actorLabel` |
| `create_game_mode` | Create a Game Mode BP | `name` |
| `create_game_state` | Create a Game State BP | `name` |
| `create_player_controller` | Create a Player Controller BP | `name` |
| `create_player_state` | Create a Player State BP | `name` |
| `create_hud` | Create a HUD BP | `name` |
| `set_world_game_mode` | Set the world's game mode | `gameModePath` |
| `get_framework_info` | Get game framework class assignments | |

---

## gas — Gameplay Ability System

| Action | Description | Key Params |
|--------|-------------|------------|
| `add_asc` | Add AbilitySystemComponent to a BP | `blueprintPath` |
| `create_attribute_set` | Create an AttributeSet Blueprint | `name`, `packagePath?` |
| `add_attribute` | Add an attribute to a set | `attributeSetPath`, `attributeName` |
| `create_ability` | Create a GameplayAbility Blueprint | `name`, `parentClass?` |
| `set_ability_tags` | Set activation/cancel/block tags | `abilityPath`, `ability_tags?`, `activation_required_tags?` |
| `create_effect` | Create a GameplayEffect Blueprint | `name`, `durationPolicy?` |
| `set_effect_modifier` | Add a modifier to an effect | `effectPath`, `attribute`, `operation?`, `magnitude?` |
| `create_cue` | Create a GameplayCue notify | `name`, `cueType?` (Static/Actor) |
| `get_info` | Inspect GAS setup on a Blueprint | `blueprintPath` |

---

## networking — Replication and Networking

| Action | Description | Key Params |
|--------|-------------|------------|
| `set_replicates` | Enable/disable actor replication | `blueprintPath`, `replicates?` |
| `set_property_replicated` | Mark a variable as replicated | `blueprintPath`, `propertyName` |
| `configure_net_frequency` | Set net update frequency | `blueprintPath`, `netUpdateFrequency?` |
| `set_dormancy` | Set net dormancy mode | `blueprintPath`, `dormancy` |
| `set_net_load_on_client` | Control client loading | `blueprintPath`, `loadOnClient?` |
| `set_always_relevant` | Always network relevant | `blueprintPath`, `alwaysRelevant?` |
| `set_only_relevant_to_owner` | Only relevant to owning connection | `blueprintPath`, `onlyRelevantToOwner?` |
| `configure_cull_distance` | Set net cull distance | `blueprintPath`, `netCullDistanceSquared?` |
| `set_priority` | Set net priority | `blueprintPath`, `netPriority?` |
| `set_replicate_movement` | Enable/disable movement replication | `blueprintPath`, `replicateMovement?` |
| `get_info` | Get current networking config | `blueprintPath` |

---

## demo — Neon Shrine Demo Scene

| Action | Description | Key Params |
|--------|-------------|------------|
| `step` | Execute a demo step (omit stepIndex for step list) | `stepIndex?` |
| `cleanup` | Remove all demo assets and actors | |

See **[Neon Shrine Demo](neon-shrine-demo.md)** for a full walkthrough.

---

## feedback — Agent Feedback Submission

| Action | Description | Key Params |
|--------|-------------|------------|
| `submit` | Submit feedback about a tool gap | `title`, `summary`, `pythonWorkaround?`, `idealTool?` |

See **[Feedback](feedback.md)** for details on how the feedback system works.
