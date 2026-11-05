# The `ue-mcp.yml` config file

`ue-mcp.yml` sits next to your `.uproject` and declares the project's ue-mcp surface: which tool categories are on, the content roots to search, the bridge port, the context strategy, and your custom `tasks:` and `flows:`. `npx ue-mcp init` creates and maintains it. It is plain YAML, and hand-editing is expected.

This page is the key reference. For MCP client setup, the daemon, the bridge connection and CLI commands, see [Configuration](configuration.md).

## Anatomy

Only the `ue-mcp:` block is needed. `init` writes `version: 1`, `contentRoots: [/Game/]`, `tasks: {}` and `flows: {}`.

```yaml
ue-mcp:            # project and server config (reference below)
  version: 1
  contentRoots: [/Game/, /MyPlugin/]
  disable: [gas, networking]
  nativeTools: { enabled: true, exclude: [animation] }
  context: { strategy: micro }  # micro (default) | lean | full
  server: { mode: daemon }      # daemon (default) | in-process

tasks: {}          # custom flow-engine task definitions
flows: {}          # custom multi-step flows
plugins: []        # npm packages that inject new actions
```

| Top-level key | What it holds | Detailed in |
|---|---|---|
| `ue-mcp:` | Project and server config | [block reference](#ue-mcp-block-reference) below |
| `tasks:` | Custom flow-engine task definitions | [Flows](flows.md) |
| `flows:` | Custom multi-step flows | [Flows](flows.md) |
| `plugins:` | npm packages injecting actions into built-in categories (`name`, optional `version` range) | [Using plugins](plugins-using.md) |
| `guards:` | Project guards: deny or observe policies around actions | [Guards](plugins-guards.md) |
| `git_snapshot:` | Snapshot `Content/` and `Config/` before a flow and reset on failure | [Flows](flows.md#git-snapshot) |

## Config layering

`ue-mcp.yml`, its overlays and your user-global config are **deep-merged** into one effective config. Layers, lowest precedence first:

| Layer | File | Tracked? | For |
|-------|------|----------|-----|
| Built-in defaults | (shipped in the package) | - | The baseline every project starts from. |
| **User-global** | `~/.ue-mcp/config.yml` (or `UE_MCP_GLOBAL_CONFIG`) | No | Your defaults for every project on this machine, such as `context.strategy`. Same shape as the project file. |
| **Project** | `<project>/ue-mcp.yml` | **Yes** | The shared surface every collaborator gets. |
| Env overlay | `<project>/ue-mcp.{env}.yml` | Optional | Loaded when `UE_MCP_ENV` names it, or when a config layer sets `env:`. `UE_MCP_ENV=ci` loads `ue-mcp.ci.yml`. The variable wins and applies to every project the server drives. |
| **Local** | `<project>/ue-mcp.local.yml` | No (init git-ignores it) | Your overrides for this one project. |
| Env vars | `UE_MCP_CONTEXT_STRATEGY`, `UE_MCP_SERVER_MODE`, `UE_MCP_PORT` | - | Win over every file. |

**Merge rules.** Nested objects merge key by key, so a later layer setting `context.strategy` keeps its sibling keys. Arrays replace; put `__merge: append` on an override array to append to the base instead. A `null` in a later layer clears a value.

## Where each setting belongs

Every key is valid in every layer. The layer decides whether a value is shared with the team, personal, or tied to one machine.

| Setting | Usually lives in | Because |
|---|---|---|
| `version`, `contentRoots`, `http`, `tasks`, `flows`, `plugins` | **Project** `ue-mcp.yml` | The whole team needs the same value. |
| `context.strategy` | **User-global** or **local** | A token-budget preference that differs per person. |
| `disable`, `nativeTools` | Either | A project can ship a default (no GAS here), and anyone overrides it in their user or local layer. |
| `bridge.port` | **Project** for a team-wide known port; **local** for one machine | The client and the plugin read the same winning layer, so they agree either way. |
| `bridge.host`, `editor.*`, `pie.allowIgnoreBlueprintErrors` | **Local** | Paths, hosts and permission to skip a prompt are machine or personal facts. |

Machine **state** the tool writes for itself (installed-hook paths, feedback, dialog and playtest modes) is in no yml file. It lives in `~/.ue-mcp/state.json`; see [User-machine state](configuration.md#user-machine-state-ue-mcp).

## `ue-mcp:` block reference

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `version` | `1` | `1` | Schema version. A layer without it reads as 1. |
| `contentRoots` | `string[]` | init writes `["/Game/"]` | Content roots `asset(action="search")` searches when no `directory` is given, one root per page. Add plugin content roots here. |
| `disable` | `string[]` | `[]` | Tool categories not registered with the server. `feedback` here turns off the feedback tool. |
| `nativeTools` | `object` | `{ enabled: true }` | Native (UE 5.8 toolset registry) tools. `enabled: false` leaves only the `epic` gateway. `exclude` (`string[]`) names categories that do not get the wrapped tools; they stay reachable through `epic(call_tool)`. See [Native Epic tools](configuration.md#native-epic-58-tools). |
| `bridge` | `object` | port derived from the project path | `port` (integer `1-65535`) pins the editor bridge port; the client connects there and the plugin listens there. `host` (default `127.0.0.1`) is where the client reaches this project's bridge, the per-project form of `UE_MCP_HOST`. See [Pinning the port](configuration.md#pinning-the-port). |
| `editor` | `object` | the engine `EngineAssociation` names | Which engine this project uses. `path` is the editor binary and `buildToolPath` is `Build.bat` / `Build.sh`, the per-project forms of `UE_EDITOR_PATH` and `UE_BUILD_TOOL_PATH`. `launchArgs` is a list of extra arguments `start_editor` and `restart_editor` pass to the editor. See [Which Engine A Project Uses](configuration.md#which-engine-a-project-uses). |
| `env` | `string` | unset | Which `ue-mcp.{env}.yml` overlay this project merges. `UE_MCP_ENV` wins. |
| `http` | `object` | off | REST surface for the flow engine: `enabled` (bool), `port` (default `7723`), `host` (default `127.0.0.1`). Serves `GET /flows`, `GET /flows/<name>/plan`, `POST /flows/<name>/run` and the event stream `GET /flows/events` ([Live Observation](flows.md#http-and-live-events)). Every route needs the token, sent as `Authorization: Bearer <token>` or `X-UE-MCP-Token`; it is `UE_MCP_HTTP_TOKEN`, or a generated one printed to the server log. |
| `locking` | `object` | off | Per-asset locks for several agents on one editor. `enabled: true` wraps every mutating call in acquire and release, at the cost of two extra bridge calls; `ttlSeconds` (default 300) is how long a crashed agent's locks last. `asset(lock)`, `asset(unlock)` and `asset(list_locks)` work without it. |
| `pie` | `object` | `{ allowIgnoreBlueprintErrors: false }` | `allowIgnoreBlueprintErrors: true` pre-approves `editor(action="play_in_editor_ignore_blueprint_errors")`, which starts PIE with the unresolved-Blueprint-error prompt suppressed. Left off, each such launch waits for an MCP approval prompt. |
| `checkpoints` | `on`, `off`, bool or `object` | `off` | A shadow git history of every file written through ue-mcp, with per-file restore. The object form `{ enabled, largeFileBytes }` also sets the size (default 1 GB) above which a file is kept as a plain copy. See [Checkpoints](checkpoints.md). |
| `context` | `object` | `{ strategy: micro }` | `strategy: micro` puts everything behind one gateway tool (<!-- tax:micro -->~2.2k<!-- /tax --> tokens); `lean` keeps the category tools and action names with signatures on demand (<!-- tax:lean -->~20k<!-- /tax -->); `full` lists one signature per action inline (<!-- tax:full -->~55k<!-- /tax -->). `UE_MCP_CONTEXT_STRATEGY` wins. See [Context strategy](configuration.md#context-strategy-full-lean-micro). |
| `server` | `object` | `{ mode: daemon }` | `mode: daemon` relays to the project's daemon; `in-process` runs the server in the process your MCP client launched. `UE_MCP_SERVER_MODE` wins. `waitForEditorSeconds` (default 60, `0` fails at once) is how long a daemon call waits for an editor that went away; `openBrowserOnCrash` (default false) opens the client UI when the editor crashes. See [The daemon](configuration.md#the-daemon). |
| `pluginConfig` | `object` | unset | Per-plugin settings keyed by plugin slug. `groups` turns a plugin's flow groups on or off. Written by `ue-mcp plugin config`. See [Configuring a plugin](plugins-using.md#flow-groups). |

A `.ue-mcp.json` from very old versions is migrated into `ue-mcp.yml` and `~/.ue-mcp/state.json` on first load and then removed.

## See also

- [Configuration](configuration.md): MCP client setup, the daemon, the bridge, CLI commands
- [Flows](flows.md): the `tasks:` and `flows:` engine
- [Using plugins](plugins-using.md): installing the packages `plugins:` declares
