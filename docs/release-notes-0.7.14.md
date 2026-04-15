# v0.7.14 Release Notes

Niagara authoring depth + engine source search. Closes the remaining feature gaps against Monolith on the Niagara stack, and lets agents grep the engine tree across Runtime/Editor/Developer/Plugins.

## New Actions

### niagara — module inputs, static switches, HLSL modules, batching
- `list_module_inputs` — enumerate every module (`UNiagaraNodeFunctionCall`) in an emitter's script stack, with per-module input + output pins (name, type, default value, linked-state). Params: `systemPath`, `emitterName?`, `emitterIndex?`, `stackContext?` (`ParticleSpawn` | `ParticleUpdate` | `EmitterSpawn` | `EmitterUpdate` | `all`, default `all`), `moduleName?` to filter.
- `set_module_input` — write a literal default onto a module's input pin. Params: `systemPath`, `moduleName`, `inputName`, `value`, plus the usual emitter/stack selectors. Emits a rollback record with the previous value. Note: writes the function-call node's pin default — inputs already overridden via the stack override-map node aren't touched by this path (documented limitation; override-map support lands in a later patch).
- `list_static_switches` — list static-switch inputs per module by walking the function script's graph (`UNiagaraGraph::FindStaticSwitchInputs`) and cross-referencing pins on the function-call node. Reports name, type, current default, and whether a pin is wired for the switch.
- `set_static_switch` — set the default value on a module's static-switch input pin. Params: `systemPath`, `moduleName`, `switchName`, `value`. Rollback record included.
- `create_module_from_hlsl` — create a new `NiagaraScript` module seeded by `UNiagaraModuleScriptFactory`, then inject a `UNiagaraNodeCustomHlsl` carrying the supplied HLSL body. Params: `name`, `hlsl`, `packagePath?` (default `/Game/VFX/Modules`). Pin signatures are re-parsed from HLSL the first time the asset is opened in the Niagara editor.
- `batch` — run a sequence of niagara operations against the bridge in order, fail-fast, returning per-step results. Params: `ops: [{ action, params }]`. Nested batches are rejected.

### project — engine source search
- `search_engine_cpp` — grep `.h` / `.cpp` / `.inl` across `Engine/Source/{Runtime,Editor,Developer}` and `Engine/Plugins`. Params: `query`, `tree?` (`Runtime` | `Editor` | `Developer` | `Plugins` | `all`, default `Runtime`), `subdirectory?`, `maxResults?` (default 500). Skips `Intermediate/` and `Binaries/`. Complements the existing `find_engine_symbol` (header-only) and `read_engine_header`.

## Known constraints
- `set_module_input` writes to the function-call node's pin default. If the stack editor has installed an override-map node upstream of that module (the usual path when a user has already edited the input in the UI), the override value wins — the handler's change will not be visible in PIE until the override is cleared. A future patch will add an override-map write path.
- `create_module_from_hlsl` accepts an `inputs?` / `outputs?` spec for forward-compatibility, but v0.7.14 relies on the HLSL body's own signature parsing rather than pre-declaring pins. Open the resulting asset in the Niagara editor to verify the regenerated signature.
- Static switch writes use the pin's `DefaultValue` string — format must match the switch's declared type (`true`/`false` for bool, integer literal for int/byte/enum).

## Internals
- New Niagara handlers in `plugin/ue_mcp_bridge/Source/.../Handlers/NiagaraHandlers.cpp` (+ header declarations) wire into the existing `FNiagaraHandlers` registry.
- `FindStaticSwitchInputPin` and `SetCustomHlsl` are not exported from `NiagaraEditor`; this patch walks pins explicitly for the former and sets the `CustomHlsl` UPROPERTY via reflection for the latter, then invokes the exported `ReconstructNode` + `PostEditChange` path.
- `niagara.batch` is a pure-TS dispatcher that reuses the existing action registry — no plugin changes required.
- `project.search_engine_cpp` mirrors `search_cpp` but resolves the engine root via `findEngineInstall` (same helper used by `read_engine_header` and `find_engine_symbol`).
