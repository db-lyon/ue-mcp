# Open Issues — db-lyon/ue-mcp

*Generated 2026-04-08 — 68 open issues*

---

## Blueprint (13)

- [x] #92 Cannot set CDO default values for class reference arrays
- [x] #87 Blueprint tool missing add function parameter support
- [x] #86 Blueprint creation parentClass/path ambiguity
- [x] #85 Blueprint create/set defaults path and parentClass ambiguity
- [x] #84 add_node creates broken function call nodes with no pins
- [x] #83 Cannot set inherited component default properties
- [x] #77 Add delete_variable action to blueprint tool
- [x] #55 Remove component from blueprint SCS
- [x] #41 Cannot remove Blueprint SCS components via native tools
- [x] #22 No remove_component action on blueprint tool
- [x] #9 search_node_types does not find AnimGraph node types
- [x] #8 ControlRigBlueprint.add_member_variable crashes on struct types (FVector, FTransform)
- [x] #30 Cannot create PrimaryDataAsset instances or set gameplay tag properties

## Niagara (8)

- [x] #88 Niagara create action causes editor crash
- [x] #74 set_emitter_property returns success but may not actually modify emitter graph
- [x] #70 niagara spawn ignores location parameter
- [x] #69 list_emitters returns empty after add_emitter succeeds
- [x] #68 get_emitter_info returns no useful data
- [x] #67 list doesn't return emitter assets, only systems
- [x] #66 spawn creates auto-destroying actor that vanishes with empty emitters
- [ ] #65 No native tools for Niagara emitter graph authoring (requires NiagaraEditor internals)

## Animation (8)

- [x] #93 Cannot create/read IK Rigs and IK Retargeters
- [x] #91 Cannot read AnimGraph node properties (RetargetPoseFromMesh, etc.)
- [x] #79 Cannot add/set animation curves on AnimSequence
- [x] #78 Cannot set montage SlotAnimTrack slot name
- [x] #27 Cannot author multi-section montages with different sequences per section
- [x] #24 Cannot add float curves to animation sequences or montages
- [x] #23 Cannot read AnimBP AnimGraph nodes via read_graph
- [x] #11 No native tool for Control Rig graph construction

## Material (4)

- [x] #72 set_parameter doesn't accept parameterType in schema
- [x] #71 set_parameter fails on non-parameter expressions (Constant nodes)
- [x] #44 Material graph authoring via native tools is unreliable for multi-step operations
- [x] #43 Cannot disconnect material property inputs or set Constant3Vector values

## Level / Actors (7)

- [x] #73 place_actor can't resolve StaticMeshActor
- [x] #63 set_component_property can't find components by default name convention
- [x] #62 spawn_volume doesn't support PostProcessVolume
- [x] #59 place_actor fails for engine actor types like SkyAtmosphere
- [x] #58 level(action="create") fails — cannot create levels
- [x] #47 get_outliner doesn't show child components
- [x] #80 Set level WorldSettings default GameMode via native tool

## Input / PIE Runtime (7)

- [x] #90 Expose PIE runtime input binding inspection
- [x] #89 Add native PIE runtime controller/input binding inspection
- [x] #60 No native tool to read/modify IMC mappings
- [x] #57 No native tool to add key mappings to InputMappingContext
- [x] #54 PIE actor and component runtime inspection is insufficient
- [x] #50 create_input_action valueType parameter not applied
- [x] #26 Cannot inspect runtime PIE anim instance state

## Widget (2)

- [x] #81 widget tool has no action to add child widgets to WidgetTree
- [x] #13 widget tool cannot construct UI content (add children, buttons, sliders, text)

## Asset / Data (4)

- [ ] #53 Asset/material tools lack parenting and package reload
- [ ] #48 Read protected UObject UPROPERTY arrays from DataAsset sub-objects
- [x] #29 set_property cannot set complex nested struct arrays
- [ ] #75 Cannot create serialized UObject subobjects for IMC/IA modifier and trigger arrays

## Editor / Stability (5)

- [x] #61 Editor crash in NiagaraHandlers::CreateNiagaraSystem — array index out of bounds
- [x] #56 Editor crash in FMCPGameThreadExecutor::ExecuteOnGameThread during startup
- [x] #17 open_asset crashes editor when opening StaticMesh assets
- [x] #64 capture_screenshot silently fails when editor window not focused
- [x] #82 get_log and search_log always return empty results

## Project / Build (2)

- [x] #14 No native build action — project tool should support building C++
- [x] #49 Add native support for generating Visual Studio project files

## Skeletal Mesh / Post-Process (3)

- [ ] #25 Cannot inspect post-process AnimBP assignment on skeletal mesh component
- [ ] #20 Cannot set post-process anim blueprint on SkeletalMesh via native tools
- [ ] #21 Cannot read World Settings or GameMode default pawn class via native tools

## Level Sequence (1)

- [ ] #52 Read Level Sequence binding attach sockets and transform key values

## Chooser (1)

- [ ] #76 Nested chooser row editing is missing
