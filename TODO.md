# TODO — ue-mcp

> Remaining work to finish and ship v1.0.
> Current state: v0.3.0, builds clean, strict TS, 16 smoke test suites, CI/CD in place.

---

## C++ Bridge — Port & Ship (Python Bridge Removal)

> **Decision: C++ only.** Everything Python does, C++ can do — plus things Python can't
> (blueprint variable ops on 5.7+, FCoreDelegates dialog hooks, direct FProperty access).
> `execute_python` stays as an escape hatch via `IPythonScriptPlugin` in EditorHandlers.cpp.
> The Python WebSocket bridge server is retired.

### Handler Parity — DONE

~95 new C++ handler methods ported across all categories:

- [x] **Animation** — read_anim_blueprint, read_anim_montage, read_anim_sequence, create_anim_blueprint, create_montage, create_blendspace
- [x] **Asset** — import_datatable_json, export_datatable_json, import_static_mesh, import_skeletal_mesh, import_animation, list_texture_properties, set_texture_properties, import_texture
- [x] **Audio** — play_sound_at_location, spawn_ambient_sound
- [x] **Blueprint** — list_node_types_detailed, search_callable_functions, connect_pins, delete_node, set_node_property
- [x] **Editor** — read_config, save_asset, save_all, get_crash_reports, read_editor_log, pie_get_runtime_value, build_lighting, build_all, validate_assets, cook_content
- [x] **Foliage** — get_foliage_settings, paint_foliage, erase_foliage, sample_foliage_instances, create_foliage_layer
- [x] **GAS** — add_ability_tag, create_gameplay_cue_notify
- [x] **Gameplay** — set_collision_profile, set_physics_enabled, set_collision_type, set_body_properties, spawn_nav_modifier_volume, rebuild_navmesh, get_cdo_defaults, set_world_game_mode, create_ai_perception_config, add_blackboard_key
- [x] **Landscape** — sculpt_landscape, paint_landscape_layer, import_heightmap, set_landscape_material, get_landscape_bounds
- [x] **Level** — load_level, save_level, list_sublevels
- [x] **Material** — set_material_parameter, connect_expression, connect_material_property, delete_expression, set_expression_value, create_material_from_texture, read_material_instance
- [x] **Networking** — set_variable_replication, get_replication_info, set_owner_only_relevant
- [x] **Niagara** — spawn_niagara_at_location, set_niagara_parameter, create_niagara_system_from_emitter
- [x] **PCG** — add_pcg_node, connect_pcg_nodes, remove_pcg_node, set_pcg_node_settings, execute_pcg_graph, spawn_pcg_volume
- [x] **Widget** — search_widget_by_name, get_widget_properties, set_widget_property, read_widget_animations, run_editor_utility_widget, run_editor_utility_blueprint, add_widget, remove_widget, move_widget, list_widget_classes

**New handler files created:**
- [x] SequencerHandlers (create_level_sequence, read_sequence_info, add_track, sequence_control)
- [x] SplineHandlers (create_spline_actor, read_spline, set_spline_points)
- [x] PhysicsHandlers (set_collision_profile, set_physics_enabled, set_collision_enabled, set_body_properties)

### Infrastructure

- [x] Update deployer (`deployer.ts`) — removed Python bridge deployment, startup script patching, `pip install websockets`
- [x] Register new handler files in BridgeServer.cpp (Sequencer, Spline, Physics)
- [x] Keep `execute_python` in EditorHandlers.cpp (escape hatch)
- [ ] Remove Python bridge server code (`plugin/ue_mcp_bridge/handlers/*.py`, `bridge_server.py`, `startup_script.py`, `__init__.py`)
- [ ] Merge `feature/tests__cpp` → `main` once stable
- [ ] Ship prebuilt binaries per UE version OR document build-from-source

### Remaining Handler Gaps (lower priority)

- [ ] **Demo** — port the 19-step procedural scene builder (nice for showcase, not critical for v1)
- [ ] **Skeleton** — physics asset body setup detail already in AnimationHandlers; verify completeness
- [ ] **Volume** — LevelHandlers already has spawn_volume; verify all 10 volume types work
- [ ] **Performance** — EditorHandlers already has get_editor_performance_stats + capture_screenshot; verify stat commands
- [ ] **Reflection** — verify gameplay tag INI fallback works via GConfig in C++

### Multi-Version Support

- [ ] Add `#if ENGINE_MAJOR_VERSION` / `ENGINE_MINOR_VERSION` guards for API differences across 5.4–5.7
- [ ] Test C++ plugin compilation against UE 5.4, 5.5, 5.6, 5.7
- [ ] Document minimum supported UE version for C++ bridge

---

## Type Safety — DONE

- [x] Eliminate `as any` casts in test files (animation, asset, landscape, level tests)
- [x] Replace `catch (e: any)` with `catch (e: unknown)` + type narrowing in `tests/setup.ts` and `tests/reload-bridge.ts`
- [x] Add `resultArray()` and `resultField()` typed helpers in setup.ts
- [ ] Type bridge response payloads — define interfaces for each handler's return shape instead of using `Record<string, unknown>` / untyped `.result`
- [ ] Add typed action parameter maps per tool (currently all params merge into one loose `Record<string, unknown>`)

## Testing

- [ ] Add offline unit tests (no editor required) for core modules:
  - [ ] `project.ts` — path resolution, INI parsing, C++ header parsing
  - [ ] `bridge.ts` — connection lifecycle, message framing, timeout/reconnect
  - [ ] `deployer.ts` — .uproject mutation, file deployment logic
  - [ ] `types.ts` / `categoryTool()` — action dispatch, param mapping
- [ ] Add unit tests for `editor-control.ts` (mock process spawning)
- [ ] Set up test coverage reporting (vitest `--coverage`)
- [ ] Add CI-runnable test job (unit tests that don't need a live editor)
- [ ] Document smoke test prerequisites (which UE project, required plugins, expected test assets)

## Documentation

- [ ] Add troubleshooting section to README (common connection issues, port conflicts, C++ plugin build failures)
- [ ] Document error codes / bridge error response format
- [ ] Add per-action parameter docs (types, required vs optional, defaults)
- [ ] Add example workflows (e.g., "create a blueprint actor from scratch", "set up a material instance")
- [ ] Add CONTRIBUTING.md with dev setup, testing guide, PR conventions
- [ ] Update README to reflect C++-only architecture

## Error Handling & Resilience

- [ ] Validate bridge is connected before dispatching tool calls (return clear error instead of hang/crash)
- [ ] Add structured error types (connection lost, timeout, handler not found, UE exception)
- [ ] Surface C++ handler errors with context in dev mode
- [ ] Handle editor crash / unexpected disconnect gracefully (auto-reconnect + notify user)

## Security

- [ ] Add optional auth token for WebSocket bridge (prevent unintended connections)
- [ ] Validate/sanitize file paths in deployer to prevent path traversal
- [ ] Audit `execute_python` and `execute_console_command` for injection risks

## Pre-Release Polish

- [ ] Bump to v1.0.0 and update CHANGELOG
- [ ] Audit `package.json` — verify `files` field, `engines`, `bin`, `repository`, `keywords`
- [ ] Test fresh install flow end-to-end on a clean machine (npm install → first editor launch → tool invocation)
- [ ] Test against UE 5.4, 5.5, 5.6, 5.7 matrix
- [ ] Review and trim `instructions.ts` — ensure AI-facing docs match actual tool surface
- [ ] Remove `debug.log` from repo (add to .gitignore if not already)
- [ ] Clean up local-only branches (`feature/bak`, `feature/let-it-rip`, etc.)

## Nice-to-Have (Post v1)

- [ ] Configurable bridge port (currently hardcoded to 9877)
- [ ] Tool-level timeout configuration
- [ ] Batch action support (multiple actions in one tool call)
- [ ] WebSocket reconnect backoff strategy (exponential instead of fixed 15s)
- [ ] Publish to npm registry
- [ ] UE Marketplace listing for the C++ bridge plugin
