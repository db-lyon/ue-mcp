# Troubleshooting

## Connection Issues

### "Editor not connected" / Bridge not running

**Symptoms:** `project(action="get_status")` shows disconnected. Tools that require the editor return errors.

**Fixes:**

1. **Is the editor running?** The C++ bridge plugin only runs when the editor is open.
2. **Was the editor restarted after first setup?** The plugin is deployed on first run but needs an editor restart to load.
3. **Check the Output Log.** In the editor: **Window > Developer Tools > Output Log**, filter on `LogMCPBridge`. You should see:
   ```
   LogMCPBridge: [UE-MCP] Bridge listening on ws://localhost:9877
   ```
4. **Port conflict.** If another process is using port 9877, the bridge can't start. Check with:

    === "Windows"
        ```bash
        netstat -ano | findstr 9877
        ```

    === "macOS / Linux"
        ```bash
        lsof -i :9877
        ```

### Handlers time out, or the editor never finishes starting

**Symptoms:** every call returns `Handler execution timed out`, `start_editor` waits out its timeout, or `get_status` says `disconnected` while an editor is plainly open.

All of these mean the game thread is not returning to its tick loop, and the usual cause is something the editor is showing on screen: a modal dialog waiting for an answer, a long slow task (shader compile, asset registry scan, map load), or a startup phase that runs before the bridge plugin loads.

**Ask what the engine is actually doing:**

```
editor(action="get_engine_state")
```

This one call is answered without the game thread. It reports:

- **`snapshot.slowTask`** - the name and percentage of the task the editor's own progress bar is showing.
- **`snapshot.modal`** - the title, message, and buttons of the dialog blocking the game thread. Answer it with `editor(action="respond_to_dialog")`, or stop it happening again with `editor(action="set_dialog_policy")`.
- **`snapshot.gameThreadStalledSeconds`** - how long the game thread has gone without ticking. Everything else in the snapshot is as old as this number says. It is `null` while `gameThreadTicking` is false, which means the editor is still starting and has no engine loop yet; `modulesLoaded` is the progress signal during that window.
- **`snapshot.compiling`** - remaining shader jobs and asset compiles.
- **`log.phase` / `log.tail`** - the startup phase parsed from the editor's own log, which is written from the first millisecond and so covers the window before the plugin exists. This is where "the following modules are missing or built with a different engine version" shows up.
- **`processes`** - PID, command line, and whether the OS considers the process responsive.
- **`dialogs`** - native (pre-Slate) message boxes, including the rebuild prompt above.

A timed-out handler carries the same snapshot in its `engineState` field, so a timeout says what the engine was doing while the request waited.

The snapshot is also written to `<Project>/Saved/UE_MCP_Bridge/status.json` four times a second by a thread that keeps running while the game thread is blocked. Read that file directly when the bridge socket itself is unreachable.

**During startup**, the snapshot comes from a second plugin module that loads at `PostConfigInit`, well before the bridge itself (`PostEngineInit`) and before any socket exists. A cold launch publishes its first state after about a second and then tracks what the splash screen shows:

```
+1.6s  config init              | modules=0   | Initializing... 0%
+3.3s  config init              | modules=16  | Initializing Render Hardware Interface... 5%
+13.0s config init              | modules=253 | Loading Default Modules for Plugin: ChaosVD 73%
+17.5s engine loop initialized  | modules=725 | New Map 92%
+20.7s ready                    | modules=733 | Running Python start-up scripts... 95%
```

That is the window where "the editor is stuck on the splash screen" reports come from, so read `status.json` (or `editor(get_engine_state)`, which merges it with the log) before assuming a launch failed.

`editor(start_editor)` already waits through all of it. It blocks until the snapshot reports `ready`, draws that same trace as a progress bar in the terminal, and returns the phase timeline:

```
Editor ready in 24.7s
launching 0s -> loading modules and plugins 0.8s -> config init 1.8s
  -> bridge starting 21.1s -> engine loop initialized 21.5s -> ready 24.7s
```

There is no reason to poll after it returns, and no reason to poll while it runs.

### The tool call sits there showing nothing while the editor starts

**Symptom:** `start_editor` displays as a motionless line - `ue-mcp - editor (MCP)(action: "start_editor", timeout: 600)` - for the whole launch, with no progress.

The call is not stuck; it returns as soon as the editor is ready, and the number you see is the `timeout` argument, not elapsed time. The missing progress is a client-side regression: Claude Code collapses MCP tool calls unconditionally from 2.1.116 on, so the `notifications/progress` messages the server emits throughout the wait are received and never drawn ([anthropics/claude-code#51713](https://github.com/anthropics/claude-code/issues/51713); 2.1.101 was the last version that displayed them). An MCP server's stderr does not reach the transcript either - the client writes it to a log file.

Nothing is wrong on the ue-mcp side and there is nothing to fix in your setup: other MCP clients render the same stream normally. When ue-mcp detects an affected Claude Code version, `start_editor` says so in its result rather than leaving the call looking hung, and the phase timeline it returns is exactly what you would have watched live.

### Connection drops / reconnecting

The MCP server auto-reconnects every 15 seconds. If the editor is restarted, the connection will restore automatically.

If the connection is flapping (connecting then immediately disconnecting), check the editor's Output Log for errors in the `LogMCPBridge` category.

### stop_editor or restart_editor says no port is published

**Symptom:** `stop_editor` returns something like `No bridge port published at C:/Game/Saved/UE_MCP_Bridge/port.json`, or reports that the PID in that file is no longer running.

The editor publishes that file while its bridge is listening and removes it on a clean exit, and lifecycle actions read it and nothing else: a guessed port is how a quit request reaches whichever editor happens to hold that number (#819). So the message means one of three things.

1. **No editor is running for this project.** Nothing to stop. Start one with `editor(start_editor)`.
2. **The editor is running but its bridge never started.** Check the Output Log for `LogMCPBridge` (see [Bridge not running](#editor-not-connected--bridge-not-running)). The message includes the phase the editor is actually at.
3. **A previous editor was killed rather than closed.** Its lockfile is still on disk naming a PID that has gone. Delete `<project>/Saved/UE_MCP_Bridge/port.json` and the message clears.

`start_editor` refusing with "Editor is already running for this project" is the same targeting rule from the other side: it names the PID, and that PID has this project's `.uproject` on its command line. Editors for other projects and headless shards never trigger it.

### A call ran in the wrong editor

Only possible with more than one editor session registered. Start with `project(action="list_editors")`: it reports every session, the bridge port each resolved to, and which one untargeted calls fall through to.

- **The call had no target.** Untargeted calls run in the active session. Pass `editor="<name>"` on the call, or move the default with `project(action="use_editor", editorTarget="<name>")`.
- **The `editor` parameter is not advertised.** It appears only while more than one session is registered. Add the other project with `project(action="add_editor", projectPath="...")`, or list both `.uproject` paths in your MCP client config.
- **Two sessions on one port.** `list_editors` reports it as `portSharedWith`. It happens when two projects pin the same `bridge.port`, or when a global `UE_MCP_PORT` overrides both, and it means the editor answering there cannot be attributed to either project. Give each project its own port, or unset the variable, then restart the server.

Lifecycle actions are not affected by the last case: they resolve through the addressed project's own lockfile and the PID it names, and refuse rather than guess.

## Plugin Build Issues

### Plugin fails to compile

The C++ bridge links against many UE modules. If compilation fails:

1. **Missing plugins.** Ensure these are enabled in your `.uproject`:
    - `PythonScriptPlugin`
    - `EnhancedInput`
    - `GameplayAbilities`
    - `Niagara`
    - `PCG`

2. **UE version mismatch.** The plugin is tested with UE 5.4–5.8. Older versions may have API differences. Check the build log for specific errors.

3. **Rebuild from clean.** Delete `<Project>/Plugins/UE_MCP_Bridge/Binaries/` and `<Project>/Plugins/UE_MCP_Bridge/Intermediate/`, then rebuild.

### Plugin not loading

If the editor starts but the bridge doesn't appear in the Output Log:

1. Check **Edit > Plugins** in the editor — search for "UE_MCP_Bridge" and ensure it's enabled.
2. Check that the plugin is listed in your `.uproject`:
   ```json
   { "Name": "UE_MCP_Bridge", "Enabled": true }
   ```

## MCP Server Issues

### Server won't start

1. **Node.js version.** Requires Node 18+. Check with `node --version`.
2. **Build step.** Make sure you ran `npm run build` — the server runs from `dist/index.js`, not source.
3. **Path to .uproject.** The path must be absolute and point to a valid `.uproject` file.

### Tools return errors

- **"Bridge not connected"** — the editor isn't running or the plugin isn't loaded. See connection issues above.
- **"Handler not found"** — the action name might be wrong. Check the [Tool Reference](tool-reference.md) for valid action names.
- **"Asset not found"** — asset paths should use the `/Game/` prefix (e.g., `/Game/Blueprints/BP_Player`), not filesystem paths.
- **Timeout** — the default timeout is 30 seconds. Long operations (build lighting, cook content) may need patience.

## Asset Path Issues

UE-MCP expects Unreal-style asset paths:

| Format | Example |
|--------|---------|
| Content path | `/Game/Blueprints/BP_Player` |
| Plugin content | `/MyPlugin/Assets/SomeAsset` |
| Full object path | `/Game/Blueprints/BP_Player.BP_Player_C` |

!!! warning "Common mistakes"
    - Using filesystem paths (`C:/Users/.../Content/...`) — use `/Game/...` instead
    - Including file extensions (`.uasset`) — omit the extension
    - Missing the leading slash — `/Game/Foo`, not `Game/Foo`

## Class Names and the A/U/F/E Prefix

Unreal registers a class under its C++ name **minus** the type prefix. `AActor` is the class named `Actor`, `UMyConfig` is `MyConfig`, and its object path is `/Script/MyGame.MyConfig` with no `U` in it.

Every class parameter in the bridge (`className`, `parentClass`, `parentFilter`, `componentClass`, `actorClass`, `nodeClass`, `schema`, and the rest) accepts either spelling. Resolution tries, in order:

1. The literal spelling you passed.
2. The prefix-stripped spelling (`UMyConfig` to `MyConfig`).
3. The prefixed spellings (`MyActor` to `AMyActor` / `UMyActor`).
4. The object path form, including `Module.Class` promoted to `/Script/Module.Class` and the prefix stripped from the object part of a path.
5. The Blueprint generated class (`BP_Thing` to `BP_Thing_C`).
6. A case-insensitive sweep of loaded classes, native classes winning ties.

When nothing resolves, the error lists every spelling that was tried and the closest loaded class names:

```
Class not found for className 'UXianGameConfig'. Tried: UXianGameConfig, XianGameConfig,
AUXianGameConfig, UUXianGameConfig, UXianGameConfig_C, XianGameConfig_C. UE reflection
stores class names without the C++ type prefix, so UMyConfig is registered as 'MyConfig'
and its path is /Script/<Module>.MyConfig. Closest loaded classes: XianGameConfig.
```

Two error shapes are deliberately distinct, and the JSON carries a `reason` field:

| `reason` | Meaning |
|---|---|
| `class_not_found` | No spelling resolved. The response also carries `tried` and `suggestions` arrays. |
| `abstract` / `deprecated` | The name resolved. The class itself cannot be instantiated; pass a concrete subclass. |
| `wrong_base` | The name resolved to a class outside the family the action needs (for example a non-`UDataAsset` passed to `create_data_asset`). |

If the suggestion list is empty, the owning module may not be loaded yet. Check with `reflection(action="is_module_loaded", moduleName="MyGame")` and `reflection(action="is_class_loaded", className="MyConfig")`.

## Updates Don't Take Effect (server stuck on an old version)

If `ue-mcp update` reports "already up to date" but the running server keeps reporting an old version, a project-local `node_modules/ue-mcp` is shadowing the global install:

```bash
ue-mcp doctor
```

```
local shadow:   ./node_modules/ue-mcp @ 1.0.64   <-- WARN npx runs THIS, not global
effective (npx):1.0.64  (behind latest 1.0.76)
```

When `ue-mcp` is a dependency in the project's `package.json`, `npx ue-mcp` runs the local copy, so `npm i -g ue-mcp@latest` updates a copy npx never uses. Fixes:

- `ue-mcp update --build` aligns the local copy to latest automatically, or
- remove `ue-mcp` from the project's `package.json` and delete `node_modules/ue-mcp`, or
- pin `.mcp.json` to `npx -y ue-mcp@latest` so the server self-heals to latest on every launch.

Then quit and relaunch your MCP client so it spawns the updated server.

## A Fix Shipped but the Editor Behaves the Same (stale compiled plugin)

Different from the case above. Here the version is correct - `ue-mcp doctor` shows latest, the server is up to date - but a fix that changes **editor behavior** (a dialog being auto-cancelled, an actor placed wrong, anything the C++ plugin does) still happens.

Cause: the bridge's editor-side half is a C++ plugin shipped as source. Your editor runs the **compiled** version of it, and a plain `ue-mcp update` neither deploys the new source into your project nor recompiles it. The version `doctor` reports is the npm/server half, so it looks up to date while the loaded plugin is stale. The fix never reaches the editor.

Fix: rebuild the plugin, then restart the editor so the new binary loads.

```bash
ue-mcp update --build
```

If `--build` reports success but the behavior still persists, force a clean rebuild (incremental builds and Live Coding can load stale patches over a fresh build):

1. Delete `<Project>/Plugins/UE_MCP_Bridge/Binaries/` and `<Project>/Plugins/UE_MCP_Bridge/Intermediate/`.
2. Delete any `*.patch_*.{dll,pdb,lib,exp}` under `<Project>/Binaries/Win64/`.
3. Run `ue-mcp update --build` again, then restart the editor.

## Search Not Finding Assets

If `asset(action="search")` misses assets in plugin directories:

1. Add the content root to `ue-mcp.yml`:
   ```yaml
   ue-mcp:
     version: 1
     contentRoots:
       - /Game/
       - /MyPlugin/
   ```
2. Wildcards work in search queries: `asset(action="search", query="/Game/Characters/*")`

## Logs

### Bridge logs
```
editor(action="get_log", category="LogMCPBridge")
```

### Full output log
```
editor(action="get_log")
```

### Search logs
```
editor(action="search_log", query="error")
```
