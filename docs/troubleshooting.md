# Troubleshooting

## Connection Issues

### "Editor not connected" / Bridge not running

**Symptoms:** `project(action="get_status")` shows `editorConnected: false`. Calls that need the editor fail with `Failed to connect to editor bridge at ws://127.0.0.1:<port>` or say no editor is connected.

**Fixes:**

1. **Is the editor running?** The bridge runs only inside an open editor.
2. **Is the plugin installed?** `npx ue-mcp status` reports it. The server does not install it; run `npx ue-mcp init` (or `npx ue-mcp deploy`), then restart the editor.
3. **Check the Output Log.** In the editor, open **Window > Output Log** and filter on `LogMCPBridge`. You should see:
   ```
   LogMCPBridge: [UE-MCP] Bridge listening on ws://127.0.0.1:51234 (loopback only)
   ```
   The line before it says where the port came from (`-MCPPort`, `UE_MCP_PORT`, `requested.json`, `bridge.port` or derived from the project path).
4. **Compare ports.** `get_status` reports `editorTarget.port` and how it was chosen. The bridge publishes the port it bound to `<project>/Saved/UE_MCP_Bridge/port.json`. If they differ, see [The editor is not listening on the port I pinned](#the-editor-is-not-listening-on-the-port-i-pinned).
5. **The editor is up but the bridge is not.** If the bridge could not bind any port (it tries 50 above its base), it writes `<project>/Saved/UE_MCP_Bridge/bridge-error.json` with the range and the socket error, and the client quotes that detail in the connection error. Read the file directly if you are looking without the client.
6. **A stale record.** An editor that was killed rather than closed leaves its `port.json` behind; see [stop_editor or restart_editor says no port is published](#stop_editor-or-restart_editor-says-no-port-is-published).

### Handlers time out, or the editor never finishes starting

**Symptoms:** every call returns `Handler execution timed out`, `start_editor` waits out its timeout, or `get_status` says disconnected while an editor is plainly open.

The game thread is not returning to its tick loop. The usual cause is on screen: a modal dialog waiting for an answer, a long task (shader compile, asset registry scan, map load), or a startup phase before the bridge module loads.

**Ask what the engine is doing:**

```
editor(action="get_engine_state")
```

This call is answered without the game thread. It reports:

- **`snapshot.slowTask`**: the name and percentage of the task the editor's progress bar shows.
- **`snapshot.modal`**: the title, message and buttons of the dialog blocking the game thread. See [Dialog handling mode](configuration.md#dialog-handling-mode) for how it gets answered, or arm an answer with `editor(action="set_dialog_policy")`.
- **`snapshot.gameThreadStalledSeconds`**: how long the game thread has gone without ticking. The rest of the snapshot is that old. It is `null` while `gameThreadTicking` is false, meaning the editor is still starting; `modulesLoaded` is the progress signal then.
- **`snapshot.compiling`**: remaining shader jobs and asset compiles.
- **`log.phase` / `log.tail`**: the startup phase parsed from the editor's log, which covers the window before the plugin exists. "The following modules are missing or built with a different engine version" shows up here.
- **`processes`**: PID, command line, and whether the OS considers the process responsive.
- **`dialogs`**: native (pre-Slate) message boxes, including that rebuild prompt.

A timed-out handler carries the same snapshot in its `engineState` field.

The snapshot is also written to `<Project>/Saved/UE_MCP_Bridge/status.json` four times a second by a thread that keeps running while the game thread is blocked. Read that file when the bridge socket is unreachable.

**During startup** the snapshot comes from a second plugin module that loads at `PostConfigInit`, before the bridge (`PostEngineInit`) and before any socket exists. A cold launch publishes its first state after about a second and then tracks the splash screen:

```
+1.6s  config init              | modules=0   | Initializing... 0%
+3.3s  config init              | modules=16  | Initializing Render Hardware Interface... 5%
+13.0s config init              | modules=253 | Loading Default Modules for Plugin: ChaosVD 73%
+17.5s engine loop initialized  | modules=725 | New Map 92%
+20.7s ready                    | modules=733 | Running Python start-up scripts... 95%
```

Read `status.json` (or `editor(get_engine_state)`) before assuming a launch failed. `editor(start_editor)` already waits through all of it: it blocks until the snapshot reports `ready`, streams progress, and returns the phase timeline:

```
Editor ready in 24.7s
launching 0s -> loading modules and plugins 0.8s -> config init 1.8s
  -> bridge starting 21.1s -> engine loop initialized 21.5s -> ready 24.7s
```

There is no need to poll during or after it.

### A call timed out: did it happen or not?

**Symptom:** a mutating call such as `widget(add_widget)` returns `timed out after 30s`, and reading the asset back shows the change is already there and saved.

It happened. A handler applies the mutation, compiles and saves before it replies, so a client-side timeout says only that the reply was late. The error says so:

```
Bridge call 'add_widget' timed out after 30s. Outcome is unknown: the editor may
have already applied and saved this call (operation 12). Read the current state
back before retrying, and prefer an idempotent retry (pass the same names) so a
call that did land is not repeated.
```

The `MACHINE_ERROR` block carries the same: `{"code":"BRIDGE_TIMEOUT","outcome":"unknown","operationId":"12","method":"add_widget"}`. Treat `outcome: "unknown"` as "verify, then retry", never as "failed".

1. **Read the state back** (`widget(read_tree)`, `asset(read_properties)`, `level(get_outliner)`) before deciding anything.
2. **Retry idempotently.** Pass the same names. `widget(add_widget)` is idempotent by `assetPath` + `widgetName`: a retry that finds the widget returns `existed: true`. Without `widgetName` there is nothing to match a replay against.
3. **Check the server log** for the late reply. The connection stays open through a timeout, and when the editor answers the client logs `editor finished 'add_widget' (operation 12) 41000ms after the client timed out; the call completed`. That line is the answer for that operation id.

The default timeout is 30 seconds. Slow handlers register longer ones (`widget(add_widget)` and `widget(remove_widget)` allow 120 seconds), any call can pass `timeoutMs`, and `UE_MCP_BRIDGE_TIMEOUT_MS` raises the floor for every call. A Widget Blueprint open in the UMG designer makes every compile much slower; closing the asset tab helps.

### The tool call sits there showing nothing while the editor starts

**Symptom:** in Claude Code, `start_editor` shows as a motionless line, `ue-mcp - editor (MCP)(action: "start_editor", timeout: 600)`, for the whole launch.

The call is not stuck. It returns when the editor is ready, and the number is the `timeout` argument, not elapsed time. Claude Code 2.1.116 through 2.1.152 collapse MCP tool calls, so the `notifications/progress` messages the server sends are received and never drawn ([anthropics/claude-code#51713](https://github.com/anthropics/claude-code/issues/51713)). Update Claude Code. On an affected version `start_editor` says so in its result, and the phase timeline it returns is what you would have watched.

### Connection drops / reconnecting

The client retries every 15 seconds while disconnected and reconnects at once when the editor publishes its `port.json`, so an editor restart restores the connection on its own. With the [daemon](configuration.md#the-daemon), your MCP client stays connected throughout.

If the connection flaps, check the Output Log for errors in `LogMCPBridge`. The bridge closes with a WebSocket status code and a reason, and the client repeats both:

| Code | Meaning |
|------|---------|
| `1001` | The editor is shutting down |
| `1002` | The frame stream stopped parsing. The reason names what was wrong |
| `1009` | A message, a frame, or the unparsed receive buffer exceeded 64 MiB. The reason names the size and the limit |

### stop_editor or restart_editor says no port is published

**Symptom:** `stop_editor` returns something like `No bridge port published at C:/Game/Saved/UE_MCP_Bridge/port.json`, or reports that the PID in that file is no longer running.

Lifecycle actions read that file and nothing else, so that a quit request never reaches another project's editor. The message means one of three things:

1. **No editor is running for this project.** Start one with `editor(start_editor)`.
2. **The editor is running but its bridge never started.** See [Bridge not running](#editor-not-connected--bridge-not-running). The message includes the phase the editor is at.
3. **A previous editor was killed rather than closed.** Its lockfile names a PID that has gone. Delete `<project>/Saved/UE_MCP_Bridge/port.json`.

`start_editor` refusing with "Editor is already running for this project" is the same rule from the other side: it names the PID, and that PID has this project's `.uproject` open. Editors of other projects never trigger it.

### The editor is not listening on the port I pinned

**Symptom:** `ue-mcp.yml` sets `bridge.port`, but the editor bound something else, or the client cannot reach it.

Search the Output Log for the `LogMCPBridge` line naming the port's source: `-MCPPort command line`, `UE_MCP_PORT env`, `requested by the ue-mcp client in <path>`, `bridge.port in <file>`, or `Derived per-project port`.

- **Something outranks the config.** `-MCPPort` and `UE_MCP_PORT` win over `bridge.port`, on both sides. Clear the one that is set.
- **The port was taken.** A warning names the port asked for and the one bound. The bridge publishes the bound port to `port.json` and the client follows it, so the connection works. Free the port or pin another.
- **The value was rejected.** A `bridge.port` that is not a whole number in `1-65535` is warned about and the derived port is used.
- **The key is written in a form the plugin does not read.** The plugin reads this key without a YAML parser. Write it as plain nested keys:

    ```yaml
    ue-mcp:
      bridge:
        port: 50123
    ```

    A flow mapping (`bridge: { port: 50123 }`), an anchor or an alias is read by the client and not by the plugin. Once the client has run against the project it also writes the port to `Saved/UE_MCP_Bridge/requested.json`, which the plugin reads first.

### A call ran in the wrong editor

Only possible with more than one editor session. Start with `project(action="list_editors")`: it reports every session, its bridge port, and which one untargeted calls use.

- **The call had no target.** Untargeted calls run in the active session. Pass `editor="<name>"`, or move the default with `project(action="use_editor", editorTarget="<name>")`.
- **The `editor` parameter is not advertised.** It appears only while more than one session is registered. Add the other project with `project(action="add_editor", projectPath="...")`, or list both `.uproject` paths in your MCP client config.
- **Two sessions on one port.** `list_editors` reports it as `portSharedWith`. Two projects pin the same `bridge.port`, or a global `UE_MCP_PORT` overrides both. Give each project its own port or unset the variable, then restart the server.

Lifecycle actions are not affected by the last case: they use the addressed project's own lockfile and the PID it names, and refuse rather than guess.

## Plugin Build Issues

### Plugin fails to compile

1. **Engine plugins.** The bridge's `.uplugin` depends on `PythonScriptPlugin`, `EnhancedInput`, `GameplayAbilities`, `Niagara`, `PCG`, `ControlRig`, `StateTree` and others ([full list](configuration.md#plugin-deployment)). Unreal enables them with the bridge. If an error names one, make sure the `.uproject` does not disable it.
2. **Engine version.** The supported range is UE 5.4 to 5.8. Check the build log for the first error.
3. **Rebuild from clean.** Delete `<Project>/Plugins/UE_MCP_Bridge/Binaries/` and `<Project>/Plugins/UE_MCP_Bridge/Intermediate/`, then run `npx ue-mcp build`.
4. **No C++ toolchain.** Use prebuilt binaries instead: remove `Plugins/UE_MCP_Bridge/` and run `npx ue-mcp init --install=binary`.

### build_project fails with LNK1104 and no editor is running

**Symptom:** `editor(build_project)` fails with `LINK : fatal error LNK1104: cannot open file '...UnrealEditor-<Module>.dll'`, and the line before it says the file is `being used by another process. - ...CrashReportClientEditor.exe`.

**Cause:** an editor of this project crashed, and its crash reporter still holds the project's module DLLs open.

**Fix:** run `editor(action="stop_editor")`, then build again. With no editor of this project running, the stop ends that crash reporter and reports it under `crashReporters`. The build error names the locked file and the process holding it under `lockedOutputs`, so a different holder is named too.

### Plugin not loading

If the editor starts but `LogMCPBridge` never appears:

1. Check **Edit > Plugins**: search for "UE MCP Bridge" and make sure it is enabled.
2. Check that the `.uproject` lists it:
   ```json
   { "Name": "UE_MCP_Bridge", "Enabled": true }
   ```

## MCP Server Issues

### Server won't start

1. **Node.js version.** ue-mcp needs Node 20 or newer. Check with `node --version`.
2. **Project path.** The argument must be a `.uproject` or a directory holding one. Use an absolute path: the client chooses the working directory.
3. **Daemon log.** With one project the client launches a shim that starts the project's daemon. `npx ue-mcp daemon status` shows whether it is up, and `~/.ue-mcp/daemons/<key>.log` holds its output. If the daemon cannot start, the server falls back to running in process and logs why. `UE_MCP_SERVER_MODE=in-process` skips the daemon entirely.
4. **More detail.** `UE_MCP_LOG_LEVEL=debug` in the client config's environment. The server logs to stderr, which most clients write to their own log file.

### Server won't start after an update, with npm's `min-release-age` set

npm 11.10 and later refuse a package version younger than `min-release-age` days. `npx` does not apply `min-release-age-exclude`, so excluding `ue-mcp` does not help, and an entry that runs `npx -y ue-mcp@<version>` fails with `ETARGET No matching version found` until the release is old enough.

The entries ue-mcp writes carry `--min-release-age=0` before the package spec, which lifts the gate for that launch and its dependencies. `npx ue-mcp doctor` reports any npx entry without it, and `npx ue-mcp doctor --fix` adds it. Keep the `=` form: npm before 11.10 ignores the unknown key, but would read a separate `0` as the package to run.

### Tools return errors

- **`Failed to connect to editor bridge`**: the editor is not running or the plugin is not loaded. See [Connection Issues](#connection-issues).
- **`Unknown action '<name>' on '<category>'`**: the action name is wrong. The error lists the closest names; see the [Tool Reference](tool-reference.md).
- **`Unknown method`**: the server knows the action but the loaded plugin does not, so the plugin is older than the server. See [A Fix Shipped but the Editor Behaves the Same](#a-fix-shipped-but-the-editor-behaves-the-same-stale-compiled-plugin).
- **"Asset not found"**: use Unreal asset paths with the `/Game/` prefix (`/Game/Blueprints/BP_Player`), not filesystem paths.
- **Timeout**: not a failed call. See [A call timed out: did it happen or not?](#a-call-timed-out-did-it-happen-or-not) before retrying.

## Line Traces From a Python Script

A script run with `editor(run_python_file)` cannot call the engine's trace functions the obvious way. `unreal.SystemLibrary.line_trace_single` takes a `TraceTypeQuery` (`unreal.TraceTypeQuery.TRACE_TYPE_QUERY1` and so on), not a `CollisionChannel`, so passing `unreal.CollisionChannel.ECC_WORLD_STATIC` fails with "Failed to convert parameter 'trace_channel'", and `unreal.World` has no trace methods.

The bridge plugin exposes the query `level(line_trace)` runs:

```python
import unreal

hit = unreal.MCPTraceLibrary.line_trace(
    unreal.Vector(0, 0, 10000),   # start
    unreal.Vector(0, 0, -10000),  # end
    "pie",                        # world: pie (default), editor or auto
    -1,                           # pie_instance: -1 (default) for the first PIE world
    "WorldStatic",                # channel: the project's channel names (default Visibility)
    False,                        # trace_complex
)
if hit.error:
    raise RuntimeError(hit.error)
if hit.hit:
    ground_z = hit.impact_point.z
    print(hit.actor_path, hit.component_name, hit.physical_material)
```

The result carries `hit`, `location`, `impact_point`, `impact_normal`, `distance`, `actor_path`, `actor_label`, `component_name`, `physical_material`, `world` and `error` (empty when the trace ran). The class exists whenever the bridge is loaded.

## Asset Path Issues

UE-MCP expects Unreal asset paths:

| Format | Example |
|--------|---------|
| Content path | `/Game/Blueprints/BP_Player` |
| Plugin content | `/MyPlugin/Assets/SomeAsset` |
| Full object path | `/Game/Blueprints/BP_Player.BP_Player_C` |

!!! warning "Common mistakes"
    - Filesystem paths (`C:/Users/.../Content/...`): use `/Game/...` instead
    - File extensions (`.uasset`): omit the extension
    - A missing leading slash: `/Game/Foo`, not `Game/Foo`

## Class Names and the A/U/F/E Prefix

Unreal registers a class under its C++ name **without** the type prefix. `AActor` is the class named `Actor`, `UMyConfig` is `MyConfig`, and its object path is `/Script/MyGame.MyConfig`.

Every class parameter (`className`, `parentClass`, `parentFilter`, `componentClass`, `actorClass`, `nodeClass`, `schema`, and the rest) accepts either spelling. Resolution tries, in order:

1. The spelling you passed.
2. The prefix-stripped spelling (`UMyConfig` to `MyConfig`).
3. The prefixed spellings (`MyActor` to `AMyActor` / `UMyActor`).
4. The object path form, including `Module.Class` promoted to `/Script/Module.Class` and the prefix stripped from the object part of a path.
5. The Blueprint generated class (`BP_Thing` to `BP_Thing_C`).
6. A case-insensitive sweep of loaded classes, native classes winning ties.

When nothing resolves, the error lists every spelling tried and the closest loaded class names:

```
Class not found for className 'UXianGameConfig'. Tried: UXianGameConfig, XianGameConfig,
AUXianGameConfig, UUXianGameConfig, UXianGameConfig_C, XianGameConfig_C. UE reflection
stores class names without the C++ type prefix, so UMyConfig is registered as 'MyConfig'
and its path is /Script/<Module>.MyConfig. Closest loaded classes: XianGameConfig.
```

The JSON carries a `reason` field:

| `reason` | Meaning |
|---|---|
| `class_not_found` | No spelling resolved. The response also carries `tried` and `suggestions` arrays. |
| `abstract` / `deprecated` | The name resolved, but the class cannot be instantiated. Pass a concrete subclass. |
| `wrong_base` | The name resolved to a class outside the family the action needs (for example a non-`UDataAsset` passed to `create_data_asset`). |

If the suggestion list is empty, the owning module may not be loaded. Check with `reflection(action="is_module_loaded", moduleName="MyGame")` and `reflection(action="is_class_loaded", className="MyConfig")`.

## Updates Don't Take Effect (server stuck on an old version)

If `ue-mcp update` reports "already up to date" but the running server reports an old version, a project-local `node_modules/ue-mcp` is shadowing the global install:

```bash
npx ue-mcp doctor
```

```
local shadow:   ./node_modules/ue-mcp @ 1.0.64   <-- WARN npx runs THIS, not global
effective (npx):1.0.64  (behind latest 1.0.76)
```

With `ue-mcp` in the project's `package.json`, `npx ue-mcp` runs the local copy, so `npm i -g ue-mcp@latest` updates a copy npx never uses. Fixes:

- `npx ue-mcp update` aligns the local copy, or
- remove `ue-mcp` from the project's `package.json` and delete `node_modules/ue-mcp`, or
- launch with `npx -y --min-release-age=0 ue-mcp@latest` in the client config, so every launch fetches the current release.

Then quit and relaunch your MCP client.

## A Beta Install Keeps Coming Back as the Stable Release

A bare `npx ue-mcp` resolves to the `latest` dist-tag, which is the stable line. On the beta channel, run every command against the channel:

```bash
npx ue-mcp@beta update
npx ue-mcp@beta doctor
```

`ue-mcp update` run by an older package leaves a newer binary bridge alone and prints the command that updates it. A client entry launching `ue-mcp@latest` still works with a beta binary bridge: the shim starts the daemon at the bridge's version. `doctor` shows what a bare `npx ue-mcp` runs as `effective (npx)`.

## A Fix Shipped but the Editor Behaves the Same (stale compiled plugin)

Here `ue-mcp doctor` shows the current version, but a fix to **editor behavior** (anything the C++ plugin does) has no effect, or actions fail with `Unknown method`.

Cause: the editor runs the **compiled** plugin. A source install compiles it from the source in your project, and `ue-mcp update` run outside the project directory, or with the editor open, does not rebuild it. The version `doctor` reports is the server's, so it looks current while the loaded plugin is old.

Fix: from the project directory, with the editor closed, run the update (it deploys and builds), then start the editor:

```bash
npx ue-mcp update
```

A start or restart through `editor(start_editor)` also builds a source bridge that owes a build.

To see which binary is loaded, read `bridgeProtocol` from `project(action="get_status")`. `builtAt` is the compile time of the running plugin, and `plugin` versus `client` are the two protocol versions. `bridgeApiVersion` in the same response is read from the header on disk, so it disagreeing with the running plugin means the deployed source has not been rebuilt.

If the build succeeds and the behavior persists, rebuild from clean (incremental builds and Live Coding can load stale patches over a fresh build):

1. Delete `<Project>/Plugins/UE_MCP_Bridge/Binaries/` and `<Project>/Plugins/UE_MCP_Bridge/Intermediate/`.
2. Delete any `*.patch_*.{dll,pdb,lib,exp}` under `<Project>/Binaries/Win64/`.
3. Run `npx ue-mcp build`, then start the editor.

## Widget Blueprint Says a Variable Was Deleted but Still Has a GUID

**Symptom:** compiling a Widget Blueprint raises an ensure, on every compile:

```
Ensure condition failed: SeenVariableNames.Contains(It.Key())
Variable [TaskbarContent] was deleted but still has a GUID referenced by WidgetBlueprint [WBP_ComputerTaskbar]
```

A Widget Blueprint maps widget variable names to GUIDs, and the compiler requires every GUID to have a widget behind it. An entry left by a widget that is gone breaks that check. `widget(add_widget)`, `widget(remove_widget)`, `widget(set_root)` and `widget(wrap_root)` prune the map, so this does not build up through ue-mcp.

To repair an asset that already has a dead entry, call `remove_widget` with the name from the message:

```
widget(action="remove_widget", assetPath="/Game/UI/WBP_ComputerTaskbar", widgetName="TaskbarContent")
```

The widget is already gone, so the call reports `alreadyDeleted: true`, drops the dead entries and saves. `prunedGuidEntries` says how many it removed; `0` means the map was clean and nothing was written.

## A Call Node Came Back With No Title and No Pins

**Symptom:** `blueprint(add_node)` reports `created: true`, but the node's title is `None`, it has no pins, and it cannot be wired.

That is an unbound stub: the node was added, but no `UFunction` was bound to it. The resolver searches an explicit target class, the parent class, the common Kismet libraries, the Blueprint's component classes, the Blueprint's own generated and skeleton classes, and every loaded class. A function declared with `blueprint(create_function)` binds without a compile in between.

If you still get a stub, check the function name first: `blueprint(action="list_functions", assetPath=...)` reports what the Blueprint declares. For a C++ `UFUNCTION` on a class the Blueprint does not own, name the class:

```
nodeParams={"functionName": "MyFunction", "className": "/Script/MyModule.MyClass"}
```

## Reimport

`asset(action="reimport")` rebuilds an asset from the file it was imported from, so it applies only to imported assets. Asked to reimport something authored in the editor (a Blueprint, a material, a data asset), it refuses at once:

```
Nothing can reimport a Blueprint: '/Game/BP_Thing' has no registered reimport handler.
```

`filePath` repoints the asset at a new source first. The response says whether it did:

```
sourceFileUpdated: true
sourceFile: C:/path/to/the/new/file.png
```

`sourceFileUpdated: false` means the asset was rebuilt from the source it already had, which is expected when you pass no `filePath`. An asset that cannot record a new source path refuses the call rather than re-reading the old file.

## Search Not Finding Assets

If `asset(action="search")` misses assets in plugin content:

1. Add the content root to `ue-mcp.yml`. With no `directory`, search runs over each root, one root per page:
   ```yaml
   ue-mcp:
     contentRoots:
       - /Game/
       - /MyPlugin/
   ```
2. Wildcards work in queries: `asset(action="search", query="/Game/Characters/*")`.

## Logs

### Bridge log
```
editor(action="get_log", category="LogMCPBridge")
```

### Full output log
```
editor(action="get_log")
```

### Search the log
```
editor(action="search_log", query="error")
```

### Server and daemon logs

The server writes to stderr, which your MCP client stores in its own log. The daemon writes `~/.ue-mcp/daemons/<key>.log`. Set `UE_MCP_LOG_LEVEL=debug` for more.
