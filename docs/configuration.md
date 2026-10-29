# Configuration

## MCP Client Configuration

`npx ue-mcp init` detects your MCP clients and writes the entry for each. To write it yourself, see [Manual configuration](getting-started.md#manual-configuration). Every client gets the same launch:

```json
{
  "mcpServers": {
    "ue-mcp": {
      "command": "npx",
      "args": ["-y", "--min-release-age=0", "ue-mcp@latest", "C:/path/to/MyGame.uproject"]
    }
  }
}
```

| Client | Config file |
|--------|-------------|
| Claude Code | `.mcp.json` in the project root, or `~/.claude/.mcp.json` for every project |
| Claude Desktop | `%APPDATA%/Claude/claude_desktop_config.json` |
| Cursor | `.cursor/mcp.json` in the project root |
| Codex | `~/.codex/config.toml` (TOML, with `cwd` set to the project directory) |

An entry written with `--pin` names `ue-mcp@<version>` instead of `@latest`, and `ue-mcp update` moves it with the bridge. The project path may be the `.uproject` or its directory. Use an absolute path, since clients launch the server from a working directory you do not control.

### Without a Project Path

Started without a `.uproject` argument, the server runs without a project. Call `project(action="set_project", projectPath="...")` to attach one.

### Several Editors From One Server

Pass more than one `.uproject` and each becomes an addressable **editor session** with its own bridge connection, port and port lockfile. Several projects always run in process, without the [daemon](#the-daemon).

```json
{
  "mcpServers": {
    "ue-mcp": {
      "command": "npx",
      "args": ["-y", "--min-release-age=0", "ue-mcp@latest", "C:/games/Alpha/Alpha.uproject", "C:/games/Beta/Beta.uproject"]
    }
  }
}
```

Sessions are keyed by resolved project root, so one project is one session however its path is spelled.

| Action | What it does |
|--------|--------------|
| `project(action="list_editors")` | Every session: name, project, bridge port, socket state, liveness, and which one untargeted calls use |
| `project(action="add_editor", projectPath="...")` | Register another project at runtime. `start: true` also launches its editor and waits until it is ready |
| `project(action="use_editor", editorTarget="Beta")` | Move the default target |
| `project(action="drop_editor", editorTarget="Beta")` | Forget a session and close its socket. The editor keeps running |

While more than one session is registered, every category tool, the `flow` tool and the HTTP `run` route accept an `editor` parameter naming the session, the project, or a `.uproject` path:

```
level(action="place_actor", editor="Beta", assetPath="/Game/Rock", location=[0,0,0])
editor(action="start_editor", editor="Beta")
flow(action="run", flowName="build_and_check", editor="Beta")
```

Calls with no `editor` run in the active session: the first project on the command line until `use_editor` moves it. The `editor` parameter is advertised only while a second session exists. Registering or dropping a session re-advertises the tool list, so a client that honours `tools/list_changed` can target an editor it registered at runtime.

**Each project needs its own port.** Derived ports differ per project, so this works by default. Pinning two projects to the same port (the same `bridge.port`, or a global `UE_MCP_PORT`) collapses their sessions onto one address. `list_editors` reports the clash under `portSharedWith`.

**Lifecycle actions act only on the editor you addressed.** See [Which editor lifecycle actions act on](#which-editor-lifecycle-actions-act-on). `drop_editor` only detaches; stopping an editor is always an explicit `editor(action="stop_editor")`.

Each project needs a bridge built from the current plugin source. A project whose bridge is missing or stale is still registered and startable, and `list_editors` reports it.

**The CLI takes a target too.** Every subcommand that acts on a project accepts `--editor <name-or-path>`, resolved as a session name first and a project path second:

```bash
npx ue-mcp deploy --editor Beta
npx ue-mcp build --editor Beta
npx ue-mcp context lean --editor Beta
```

The name is the one `list_editors` reports, read from the argv in your MCP client configs. Without the flag a subcommand takes the positional path, then the project in the current directory.

## Project Configuration (`ue-mcp.yml`)

Project config lives in `ue-mcp.yml` next to your `.uproject`, tracked in git so every collaborator gets the same surface. It is one layer in a deep-merged stack (user-global, project, env overlay, local, environment variables). The **[ue-mcp.yml Reference](config-file.md)** lists every key, the layers and where each setting belongs.

Per-user preferences (feedback, dialog and playtest modes) are not in `ue-mcp.yml`. They live in `~/.ue-mcp/state.json`, set by the commands below.

### Native Epic 5.8 tools

UE 5.8 ships an experimental AI Toolset Registry (the plugin behind Unreal's own MCP server). ue-mcp reaches it in process and adds each official tool as an action in the matching category: Epic's GAS tools appear in `gas`, Niagara in `niagara`, and so on. Toolsets with no natural home are reached through the `epic` gateway (`status`, `list_toolsets`, `describe_toolset`, `call_tool`). [Native Tools](native-tools.md) lists them.

- **On by default.** The `init` wizard has a page for it. The choice is written to `nativeTools` in `ue-mcp.yml`.
- **Needs UE 5.8 or later** with the `ToolsetRegistry` plugin (and the toolset plugins you want) enabled. Otherwise enrichment is skipped and `epic(status)` reports `available: false`.
- **Same surface with or without an editor.** The catalog comes from the live editor when connected, then a per-project cache, then a snapshot shipped in the package, so the wrapped tools appear on a cold start and match the [tool reference](tool-reference.md).

`nativeTools.enabled: false` turns it off (the `epic` gateway stays). Add a category to `nativeTools.exclude` to keep it on but leave that category unenriched.

### Dialog handling mode

A modal dialog blocks Unreal's game thread, so every action is refused until it is answered. Every route to the editor checks for one, including each step of a running flow. This mode decides what happens next. It is a per-machine preference: whether somebody is at the keyboard is not project policy.

| Mode | What happens to a blocking dialog |
|------|-----------------------------------|
| `interactive` | You get an elicitation form titled "Submission" with the dialog's buttons as choices, plus "leave it open". The button you pick is pressed and the blocked call then runs. A dialog with a checkbox per row (the "Save Content" prompt) shows those rows as one checkbox group titled "Assets", and what you tick is applied with the button. |
| `auto` | The refusal includes the `editor(action='respond_to_dialog')` call for each button. The agent picks one and makes that call. |
| `defer` | The refusal names the dialog and its buttons but not the calls. Answer it in the Unreal Editor window. |

`editor(respond_to_dialog)` is accepted only in `auto`; under `interactive` and `defer` it is refused, because the answer belongs to the person. `editor(list_dialogs)` works in every mode. A dialog matching a pattern armed with `editor(set_dialog_policy)` is answered at once under every mode. Nothing else presses a button on its own, and no policy is armed by default.

Precedence, highest first: `UE_MCP_DIALOG_MODE`, the project's `dialog.mode` in `~/.ue-mcp/state.json`, your user `dialog.mode` in the same file, then the default. **The default is `interactive` when your MCP client supports elicitation and `defer` when it does not. It is never `auto`.** An environment value that names no mode is ignored, and the result says so.

Set it with `npx ue-mcp dialog mode <interactive|auto|defer>`. Add `--editor <name>` to scope it to one project; `default` clears it.

What counts as blocking: the modal stack, then every top-level window that is modal or parented to another regular window. Menus, tooltips and notifications are not regular windows. Visibility is not consulted, because a window drawn behind the editor still blocks. Detection lives in the C++ bridge, so it changes only when the plugin is rebuilt.

Under `interactive` the dialog text is returned on one call and the form goes up on the next, so a client that collapses long form messages cannot hide the question. `dialogPhase` on the refusal says which call this is: `relay` or `asking`. A client known to render the whole message skips the relay. `UE_MCP_DIALOG_RELAY=off` turns the relay off for every client, and `=on` forces it on.

A blocked call waits a minute for your answer and then refuses, but the form stays up and the button you pick is still pressed. The screen is re-read first, so an answer to a dialog that was already closed or replaced presses nothing.

A daemon extension that runs actions in process answers the dialogs its own calls meet: `callAction` takes a `dialogPrompt` with the same form an MCP client is shown. Its answer may also be `delegate`, which hands that one dialog to the agent with the press calls, as `auto` would. An answer sooner than `UE_MCP_ELICIT_MIN_HUMAN_MS` is not taken as a person's. Every dialog is published on the daemon's event stream as `dialog.opened`, `dialog.answered`, `dialog.delegated` and `dialog.closed`.

### Playtest mode

Who starts playtest runs (`pie(action="script_run")` and `pie(action="script_replay")`): a person or an agent.

| Mode | What happens |
|---|---|
| `manual` | A person starts playtests. An MCP client's `script_run` and `script_replay` are refused before anything reaches the editor, with `success: false`, `refusedMethod`, and a `reason` naming the setting and the command that changes it. |
| `automatic` | An agent may start them. |
| unset | Each client decides. |

Set it with `npx ue-mcp playtest mode <manual|automatic>`. Add `--editor <name>` to scope it to one project; `clear` clears it. A project's setting wins over the user one. A daemon extension reads and sets it through `api.modes` (feature `modes.playtest`); its own `api.callAction` calls are not refused, so it applies its own rule.

### User-machine state (`~/.ue-mcp/`)

Files ue-mcp writes for itself. None belong in your project or in version control.

| Path | What |
|------|------|
| `~/.ue-mcp/config.yml` | Your user-global config layer, hand-edited. See [Config layering](config-file.md#config-layering). |
| `~/.ue-mcp/state.json` | Per project: the Claude Code `settings.json` files holding the feedback hook (`installedHooks`), the feedback, dialog and playtest modes, and the daemon extension version the project loads. Per user (`preferences`): the feedback, dialog and playtest modes. Written by `init`, `uninstall-hooks`, `feedback mode`, `dialog mode`, `playtest mode` and `extension`. `UE_MCP_USER_STATE` moves it. |
| `~/.ue-mcp/auth.json` | GitHub OAuth token for `feedback(submit)` with `author="user"`. Mode 600. Written by `npx ue-mcp auth`. |
| `~/.ue-mcp/registry.json` | Plugin registry publish token. Written by `npx ue-mcp login`. |
| `~/.ue-mcp/pending-feedback/<id>.json` | Submissions queued while feedback mode is `defer`. |
| `~/.ue-mcp/daemons/` | One discovery file (`<key>.json`, with the daemon's port and token) and one log (`<key>.log`) per project daemon. |
| `~/.ue-mcp/extensions/` | Installed daemon extensions, one folder per `<name>@<version>`. |

## Plugins

The `plugins:` array in `ue-mcp.yml` declares npm packages that inject actions into the built-in categories:

```yaml
plugins:
  - name: ue-mcp-perforce
  - name: some-other-plugin
    version: "0.2.x"        # optional npm semver range
```

`npx ue-mcp plugin install <name>` adds the entry and installs the package into the project's `node_modules/`. Plugins load when the server starts, so restart your MCP client after a change. `plugins(action="list")` and `plugins(action="describe", name="<package>")` show what loaded. [Using plugins](plugins-using.md) covers validation, ordering, host UE plugin dependencies and flow groups; [Plugins](plugins.md) is the author contract.

## Context strategy (full, lean, micro)

Everything the server sends at session start (the `initialize` instructions plus the whole `tools/list` payload of names, descriptions and parameter schemas) is the "context tax". Three strategies trade that seed cost against discovery round trips.

| Strategy | Seed | What's advertised | Cost to use |
|----------|------|-------------------|-------------|
| **`micro`** (default) | <!-- tax:micro -->~2.2k<!-- /tax --> tokens | a single `tools` gateway (`search`, `list_categories`, `describe`, `call`) fronting every category | discovery for everything |
| **`lean`** | <!-- tax:lean -->~20k<!-- /tax --> tokens | the <!-- count:tools -->30<!-- /count --> category tools with their `action` enums and a one-line summary each; a `catalog` tool (`search` / `describe` / `list_categories`) and a per-category `describe` action serve the signatures | about one round trip to learn a category |
| **`full`** | <!-- tax:full -->~55k<!-- /tax --> tokens | the <!-- count:tools -->30<!-- /count --> category tools, each with one signature line per action inline | no discovery calls |

On demand, micro's `describe` of the largest category (a page of signatures) costs <!-- tax:describe -->~1.5k<!-- /tax --> tokens and a default `search` <!-- tax:search -->~520<!-- /tax -->.

The figures include the <!-- count:nativeToolActions -->830<!-- /count --> wrapped Epic tools, which are declared in the package, and are measured hermetically (throwaway project, no editor) by `npm run context-tax`, which stamps them into these docs. Set `ANTHROPIC_API_KEY` for exact token counts instead of the chars/4 estimate. The `context-tax` release gate (`node scripts/check-release-gates.mjs context-tax`) fails the build when a seed exceeds its budget: micro <!-- tax:microBudget -->~5.0k<!-- /tax -->, lean <!-- tax:leanBudget -->~30k<!-- /tax -->, full <!-- tax:fullBudget -->~60k<!-- /tax -->, a category describe <!-- tax:describeBudget -->~2.0k<!-- /tax --> and a search <!-- tax:searchBudget -->~1.0k<!-- /tax -->.

### Calls, signatures and validation

Every category tool takes `action` and `args`: `level(action="place_actor", args={actorClass: "PointLight", location: {x: 0, y: 0, z: 100}})`. Parameters sent flat beside `action` are also accepted. A call is read as flat when anything other than `args` and the routing parameters (`timeoutMs`, `select`, `omit`, `editor`, `toEditor`) is at the top level, so a category's own `args` parameter (`editor(invoke_function)`'s function arguments) keeps its meaning.

Actions are described by one-line signatures such as `set_property(assetPath|path, propertyName, value:*, save?:b)`. The server instructions explain the notation once: `?` optional, `a|b` an alias, `one(a; b+c)` exactly one group, `any(a; b)` at least one, and a type code after a colon (none means a string). Signatures come from each action's recorded C++ spec or Epic input schema. `project(action="describe_action")` (or `describe` with a `method` in lean and micro) returns one action's full schema: every parameter's description, nested fields, allowed values and defaults.

Every call, on every strategy, is validated against the category's full parameter schema, and a wrong type is refused with an `Input validation error`.

- **micro** works like the native MCP toolset gateway: `tools(action="search", query="rotate clockwise")`, then `tools(action="describe", category="level", method="nudge_component")`, then `tools(action="call", category="level", method="nudge_component", args={ ... })`. Omit `method` from describe for a page of the category's signatures, and pass `offset` (the `nextOffset` it returns) for the next page.
- **lean** keeps the category tools and action names visible, so the model can often call directly. Unknown actions are refused with the closest names.
- **full** suits an agent that should see every signature up front when the seed is affordable.

A smaller seed is not automatically a cheaper session: count the discovery round trips too. All three carry the same spatial interpretation and verification guidance.

Set the strategy with the CLI, which writes `ue-mcp.yml`:

```
npx ue-mcp context micro     # one gateway tool fronts everything (default)
npx ue-mcp context lean      # names visible, signatures on demand
npx ue-mcp context full      # every signature inline
npx ue-mcp context           # show the current strategy
```

The `init` wizard has the same choice, or set `context.strategy` in `ue-mcp.yml`. `UE_MCP_CONTEXT_STRATEGY` wins over the file for one launch; anything other than `full` or `lean` resolves to `micro`. Restart your MCP client (`/mcp` in Claude Code) after changing it.

## The daemon

With one project, `npx ue-mcp <uproject>` is a stdio shim that relays to that project's daemon: one background process per project that holds the editor connection. Your MCP client stays connected through an editor restart, a crash or a rebuild, and calls made while the editor is down get the offline actions or wait for it to return. The daemon starts on first use, reattaches within a second of the editor coming back, and exits after 30 minutes with no editor, client or UI attached. Its log is `~/.ue-mcp/daemons/<key>.log`.

Set `server.mode: in-process` in `ue-mcp.yml`, or `UE_MCP_SERVER_MODE=in-process` for one launch, to run the server inside the process your client launched instead. Several projects, or none, always run in process. If the daemon cannot start, the server runs in process and logs why.

A client entry launching `ue-mcp@latest` against a project whose binary bridge comes from a newer release (a beta) starts the daemon at the bridge's version, so the server and bridge stay matched.

| Command | What it does |
|---|---|
| `npx ue-mcp daemon status` | The daemon's pid, port and version, its editors, and why each one last disconnected (`closed`, `crashed`, `restarting`, `rebuild` or `unknown`). `--json` for a machine. |
| `npx ue-mcp daemon start` / `stop` | Start one for the project, or ask it to exit. |
| `npx ue-mcp daemon run` | Run the daemon in the foreground. |
| `npx ue-mcp daemon open` | Print the URL of the client UI it serves, if one is installed. `--browser` opens it. |
| `npx ue-mcp extension install <source> [--sha256 <hex>] [project]` | Install a daemon extension into `~/.ue-mcp/extensions/<name>@<version>/`, record the project at that version, and restart the project's daemon to load it. The source is a zip, a URL with its SHA-256, or a release manifest (`.json` with `url` and `sha256`). Versions sit side by side: each project loads the version it records. `extension use <name> <version> [project]` moves a project to an installed version, `extension list [project]` shows every installed version and the one the project records, and `extension remove <name>` removes every version. A version no project records is removed on the next install. |

The daemon listens on 127.0.0.1 only, and every request needs the token in `~/.ue-mcp/daemons/<key>.json`, which only your user can read.

```yaml
ue-mcp:
  server:
    waitForEditorSeconds: 60     # a call waits this long for an editor that went away; 0 fails at once
    openBrowserOnCrash: false    # open the client UI in your browser when the editor crashes
```

A call that still cannot reach the editor says why it last went away. The daemon's event stream (`GET /v1/events`) carries `call.started` and `call.finished` for every MCP call from every client, named by the client, with the category, action, duration and outcome. It also carries `dialog.opened`, `dialog.answered`, `dialog.delegated` and `dialog.closed`, and the progress of every PIE test script run: `script.run_started`, `script.step_started`, `script.step_passed`, `script.step_failed`, `script.verdict_requested`, `script.verdict` and `script.run_finished`. [Test scripts](pie-scripts.md#daemon-events) lists their payloads.

The daemon also manages the project's plugins over HTTP. `GET /v1/plugins` lists the plugins `ue-mcp.yml` declares with their version, status and categories, and `GET /v1/plugins/catalog` lists the registry's published plugins. `POST /v1/plugins/install` (`{ name, version? }`), `POST /v1/plugins/update` (`{ name? }`) and `POST /v1/plugins/uninstall` (`{ name }`) run the same `ue-mcp plugin` commands, one at a time, and publish `plugins.changed` on the event stream. A change takes effect when the server restarts.

### What a client can read from the daemon

These routes serve a client that builds its own agent context, such as an in-editor panel. Each is also on the extension API, and `/v1/health` lists its feature name under `features` (an extension reads `api.features`).

| Route | Extension API | Feature | Answers |
|---|---|---|---|
| `GET /v1/skills` | `api.skills.list()` | `skills` | Every skill a client of this project can load |
| `GET /v1/context/projection?strategy=&categories=` | `api.context.project(opts)`, `api.context.call(tool, input, opts?)` | `context.projection` | What a context strategy and category scope advertise, and a call against that surface |
| `GET /v1/actions?categories=` | `api.actions.effects(categories?)` | `actions.effects` | Each action's effect and whether it is destructive |

**Skills.** `GET /v1/skills` returns `{ skills }`: ue-mcp's packaged skills and every skill directory under the project's `.claude/skills` and `.agents/skills`. Each entry has its `id` (the directory name; a plugin's skills are installed as `<namespace>-<skill>`), `name` and `description` from the frontmatter, the absolute `dir`, the other `files` in it, the `body` without frontmatter, the parsed `frontmatter`, `tokens` (SKILL.md characters / 4, rounded up), `location` (`packaged` or `project`) with the project `folder`, and `owner`:

- `{ kind: "ue-mcp", version }` for a packaged skill, or a project copy `.ue-mcp/skills.json` records as ue-mcp's own.
- `{ kind: "plugin", package, version }` for a copy recorded under a plugin package. `version` is the installed package's, or `null` when it cannot be found.
- `{ kind: "project" }` for anything else, and for everything in `.agents/skills`.

One id is listed once. A project copy wins over the packaged one, and `.claude/skills` over `.agents/skills`; the winner lists what it hides in `shadows`. A SKILL.md whose frontmatter does not parse is still listed, with the reason in `problems`.

**Context projection.** `GET /v1/context/projection?strategy=micro|lean|full&categories=a,b` returns what the server would hand a client under that [context strategy](#context-strategy-full-lean-micro), narrowed to those categories: `strategy`, `categories` (the scope after resolving; omitted means every enabled category), `instructions` (the initialize text, quoting the scope's counts and categories), `tools` (each tool's `name`, `description`, `inputSchema` as `tools/list` sends it, and `category`, null for micro's `tools` gateway, lean's `catalog` and `flow`), and `seedTokens`, the characters of the instructions and tools JSON divided by 4 (`estimator: "chars/4"`). Over every category it is exactly what the server sends for that strategy. Lean's `catalog` and micro's gateway search and describe only the scope. `strategy` defaults to the server's own; an unknown strategy or category answers 400.

`api.context.call(tool, input, { strategy?, categories?, dialogPrompt?, dialogWaitMs?, elicit? })` runs one call to a projected tool as the MCP server would: a category, its lean `describe`, `catalog`, micro's `tools` gateway, or `flow`, through the same dispatch as `api.callAction`, so dialogs, forms, effect gates and checkpoints apply the same way. A call to a category outside the scope is refused with an error result and nothing runs.

**Action effects.** `GET /v1/actions?categories=a,b` returns `{ actions }`, one entry per enabled action: `category`, `action`, `effect` (`read`, `mutate` or `unknown`) and `effectSource` (`declared` on the action, `inferred` from the name for a plugin action whose manifest gave none, `undeclared` when nothing claims it), read from the graph the server dispatches, plugin actions included. `destructive` says the action destroys or overwrites something it cannot give back. An action can declare it; otherwise a `mutate` or `unknown` action is destructive when its name leads with `delete`, `remove`, `clear`, `discard`, `revert`, `reset`, `purge`, `wipe`, `destroy`, `drop`, `overwrite`, `replace`, `restore` or `uninstall` (after an `epic_`, `bulk_` or `batch_` prefix), and a read never is. `destructiveSource` says which: `declared`, `lexicon` or `none`. An unknown category answers 400.

### Context measurements

A client that measures which context strategy costs least writes the result in one shared format, so measurements from different clients and models can be compared. `ue-mcp/daemon` exports the `ContextMeasurement` type, `parseContextMeasurement(value)` (returns `{ ok: true, value }` or `{ ok: false, errors }`, every problem named by its path) and `scopeBucket(categoryCount)`. The package ships the same shape as a JSON Schema at `ue-mcp/schemas/context-measurement.schema.json`.

```json
{
  "version": 1,
  "measuredAt": "2026-10-25T12:30:00Z",
  "ueMcpVersion": "1.4.11",
  "client": { "name": "my-client", "version": "0.2.0" },
  "runs": [
    {
      "model": { "vendor": "anthropic", "model": "claude-opus-5-5" },
      "scope": "1-3",
      "strategy": "micro",
      "seedTokens": 2207,
      "tasks": [
        { "id": "place-light", "totalTokens": 18000, "modelCalls": 6, "discoveryCalls": 2, "passed": true }
      ]
    }
  ]
}
```

One run is one model, one strategy and one scope over a set of tasks. `scope` buckets how many categories the context was narrowed to: `1-3`, `4-8` or `9+`, as `scopeBucket` computes it. `seedTokens` is the projection's estimate for that strategy and scope (`GET /v1/context/projection`). Per task, `totalTokens` counts everything the task spent, `modelCalls` the model requests, `discoveryCalls` the tool calls that only searched or described the surface, and `passed` whether it did what it was asked. A task keeps its `id` across runs. Keys outside the format are refused.

### Updates through the daemon

A minute after it starts, and every six hours, the daemon looks for a newer release on the install's channel (stable, or the prerelease channel you are on) and publishes `update.available` on its event stream. `GET /v1/update` reports the target version and, for a binary install, whether that release ships binaries for the project's engine and platform. `POST /v1/update/apply` (optional body `{ "version": "x.y.z" }`) installs it:

| Install | Editor down | Editor running |
|---|---|---|
| Binary, Windows | Swapped in now | Each loaded file renamed aside and replaced; the new bridge loads on the next start, and the renamed files are removed once the editor exits |
| Binary, Mac and Linux | Swapped in now | Staged beside the plugin and swapped in when the editor exits |
| Source | Deployed and built now; a failed build fails the update | Deployed, with the build recorded as owed; the next start or restart through ue-mcp builds before the editor opens |

Unreal rebuilds a module on start only when its DLL is missing, never when its source is newer, so ue-mcp always builds a source update itself. An owed build shows as `plugin.pendingBuild` in `GET /v1/install`, and `POST /v1/editor/rebuild` stops the editor, builds, and starts it again only when the build passed; a failed step carries `reason`. Running `ue-mcp update --apply-only` again with the editor down also finishes an owed build.

Each step is reported as `update.staged`, `update.building`, `update.applied` or `update.failed`. A downloaded archive is installed only when the release manifest lists it with a matching SHA-256, and source is never written over a binary install. `UE_MCP_DISABLE_UPDATE_CHECK=1` turns the periodic check off, here and in the server.

## Bridge Connection

The C++ plugin listens on a **per-project WebSocket port** derived from a SHA-1 of the project root path, in the range `49152-65535`. Two checkouts of a project, or several projects, on one machine each get a stable port of their own. The client and the plugin compute the same value independently. If the port is taken, the plugin tries up to 50 ports above it, and publishes the port it bound to `<project>/Saved/UE_MCP_Bridge/port.json`, which the client reads first. The fixed port `9877` is used only when no project is loaded.

The client connects on startup, retries every 15 seconds while disconnected, and reconnects at once when an editor publishes its `port.json`.

On Windows the listening socket is claimed with `SO_EXCLUSIVEADDRUSE`, so a second editor of the same project cannot bind the same port. It moves upward and publishes where it landed.

### Pinning the port

Pin a port with `bridge.port` in `ue-mcp.yml`:

```yaml
ue-mcp:
  bridge:
    port: 50123
```

Both halves honour it, with the same precedence, highest first:

| Rank | Source | Notes |
|------|--------|-------|
| 1 | `-MCPPort=NNNN` on the editor command line, or an explicit port argument to the client | Per launch |
| 2 | `UE_MCP_PORT` | Per environment. Applies to every project the shell starts, so it is the wrong tool for pinning one project |
| 3 | `bridge.port` in the config layers | Per project. The client also writes the port it resolved to `<project>/Saved/UE_MCP_Bridge/requested.json`, and the plugin reads that before its own config read, so an editor launched without the client's environment still binds the client's port |
| 4 | The port derived from the project root path | The default |

The plugin reads `bridge.port` from the same layered files as the client: `~/.ue-mcp/config.yml`, `<project>/ue-mcp.yml`, `<project>/ue-mcp.{env}.yml` when `UE_MCP_ENV` is set, then `<project>/ue-mcp.local.yml`. See [Config layering](config-file.md#config-layering).

- **A pin is a request.** If something already holds the port, the plugin moves upward, logs a warning naming the port asked for and the one taken, and publishes the port it bound. The client follows `port.json`.
- **A value the plugin cannot use is announced, not applied.** A `bridge.port` that is not a whole number in `1-65535` is logged as a warning and the derived port is used. The plugin reads this key with a small reader rather than a YAML parser: write it as plain nested keys, as above. A flow mapping (`bridge: { port: 50123 }`) or an anchor is not read by the plugin, and the editor log says so.

### Which editor lifecycle actions act on

`editor(start_editor)`, `editor(stop_editor)` and `editor(restart_editor)` act on a process, so they resolve their target more strictly than ordinary calls:

- **The port comes from the lockfile only.** The plugin writes `<project>/Saved/UE_MCP_Bridge/port.json` whatever port it binds and removes it on exit. When it is absent, the action fails and names the path it checked instead of probing a port another project's editor could answer on.
- **The process check is scoped by `.uproject`.** The question is whether an editor holding this project is running. Another project's editor or a headless editor does not block a launch.
- **Stopping verifies before it acts.** A lockfile outlives a crash, so `stop_editor` confirms the PID it names is still an editor for this project before it sends the quit request.
- **Stopping returns once the process has exited.** `stop_editor` waits for the PID to leave the process table, and fails naming the PID if it outlives the wait. `start_editor` likewise waits out an editor whose log is already closed instead of calling it running.
- **Stopping clears a crash reporter a crashed editor left behind (Windows).** `CrashReportClientEditor.exe` keeps the project's module DLLs open, so the next build fails with `LNK1104`. Once no editor of this project is running, `stop_editor` ends each crash reporter that belongs to this project and lists it under `crashReporters` with its pid and why it was judged this project's. A crash reporter watching a live editor, or tied to another project, is never touched.
- **No project loaded means no lifecycle action.**

If an editor is killed rather than closed, its lockfile stays behind and `stop_editor` reports the stale PID. Delete `<project>/Saved/UE_MCP_Bridge/port.json` to clear it.

### Bridge state files

The plugin keeps these under `<project>/Saved/UE_MCP_Bridge/`, each published by rename so a reader never sees a half-written file.

| File | Written when | Contents |
|------|--------------|----------|
| `port.json` | The bridge is listening | `port`, `pid`, `instanceId`, `startedAt`, `status`, `protocolVersion`, `handlerApiVersion` |
| `bridge-error.json` | The editor came up but the bridge could not bind | `status: "bind-failed"`, the port range tried, the socket error, `pid`, `instanceId` |
| `status.json` | Four times a second from editor startup | The engine state snapshot `editor(get_engine_state)` reports, written by a thread that runs while the game thread is blocked |
| `requested.json` | The client resolved a pinned port | The port the plugin should bind. See [Pinning the port](#pinning-the-port) |

`instanceId` identifies the server object that wrote a record, and only that instance removes it, so an editor that failed to start cannot delete the record of one that is running. `bridge-error.json` is what tells "no editor" from "editor running, bridge dead": when a connection fails and this record names a live process, the client quotes its detail in the error.

### Connection state

`project(action="get_status")` reports `editorConnected` and `editorTarget`: the `.uproject` the connection belongs to, the port, and how the port was chosen (`lockfile`, `config`, `derived`, `env`, `explicit`). With no editor connected, filesystem tools still work (INI parsing, C++ headers, asset listing) and bridge actions refuse with the reason.

### Who may connect

The bridge binds loopback only and refuses every upgrade that carries an `Origin` header. Browsers always send one on a WebSocket upgrade and cannot suppress or forge it, so no web page can reach the bridge. Native clients (the npm client, curl, editor tooling) omit the header.

### Switching Projects

`project(action="set_project", projectPath="...")` moves path resolution and the editor connection together. The socket to the previous project's editor is dropped before the new project loads, so no action runs in the project you left.

The new project's port is chosen from that project alone: its `Saved/UE_MCP_Bridge/port.json` first, then its own `bridge.port`, then the port derived from its root.

A port pinned with `UE_MCP_PORT` (or an explicit port argument) was chosen for the project the server started on. If the new project has published no lockfile, the switch still completes and the connection is refused with an explanation. Start the new project's editor, which publishes the lockfile, or clear the pin.

## Plugin Deployment

`npx ue-mcp init` and `npx ue-mcp deploy` install the bridge into `<Project>/Plugins/UE_MCP_Bridge/`, add `UE_MCP_Bridge` to the `.uproject`, and enable `PythonScriptPlugin` (needed for `execute_python`). `deploy` copies source and also refreshes installed skills; it never writes source over a binary install. The server itself only checks the plugin at startup: it enables the two `.uproject` entries for a plugin that is present, and logs a note telling you to run `init` when it is missing or `deploy` (`update` for a binary install) when its version differs.

The plugin is editor-only. Its `.uplugin` depends on these engine plugins, which Unreal enables with it: `PythonScriptPlugin`, `DataValidation`, `EditorScriptingUtilities`, `EnhancedInput`, `GameplayAbilities`, `GameplayTagsEditor`, `Niagara`, `PCG`, `IKRig`, `ControlRig`, `PoseSearch`, `Chooser`, `PropertyBindingUtils`, `Metasound`, `Synthesis`, `StateTree`, and `Fab` (optional).

## CLI Subcommands

Run `npx ue-mcp <command> --help` for a command's flags.

| Command | Description |
|---------|-------------|
| `npx ue-mcp init` | Setup wizard. Installs the bridge, writes MCP client configs and `ue-mcp.yml`, optionally installs Claude Code skills and the feedback hook. `--yes` / `--json` run it from flags. See [Getting Started](getting-started.md#1-installation). |
| `npx ue-mcp update` | Update the package, deploy the plugin source and rebuild the editor target. Skips the build while the editor runs and records it as owed. `--no-build` stops after the deploy, `--no-deploy` updates the package only. A binary install gets verified binaries instead. `--apply-only` installs this package's own bridge into the project and stops. |
| `npx ue-mcp doctor [--fix]` | Report every version source and install problem. `--fix` removes client entries that point at a project which no longer exists or has no bridge, and adds `--min-release-age=0` to npx launches. |
| `npx ue-mcp status [--json]` | The project's install and editor state, read-only. |
| `npx ue-mcp deploy` | Copy the bridge plugin source into the project and refresh installed skills. |
| `npx ue-mcp build` | Build the project's editor target with UnrealBuildTool. Stop the editor first. |
| `npx ue-mcp context [full\|lean\|micro]` | Read or set the [context strategy](#context-strategy-full-lean-micro) in `ue-mcp.yml`. |
| `npx ue-mcp daemon <run\|start\|stop\|status\|open>` | Inspect or control the project's [daemon](#the-daemon). |
| `npx ue-mcp extension <install\|use\|remove\|list>` | Manage [daemon extensions](#the-daemon). |
| `npx ue-mcp dialog mode [<mode>]` | Read or set the [dialog handling mode](#dialog-handling-mode) (`interactive`, `auto`, `defer`; `default` clears). |
| `npx ue-mcp playtest mode [<mode>]` | Read or set the [playtest mode](#playtest-mode) (`manual`, `automatic`; `clear` clears). |
| `npx ue-mcp feedback mode [<mode>]` | Read or set your feedback approval mode (`interactive`, `auto-approve`, `defer`). See [Feedback modes](feedback.md#feedback-modes). |
| `npx ue-mcp feedback list \| show \| approve \| discard \| review` | Manage submissions queued under `defer`. `review` (experimental) walks the queue one entry at a time. See [Reviewing deferred submissions](feedback.md#reviewing-deferred-submissions). |
| `npx ue-mcp auth` | Run the GitHub device flow so `feedback(submit)` can file issues as your account. |
| `npx ue-mcp login` / `logout` | Cache or forget the registry token `ue-mcp plugin publish` uses. |
| `npx ue-mcp uninstall-hooks` | Remove the feedback hook from every Claude Code settings file recorded for this project. |
| `npx ue-mcp resolve <issue>` | Fetch a feedback issue, branch, hand it to Claude Code to implement, open a PR. See [Development](development.md#resolving-an-issue-with-claude-code). |
| `npx ue-mcp plugin <install\|uninstall\|update\|list\|config\|create\|publish\|check-skills\|record-specs>` | Install, configure, create and publish plugins. See [Using plugins](plugins-using.md) and [Plugins](plugins.md). |
| `npx ue-mcp version` | Print the installed version. |

Every command that acts on a project accepts `--editor <name-or-path>`. See [Several Editors From One Server](#several-editors-from-one-server).

## Editor Lifecycle

| Action | Description |
|---------|-------------|
| `editor(action="start_editor")` | Launch the editor on the current project and wait until it is ready |
| `editor(action="stop_editor")` | Close the editor and wait for the process to exit |
| `editor(action="restart_editor")` | Stop and relaunch |
| `editor(action="build_project")` | Build the project C++ with UnrealBuildTool |
| `project(action="build")` | The same build, with `configuration`, `platform` and `clean` parameters |

A start or restart through ue-mcp first builds a source bridge that owes a build after an update. Both builds run UnrealBuildTool as a separate process, so they work with the editor stopped, which a full rebuild needs: UnrealBuildTool cannot link while an editor holds the module DLLs.

### Build parallelism

UnrealBuildTool compiles one file per physical core by default, and each compiler process maps Unreal's precompiled header, which costs several GB. On a machine with fewer GB than cores, the compiler fails with `C3859` or `C1076` several minutes in, with a message about the paging file.

ue-mcp computes a safe number from the machine's memory and core count and passes it to every build (`ue-mcp build`, `ue-mcp update`, `editor(build_project)`). The cap applies only when it is below what UnrealBuildTool would choose. Set `UE_MCP_MAX_PARALLEL_ACTIONS` to a positive integer to override it. A build that runs out of memory anyway says what happened and what to change.

## Which Engine A Project Uses

The engine is resolved once, and the same answer drives the build tool, the editor launch, engine plugin discovery and the engine-source readers (`project(find_engine_symbol)`, `read_engine_header`, `search_engine_cpp`, `list_engine_modules`). Order, most specific first:

1. `UE_MCP_TEST_ENGINE_ROOT`: an engine root pinned for the whole process.
2. `UE_BUILD_TOOL_PATH`: a `Build.bat` / `Build.sh` pinned for the process.
3. `editor.buildToolPath` in the project's `ue-mcp.yml`.
4. `UE_EDITOR_PATH`, then `editor.path`: an editor binary names its own tree.
5. `EngineAssociation` read as a path, absolute or relative to the project.
6. An engine tree beside or above the project: the first directory from the project upward that holds `Engine/Build/BatchFiles/`. This is the layout a Perforce stream produces, with `<stream>/Engine` next to `<stream>/MyProject/MyProject.uproject`.
7. `EngineAssociation` as a registered build (a GUID, looked up in `HKCU\Software\Epic Games\Unreal Engine\Builds`) or a version string such as `5.8`, looked up as a launcher install.
8. The engine the project's own log says it was last opened with.
9. The default launcher install locations.

`UE_MCP_PROTECTED_ENGINE_ROOTS` outranks all of it. An engine equal to or under a protected root is refused, including one named by an explicit pin. Entries are separated by the platform path-list delimiter (`;` on Windows, `:` on macOS and Linux) and must be absolute.

When nothing resolves, the error lists every path probed and where each came from.

## Environment variables

Each wins over the matching config key. The full list, with the ones tests and services use, is in `src/core/env.ts`.

| Variable | Effect |
|---|---|
| `UE_MCP_PORT` | Bridge port for every session, over the derived port and `bridge.port` |
| `UE_MCP_HOST` | Host every bridge is reached on (default `127.0.0.1`) |
| `UE_MCP_BRIDGE_TIMEOUT_MS` | Floor under every bridge call's timeout, in milliseconds |
| `UE_MCP_STRICT_PARAMS` | `1` refuses a call with a parameter the action does not forward, instead of warning |
| `UE_MCP_ENV` | Selects the `ue-mcp.<env>.yml` overlay |
| `UE_MCP_GLOBAL_CONFIG` | Path of the user-global config layer |
| `UE_MCP_CONTEXT_STRATEGY` | `full`, `lean` or `micro` |
| `UE_MCP_SERVER_MODE` | `daemon` or `in-process` |
| `UE_MCP_LOG_LEVEL` | `debug`, `info` (default), `warn` or `error` |
| `UE_MCP_USER_STATE` | Path of `state.json` |
| `UE_EDITOR_PATH`, `UE_BUILD_TOOL_PATH` | Editor binary and build script, for every project |
| `UE_MCP_TEST_ENGINE_ROOT`, `UE_MCP_PROTECTED_ENGINE_ROOTS` | Engine pin and deny list (see above) |
| `UE_MCP_MAX_PARALLEL_ACTIONS` | Cap on UnrealBuildTool's parallel compile actions |
| `UE_MCP_DIALOG_MODE`, `UE_MCP_DIALOG_RELAY` | Dialog mode, and `on` / `off` for the dialog text relay |
| `UE_MCP_FEEDBACK_MODE` | `interactive`, `auto-approve` or `defer` |
| `UE_MCP_ELICIT_MIN_HUMAN_MS` | Fastest a person can answer an approval form; `0` disables the check |
| `UE_MCP_HTTP_TOKEN` | Token for the flow HTTP server, over a generated one |
| `UE_MCP_BINARIES` | Where prebuilt bridge binaries come from: a plugin directory, a `.zip`, a directory of archives, or a URL base |
| `UE_MCP_DISABLE_UPDATE_CHECK` | `1` skips the check for a newer release |
| `UE_MCP_DAEMON_IDLE_MS` | How long an idle daemon waits before exiting |
| `UE_MCP_DAEMON_DIR`, `UE_MCP_EXTENSIONS_DIR`, `UE_MCP_PENDING_DIR`, `UE_MCP_AUTH_DIR` | Move the daemon, extension, deferred-feedback and token directories |
| `UE_MCP_PUBLISH_TOKEN` | Registry publish token for CI, over the one `ue-mcp login` cached |
