---
name: ue-mcp-niagara
description: Use when authoring Niagara VFX systems via ue-mcp - creating systems and emitters, adding renderers, setting module inputs and static switches, building HLSL modules, and batching operations. Pulls in any time the user asks for a particle system, VFX, Niagara emitter, or motion matching cost visualization.
---

# ue-mcp Niagara authoring

The `niagara` tool covers systems, emitters, modules, renderers, and HLSL authoring.

## Create and inspect

- `niagara(action="create", name, packagePath?)` - stand up a blank `UNiagaraSystem`.
- `niagara(action="create_system_from_spec", name, packagePath?, emitters=[{path}])` - declaratively create a system with emitters in one call.
- `niagara(action="list_emitters", systemPath)` / `niagara(action="get_info", assetPath)` - inspect shape.
- `niagara(action="inspect_data_interfaces", systemPath)` - enumerate user-scope data interfaces.
- `niagara(action="list_system_parameters", systemPath)` - user-exposed parameters (with isDataInterface flag).

## Renderers

- `niagara(action="list_renderers", systemPath, emitterName?)` - current renderer stack.
- `niagara(action="add_renderer", systemPath, rendererType="sprite"|"mesh"|"ribbon", emitterName?)` - or pass a full class name.
- `niagara(action="set_renderer_property", systemPath, rendererIndex, propertyName, value, emitterName?)` - bool / numeric / string properties via reflection.
- `niagara(action="remove_renderer", systemPath, rendererIndex, emitterName?)`.

## Module inputs + static switches (v0.7.14)

- `niagara(action="list_module_inputs", systemPath, emitterName?, stackContext?)` - walks every module in the ParticleSpawn / ParticleUpdate / EmitterSpawn / EmitterUpdate script stacks, reports input + output pins (name, type, default, linked-state).
- `niagara(action="set_module_input", systemPath, moduleName, inputName, value, emitterName?, stackContext?)` - writes a literal default on the module's function-call input pin. **Limitation:** inputs already overridden via the stack editor's override-map node aren't touched by this path. Clear the override in-editor if the write doesn't take effect.
- `niagara(action="list_static_switches", systemPath, moduleName?, emitterName?, stackContext?)` - uses `UNiagaraGraph::FindStaticSwitchInputs` on each module's function script, cross-referenced against pins on the calling node.
- `niagara(action="set_static_switch", systemPath, moduleName, switchName, value, emitterName?, stackContext?)` - format the `value` per the switch type: `"true"`/`"false"` for bool, integer literal for int/byte/enum.

## HLSL module authoring (v0.7.14)

- `niagara(action="create_module_from_hlsl", name, hlsl, packagePath?)` - creates a `UNiagaraScript` module and injects a `UNiagaraNodeCustomHlsl` carrying the supplied HLSL body. Pin signatures are re-parsed from the HLSL body the first time the asset is opened.
- `niagara(action="get_compiled_hlsl", systemPath, emitterName?)` - introspect the GPU compute script for a system's emitter.

## Batching (v0.7.14)

- `niagara(action="batch", ops=[{action, params}])` - run a sequence of niagara sub-actions fail-fast in order. Returns per-step results plus `stoppedAt` (null on full success, index of the first failure otherwise). Nested batches are rejected.

## Typical authoring flow

1. Create the system + emitters (`create_system_from_spec` or step-by-step).
2. Inspect the stack: `list_module_inputs` to see what you're working with.
3. Tune: `set_module_input` / `set_static_switch` for literal values; `set_renderer_property` for visual output.
4. For custom HLSL behavior: `create_module_from_hlsl` to produce a reusable module, then `add_emitter` / reference it from your stack.
5. Optional: wrap a long sequence in `batch` so a mid-sequence failure gets reported as a single result.

## Pitfalls

- **Emitter references are version-pinned.** When using `create_system_from_spec`, the handler uses the source emitter's current exposed version - save the emitter first if you've been editing it.
- **`set_emitter_property` uses reflection on `FVersionedNiagaraEmitterData`.** Not all properties are writable this way (some require the stack editor's validators). If `success: false`, the response lists available properties so you can pick the right one.
- **GPU emitters** expose `get_compiled_hlsl` results only when `simTarget == GPU`. CPU emitters return a note explaining no HLSL is available.
