# ue-mcp v0.5.0 Release Notes

**61 issues resolved** across all 19 tool categories. This release dramatically expands what AI agents can build in Unreal Engine without falling back to Python workarounds.

---

## New Actions

### Blueprint (6 new actions)
- **remove_component**: Remove SCS components from blueprints (#22, #41, #55)
- **delete_variable**: Delete member variables (#77)
- **add_function_parameter**: Add input/output parameters to blueprint functions (#87)
- **set_variable_default**: Set default values on BP variables, including class reference arrays that can't be edited on the CDO (#92)
- Idempotent `create` — returns existing asset with `alreadyExisted: true` instead of triggering overwrite dialogs

### Animation (7 new actions)
- **read_anim_graph**: Read AnimBP AnimGraph nodes with properties via reflection (#23, #91)
- **add_curve**: Add float curves to AnimSequences (#24, #79)
- **set_montage_slot**: Set SlotAnimTrack slot name on montages (#78)
- **add_montage_section**: Add composite sections to montages (#27)
- **create_ik_rig** / **read_ik_rig**: Create and inspect IKRigDefinition assets (#93)
- **list_control_rig_variables**: Read ControlRig blueprint variables and graphs (#11)

### Input / PIE Runtime (4 new actions)
- **read_imc**: Read InputMappingContext mappings (keys, triggers, modifiers) (#57, #60)
- **add_imc_mapping**: Add key mappings to an IMC (#57, #60)
- **inspect_pie**: Inspect PIE world actors and components at runtime (#54, #89, #90)
- **get_pie_anim_state**: Read runtime AnimInstance state (montages, state machines) (#26)

### Level (2 new actions)
- **get_world_settings**: Read WorldSettings (GameMode, KillZ, gravity) (#21, #80)
- **set_world_settings**: Set DefaultGameMode and other world settings (#80)

### Material (2 new actions)
- **set_expression_value**: Set values on Constant, Constant3Vector, etc. expression nodes (#43)
- **disconnect_property**: Clear material property input connections (#43)

### Asset (1 new action)
- **reload_package**: Force reload an asset package from disk (#53)

### Project (2 new actions)
- **build**: Launch UnrealBuildTool to build C++ (#14)
- **generate_project_files**: Generate Visual Studio/Xcode project files (#49)

---

## Bug Fixes

### Crash Fixes
- **Editor startup crash**: Defer request processing until GEditor and editor world are fully initialized. Requests during startup now get a retry-friendly error instead of crashing (#56)
- **Niagara create crash**: Use NiagaraSystemFactoryNew instead of null factory (#61, #88)
- **open_asset crash**: Return error instead of crashing on StaticMesh and other asset types (#17)
- **Overwrite dialog crash**: Auto-decline "already exists" dialogs as safety net; create handlers return existing assets idempotently instead of triggering modal dialogs
- **capture_screenshot crash**: Find active viewport explicitly instead of silently failing when editor window isn't focused (#64)

### Blueprint Fixes
- **create_blueprint path normalization**: Accept short class names (`Actor`, `Pawn`, `Character`), module-qualified names, and full `/Script/` paths. Return canonical `objectPath` in response (#85, #86)
- **LoadBlueprint**: Try object path format (`/Game/Foo/Bar.Bar`) when package path fails (#85, #86)
- **add_node CallFunction**: Search library classes (GameplayStatics, KismetSystemLibrary, etc.) when function not found on target class. Parse `Class:Function` path format. Fixes broken "None" nodes (#84)
- **set_component_property**: Search inherited component templates on parent CDO when SCS node not found (#83)
- **search_node_types**: Include AnimGraph node classes (AnimGraphNode_ControlRig, etc.) in results (#9)
- **MakePinType**: Support struct types: FVector, FRotator, FTransform, FLinearColor, FGameplayTag, FGameplayTagContainer, and arbitrary structs (#8)
- **set_class_default**: Add TSubclassOf/FSoftClass handling, report ImportText failures with helpful errors (#30)
- **LeftChop path bug**: Fixed `LeftChop(1)` that corrupted package paths (e.g. `/Game/MCPTest` became `/Game/MCPTes`) in CreateBlueprint, GameplayHandlers, and GasHandlers

### Niagara Fixes
- **list**: Returns both system and emitter assets (#67)
- **spawn**: Parse nested `location`/`rotation` objects; default `autoDestroy=false` so VFX persists in editor (#66, #70)
- **add_emitter**: Actually calls `AddEmitterHandle` (was a stub returning fake success) (#69)
- **set_emitter_property**: Implemented with reflection on emitter data (was a stub) (#74)
- **get_emitter_info**: Returns simTarget, renderers, and properties (#68)

### Material Fixes
- **set_parameter**: `parameterType` now optional with auto-detection; base Materials get helpful error pointing to `set_expression_value` (#71, #72)
- **add_expression**: Short names like `Multiply`, `Constant`, `Lerp` now auto-resolve to full class names (#44)
- **read_material**: Returns expression-to-expression input connections (#44)
- **delete_expression**: Disconnects all references before removing (#44)

### Level Fixes
- **place_actor**: Resolves engine types like `StaticMeshActor`, `SkyAtmosphere` via broad class search (#59, #73)
- **get_outliner**: Includes child components per actor (#47)
- **set_component_property**: Prefix matching for UE default names like `StaticMeshComponent0` (#63)
- **spawn_volume**: Supports PostProcessVolume, AudioVolume, NavMeshBoundsVolume, and more (#62)

### Editor Fixes
- **get_log / search_log**: Ring-buffer log capture (4096 lines) replaces empty stub (#82)
- **set_property**: Uses `ImportText_Direct` for full UE text format support including nested struct arrays (#29)
- **read_properties**: Exports property values via `ExportText`, new `includeValues` param (#48)

### Input Fixes
- **create_input_action**: `valueType` parameter now applied (Boolean, Axis1D, Axis2D, Axis3D) (#50)

### Widget Fixes  
- **add_widget**: Register widget GUID before compile to prevent WidgetBlueprintCompiler assertion
- Widget tree construction with `parentWidgetName` parameter (#13, #81)

### Infrastructure
- **GameplayTag creation**: Uses INI file approach instead of `AddNativeGameplayTag` which asserts after init
- **FindObject warnings**: Silent `FindClassByShortName` tried before `LoadObject` to avoid log spam
- **Dialog safety net**: Default policies auto-decline overwrite dialogs to prevent game thread blocking
- **Test retry**: `callBridge` retries on "not ready" and connection loss

---

## Known Remaining Issues (7)

| # | Issue | Reason |
|---|-------|--------|
| 65 | Niagara emitter graph authoring | Requires NiagaraEditor private module APIs |
| 75 | UObject subobject serialization for IMC/IA | Requires deep UE serialization internals |
| 52 | Level Sequence binding details | Needs dedicated MovieScene handler |
| 76 | Nested chooser row editing | Requires ChooserTable plugin private APIs |
| — | Delegate signature warning | `CreateDefaultNodesForGraph` doesn't create proper delegate entry; cosmetic only |
| — | `afterAll` delete log noise | Asset registry race in test cleanup; harmless |
| — | `SetReplicates` CDO warning | Networking test sets replication on pre-init actor CDO; harmless |
