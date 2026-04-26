# Getting Started

<Badge variant="time">⏱ ~5 minutes</Badge>

UE-MCP lets you tell an AI assistant what you want done in Unreal, and it actually does it - placing actors, writing blueprints, authoring materials, configuring Niagara, building lighting, all by driving the editor for you.

## Prerequisites

1. [**Unreal Engine 5.4 to 5.7**](https://www.unrealengine.com/en-US/download "Free download via the Epic Games Launcher.") installed, with a `.uproject` for UE-MCP to attach to.
2. [**Node.js 18 or newer**](https://nodejs.org/ "JavaScript runtime that UE-MCP needs to run.") installed and on your PATH.
3. **An MCP-capable AI client** — [Claude Code](https://docs.anthropic.com/en/docs/claude-code "Anthropic's official CLI agent. The best fit for UE-MCP and the client most of the docs assume."), [Claude Desktop](https://claude.ai/download "The standalone Claude desktop app."), or [Cursor](https://cursor.com "AI-powered code editor based on VS Code. Reads MCP server configuration from .cursor/mcp.json.") — to drive UE-MCP and, through it, the editor.

## 1. Install

Close the Unreal Editor first - the installer deploys a C++ plugin into your project, and the editor needs to be closed to pick it up.

Open a terminal and run:

```bash
npx ue-mcp init
```

`npx` ships with Node.js. It downloads `ue-mcp` from npm and runs the interactive setup. The first run takes ~30 seconds; after that it's cached.

If you run the command from inside your project folder it auto-detects the `.uproject`. Otherwise pass it directly:

```bash
npx ue-mcp init C:/path/to/MyGame.uproject
```

!!! tip "Use forward slashes on Windows"
    `C:/Users/...`, not `C:\Users\...`. Backslashes need escaping inside JSON config and tend to bite later. Forward slashes work everywhere.

### What the installer does

1. Asks for your `.uproject` path (or auto-detects).
2. Asks which **tool categories** to enable (`level`, `blueprint`, `material`, `niagara`, etc.). Default is all on - you can turn things off later.
3. Copies the C++ bridge plugin into `<YourProject>/Plugins/UE_MCP_Bridge/`.
4. Edits your `.uproject` to enable the bridge plugin and the UE plugins it depends on (Niagara, PCG, GameplayAbilities, Enhanced Input).
5. Detects which MCP clients you have installed (Claude Code, Claude Desktop, Cursor) and writes the config for each one.
6. **Claude Code only**: optionally installs a hook that nudges agents to report tool gaps when they fall back to raw Python.

When it finishes you'll see a summary of what was deployed.

## 2. Restart the Editor

Open your project. The editor will detect the new plugin (`UE_MCP_Bridge`) and ask whether to compile it. Say yes. Compilation takes ~30-60 seconds the first time; after that the binary is cached.

Once the editor finishes loading, the bridge starts automatically and listens on `ws://localhost:9877`.

### Verify the bridge

Open **Window → Output Log**. In the filter box at the top, type `LogMCPBridge`. You should see:

```
LogMCPBridge: UE-MCP Bridge server started on ws://localhost:9877
```

If that line is there, the editor side is ready. If it isn't, jump to [Troubleshooting](troubleshooting.md).

## 3. Verify the connection

You don't start the MCP server yourself - your AI client launches it the first time you use a UE-MCP tool. Open your client and paste:

```text
Run project(action="get_status").
```

You should get a result that includes `"bridgeConnected": true`, your project name, and the engine version. That's the green light.

## 4. Try things

Some good first prompts. The AI translates these into the right tool calls:

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

When a new UE-MCP version is released:

```bash
npx ue-mcp update
```

Run it from your project directory. It redeploys the C++ bridge plugin and tells you whether you need to restart the editor. Safe to re-run.

## Switching projects

To attach to a different `.uproject` without restarting your AI client, ask:

```text
Switch to the project at C:/path/to/Other.uproject.
```

That's `project(action="set_project", projectPath="...")` under the hood. UE-MCP redeploys the bridge into the new project and reconnects.

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

The first run against a project auto-deploys the C++ plugin, but you can deploy it explicitly with `npx ue-mcp update <path>`. Restart the editor afterward.

## Resolving GitHub issues

Anyone can let Claude Code take a swing at an open issue:

```bash
npx ue-mcp resolve 16
```

This fetches issue #16, creates a `resolve/<n>` branch, runs Claude Code with the issue body as context, then opens a PR. Requires the `claude` and `gh` CLIs.

## Where to next

- **[Tool Reference](tool-reference.md)** - every tool and every action
- **[Flows](flows.md)** - chain actions into reusable YAML workflows with rollback and retries
- **[Architecture](architecture.md)** - what's actually happening when you call a tool
- **[Configuration](configuration.md)** - `.ue-mcp.json` options and per-client config
- **[Neon Shrine Demo](neon-shrine-demo.md)** - guided 19-step procedural scene build
- **[Troubleshooting](troubleshooting.md)** - connection errors, build errors, asset path errors
