# Getting Started

<Badge variant="time">⏱ ~5 minutes</Badge>

UE-MCP lets an AI assistant work in the Unreal Editor: place actors, write Blueprints, author materials, configure Niagara, build lighting, generate PCG systems and more.

## Prerequisites

1. Install [**Unreal Engine 5.4 to 5.8**](https://www.unrealengine.com/en-US/download "Free download via the Epic Games Launcher.")
2. Install [**Node.js 20 or newer**](https://nodejs.org/ "JavaScript runtime that UE-MCP needs to run.")
3. Install an MCP-capable AI client (e.g. [**Claude Code**](https://docs.anthropic.com/en/docs/claude-code "Anthropic's official CLI agent."))

## 1. Installation

1. If your Unreal Editor is open, close it.
2. `cd` into your project folder (the one with the `.uproject`).
3. Run the wizard:

   ```bash
   npx ue-mcp init
   ```

The wizard:

1. Finds the `.uproject` in the current directory (or asks for its path) and prints the bridge port this project will use.
2. Asks which **tool categories** to enable (`level`, `blueprint`, `material`, `niagara`, etc.). All are checked on a fresh install. On a re-run, categories in `ue-mcp.yml`'s `disable` list stay unchecked.
3. Asks whether to wrap **native Unreal tools** (UE 5.8's toolset registry) and which categories get them. See [Native Epic tools](configuration.md#native-epic-58-tools).
4. Asks for the **context strategy**: `micro` (default), `lean` or `full`. See [Context strategy](configuration.md#context-strategy-full-lean-micro).
5. Lists the MCP clients it detects (Claude Code project and global, Claude Desktop, Cursor, Codex) and writes the config for each one you keep checked. Project-scoped clients start checked; global and Desktop configs start unchecked because they affect every project on the machine.
6. Asks about **agent behavior**. Everything starts unchecked on a fresh install:
    - Enable the `feedback(submit)` tool.
    - With Claude Code selected: install a PostToolUse hook that nudges the agent to offer feedback after `execute_python`, and install the bundled Claude Code skills into `.claude/skills/`.
7. Installs the C++ bridge plugin into `<YourProject>/Plugins/UE_MCP_Bridge/`. With a C++ toolchain on the machine it copies the source; without one it installs prebuilt binaries for your engine and platform. A plugin that is already there is adopted as it is, and source is never written over a binary install. `Plugins/UE_MCP_Bridge/.ue-mcp-install.json` records which kind it is.
8. Enables `UE_MCP_Bridge` and `PythonScriptPlugin` in your `.uproject`, plus `Niagara`, `PCG`, `EnhancedInput` or `GameplayAbilities` for the categories you kept.
9. Creates `ue-mcp.yml` if it is missing, writes your choices into it, and adds `ue-mcp.local.yml` to `.gitignore`.
10. With the feedback hook on, offers the **GitHub device flow** so `feedback(submit)` can file issues as your GitHub account. The token is cached at `~/.ue-mcp/auth.json` (mode 600). Skip it and run `npx ue-mcp auth` later, or call `feedback(submit)` with `author="bot"`.

It prints one line per file it changed. Running `init` again is safe: it adopts what is installed and changes only what differs.

### Without prompts

`init --yes` makes the same decisions from flags, and `--json` prints one result document on stdout:

```bash
npx ue-mcp init --yes --json --project=C:/Games/MyGame/MyGame.uproject --clients=claude-code,cursor
```

| Flag | Meaning |
|---|---|
| `--project=<path>` | The `.uproject` or its directory. Default: the one in the current directory. |
| `--clients=<ids>` | `claude-code`, `claude-code-global`, `claude-desktop`, `cursor`, `codex`. Unset keeps the clients already configured, or on a fresh project every detected project-scoped one. |
| `--install=auto\|source\|binary` | `auto` (default) adopts what is installed, else source with a toolchain and binaries without. |
| `--binaries=<dir\|zip\|url>` | Where prebuilt binaries come from. Also `UE_MCP_BINARIES`. Default is the GitHub release for the version. |
| `--disable=<categories>` | Tool categories to turn off. |
| `--context=micro\|lean\|full` | Context strategy. |
| `--[no-]feedback`, `--feedback-mode=<mode>`, `--[no-]hook`, `--[no-]skills` | Agent behavior. `--feedback-mode` stores `interactive`, `auto-approve` or `defer` for this project. |
| `--[no-]native-tools`, `--native-exclude=<categories>` | Native Unreal tools. |
| `--[no-]pin` | Pin client configs to `ue-mcp@<this version>`. On by default with `--yes`, so the server and bridge move together; off by default in the wizard. |
| `--command=<path>` | Use this instead of `npx` in client configs. |
| `--dry-run` | Print the plan and change nothing. |

Unset flags keep the project's current choices. The exit code is 0 on success, 1 on failure, 2 on bad arguments.

### `ue-mcp status`

```bash
npx ue-mcp status --json
```

Read-only. Reports the plugin (directory, install kind, marker, versions, whether the `.uproject` enables it), running editors, toolchain, engine, `ue-mcp.yml`, each MCP client with the version its entry pins, and any install problem with its fix.

## 2. Open the Editor

1. Open your project.
2. With a source install, the editor asks to build the missing `UE_MCP_Bridge` modules. Say yes. The first build takes a few minutes. A binary install opens without a build.

The bridge starts once the editor finishes loading. It listens on a port derived from the project path (see [Bridge Connection](configuration.md#bridge-connection)).

### Verify the bridge

1. Open **Window > Output Log**.
2. Filter on `LogMCPBridge`.
3. Confirm a line like this appears:

   ```
   LogMCPBridge: [UE-MCP] Bridge listening on ws://127.0.0.1:51234 (loopback only)
   ```

If it is not there, see [Troubleshooting](troubleshooting.md).

## 3. Verify the connection

1. Open your AI client.
2. Paste:

   ```text
   Run project(action="get_status").
   ```

3. Look for `"editorConnected": true` in the response.

## 4. Try things

Good first prompts:

```text
What's in my level right now?
```

```text
List all blueprints under /Game/Blueprints.
```

```text
Place a directional light at (0, 0, 500), a SkyLight at the origin, then build lighting at preview quality.
```

```text
Run the Neon Shrine demo so I can see what you can build.
```

```text
Read the Blueprint at /Game/Blueprints/BP_Player and explain what it does.
```

Direct tool-call syntax also works:

```text
level(action="get_outliner")
asset(action="list", directory="/Game/")
demo(action="step", stepIndex=1)
reflection(action="reflect_class", className="StaticMeshActor")
```

Under the default `micro` strategy the agent reaches these through the `tools` gateway: `tools(action="call", category="level", method="get_outliner")`. See the [Tool Reference](tool-reference.md) for everything available.

## Updating

Update from a plain terminal (not your MCP client), in the project directory:

```bash
npx ue-mcp update
```

This updates the package, deploys the new plugin source and rebuilds the editor target. Outside a project directory only the package updates, and the command says so. With the editor open it deploys and skips the build; the next editor start or restart through ue-mcp builds first, or close the editor and run `npx ue-mcp build`. `--no-build` stops after the deploy and `--no-deploy` updates the package only.

A binary install updates to verified binaries for the new version instead, with nothing to build. The editor can stay open: on Windows the new files replace the loaded ones and load on the next editor start; on Mac and Linux they wait until the editor exits. Pinned client configs move to the new version either way.

Then quit your MCP client and relaunch it so it starts the new server, and restart the editor so the new plugin loads.

On the beta channel, run every command as `npx ue-mcp@beta`. A bare `npx ue-mcp` is the stable release.

### `ue-mcp doctor`

If an update succeeds but the running server still reports an old version, run:

```bash
npx ue-mcp doctor
```

It prints every version source (registry latest, npm global, the running servers, the deployed bridge plugin) and flags a project-local `node_modules/ue-mcp` that **shadows** the global install: with `ue-mcp` in a project's `package.json`, `npx ue-mcp` runs that local copy. `ue-mcp update` aligns a stale local copy. `doctor` also flags a client config pinned to a version other than the installed bridge's, and one that launches without `--min-release-age=0`.

After removing the bridge or a project, MCP client configs can be left pointing at it. `doctor` names each such entry, in the project's configs and the machine-wide ones, and `--fix` removes exactly those entries (and adds `--min-release-age=0` where it is missing):

```bash
npx ue-mcp doctor --fix
```

## Building the project

To build your project's C++ editor target from the command line:

```bash
npx ue-mcp build
```

Stop the editor first. Pass a `.uproject` path if you are not in the project directory. Agents can build with `editor(action="build_project")`.

## Unattended agent sessions

`feedback(submit)` waits for your approval before it posts anything, which stalls an agent left running alone. For unattended runs, change your feedback mode:

```bash
npx ue-mcp feedback mode defer          # or: auto-approve
```

The preference is stored in `~/.ue-mcp/state.json`, per user and per machine, not in the project. `defer` queues submissions in `~/.ue-mcp/pending-feedback/` for `npx ue-mcp feedback list/show/approve/discard`. `auto-approve` posts without asking. Both still run the credential and privacy scrubs.

For one run without changing the stored preference, set the environment variable:

```bash
UE_MCP_FEEDBACK_MODE=defer npx ue-mcp ./MyGame.uproject
```

See [Feedback modes](feedback.md#feedback-modes).

## Switching projects

To point ue-mcp at a different `.uproject` without restarting your AI client, ask:

```text
Switch to the project at C:/path/to/Other.uproject.
```

This calls `project(action="set_project")`. The connection to the previous project's editor is dropped before the new project loads, so nothing runs in the project you left. If the new project's editor is not running, the switch still completes: filesystem tools work, and bridge actions wait for that editor. The switch does not install the bridge; run `npx ue-mcp init` in a project that lacks it. See [Switching Projects](configuration.md#switching-projects) for how the port is chosen.

## Manual configuration

To skip `init`, install the bridge with `npx ue-mcp deploy` from the project directory, then add the server to your MCP client config yourself. The server does not install the plugin on its own; without it, the startup log says to run `init`.

!!! tip "Use forward slashes on Windows"
    `C:/Users/...`, not `C:\Users\...`. Backslashes need escaping inside JSON.

=== "Claude Code"

    `.mcp.json` in your project root (or `~/.claude/.mcp.json` for every project):
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

=== "Claude Desktop"

    `%APPDATA%/Claude/claude_desktop_config.json`:
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

=== "Cursor"

    `.cursor/mcp.json` in your project root:
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

=== "Codex"

    `~/.codex/config.toml`:
    ```toml
    [mcp_servers.ue-mcp]
    command = "npx"
    args = ["-y", "--min-release-age=0", "ue-mcp@latest", "C:/path/to/MyGame.uproject"]
    cwd = "C:/path/to"
    enabled = true
    ```

`--min-release-age=0` lets npx run a release that is younger than npm's `min-release-age` setting. See [Troubleshooting](troubleshooting.md#server-wont-start-after-an-update-with-npms-min-release-age-set).

## Where to next

- **[Tool Reference](tool-reference.md)**: every tool and every action
- **[Flows](flows.md)**: chain actions into reusable YAML workflows with rollback and retries
- **[Architecture](architecture.md)**: what happens when you call a tool
- **[Configuration](configuration.md)**: client config, the daemon, the bridge connection, CLI commands
- **[ue-mcp.yml Reference](config-file.md)**: every project config key
- **[Neon Shrine Demo](neon-shrine-demo.md)**: guided 19-step procedural scene build
- **[Troubleshooting](troubleshooting.md)**: connection errors, build errors, asset path errors
