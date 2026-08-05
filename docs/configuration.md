# Configuration

## MCP Client Configuration

The easiest way to configure UE-MCP is to run `npx ue-mcp init` — it detects your MCP clients and writes the config automatically.

### Manual Configuration

```json
{
  "mcpServers": {
    "ue-mcp": {
      "command": "npx",
      "args": ["ue-mcp", "C:/path/to/MyGame.uproject"]
    }
  }
}
```

Codex uses TOML instead:

```toml
[mcp_servers.ue-mcp]
command = "npx"
args = ["ue-mcp", "C:/path/to/MyGame.uproject"]
cwd = "C:/path/to"
enabled = true
```

### Where to Put This

| Client | Config File |
|--------|-------------|
| Claude Code | `.mcp.json` in project root, or `~/.claude/` global config |
| Claude Desktop | `claude_desktop_config.json` |
| Cursor | `mcp.json` in `.cursor/` or project root |
| Codex | `~/.codex/config.toml` |

### Without a Project Path

You can start the server without a `.uproject` argument. It will run in a limited mode — you can then use `project(action="set_project", projectPath="...")` at runtime to attach to a project.

### Several Editors From One Server

Pass more than one `.uproject` and each becomes an addressable **editor session** with its own bridge connection, its own port and its own port lockfile:

```json
{
  "mcpServers": {
    "ue-mcp": {
      "command": "npx",
      "args": ["ue-mcp", "C:/games/Alpha/Alpha.uproject", "C:/games/Beta/Beta.uproject"]
    }
  }
}
```

Sessions are keyed by resolved project root, so one project is one session however its path is spelled, and two editors of the same project are never addressed as two sessions.

| Action | What it does |
|--------|--------------|
| `project(action="list_editors")` | Every session: name, project, bridge port, socket state, liveness, and which one untargeted calls use |
| `project(action="add_editor", projectPath="...")` | Register another project at runtime. `start: true` also launches its editor and waits until it is ready |
| `project(action="use_editor", editorTarget="Beta")` | Move the default target |
| `project(action="drop_editor", editorTarget="Beta")` | Forget a session and close its socket. The editor keeps running |

While more than one session is registered, **every** category tool, the `flow` tool and the HTTP `run` route accept an `editor` parameter naming the session, the project, or a `.uproject` path:

```
level(action="place_actor", editor="Beta", assetPath="/Game/Rock", location=[0,0,0])
editor(action="start_editor", editor="Beta")
flow(action="run", flowName="build_and_check", editor="Beta")
```

Calls with no `editor` run in the active session, which is the first project on the command line until `use_editor` moves it.

**One editor is unchanged.** The `editor` parameter is advertised only while a second session exists, so a single-editor client sees exactly the schema, the status response and the events it always has. Registering or dropping a session re-advertises the tool list, so a client that honours `tools/list_changed` can target an editor it registered at runtime.

**Each project needs its own port.** Ports are derived from the project root, so this works out of the box. Pinning every project to the same port (the same `bridge.port` in two `ue-mcp.yml` files, or a global `UE_MCP_PORT`) collapses the sessions onto one address, and a call targeted at one of them can then be served by the other. `list_editors` reports the clash under `portSharedWith`. Give each project its own port, or leave them derived.

**Lifecycle actions act only on the editor you addressed.** `start_editor`, `stop_editor` and `restart_editor` resolve their target from the addressed session's own project: its port lockfile, and the PID that lockfile names, checked against the `.uproject` that process has open. See [Which editor lifecycle actions act on](#which-editor-lifecycle-actions-act-on). `drop_editor` detaches only: stopping an editor is always an explicit `editor(action="stop_editor")` against that session.

Multi-editor needs a bridge built from the current plugin source in each project involved. A project whose bridge is missing or stale is still registered and still startable; `project(action="list_editors")` reports it.

## Project Configuration (`ue-mcp.yml`)

Project config lives in `ue-mcp.yml` next to your `.uproject`, tracked in git so every collaborator shares the same surface. `npx ue-mcp init` scaffolds and maintains it.

```yaml
ue-mcp:
  version: 1
  contentRoots: [/Game/, /MyPlugin/]
  disable: [gas]
  nativeTools: { enabled: true }
  http: { enabled: false }
  context: { strategy: full }
tasks: {}
flows: {}
plugins: []
```

The file is one layer in a deep-merged stack (user-global → project → env → local → env vars). For the full anatomy, the layer cascade, where each setting belongs, and every `ue-mcp:` key, see the dedicated **[ue-mcp.yml Reference](config-file.md)**. The behavioral deep-dives for two of its keys live below: [Native Epic tools](#native-epic-5-8-tools) (`nativeTools`) and [Context strategy](#context-strategy-full-lean-micro) (`context`).

### Native Epic 5.8 tools

Unreal Engine 5.8 ships an experimental AI Toolset Registry (the plugin behind Unreal's own MCP server). ue-mcp reaches that registry in-process and surfaces every official toolset as first-class actions inside the matching ue-mcp category - Epic's GAS tools appear in `gas`, Niagara in `niagara`, and so on - so an agent discovers them in context. Toolsets with no natural home are reachable through the `epic` gateway (`status` / `list_toolsets` / `describe_toolset` / `call_tool`).

- **On by default.** `npx ue-mcp init` includes a "Native Unreal tools (Epic 5.8)" page where you enable the feature and optionally exclude specific categories. The choice is written to `nativeTools` in `ue-mcp.yml`.
- **Requires UE 5.8+** with the `ToolsetRegistry` plugin (and the toolset plugins you want) enabled in your project. On older engines or when the plugin is absent, enrichment is skipped and `epic(status)` reports `available: false`.
- **Deterministic surface.** The catalog is sourced from the live editor when connected, falling back to a per-project cache and then a snapshot baked into the ue-mcp package, so the wrapped tools appear even on a cold first start and match the generated [tool reference](tool-reference.md) (both are built from the same snapshot). The 🧩 badge in the tool reference marks every wrapped official tool.

To turn it off entirely, set `nativeTools.enabled: false` (the `epic` gateway stays available). To keep it on but drop a noisy domain, add that category to `nativeTools.exclude`.

The feedback approval mode (`interactive` / `auto-approve` / `defer`) is intentionally **not** in `ue-mcp.yml` — it varies per developer and per machine, so it lives in `~/.ue-mcp/state.json` and is managed with `npx ue-mcp feedback mode ...` or the `UE_MCP_FEEDBACK_MODE` env var. See [Feedback → modes](feedback.md#feedback-modes).

### User-machine state (`~/.ue-mcp/`)

Machine-specific state that ue-mcp commands write but you wouldn't hand-edit lives under `~/.ue-mcp/`:

| Path | What |
|------|------|
| `~/.ue-mcp/state.json` | Two things: (a) per-project `installedHooks` — absolute paths of every Claude Code `settings.json` where ue-mcp installed the feedback PostToolUse hook, keyed by absolute project root; (b) `preferences.feedback.mode` — your personal default for the feedback approval mode (`interactive` / `auto-approve` / `defer`). Maintained by `npx ue-mcp init`, `npx ue-mcp uninstall-hooks`, and `npx ue-mcp feedback mode`. |
| `~/.ue-mcp/auth.json` | Cached GitHub OAuth token for `feedback(submit)` author=user mode. Mode 600. Written by `npx ue-mcp auth`. |
| `~/.ue-mcp/pending-feedback/<id>.json` | Submissions captured while `feedback mode` is `defer`. Acted on with `npx ue-mcp feedback list/approve/discard`. |

These files never need to be in your project tree or in version control.

## Plugins

The `plugins:` array in **`ue-mcp.yml`** declares npm packages that inject new actions into existing built-in categories. The full author contract lives in [Plugins](plugins.md); this is the consumer view.

```yaml
plugins:
  - name: pie-studio
  - name: some-other-plugin
    version: "0.2.x"        # optional - npm semver range
```

At server start, ue-mcp resolves each entry against the project's `node_modules/`, validates the plugin manifest, and merges its injected actions into the host category tools. Stay-on-disk facts:

- The package must already be installed under `<project>/node_modules/`. Use `ue-mcp plugin install <name>` to add an entry **and** run `npm install --save` in one step.
- Plugins are loaded only when the server boots — edit the array and restart your MCP client (`/mcp` in Claude Code).
- A plugin that fails validation is skipped with a loud warning. Other plugins keep loading; the host tools are never partially mutated.
- Use the `plugins` tool to introspect the loaded set:
  - `plugins(action="list")` — name, version, prefix, status, injected count, host UE plugin presence.
  - `plugins(action="describe", name="<package>")` — full detail including injected actions, knowledge files, and flows.

Order matters: earlier entries win on inter-plugin action-name collisions. A plugin action can never overwrite a built-in.

### Host UE plugin dependencies

A plugin can declare `uePluginDependency: <PluginName>` in its `ue-mcp.plugin.yml`. The MCP server checks the project's `.uproject` for `Plugins[].Name == "<PluginName>"` and exposes the result as `uePluginPresent` in `plugins(action="list")`. The npm side loads regardless — the flag is a signal that the host UE plugin needs to be enabled before the injected actions will actually run.

For example, a plugin that declares `uePluginDependency: SomePlugin` will report `uePluginPresent: false` until `SomePlugin` is added to `<Project>.uproject`'s `Plugins` array and the C++ modules are built.

## Context strategy (full, lean, micro)

Everything the server injects at session start - the `initialize` instructions plus the whole `tools/list` payload (names, descriptions, and parameter schemas) - is the "context tax". Three strategies trade that seed cost against how many discovery round-trips an agent makes. Measure the tax on your own project with `npm run context-tax` (set `ANTHROPIC_API_KEY` for exact token counts).

| Strategy | Seed (test project) | What's advertised | Cost to use |
|----------|--------------------|-------------------|-------------|
| **`full`** (default) | ~45k tokens | all 22 category tools, every action + parameter inline | zero discovery calls |
| **`lean`** | ~23k tokens | the same 22 tools with their validated `action` enums, but descriptions collapsed to a summary; a `catalog` tool (`search` / `describe` / `list_categories`) and a per-category `describe` action serve the details on demand | ~1 round-trip to learn a category |
| **`micro`** | ~1k tokens | a single `tools` gateway - `list_categories`, `describe`, and `call` - fronting every category; nothing else | discovery for everything |

- **full** is best when the agent should see the entire surface up front and you are not token-constrained.
- **lean** keeps action names visible (so the model can often call directly, and unknown actions are still rejected up front) while dropping the prose. A solid middle ground.
- **micro** mirrors the native MCP toolset gateway (`list_toolsets` / `describe_toolset` / `call_tool`): the agent calls `tools(action="list_categories")`, then `tools(action="describe", category="blueprint")`, then `tools(action="call", category="blueprint", method="create", args={ ... })`. Smallest possible seed, most discovery traffic.

Set the strategy with the standalone command (writes `ue-mcp.yml` for you):

```
npx ue-mcp context full      # every action inline (default)
npx ue-mcp context lean      # names visible, descriptions on demand
npx ue-mcp context micro     # one gateway tool fronts everything
npx ue-mcp context           # show the current strategy
```

`npx ue-mcp init` also has a **Context strategy** page. Or edit `ue-mcp.yml` directly:

```yaml
ue-mcp:
  context:
    strategy: micro
```

Or per session, without editing the file: `UE_MCP_CONTEXT_STRATEGY=micro` (the env var wins over the config value). Anything other than `lean` or `micro` resolves to `full`. Restart your MCP client (`/mcp` in Claude Code) after changing the strategy.

## Bridge Connection

The C++ plugin listens on a **per-project WebSocket port** derived from a hash of the project root path (in the IANA ephemeral range `49152-65535`). Deriving the port from the path means two checkouts of the same project - or several unrelated projects - on one machine each get a stable, launch-order-independent port, so their MCP clients never collide on a single fixed number. The Node client and the C++ bridge compute the identical value independently, and the bridge also publishes the actual bound port to `<project>/Saved/UE_MCP_Bridge/port.json` as the authoritative source (if the port is already taken, the bridge probes upward and the lockfile records where it really landed). The legacy fixed port `9877` remains the fallback when no project root is known. The MCP server auto-connects on startup and reconnects every 15 seconds if the connection drops.

On Windows the listening socket is claimed with `SO_EXCLUSIVEADDRUSE`, so a second editor of the same project cannot bind the same port. It walks upward instead and publishes where it landed, which is what lets two editors of one project coexist without their clients reaching the wrong one.

### Pinning the port

Pin an explicit port with `bridge.port` in `ue-mcp.yml`:

```yaml
ue-mcp:
  bridge:
    port: 50123
```

**Both halves honour it.** The client reads the key when it chooses where to connect, and the editor-side plugin reads it when it chooses where to listen, so a pinned project has one number on both ends (#819). Precedence is identical on both sides, highest first:

| Rank | Source | Notes |
|------|--------|-------|
| 1 | `-MCPPort=NNNN` on the editor command line, or an explicit port argument to the client | Per launch |
| 2 | `UE_MCP_PORT` | Per environment. Applies to every project the shell starts, which is why it is the wrong tool for pinning one project |
| 3 | `bridge.port` in the config layers | Per project, and the one to reach for |
| 4 | The port derived from the project root path | The default when nothing is pinned |

The plugin reads `bridge.port` from the same layered files as the client, and honours the same winner: `~/.ue-mcp/config.yml`, then `<project>/ue-mcp.yml`, then `<project>/ue-mcp.{env}.yml` when `UE_MCP_ENV` is set, then `<project>/ue-mcp.local.yml`. See [Config layering](config-file.md#config-layering).

Two things to know about a pinned port:

- **The lockfile still wins at connect time.** A pin is a request, not a guarantee. If something else already holds the port, the bridge walks upward, binds what it can, logs a warning naming the port you asked for and the one it took, and publishes the port it actually bound. The client reads `port.json`, so it follows.
- **A value the plugin cannot use is announced, not applied.** A `bridge.port` that is not a whole number in `1-65535` is logged as a warning and the derived port is used instead. The plugin reads this one key with a small purpose-built reader rather than a full YAML parser (Unreal ships none, and one integer is not worth a dependency), so it handles plain nested keys and refuses anything more exotic. Written the ordinary way, as in the example above, it is read. Written as a flow mapping (`bridge: { port: 50123 }`) or with anchors, it is not, and the editor log says so.

### Which editor lifecycle actions act on

`editor(start_editor)`, `editor(stop_editor)` and `editor(restart_editor)` act on a process, so they resolve their target more strictly than ordinary tool calls (#819):

- **The port comes from the lockfile and nowhere else.** No environment variable, no derived value, no `9877`. The bridge writes `<project>/Saved/UE_MCP_Bridge/port.json` whatever port it binds and removes it on exit, so it is the only file that says where this project's editor is listening. When it is absent, the action fails and names the path it checked instead of probing a port some other project's editor could answer on.
- **The process check is scoped by `.uproject`.** "Is an editor running" is never the question; "is the editor holding this project open running" is. A second editor, a headless shard, or somebody else's project no longer blocks a launch.
- **Stopping verifies before it acts.** The lockfile records the editor's PID, and a lockfile outlives a crash, so `stop_editor` confirms that PID is still an editor for this project before it sends the quit request.
- **No project loaded means no lifecycle action.** With nothing loaded there is no editor these calls could be about, so they say so rather than picking one.

Consequence worth knowing: if an editor is killed rather than closed, its lockfile stays behind and `stop_editor` reports the stale PID it names. Delete `<project>/Saved/UE_MCP_Bridge/port.json` to clear it.

### Bridge state files

The bridge keeps two records under `<project>/Saved/UE_MCP_Bridge/`. Both are published by rename, so a client polling them never reads a half-written file.

| File | Written when | Contents |
|------|--------------|----------|
| `port.json` | The bridge is listening | `port`, `pid`, `instanceId`, `startedAt`, `status`, `protocolVersion`, `handlerApiVersion` |
| `bridge-error.json` | The editor came up but the bridge could not bind | `status: "bind-failed"`, the port range tried, the socket error, `pid`, `instanceId` |

`instanceId` identifies the server object that wrote the record, and only that instance removes it. A pid alone is not enough, since pids are recycled: an editor that failed to start can no longer delete the record of one that is running, and a second instance of a project cannot overwrite the first's. The `pid` is what lifecycle actions check before they act on a record, as described above.

`bridge-error.json` is what distinguishes "no editor" from "editor running, bridge dead". When a connection fails and this record names a live process, the client quotes its detail in the error.

### Connection States

| State | Meaning |
|-------|---------|
| **Connected** | Bridge is active, all tools available |
| **Disconnected** | Editor not running or plugin not loaded. Filesystem tools still work (INI parsing, C++ headers, asset listing) |
| **Reconnecting** | Connection lost, auto-retry in progress |

### Who may connect

The bridge binds loopback only and refuses every upgrade that carries an `Origin` header. Browsers always send one on a WebSocket upgrade and cannot suppress or forge it, so this keeps out any page served by a local dev server, which would otherwise be able to scan the port range and call `execute_python`. Native clients (the npm client, curl, editor tooling) omit the header and are unaffected.

Check the current state with `project(action="get_status")`. Alongside `editorConnected`, it reports `editorTarget`: the `.uproject` the connection belongs to, the port, and how that port was chosen (`lockfile`, `config`, `derived`, `env`, `explicit`). The connection is always bound to one project, so `editorTarget.projectPath` matches the loaded project.

### Switching Projects

`project(action="set_project", projectPath="...")` moves path resolution and the editor connection together. The socket to the previous project's editor is dropped before the new project is loaded, so no action can execute in the project you just left.

The port for the new project is chosen from that project alone: its `Saved/UE_MCP_Bridge/port.json` lockfile first, then its own `bridge.port`, then the port derived from its root path.

A port pinned with `UE_MCP_PORT` (or an explicit port argument) is the exception. It was chosen for whichever project the server started on and says nothing about the one you switched to, so if the new project has published no lockfile the switch still completes and the connection is refused with an explanation. Start the new project's editor, which publishes the lockfile, or clear the pin so each project gets its derived port.

## Plugin Deployment

On first run with a project path, the server automatically:

1. Copies `plugin/ue_mcp_bridge/` → `<Project>/Plugins/UE_MCP_Bridge/`
2. Adds `UE_MCP_Bridge` to the `.uproject` plugins list
3. Enables `PythonScriptPlugin` if not already enabled (needed for `execute_python` escape hatch)

The plugin is editor-only and has no runtime footprint.

### Plugin Dependencies

The C++ bridge plugin enables these UE plugins (adding them to `.uproject` if missing):

- `PythonScriptPlugin` — for `editor(action="execute_python")`
- `EnhancedInput` — for input action/mapping creation
- `GameplayAbilities` — for GAS tools
- `Niagara` — for VFX tools
- `PCG` — for procedural generation tools

## CLI Subcommands

`npx ue-mcp` exposes a few utility subcommands beyond the default MCP server entry:

| Command | Description |
|---------|-------------|
| `npx ue-mcp init` | Interactive setup wizard. Deploys the C++ bridge plugin, writes MCP client configs, scaffolds `ue-mcp.yml`, optionally installs Claude Code skills + feedback prompt hook, optionally runs the GitHub OAuth device flow. Migrates any legacy `.ue-mcp.json` / `ue-mcp.local.yml` it finds. |
| `npx ue-mcp update` | Check npm for the latest version and install it. Pass `--deploy` to also redeploy the plugin sources. |
| `npx ue-mcp deploy` | Copy the C++ bridge plugin sources into the project. Use after `ue-mcp update` or to force a redeploy. |
| `npx ue-mcp build` | Build the project C++ code using Unreal Build Tool. Stop the editor first. |
| `npx ue-mcp auth` | Run the GitHub device flow standalone so `feedback(submit)` can author issues as your real GitHub user. Same step that lives inside `init`; use this if you skipped it at init time. |
| `npx ue-mcp uninstall-hooks` | Remove the feedback PostToolUse hook from every Claude Code settings file recorded for this project in `~/.ue-mcp/state.json`. |
| `npx ue-mcp feedback mode [<mode>]` | Read or set your personal feedback approval mode (`interactive`, `auto-approve`, or `defer`). Stored in `~/.ue-mcp/state.json`. See [Feedback → modes](feedback.md#feedback-modes). |
| `npx ue-mcp feedback list \| show \| approve \| discard \| review` | Manage submissions queued while feedback mode is `defer`. `review` (experimental) walks the queue interactively (approve/discard/skip per item). See [Feedback → Reviewing deferred submissions](feedback.md#reviewing-deferred-submissions). |
| `npx ue-mcp resolve <issue>` | Fetch a feedback issue, branch, hand it to Claude Code to implement, open a PR. See [Feedback](feedback.md#resolving-feedback-issues). |
| `npx ue-mcp plugin install <name>` | Install a ue-mcp plugin from npm and register it in `ue-mcp.yml`. See [Configuration → Plugins](#plugins). |
| `npx ue-mcp plugin uninstall <name>` | Inverse of install. |
| `npx ue-mcp plugin create <name>` | Scaffold a new plugin package. See [Plugins](plugins.md). |
| `npx ue-mcp context [full\|lean\|micro]` | Read or set the [context strategy](#context-strategy-full-lean-micro) in `ue-mcp.yml`. No argument prints the current strategy. |

## Editor Lifecycle

The server can manage the editor process:

| Command | Description |
|---------|-------------|
| `editor(action="start_editor")` | Launch UE with the current project |
| `editor(action="stop_editor")` | Gracefully stop the editor |
| `editor(action="restart_editor")` | Stop and relaunch |
| `editor(action="build_project")` | Build the project C++ code via UBT |
