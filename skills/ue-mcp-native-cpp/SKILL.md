---
name: ue-mcp-native-cpp
description: Use when writing or modifying native C++ UCLASSes in an Unreal project via ue-mcp. Covers create_cpp_class → write_cpp_file → live_coding_compile loop, when to use build vs Live Coding, and the add_module_dependency workflow. Pulls in any time the user asks to write a new native class, add a UPROPERTY, or implement a UFUNCTION.
---

# ue-mcp native C++ authoring

The `project` tool wraps `GameProjectUtils::AddCodeToProject` and `ILiveCodingModule` so agents can author native C++ end-to-end without Python or the editor UI.

## The authoring loop

1. **Pick a module.** `project(action="list_project_modules")` enumerates the project's native modules (name, host type, source path). Feed the module name into `create_cpp_class`.
2. **Create the class.** `project(action="create_cpp_class", className, parentClass?, moduleName?, classDomain?, subPath?)` writes `.h` and `.cpp` using the same engine template that `File → New C++ Class` uses. The class-name prefix (`A`, `U`, `F`) is derived from `parentClass` - pass the bare name (`"BotSpawner"`, not `"ABotSpawner"`).
3. **Inspect the generated files.** `project(action="read_cpp_header", headerPath=...)` and `project(action="read_cpp_source", sourcePath=...)`. Both paths are relative to `Source/` or absolute within it.
4. **Append declarations and bodies.** `project(action="write_cpp_file", path=..., content=...)` writes the full file contents (scoped to the `Source/` tree for safety). Read first, then write the amended content.
5. **Compile.**
   - **New UCLASS** → `project(action="build")`. Live Coding doesn't reliably register brand-new UCLASSes; you need a full build and an editor restart. `create_cpp_class` surfaces this via `needsEditorRestart` in its response.
   - **Existing UCLASS** (editing method bodies, adding UPROPERTYs, etc.) → `project(action="live_coding_compile", wait=true)`. Hot-patches method bodies without restart - the fast inner loop. Check `live_coding_status` first to confirm Live Coding is available and not already compiling.

## Adding module dependencies

When you need a new module in a project's `Build.cs`:

- `project(action="add_module_dependency", moduleName=<target Build.cs>, dependency=<module to add>, access="public"|"private")`
- Deduplicates automatically. Creates the `AddRange` block if missing.
- Rebuild afterwards (`project(action="build")`).

## Reading the engine source

When you need to know *how* UE does something, reach for engine source lookup:

- `project(action="read_engine_header", headerPath=...)` - parse a `.h` from `Engine/Source` (relative to `Engine/Source` or absolute).
- `project(action="find_engine_symbol", symbol=..., maxResults?)` - grep engine runtime headers for a symbol.
- `project(action="search_engine_cpp", query, tree?, subdirectory?, maxResults?)` - broader grep across `.h` / `.cpp` / `.inl` in `Runtime` / `Editor` / `Developer` / `Plugins` (or `all`).
- `project(action="list_engine_modules")` - enumerate modules in `Engine/Source/Runtime`.

## Pitfalls

- **UCLASSes need UnrealBuildTool + MSVC/Clang** on the host. Same requirement as the editor's own Build menu.
- **`write_cpp_file` refuses writes outside `Source/`** and rejects extensions other than `.h` / `.cpp` / `.inl` / `.cs`.
- **Live Coding ≠ full build.** If behavior doesn't reflect your code changes, check whether you added a new UCLASS, UFUNCTION reflection macro, or UPROPERTY annotation - those usually need a full build.
- **Don't edit Intermediate/ or Binaries/.** These are regenerated.

## Testing the new class

Once a class is compiled and registered, you can typically:

- `asset(action="read_properties", assetPath=...)` or `reflection(action="reflect_class", className=...)` to verify UE sees the UCLASS.
- `blueprint(action="create", parentClass="/Script/<Module>.<Class>")` to subclass it from a Blueprint.
- `level(action="place_actor", className=...)` to drop an instance into the level.
