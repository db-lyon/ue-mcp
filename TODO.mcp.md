# MCP Server — Feature Roadmap

## Tier 1 — Major Blind Spots

### C++ Source Awareness
The MCP has zero visibility into C++ code. Blueprints inherit from C++ base classes, and the AI can see that a Blueprint's parent is `AMyCharacter` but cannot know what UPROPERTY/UFUNCTION that class exposes. This makes it half-blind on every Blueprint that extends custom C++.

- [ ] `read_cpp_header` — Parse a C++ header for UCLASS, USTRUCT, UENUM, UPROPERTY, UFUNCTION declarations and their specifiers
- [ ] `read_module` — Parse a `.Build.cs` file for module dependencies, public/private includes, plugin references
- [ ] `list_modules` — List all modules in the project Source directory with their types (Runtime, Editor, Developer)
- [ ] `search_cpp` — Search C++ source by symbol name, specifier, or content
- [ ] `read_class_hierarchy` — Walk the C++ class chain for a given class name, resolving through headers

### Config / INI Reading
`DefaultEngine.ini`, `DefaultGame.ini`, `DefaultInput.ini`, `DefaultGameplayTags.ini` — these drive physics, rendering, input bindings, tag hierarchies, plugin settings. The AI has no access.

- [ ] `read_config` — Read a specific config file (by name or path) and return parsed sections/keys
- [ ] `search_config` — Search across all project config files for a key or value
- [ ] `list_gameplay_tags` — Parse the GameplayTag hierarchy from `DefaultGameplayTags.ini` and any `.ini` tag sources

### Level / World Reading
`Level` is in the asset taxonomy but no tool understands scene composition.

- [ ] `read_level` — List placed actors in a level with their classes, transforms, and component overrides
- [ ] `read_actor_properties` — Read a specific placed actor's full property state (CDO overrides)
- [ ] `list_sublevels` — List streaming/partition levels referenced by a persistent level

### GameplayTag Visibility
Tags are the backbone of data-driven UE. The AI can see tags on individual assets but not the project-wide hierarchy.

- [ ] `list_gameplay_tags` (same as above — reads from config)
- [ ] `validate_tag` — Check whether a tag exists in the project's registered tag hierarchy
- [ ] `find_tag_usage` — Search assets for uses of a specific GameplayTag

---

## Tier 2 — Meaningful Depth Gaps

### Animation Reading
AnimBlueprints have state machines. AnimMontages have sections and notifies. The offline layer dumps raw properties — no structured parser.

- [ ] `read_anim_blueprint` — Parse state machine structure: states, transitions, blend logic
- [ ] `read_anim_montage` — Parse montage sections, notifies, branching points, slot names
- [ ] `read_blend_space` — Parse blend space axes, sample points

### Material Graph Reading
Materials have node graphs like Blueprints but with different node types. Currently opaque.

- [ ] `read_material` — Parse material node graph: expressions, parameters, connections, material domain/blend mode

### Asset Dependency Graph
The import table tells you what each asset references. No tool walks the graph.

- [ ] `get_asset_dependencies` — Given an asset, return everything it references (direct and optionally transitive)
- [ ] `get_asset_referencers` — Given an asset, return everything that references it
- [ ] `find_broken_references` — Scan a directory for assets with unresolvable imports
- [ ] `find_circular_dependencies` — Detect circular hard reference chains

### Offline Write Support
UAssetAPI can write. The offline-is-readonly invariant is a design choice, not a technical limitation.

- [ ] `set_asset_property_offline` — Modify a simple property value (scalar, string, enum) on an asset and write to disk. Requires explicit confirmation parameter. Backs up original file.
- [ ] `set_datatable_row_offline` — Modify or add a DataTable row offline
- [ ] Add `--allow-offline-write` startup flag to gate this capability

### Bulk Operations
Reading 10 Blueprints = 10 tool calls. Excessive round-trip overhead.

- [ ] `read_assets_batch` — Read multiple assets in one call, return array of results
- [ ] `compare_assets` — Diff two assets (or an asset vs its parent class CDO) and return only the differences

### Enhanced Input Reading
Input actions and mapping contexts are structurally meaningful assets.

- [ ] `read_input_mapping_context` — Parse an InputMappingContext asset: bound actions, triggers, modifiers
- [ ] `read_input_action` — Parse an InputAction asset: value type, triggers, modifiers

---

## Tier 3 — Specialized / Nice to Have

- [ ] `read_niagara_system` — Parse Niagara emitter structure, modules, parameters
- [ ] `read_level_sequence` — Parse Sequencer tracks, bindings, keyframes
- [ ] `read_gameplay_ability` — Parse GAS ability: tags, costs, cooldowns, effects
- [ ] `read_gameplay_effect` — Parse GAS effect: modifiers, duration, stacking
- [ ] `read_world_partition` — World partition cell layout, data layers, streaming state
- [ ] `validate_assets` — Batch validation: broken refs, missing parents, cook errors
- [ ] `read_plugin` — Parse `.uplugin` file for module list, dependencies, supported platforms

---

## Infrastructure / Quality

- [ ] Structured error responses — Currently tools throw or return error strings. Should return typed error objects with codes, context, and recovery suggestions.
- [ ] Tool call telemetry — Track which tools are called, how often, latency, error rates. Inform prioritization.
- [ ] Caching layer — UAssetAPI parses are expensive. Cache loaded UAsset objects with file-watcher invalidation.
- [ ] Parallel asset loading — `search_assets` and `list_assets` with type filter load assets sequentially. Parallelize.
- [ ] Bridge health monitoring — Expose bridge latency, dropped messages, reconnect count in `get_status`.
- [ ] Integration tests — Test the offline tools against real .uasset fixtures from multiple UE versions.
