# Validation and troubleshooting

The rules the loader enforces, and what the common failures mean.

## Validation rules

Checked by `ue-mcp plugin install` and again when the server loads the plugin:

- `actionPrefix` is required and matches `/^[a-z][a-z0-9_]*$/`.
- Every `inject:` target is a built-in category.
- No injected action and no `provides:` category collides with a built-in. Provided category names match `/^[a-z][a-z0-9_]*$/`.
- `minServerVersion` is at or below the running ue-mcp.
- `nativeModule.minBridgeApi` is at or below the deployed bridge's `UEMCP_BRIDGE_API_VERSION`.

Checked only at load:

- Every task's `class_path` resolves inside the package (`dist/<path>.js`, `dist/tasks/<path>.js`, `<path>.js`, `dist/<path>/index.js`) to a class with `run()` and `execute()`.
- Every `inject:` and `provides:` action names a task declared under `tasks:`.
- No action declares a reserved parameter name (`action`, `timeoutMs`, `select`, `omit`, `editor`, `toEditor`).
- A `nativeModule.specs` file is readable and valid, and every surfaced handler has a recorded contract in it.
- Between plugins, the first in `plugins:` wins an action name or a provided category.

A failure that affects the whole package (the prefix, an unknown `inject:` target, a version gate, a task that fails to load, an unreadable specs file) skips the plugin with a warning; the others keep loading. A failure confined to one action, handler, flow, task or knowledge entry drops that unit only. The plugin loads, and the dropped units and their reasons appear as `degraded` on `plugins(action="list")` and `describe`, and in the `PLUGIN LOAD WARNINGS` block of the server instructions.

## An action or a category is missing

Read the `PLUGIN LOAD WARNINGS` block, or run `plugins(action="list")`:

- `status: "skipped"` with a `statusReason`: the plugin did not load. Fix the reason and restart the server.
- `status: "active"` with a non-empty `degraded`: the listed units did not load. Each entry names the manifest path and the error, for example `nativeModule.handlers.actor_set: nativeModule.handlers.actor_set.schema.value.type: Invalid enum value`.

No warning for a plugin you expected means it is not in `plugins:`, or its package is not under `node_modules/`.

## `pluginCount: 0`

The server found no `plugins:` entries. Check:

1. `ue-mcp.yml` is next to the `.uproject` and has a top-level `plugins:` array.
2. Each `name:` is installed under `node_modules/`. Run `npm install` if the lockfile lists it.
3. The server's stderr. Each failure prints `[ue-mcp] warn plugin: <package>: <reason>` at startup.

## `uePluginPresent: false`

The plugin loaded, but the Unreal plugin it depends on is not enabled in the `.uproject`. Its actions are listed but fail until that plugin is enabled and built. See [Host UE plugin dependencies](plugins-using.md#host-ue-plugin-dependencies).

## `class_path '<path>' could not be resolved`

The manifest names a task whose compiled file is not in the package. When authoring, run `npm run build` and confirm `dist/<path>.js` exists. When using a published plugin, the package was published without `dist/`; report it to the plugin's maintainers.

## `requires server >= <version>`

The plugin's `minServerVersion` is newer than the running ue-mcp. Update ue-mcp (`ue-mcp update`, or `npm install -g ue-mcp@latest` for a global install), then restart your MCP client.

## `nativeModule requires bridge ABI >= N`

The deployed bridge is older than the plugin needs. Run `ue-mcp deploy`, then `ue-mcp build`. `project(action="get_status")` reports the deployed version as `bridgeApiVersion`.

## The action is in `plugins(describe)` but not on the category tool

The MCP client still holds the tool list from before. Restarting the editor does not restart the server; reconnect the client (in Claude Code, `/mcp`).

## A provided category has no tool

Another plugin claimed the name first. `plugins(action="describe", name="<package>")` shows an empty `provided`, and the startup log has `provides target '<category>' already claimed by '<other plugin>'`. Reorder `plugins:` or remove one of the two.

## Native handlers answer `Unknown method`

The C++ module is not compiled into the running editor:

1. The project was not rebuilt after install. Run `ue-mcp build` and confirm the module's DLL is under `Binaries/Win64/`.
2. The build failed against an older bridge. Run `ue-mcp deploy`, then `ue-mcp build`.
3. A stale Live Coding patch shadows the new DLL. Delete `<project>/Binaries/Win64/*.patch_*` and rebuild.

`project(action="get_status")` reports `deployedPlugin`: the methods the running plugin registered, the ones this server advertises, and the difference. That names every missing handler in one call. `pluginBuildStale` only compares timestamps, so a clean build of an older version reads `false` and can still be missing handlers.
