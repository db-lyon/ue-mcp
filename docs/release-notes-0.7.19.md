# v0.7.19 Release Notes

Closes seven of the eight agent-feedback issues filed after 0.7.18 (#146, #147, #148, #149, #150, #151, #152). The eighth (#143) is kept open as `planned` work pending a dedicated editor-side Slate plugin release. Net: eight new actions, two bug fixes, one shared reflection helper.

## New actions

### pcg
- `force_regenerate` — unstick a PCG component whose executor desynced from its graph. `SetGraph(nullptr)` → `SetGraph(original)` → `Cleanup(true)` → `Generate(true)`. Params: `actorLabel`. Closes #146.
- `cleanup` — `UPCGComponent::Cleanup` wrapper. Params: `actorLabel`, `removeComponents?` (default `true`). Closes #146.
- `toggle_graph` — toggle a component's graph reference to force reinit without generating. Params: `actorLabel`, `graphPath?` (defaults to current graph). Closes #146.

### level
- `count_actors_by_class` — class histogram for the loaded level, sorted descending. Params: `world?` (`editor`|`pie`), `topN?`. Cheaper than `get_outliner` when you only need counts. Closes #146.
- `get_runtime_virtual_texture_summary` — per-volume summary of `RuntimeVirtualTextureVolume` actors with their bound `VirtualTexture` asset paths. Closes #150.
- `set_water_body_property` — set a property on an actor's `WaterBodyComponent` (`ShapeDilation`, `WaterLevel`, etc.). Uses runtime class lookup so the Water plugin is **not** a hard build dependency — returns a clear error if the plugin isn't loaded instead of failing at link time. Params: `actorLabel`, `propertyName`, `value`. Closes #151.

### landscape
- `get_material_usage_summary` — per-proxy dump: `landscapeMaterial` / `landscapeHoleMaterial` paths, plus landscape / grass / Nanite component counts. Aggregates `uniqueLandscapeMaterials` at the top level. Closes #150.

### asset
- `get_referencers` — reverse dependency lookup via `IAssetRegistry::GetReferencers`. Params: `packages[]` (batch) or `packagePath` (single). Returns `{referencersByPackage, totalReferencers}`. Closes #150.

### editor
- `capture_scene_png` — headless PNG screenshot via a reusable hidden `ASceneCapture2D` actor. Works when the editor window is unfocused (unlike viewport screenshots) and guarantees RGBA8 LDR PNG output that LLM image readers accept. Params: `outputPath` (absolute or project-relative), `location?`, `rotation?`, `width?` (1280), `height?` (720), `fov?` (90). Closes #148.

## Bug fixes

### pcg — `add_node` created settings in `/Engine/Transient` (#147)
`add_node` passed `GetTransientPackage()` as the settings outer, so the settings object was garbage-collected on editor restart — placed nodes reloaded with null settings and the executor skipped dispatch. Settings are now created with the graph as outer and reparented into the new node after `AddNode`, matching the proven `s.rename(None, n)` Python workaround. Also fires `PostEditChange` on node + settings so the executor picks up the settings-interface wiring immediately.

### pcg — `set_node_settings` for arrays, TSet, nested structs, object refs (#149)
The old implementation stringified every JSON value and ran it through `FProperty::ImportText_Direct`, which doesn't parse JSON arrays, doesn't walk dotted paths, and doesn't load assets from path strings. Replaced with a recursive `MCPJsonProperty::SetJsonOnProperty` helper that handles:

- `FArrayProperty` / `FSetProperty` — JSON array → `TArray<T>` / `TSet<T>`, recursing on the element type.
- `FStructProperty` + JSON object — recurse into fields (so passing a whole `SplineMeshDescriptor` as `{staticMesh: "/Game/...", overrideMaterials: [...]}` works).
- `FObjectProperty` / `FClassProperty` — string path resolved via `StaticLoadObject` / `LoadClass`.
- `FSoftObjectProperty` — path string → `FSoftObjectPtr`.
- Dotted property names like `"SplineMeshDescriptor.StaticMesh"` walk nested structs before assignment.

Scalars still fall through to `ImportText` for enum / color / etc. coverage.

### blueprint — `set_component_property` accepts JSON values (#152)
Previously required `value` as a pre-formatted string like `"(X=1,Y=2,Z=3)"` for `FVector` properties. Now accepts any JSON value — strings, numbers, booleans, or structured objects like `{x,y,z}` for `FVector` / `{staticMesh: "/Game/..."}` for struct properties / arrays for `TArray`/`TSet` fields. Uses the shared `MCPJsonProperty::SetJsonOnProperty` helper (same code path as `set_pcg_node_settings`). Previous value is still captured via `ExportText_Direct` so rollback payloads stay string-serializable.

### blueprint — `add_node` in function graphs (#152)
Nodes placed inside a newly-created function graph landed with pins allocated but the skeleton class failing to bind the function — pins couldn't be connected to a target. `add_node` now calls `TargetGraph->NotifyGraphChanged()` and `FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint)` after initialization, then compiles. The structural-modification signal is what the editor uses internally when nodes are added via the graph UI.

## Internals

- New shared helper `Private/HandlerJsonProperty.h` with `MCPJsonProperty::SetJsonOnProperty` and `SetDottedPropertyFromJson`. Callable from any handler that needs to assign JSON values to `FProperty` reflection. Used by blueprint `set_component_property` and level `set_water_body_property`; `set_pcg_node_settings` keeps its local copy to minimise diff churn on the #149 commit.
- No new module dependencies. WaterBody / RVT support uses runtime class lookup (`LoadClass<>`) so the corresponding plugins are optional.

## Out of scope for this patch — kept open as `planned`

### #143 — Editor right-click menus that run flows
Kept open with label `planned`. The server-side prerequisite (opt-in HTTP `flow.run` surface) shipped in v0.7.18, so downstream plugin work isn't blocked — a studio can register `FToolMenus` entries today that POST to `/flows/:name/run`. The proposed YAML `menu:` schema + selection-expander registry + editor-side Slate integration is a substantial standalone feature and needs its own release.

## Migration notes

- No breaking changes. All new actions are additive.
- `blueprint(set_component_property)` still accepts the old stringified `"(X=1,Y=2,Z=3)"` form — JSON objects are a strict superset.
- `set_pcg_node_settings` still accepts the old single-value form and will route it through the new recursive helper transparently.
