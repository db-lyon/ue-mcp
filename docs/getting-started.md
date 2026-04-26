# Getting Started

UE-MCP lets an AI assistant (Claude Code, Claude Desktop, Cursor) drive the Unreal Editor. The TypeScript MCP server runs on your machine and talks over WebSocket to a C++ plugin inside the editor.

## Prerequisites

- **Unreal Engine 5.4-5.7**, with a `.uproject` to attach to
- **Node.js 18+** ([nodejs.org](https://nodejs.org/) - LTS is fine). Verify with `node --version`.
- **An MCP client** - one of [Claude Code](https://docs.anthropic.com/en/docs/claude-code), [Claude Desktop](https://claude.ai/download), or [Cursor](https://cursor.com)

## 1. Install

Close the editor first (the installer deploys a plugin into your project), then:

```bash
npx ue-mcp init
```

This will:

1. Auto-detect or ask for your `.uproject` path
2. Let you toggle which tool categories to enable (default: all)
3. Deploy the C++ bridge plugin into `<YourProject>/Plugins/UE_MCP_Bridge/`
4. Enable required UE plugins (Niagara, PCG, GAS, Enhanced Input) in your `.uproject`
5. Detect installed MCP clients and write their config

You can pass the path inline:

```bash
npx ue-mcp init C:/path/to/MyGame.uproject
```

!!! tip "Forward slashes on Windows"
    Use `/` even on Windows. Backslashes need escaping in JSON and break things.

## 2. Restart the Editor

Open your project. The editor will compile the new plugin (~30-60s, first time only). When loaded, the bridge listens on `ws://localhost:9877`.

Verify in **Window → Output Log**, filter on `LogMCPBridge`:

```
LogMCPBridge: UE-MCP Bridge server started on ws://localhost:9877
```

If you don't see that line, see [Troubleshooting](troubleshooting.md).

## 3. Verify Connection

Your MCP client launches the server on demand - nothing to start manually. Ask the AI:

> Run `project(action="get_status")`.

You should get back `bridgeConnected: true` along with project name and engine version.

## 4. First Prompts

```
level(action="get_outliner")               — list every actor in the level
asset(action="list", directory="/Game/")   — browse project assets
demo(action="step", stepIndex=1)           — run the Neon Shrine demo
reflection(action="reflect_class", className="StaticMeshActor")
```

Or in plain language: "Place a directional light at (0, 0, 500) and a SkyLight at the origin."

## Updating

```bash
npx ue-mcp update
```

Run from your project directory. Redeploys the C++ bridge plugin and tells you if a restart is needed. Idempotent.

## Switching Projects

To attach to a different project at runtime:

```
project(action="set_project", projectPath="C:/path/to/Other.uproject")
```

UE-MCP redeploys the bridge into the new project and reconnects.

## Manual Configuration

If you'd rather skip `npx ue-mcp init`, write the MCP client config yourself:

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

The first run against a project auto-deploys the C++ plugin. To deploy explicitly: `npx ue-mcp update <path>`. Restart the editor after.

## Resolving Issues

Anyone can let Claude Code take a swing at an open issue:

```bash
npx ue-mcp resolve 16
```

Fetches the issue, creates a `resolve/` branch, runs Claude Code with the issue context, opens a PR. Requires `claude` and `gh` CLIs.

## Next

- **[Tool Reference](tool-reference.md)** — every tool and action
- **[Flows](flows.md)** — chain actions into reusable YAML workflows
- **[Architecture](architecture.md)** — what's actually happening
- **[Neon Shrine Demo](neon-shrine-demo.md)** — guided 19-step scene
- **[Troubleshooting](troubleshooting.md)** — when something's broken
