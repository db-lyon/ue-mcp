# v0.7.13 Release Notes

Native C++ authoring. Agents can now go from nothing to a compiled UCLASS without Python or the editor UI.

## New Actions

### project — native C++ authoring
- `create_cpp_class` — create a new native UCLASS in a project module. Wraps `GameProjectUtils::AddCodeToProject`, the same engine API behind `File -> New C++ Class`, so generated .h and .cpp match the editor dialog byte-for-byte. Params: `className` (no prefix — prefix is derived from the parent), `parentClass?` (short native name like `Actor` or `/Script/<Module>.<Class>` path; defaults to `UObject`), `moduleName?` (defaults to the first project module), `classDomain?` (`public` | `private` | `classes`, default `public`), `subPath?` (nested folder under the domain, e.g. `Gameplay/Abilities`). Returns `headerPath`, `cppPath`, `reloadStatus`, and `needsEditorRestart`.
- `list_project_modules` — enumerate the project's native modules with name, host type, and source path. Feeds `moduleName` into `create_cpp_class`.
- `live_coding_compile` — trigger a Live Coding compile (Windows only). Hot-patches existing UCLASS method bodies without an editor restart — the fast inner loop for iterating on UFUNCTION implementations. Does not reliably register brand-new UCLASSes; use `build` + editor restart for those. Param: `wait?` (default false).
- `live_coding_status` — report Live Coding availability, session state, and whether a compile is in-flight.
- `write_cpp_file` — write a `.h`/`.cpp`/`.inl`/`.cs` file under the project's `Source/` tree. Scoped to `Source/` for safety. Paired with `read_cpp_header` / `read_cpp_source` for round-trip edits (append UPROPERTYs, add method bodies after `create_cpp_class`).
- `read_cpp_source` — read a `.cpp` file from the project `Source/` tree. Companion to the existing `read_cpp_header`.
- `add_module_dependency` — add a module to a target module's `Build.cs` dependency array. Params: `moduleName` (the Build.cs to edit), `dependency` (module to add), `access?` (`public` | `private`, default `private`). Deduplicates and creates the `AddRange` block if missing.

## Workflow — the typical authoring loop

1. `project(list_project_modules)` — pick your target module.
2. `project(create_cpp_class, { className: "BotSpawner", parentClass: "Actor" })` — files land on disk.
3. `project(read_cpp_header, { headerPath: ... })` — see what the engine generated.
4. `project(write_cpp_file, { path: ..., content: ... })` — add your UPROPERTY/UFUNCTION declarations.
5. `project(write_cpp_file, { path: ..., content: ... })` — fill in the method bodies in the .cpp.
6. `project(build)` for a new class (editor restart picks up the new UCLASS), or `project(live_coding_compile)` for subsequent edits to an existing class.

## Known constraints
- Live Coding hot-patches *existing* UCLASS bodies. Brand-new UCLASSes usually need a full rebuild and an editor restart before UE registers the type. `create_cpp_class` surfaces this via `needsEditorRestart` in its response.
- UnrealBuildTool + MSVC (or equivalent toolchain) must be present on the host — same requirement as the existing `project(build)`.
- `add_module_dependency` edits the `Build.cs` in place. Rebuild afterwards.
- `write_cpp_file` refuses writes outside the project's `Source/` tree and rejects extensions other than `.h`/`.cpp`/`.inl`/`.cs`.

## Bug fixes

- **Modal dialog auto-dismiss** — the dialog hook (`set_dialog_policy` / default policies) never actually intercepted prompts on UE 5.7 because `ModalErrorMessage` was removed and the hook's install path was a no-op. Rewired to `FCoreDelegates::ModalMessageDialog` (the UE 5.7 replacement), and added default policies for the common "save changes?" / "unsaved level" prompts so headless MCP sessions don't block on a human. Existing `set_dialog_policy` semantics unchanged.

## Internals
- New `FProjectHandlers` module in the C++ bridge, pulling in `GameProjectGeneration` and `LiveCoding` (the Windows-only module was already a dependency).
- TS-side handlers for file I/O (`write_cpp_file`, `read_cpp_source`, `add_module_dependency`) live in `src/tools/project.ts` alongside the existing `read_cpp_header` / `build` / `generate_project_files`.
