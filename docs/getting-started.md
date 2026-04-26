# Getting Started

<Badge variant="time">⏱ ~5 minutes</Badge>

UE-MCP lets you tell an AI assistant what you want done in Unreal. It can place actors, write blueprints, author materials, configure Niagara, build lighting, generate PCG systems, blah, blah, blah.

## Prerequisites

1. Install [**Unreal Engine 5.4 to 5.7**](https://www.unrealengine.com/en-US/download "Free download via the Epic Games Launcher.")
2. Install [**Node.js 18 or newer**](https://nodejs.org/ "JavaScript runtime that UE-MCP needs to run.")
3. Install an MCP-capable AI client (e.g. [**Claude Code**](https://docs.anthropic.com/en/docs/claude-code "Anthropic's official CLI agent."))

## 1. Installation

1. If your Unreal Editor is open, close it.
2. Open a terminal and run:

```bash
npx ue-mcp init
```

`npx` ships with Node.js — it downloads `ue-mcp` from npm and runs it. First run takes ~30 seconds; cached after.

Run from inside the project folder to auto-detect the `.uproject`, or pass it directly:

```bash
npx ue-mcp init C:/path/to/MyGame.uproject
```

!!! tip "Use forward slashes on Windows"
    `C:/Users/...`, not `C:\Users\...`. Backslashes need escaping inside JSON config and tend to bite later. Forward slashes work everywhere.

### What the installer does

1. Asks for your `.uproject` path (or auto-detects).
2. Asks which **tool categories** to enable (`level`, `blueprint`, `material`, `niagara`, etc.). Default is all on.
3. Copies the C++ bridge plugin into `<YourProject>/Plugins/UE_MCP_Bridge/`.
4. Edits your `.uproject` to enable the bridge plugin and its dependencies (Niagara, PCG, GameplayAbilities, Enhanced Input).
5. Detects installed MCP clients (Claude Code, Claude Desktop, Cursor) and writes the config for each.
6. **Claude Code only**: optionally installs a hook that nudges agents to report tool gaps when they fall back to raw Python.

## 2. Restart the Editor

1. Open your project.
2. When the editor asks whether to compile `UE_MCP_Bridge`, say yes. First-time compile takes ~30-60 seconds.

The bridge starts automatically once the editor finishes loading, listening on `ws://localhost:9877`.

### Verify the bridge

1. Open **Window → Output Log**.
2. Filter on `LogMCPBridge`.
3. Confirm this line appears:

   ```
   LogMCPBridge: UE-MCP Bridge server started on ws://localhost:9877
   ```

If it's not there, see [Troubleshooting](troubleshooting.md).

## 3. Verify the connection

1. Open your AI client.
2. Paste:

   ```text
   Run project(action="get_status").
   ```

3. Look for `"bridgeConnected": true` in the response.

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

Idempotent. Tells you if a restart is needed.

## Switching projects

To attach to a different `.uproject` without restarting your AI client, ask:

```text
Switch to the project at C:/path/to/Other.uproject.
```

UE-MCP redeploys the bridge and reconnects. (That's `project(action="set_project")` under the hood.)

## Manual configuration

If you'd rather skip `npx ue-mcp init`, edit the MCP client config yourself:

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

The first run auto-deploys the C++ plugin. To deploy explicitly: `npx ue-mcp update <path>`, then restart the editor.

## Resolving GitHub issues

Let Claude Code take a swing at an open issue:

```bash
npx ue-mcp resolve 16
```

Fetches the issue, creates a `resolve/<n>` branch, runs Claude Code with the issue body as context, opens a PR. Requires `claude` and `gh` CLIs.

## Where to next

- **[Tool Reference](tool-reference.md)** - every tool and every action
- **[Flows](flows.md)** - chain actions into reusable YAML workflows with rollback and retries
- **[Architecture](architecture.md)** - what's actually happening when you call a tool
- **[Configuration](configuration.md)** - `.ue-mcp.json` options and per-client config
- **[Neon Shrine Demo](neon-shrine-demo.md)** - guided 19-step procedural scene build
- **[Troubleshooting](troubleshooting.md)** - connection errors, build errors, asset path errors
