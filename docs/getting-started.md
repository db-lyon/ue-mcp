# Getting Started

<Badge variant="time">⏱ ~5 minutes</Badge>

UE-MCP lets an AI assistant work in the Unreal Editor: place actors, write Blueprints, author materials, configure Niagara, build lighting, generate PCG systems and more.

## Prerequisites

1. Install [**Unreal Engine <!-- count:engineRange -->5.4 to 5.9<!-- /count -->**](https://www.unrealengine.com/en-US/download "Free download via the Epic Games Launcher.")
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
3. Asks whether to wrap **native Unreal tools** (the toolset registry in UE 5.8 and later) and which categories get them. See [Native Epic tools](configuration.md#native-epic-tools).
4. Asks for the **context strategy**: `micro` (default), `lean` or `full`. See [Context strategy](configuration.md#context-strategy-full-lean-micro).
5. Lists the MCP clients it detects (Claude Code project and global, Claude Desktop, Cursor, VS Code, Gemini CLI, Codex project and global) and writes the config for each one you keep checked. Project-scoped clients start checked; global and Desktop configs start unchecked because they affect every project on the machine. A config that already holds an Unreal MCP entry also starts checked, and `init` names the entry before it asks. See [Replacing an Unreal MCP entry](#replacing-an-unreal-mcp-entry).
    On UE 5.8 and later it then asks about Epic's own Unreal MCP server and, on UE 5.9 and later, the Unreal Assistant. See [Epic's Unreal MCP server and Unreal Assistant](#epics-unreal-mcp-server-and-unreal-assistant).
6. Asks about **agent behavior**. Everything starts unchecked on a fresh install:
    - Enable the `feedback(submit)` tool.
    - With Claude Code selected: install a PostToolUse hook that nudges the agent to offer feedback after `execute_python`, and install the bundled Claude Code skills into `.claude/skills/`.

    For Cursor, Codex, Gemini CLI and VS Code it also writes the ue-mcp block into the instruction file that client reads. See [Agent guides and skills for every client](#agent-guides-and-skills-for-every-client).
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
| `--clients=<ids>` | `claude-code`, `claude-code-global`, `claude-desktop`, `cursor`, `vscode`, `gemini`, `codex` (`~/.codex/config.toml`), `codex-project` (`.codex/config.toml`). Unset keeps the clients already configured, or on a fresh project every detected project-scoped one, plus every client whose config holds an Unreal MCP entry. |
| `--install=auto\|source\|binary` | `auto` (default) adopts what is installed, else source with a toolchain and binaries without. |
| `--binaries=<dir\|zip\|url>` | Where prebuilt binaries come from. Also `UE_MCP_BINARIES`. Default is the GitHub release for the version. |
| `--disable=<categories>` | Tool categories to turn off. |
| `--context=micro\|lean\|full` | Context strategy. |
| `--[no-]feedback`, `--feedback-mode=<mode>`, `--[no-]hook`, `--[no-]skills` | Agent behavior. `--feedback-mode` stores `interactive`, `auto-approve` or `defer` for this project. |
| `--[no-]agent-guides` | Write the ue-mcp block into `AGENTS.md`, `GEMINI.md` or `.github/copilot-instructions.md` for the clients configured (default on). `--no-agent-guides` removes every block init wrote. |
| `--[no-]native-tools`, `--native-exclude=<categories>` | Native Unreal tools. Excluded categories are written as `disable: [<category>:epic]`. |
| `--[no-]pin` | Pin client configs to `ue-mcp@<this version>`. On by default with `--yes`, so the server and bridge move together; off by default in the wizard. |
| `--command=<path>` | Use this instead of `npx` in client configs. |
| `--[no-]unreal-mcp-autostart`, `--unreal-mcp-port=<port>` | Whether Epic's Unreal MCP server starts with this project's editor, and its port. |
| `--[no-]unreal-assistant` | Attach ue-mcp to Epic's Unreal Assistant, or detach it. |
| `--dry-run` | Print the plan and change nothing. |

Unset flags keep the project's current choices. The exit code is 0 on success, 1 on failure, 2 on bad arguments.

### Agent guides and skills for every client

Claude Code loads the skills `init` copies into `.claude/skills/`. Other clients read a project instruction file instead, so `init` writes a short ue-mcp block into the one each configured client reads:

| Client | File |
|---|---|
| Cursor, Codex (global or project) | `AGENTS.md` |
| Gemini CLI | `GEMINI.md` |
| VS Code | `.github/copilot-instructions.md` |

The block sits between `<!-- ue-mcp:begin ... -->` and `<!-- ue-mcp:end -->` and says how to start (`project(get_status)`), how to find and call an action, and which workflow guides the server serves. Everything outside the markers is yours and is never changed; an existing file gets the block appended after a blank line, in the file's own line endings. A rerun replaces the block whole, so it reports `unchanged` when nothing moved. Dropping a client from `--clients` removes its block, and `--no-agent-guides` removes them all; a file that held only the block is deleted. `--json` reports each as a `guide:<file>` step.

The same guides are served by the server itself, to any client, editor or not:

- **Prompts.** `prompts/list` names one prompt per shipped skill (`ue-mcp-workflow`, `ue-mcp-blueprint` and the rest); `prompts/get` returns the guide as a user message.
- **Resources.** `resources/list` lists each as `skill://ue-mcp/<name>` (`text/markdown`) ahead of the editor's own resources, and `resources/read` returns the SKILL.md.

### Replacing an Unreal MCP entry

UE 5.9's built-in MCP server can write its own entry into the project's client configs, and its stdio proxy can rewrite `.mcp.json` to launch the proxy instead. `init` recognises both:

- an entry named `unreal-mcp` with a `url` (or Gemini CLI's `httpUrl`) on `localhost` or `127.0.0.1`, in `.mcp.json`, `.cursor/mcp.json`, `.vscode/mcp.json`, `.gemini/settings.json` or `.codex/config.toml`;
- the `unreal-mcp-proxy` entry `unreal_mcp_proxy install` writes in `.mcp.json`, which launches the proxy binary and keeps the HTTP entry under `_upstreamEntry`, or any entry that launches `unreal_mcp_proxy`.

For each client you configure, `init` copies the config to `<file>.ue-mcp-backup`, removes those entries, and writes the `ue-mcp` entry. Every other server and setting in the file is kept. The output names each entry it replaced and the backup it saved, in the wizard and with `--yes`; `--json` reports it as a `replace:<client>` step, and `--dry-run` lists it under each client's `replaces`. To undo, copy the backup over the config. A client left out of `--clients`, or unchecked in the wizard, is not touched.

With a source-built engine, the editor writes these files into the engine root (the folder holding `Engine/`) instead of the project. `init` resolves the project's engine the same way `ue-mcp build` does and, when that engine is a source build, also replaces the entries it finds there, with the same backup and reporting: `--json` reports `replace:<client>:engine-root` and `client:<client>:engine-root`, and `--dry-run` lists them under each client's `engineRootConfig`. An installed engine (one with `Engine/Build/InstalledBuild.txt`, such as a launcher install) writes into the project, so `init` never edits files in it.

### Epic's Unreal MCP server and Unreal Assistant

UE 5.8 and later ship Epic's own MCP server (the ModelContextProtocol plugin, enabled by default), and UE 5.9's engine config starts it with the editor on `http://127.0.0.1:8000/mcp`. It then runs beside ue-mcp for the same project. ue-mcp's `epic` category and the `epic_*` actions already reach the same toolsets, so an agent given both servers sees the editor's tools twice.

`init` and `doctor` read the server's state from files: whether the `.uproject` turns the plugin off, and `bAutoStartServer`, `ServerPortNumber` and `ServerUrlPath` from the engine's `BaseEditorPerProjectUserSettings.ini`, then the project's `Config/DefaultEditorPerProjectUserSettings.ini`, then your per-project user settings in `Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini`, each later file winning. `-ModelContextProtocolPort=` on the editor command line overrides the port for that launch and is not read.

- The wizard asks whether to keep **Start Epic's Unreal MCP server with the editor**. Unchecking it, or `--no-unreal-mcp-autostart`, writes `bAutoStartServer=False` to the `[/Script/ModelContextProtocol.ModelContextProtocolSettings]` section of your per-project user settings. `--unreal-mcp-autostart` writes `True`.
- `--unreal-mcp-port=<port>` writes `ServerPortNumber` there instead, to move it. The ue-mcp bridge port for the project is refused.

Every other line in that file is kept, and nothing is written when the setting already has that value. The editor reads it on start, so restart an editor that is open. `--json` reports the change as an `unreal-mcp` step, and `--dry-run` shows the server under `unrealMcp`.

On UE 5.9 and later, with the AIAssistant plugin enabled (it is by default), the wizard also offers **Attach ue-mcp to Epic's Unreal Assistant**, and `--unreal-assistant` does the same without prompts. The assistant takes MCP servers for the agent it runs (the agent in its config, or the external agent picked in Editor Preferences) from `mcp_servers` in one per-user `AIAssistant.json`. It reads the first of these that exists: the engine's own `Engine/Config/AIAssistant.json` (and its `Restricted` variants), then `%LOCALAPPDATA%/UnrealEngine/<version>/AIAssistant.json`, then `%LOCALAPPDATA%/UnrealEngine/Common/AIAssistant.json`. On a source-built engine the last two are `Engine/AIAssistant.json`.

- `init` adds a `ue-mcp` entry to that list in the user file the assistant would read, `Common` when there is none yet, launching the server the same way the client configs do. Every other key and server is kept, and an existing file is saved as `<file>.ue-mcp-backup` first.
- When the file has no `mcp_servers` list yet, the assistant gives its agent the editor's own MCP server, and a list replaces that default. So `init` writes the editor's server (`unreal-mcp`, at this project's URL) beside ue-mcp, and Epic's tools stay available.
- Running it again changes nothing. `--no-unreal-assistant`, or unchecking the box, removes only the `ue-mcp` entry, and a list left holding only the editor's server is removed so the assistant goes back to choosing it itself.
- The file is per user, not per project, so the entry serves the project you last attached. A file in the engine's `Config` folder wins over it, and `init` and `doctor` say so.

The assistant's hosted chat, without an agent, does not use these servers.

`doctor` prints the server as `epic mcp` and the attachment as `epic assistant`, and adds a note when the server runs beside ue-mcp. `project(get_status)` carries `unrealMcpServer` (its URL and where the setting came from) while the server is set to start with the editor.

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

It prints every version source (registry latest, npm global, the running servers, the deployed bridge plugin) and flags a project-local `node_modules/ue-mcp` that **shadows** the global install: with `ue-mcp` in a project's `package.json`, `npx ue-mcp` runs that local copy. `ue-mcp update` never installs into the project; it tells you how to remove the local copy. `doctor` also flags a client config pinned to a version other than the installed bridge's, and one that launches without `--min-release-age=0`. For a UE 5.8+ project it reports Epic's Unreal MCP server (see [Epic's Unreal MCP server and Unreal Assistant](#epics-unreal-mcp-server-and-unreal-assistant)) and flags it when it shares a port with ue-mcp.

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

=== "VS Code"

    `.vscode/mcp.json` in your project root. VS Code keys servers under `servers`:
    ```json
    {
      "servers": {
        "ue-mcp": {
          "type": "stdio",
          "command": "npx",
          "args": ["-y", "--min-release-age=0", "ue-mcp@latest", "C:/path/to/MyGame.uproject"]
        }
      }
    }
    ```

=== "Gemini CLI"

    `.gemini/settings.json` in your project root:
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

    `.codex/config.toml` in your project root (read once Codex trusts the project), or `~/.codex/config.toml`:
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
