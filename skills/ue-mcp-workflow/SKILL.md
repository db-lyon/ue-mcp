---
name: ue-mcp-workflow
description: Use when driving Unreal Engine editor via the ue-mcp MCP server. Covers the required order of operations (status check first), editor lifecycle, project scoping, and what to do when the bridge says "still initializing". Pulls in automatically any time the user asks to use an Unreal project or references ue-mcp tools.
---

# ue-mcp workflow

The `ue-mcp` MCP exposes one tool per category, minus any categories the project disables in `ue-mcp.yml` (action-dispatch style) that drive a live Unreal Engine editor via a C++ bridge plugin. Every tool takes an `action` parameter.

How a call is spelled depends on the project's context strategy. These skills write `level(action="get_outliner", limit=5)`; send it as:

- **micro** (the default): `tools(action="call", category="level", method="get_outliner", args={limit: 5})`. `tools(action="search")` and `tools(action="describe")` return one-line signatures.
- **lean** or **full**: `level(action="get_outliner", args={limit: 5})`. Flat parameters beside `action` are still accepted.

## Start every session with a status check

**Always** call `project(action="get_status")` before anything else. It tells you:

- Whether the bridge is connected
- Which project is loaded
- Whether the editor is responsive

If status reports `not connected` / `editor not running`, call `editor(action="start_editor")` to launch UE. Wait for the status check to succeed before issuing other calls - handlers that need the editor world will return `"Editor is still initializing. Please wait and retry."` if you call them too early.

## Orient yourself before authoring

Before writing, read. Common orientation calls:

- `level(action="get_outliner")` - what's in the current level
- `asset(action="list", directory="/Game/...", recursive=true)` - what assets exist
- `asset(action="search_fts", query="...")` - ranked search across names/classes/paths (after `asset(action="reindex_fts")` has built the index)
- `reflection(action="reflect_class", className="StaticMeshActor")` - inspect any UE class
- `project(action="list_project_modules")` - native C++ modules in the project

## Mutation recipe

For any write action:

1. Call the read/list variant first to confirm the target exists and capture the current shape (e.g. `blueprint(action="read", assetPath=...)` before `add_node`).
2. Issue the write. Most write handlers return `{ success, existed, created, updated, rollback? }` - honor `existed` as "idempotent no-op" and `updated` as "real change made".
3. If a rollback record came back and you hit a later failure in the same logical unit, call the rollback method yourself (the TS flow runner does this automatically when tasks are composed via `ue-mcp.yml`).

## Common pitfalls

- **Action typos silently fail** - each tool validates `action` against an enum; a typo returns `Unknown action '<x>'. Available: <list>`. Read the `Available:` list rather than guessing.
- **Asset paths use `/Game/...`**, not filesystem paths. Package paths (folder) vs object paths (`Folder/Name.Name`) matter - most create handlers take `packagePath` (folder) plus `name`; most read handlers take `assetPath` (full object path).
- **`execute_python` is an escape hatch, not a shortcut** - if a native tool exists for the job, use it. When you use `execute_python`, a hook may prompt you to file a GitHub issue so the native tool gap can be closed.
- **Saving under source control** - on Perforce a file must be checked out before it can be written, and a save to a read-only file is refused. Call `source_control(action="checkout", paths=[...])` first, or `source_control(action="set_checkout_mode", mode="auto")` once to have writes check out for you. `source_control(action="get_provider")` says which provider is active and which actions it supports (`operations`). `history`, `sync`, `mark_for_delete` and `connect` go through the same provider. Provider-specific work (shelves, streams, branches) comes from provider plugins as prefixed actions such as `p4_*`.
- **Editor must be fully loaded** for asset registry queries. If a `list_*` action retries with "still initializing", the editor is still starting - wait a few seconds and retry.

## When something truly can't be done

If no native action covers your case:

1. Confirm no action already covers it: `project(action="search_tools", query=...)` searches the whole surface by intent, and `project(action="describe_action", name="category.action")` gives one action's parameters.
2. Confirm no UFUNCTION exists to call directly: `reflection(action="list_classes", parentFilter=...)` finds the class and `reflection(action="reflect_class", className=...)` lists what it exposes. A BlueprintCallable function is reachable with `editor(action="invoke_object_function")`.
3. Use `execute_python` as a last resort.
4. Consider using `feedback(action="submit")` to open a GitHub issue describing the gap. It checks the plugin registry and files against whichever tracker owns the surface - `db-lyon/ue-mcp` for core, or the plugin's own repo when the gap is in a plugin-provided category. Run `feedback(action="route")` first if you want to see where it would land without posting.
