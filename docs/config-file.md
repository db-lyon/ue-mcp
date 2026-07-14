# The `ue-mcp.yml` config file

`ue-mcp.yml` sits next to your `.uproject` and declares your project's ue-mcp surface: which tool categories are on, the content roots to search, the optional HTTP flow server, the context-seeding strategy, and your custom `tasks:` and `flows:`. `npx ue-mcp init` creates and maintains it, but it is plain YAML with nothing the server treats as opaque, so hand-editing is expected.

It is also just one file in a deep-merged stack. This page is the reference for the file itself: its anatomy, how the layers merge, where each kind of setting belongs, and every key. For getting the server running (MCP client config, the C++ bridge, CLI subcommands), see [Configuration](configuration.md).

## Anatomy

Four top-level keys. Only the `ue-mcp:` block is required — `init` writes `version: 1`.

```yaml
ue-mcp:            # project-level server config (detailed below)
  version: 1
  contentRoots: [/Game/, /MyPlugin/]
  disable: [gas, networking]
  nativeTools: { enabled: true, exclude: [animation] }
  http: { enabled: false }
  context: { strategy: full }   # full (default) | lean | micro

tasks: {}          # custom flow-engine task definitions
flows: {}          # custom multi-step flows
plugins: []        # npm packages that inject new actions
```

| Top-level key | What it holds | Detailed in |
|---|---|---|
| `ue-mcp:` | Project/server config block | [block reference](#ue-mcp-block-reference) below |
| `tasks:` | Custom flow-engine task definitions | [Flows](flows.md) |
| `flows:` | Custom multi-step flows | [Flows](flows.md) |
| `plugins:` | npm packages injecting new actions into built-in categories | [Plugins](plugins-using.md) |

## Config layering

`ue-mcp.yml`, its overlays, and your user-global config are **deep-merged** into one effective config. Layers, lowest precedence first:

| Layer | File | Tracked? | For |
|-------|------|----------|-----|
| Built-in defaults | (shipped in the package) | — | The baseline every project starts from. |
| **User-global** | `~/.ue-mcp/config.yml` | No (per-user) | Your personal defaults for **every** project on this machine — e.g. `context.strategy`. Mirrors the project file's shape (a `ue-mcp:` block plus optional `tasks:` / `flows:`). Hand-edited. |
| **Project** | `<project>/ue-mcp.yml` | **Yes** | The shared project surface every collaborator gets. |
| Env overlay | `<project>/ue-mcp.{env}.yml` | Optional | Loaded only when `UE_MCP_ENV` is set — e.g. `ue-mcp.ci.yml` with `UE_MCP_ENV=ci`. |
| **Local** | `<project>/ue-mcp.local.yml` | No (git-ignore it) | Per-developer overrides for this one project that shouldn't be committed. |
| Env vars | `UE_MCP_CONTEXT_STRATEGY`, … | — | Highest precedence; win over every file, per session. |

**Deep merge semantics.** Nested objects merge key-by-key, so a later layer setting `context.strategy` does not wipe sibling keys. Arrays replace by default; put `__merge: append` on an override array to concatenate onto the base instead. A `null` in a later layer explicitly clears a value.

This is the same layering model as CumulusCI's `cumulusci.yml` — flowkit, the engine behind `tasks:` / `flows:`, is built on it, and the `ue-mcp:` block rides the same cascade.

## Where each setting belongs

Every key is valid in **every** layer — the schema is identical at each level, exactly like CumulusCI's `cumulusci.yml`. So "where does this go?" is a choice of *layer*, not a restriction the schema enforces. The layer you pick decides whether a value is shared with the team, personal to you, or scoped to one machine.

The rule of thumb:

| Setting | Usually lives in | Because |
|---|---|---|
| `version`, `contentRoots`, `http`, `tasks`, `flows`, `plugins` | **Project** `ue-mcp.yml` (tracked) | The whole team needs the same value. |
| `context.strategy` | **User-global** `~/.ue-mcp/config.yml`, or **local** `ue-mcp.local.yml` | A token-budget preference. An artist and a developer legitimately want different values. |
| `disable`, `nativeTools` | Either | A project *may* ship a shared default (this project has no GAS, so disable it for everyone), and any developer overrides it in their user or local layer. |

Nothing stops you putting `context.strategy` in the tracked `ue-mcp.yml` as a project default — a later user or local layer simply overrides it. What is deliberately kept out of every yml file is machine **state** the tool writes for itself (installed-hook paths, feedback mode); that lives in `~/.ue-mcp/state.json` (see the callout under the block reference).

## `ue-mcp:` block reference

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `version` | `1` | `1` | Schema version; required. Set automatically by init. |
| `contentRoots` | `string[]` | `["/Game/"]` | Content paths to search when using `asset(action="search")`. Add plugin content roots here if your project uses plugins with their own assets. |
| `disable` | `string[]` | `[]` | Tool categories to disable. Disabled categories are not registered with the MCP server, reducing context noise for the AI. Use `"feedback"` here to opt out of the feedback tool entirely. |
| `nativeTools` | `object` | `{ enabled: true }` | Native (Epic 5.8 ToolsetRegistry) tool surfacing. `enabled` (bool, default `true`) turns the whole feature on/off; when off, only the `epic` discovery gateway remains. `exclude` (`string[]`) names ue-mcp categories that should not be enriched with Epic tools (they stay reachable via `epic(call_tool)`). See [Native Epic tools](configuration.md#native-epic-5-8-tools). |
| `http` | `object` | `undefined` (HTTP server off) | Optional REST surface for the flow engine. Object with `enabled` (bool), `port` (default `7723`), `host` (default `127.0.0.1`). When `enabled: true`, the MCP server also serves `GET /flows`, `GET /flows/<name>/plan`, `POST /flows/<name>/run`, and the Server-Sent Events stream at `GET /flows/events` (live per-step lifecycle events; see [Live Observation](flows.md#live-observation-sse)) over HTTP so external tools can drive and observe flows without an MCP client. |
| `context` | `object` | `{ strategy: full }` | Context-seeding strategy. `strategy: full` (default) advertises every action inline; `lean` keeps action names but serves descriptions on demand (~half the seed); `micro` collapses everything behind one gateway tool (~1k tokens). See [Context strategy](configuration.md#context-strategy-full-lean-micro). |

!!! info "Config vs. machine state — two homes under `~/.ue-mcp/`"
    - `~/.ue-mcp/config.yml` — your per-user **config** layer (see [Config layering](#config-layering)). Hand-edited. Personal defaults applied across every project.
    - `~/.ue-mcp/state.json` — machine **state** the tool writes and you never hand-edit: absolute paths to the Claude Code settings files where the feedback hook was installed, plus your feedback-mode preference. Maintained by `npx ue-mcp init` / `npx ue-mcp uninstall-hooks` / `npx ue-mcp feedback mode`.

!!! tip "Migrating from older versions"
    - Pre-1.0.29 used `.ue-mcp.json` for the project config. On first load it is migrated into `ue-mcp.yml` (project fields) + `~/.ue-mcp/state.json` (machine state), then removed.
    - 1.0.29 briefly wrote machine state (`installedHooks`) into `ue-mcp.local.yml`. On first load that one key is moved to `~/.ue-mcp/state.json` and stripped from the file — **the rest of `ue-mcp.local.yml` is left in place**, because it is now a supported per-machine override layer (see [Config layering](#config-layering)). A file that held nothing but `installedHooks` is deleted once emptied.
    - Both migrations are automatic and idempotent; you don't need to do anything.

## See also

- [Configuration](configuration.md) - MCP client setup, the C++ bridge, CLI subcommands, plugin deployment
- [Flows](flows.md) - the `tasks:` and `flows:` engine in depth
- [Plugins](plugins-using.md) - installing and using plugins declared in `plugins:`
- [Context strategy](configuration.md#context-strategy-full-lean-micro) - the `context` key, full / lean / micro
- [Native Epic tools](configuration.md#native-epic-5-8-tools) - the `nativeTools` key
