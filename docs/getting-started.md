# Getting Started

<Badge variant="time">⏱ ~5 minutes</Badge>

UE-MCP lets you tell an AI assistant what you want done in Unreal. It can place actors, write blueprints, author materials, configure Niagara, build lighting, generate PCG systems, blah, blah, blah.

## Prerequisites

1. Install [**Unreal Engine 5.4 to 5.7**](https://www.unrealengine.com/en-US/download "Free download via the Epic Games Launcher.")
2. Install [**Node.js 18 or newer**](https://nodejs.org/ "JavaScript runtime that UE-MCP needs to run.")
3. Install an MCP-capable AI client (e.g. [**Claude Code**](https://docs.anthropic.com/en/docs/claude-code "Anthropic's official CLI agent."))

## 1. Installation

1. If your Unreal Editor is open, close it.
2. `cd` into your project folder (the one with the `.uproject`).
3. Run the wizard:

   ```bash
   npx ue-mcp init
   ```

The wizard then:

1. Auto-detects your `.uproject`.
2. Asks which **tool categories** to enable (`level`, `blueprint`, `material`, `niagara`, etc.), with one-line descriptions. Pre-checked on a fresh install; on re-init, prior opt-outs in `ue-mcp.yml`'s `ue-mcp.disable[]` are remembered.
3. Copies the C++ bridge plugin into `<YourProject>/Plugins/UE_MCP_Bridge/`.
4. Enables the plugins it needs in your `.uproject`: `UE_MCP_Bridge`, `PythonScriptPlugin`, plus any of `Niagara`, `PCG`, `GameplayAbilities`, `EnhancedInput` required by the categories you kept.
5. Scaffolds an empty `ue-mcp.yml` (for custom flows) if missing.
6. Detects installed MCP clients (Claude Code project + global, Claude Desktop, Cursor, Codex) and writes the config for each you confirm. Global/Desktop configs default unchecked since opting them in affects every project on the machine.
7. Asks about **agent behavior** (all default off on fresh installs — blasting through with Enter adds no surprises): enable the `feedback(submit)` tool, install the Claude-Code-only PostToolUse hook that nudges the agent to offer feedback after `execute_python`, install bundled Claude Code workflow skills.
8. If you opted into the feedback prompt hook, optionally runs the **GitHub OAuth device flow** so `feedback(submit)` can author issues as your real GitHub user (default `author="user"`). The token is cached at `~/.ue-mcp/auth.json` (mode 600) and reused. Skip if you don't want it now — you can run `npx ue-mcp auth` later, or call `feedback(submit)` with `author="bot"` to post anonymously instead.
9. Writes the final `ue-mcp.yml` and prints a recap of every file or directory init touched. Per-machine state (e.g. the list of Claude Code settings files where the feedback hook was installed) is kept under `~/.ue-mcp/`, not in the project tree.

## 2. Open the Editor

1. Open your project.
2. When the editor asks whether to compile `UE_MCP_Bridge`, say yes. First-time compile takes ~30-60 seconds.

The bridge starts automatically once the editor finishes loading, listening on `ws://localhost:9877`.

### Verify the bridge

1. Open **Window → Output Log**.
2. Filter on `LogMCPBridge`.
3. Confirm this line appears:

   ```
   LogMCPBridge: [UE-MCP] Bridge listening on ws://localhost:9877
   ```

If it's not there, see [Troubleshooting](troubleshooting.md).

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

See the [Tool Reference](tool-reference.md) for everything available.

## Updating

Run from your project directory whenever a new UE-MCP version ships:

```bash
npx ue-mcp update
```

## Unattended agent sessions

If you set up the feedback prompt hook and then leave a long-running agent working, the elicitation approval prompt on `feedback(submit)` will stall the session waiting for you. For unattended runs, switch your personal feedback mode:

```bash
npx ue-mcp feedback mode defer          # or: auto-approve
```

This writes the preference to `~/.ue-mcp/state.json` (per-user, per-machine — it is not committed to the project). `defer` writes submissions to `~/.ue-mcp/pending-feedback/` for later review with `npx ue-mcp feedback list/show/approve/discard`. `auto-approve` posts directly without prompting. Both still run the credential and privacy scrubs.

For a one-off agent run without changing the persisted preference, use the env var instead:

```bash
UE_MCP_FEEDBACK_MODE=defer npx ue-mcp ./MyGame.uproject
```

See [Feedback → modes](feedback.md#feedback-modes).

## Switching projects

To point ue-mcp at a different `.uproject` without restarting your AI client, ask:

```text
Switch to the project at C:/path/to/Other.uproject.
```

UE-MCP redeploys the bridge and reconnects. (Calls `project(action="set_project")` under the hood.)

## Manual configuration

If you'd rather skip `npx ue-mcp init`, edit the MCP client config yourself.

!!! tip "Use forward slashes on Windows"
    `C:/Users/...`, not `C:\Users\...`. Backslashes need escaping inside JSON.

=== "Claude Code"

    `.mcp.json` in your project root:
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

=== "Claude Desktop"

    `claude_desktop_config.json`:
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

=== "Cursor"

    `.cursor/mcp.json`:
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

=== "Codex"

    `~/.codex/config.toml`:
    ```toml
    [mcp_servers.ue-mcp]
    command = "npx"
    args = ["ue-mcp", "C:/path/to/MyGame.uproject"]
    cwd = "C:/path/to"
    enabled = true
    ```

The first run auto-deploys the C++ plugin. To deploy explicitly: `npx ue-mcp update <path>`, then restart the editor.

## Where to next

- **[Tool Reference](tool-reference.md)** - every tool and every action
- **[Flows](flows.md)** - chain actions into reusable YAML workflows with rollback and retries
- **[Architecture](architecture.md)** - what's actually happening when you call a tool
- **[Configuration](configuration.md)** - `ue-mcp.yml` options and per-client config
- **[Neon Shrine Demo](neon-shrine-demo.md)** - guided 19-step procedural scene build
- **[Troubleshooting](troubleshooting.md)** - connection errors, build errors, asset path errors
