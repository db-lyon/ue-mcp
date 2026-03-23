# Getting Started

## Prerequisites

- [Node.js](https://nodejs.org/) 18+
- Unreal Engine 5.4–5.7
- An MCP-capable AI client (Claude Code, Claude Desktop, Cursor, etc.)

## 1. Run Setup

```bash
npx ue-mcp init
```

The interactive setup will walk you through:

1. **Project path** — point it at your `.uproject` file or project directory
2. **Tool categories** — choose which categories to enable (use arrow keys + spacebar to toggle)
3. **Plugin deployment** — copies the C++ bridge plugin into your project
4. **UE plugin enablement** — enables required plugins (PythonScriptPlugin, Niagara, PCG, etc.) based on your selections
5. **MCP client config** — detects installed clients and writes the config for you
6. **Agent behavior** (Claude Code only) — installs hooks that prompt agents to report tool gaps when they fall back to raw Python

You can also pass the project path directly:

```bash
npx ue-mcp init C:/Users/you/UnrealProjects/MyGame/MyGame.uproject
```

!!! tip "Run from your project directory"
    If you run `npx ue-mcp init` from inside your UE project folder, it auto-detects the `.uproject` file — no need to type the path.

!!! warning "Restart Required"
    After setup, **restart the editor once** so the C++ bridge plugin loads. From then on it starts automatically with the editor.

## Updating

When a new version of ue-mcp is published, update the plugin in your project:

```bash
npx ue-mcp update
```

Run this from your project directory. It redeploys the C++ bridge plugin and tells you if an editor restart is needed.

## 2. Manual Configuration

If you prefer to skip the interactive setup, add this to your MCP client config:

=== "Claude Code"

    `.mcp.json` in project root:
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

The server auto-deploys the plugin and enables required UE plugins on first run.

## 4. Verify the Connection

Ask the AI to run:
```
project(action="get_status")
```

You should see the server mode, project path, and editor connection status.

### Manual Verification

In the editor, open **Window > Developer Tools > Output Log** and filter on `LogMCPBridge`:

```
LogMCPBridge: UE-MCP Bridge server started on ws://localhost:9877
```

## 5. Explore

Good first commands to try:

```
level(action="get_outliner")              — see what's in the current level
asset(action="list")                       — browse project assets
reflection(action="reflect_class",
  className="StaticMeshActor")            — understand any UE class
demo(action="step", stepIndex=1)          — run the Neon Shrine demo
```

## Switching Projects

To point at a different `.uproject` at runtime:
```
project(action="set_project", projectPath="C:/path/to/Other.uproject")
```

This re-deploys the bridge and reconnects.

## Resolving Issues

Anyone can pick up an open GitHub issue and let Claude Code fix it:

```bash
npx ue-mcp resolve 16
```

This fetches the issue, creates a `resolve/` branch, launches Claude Code with the issue context, then pushes and opens a PR. Requires `claude` and `gh` CLIs.

## Next Steps

- **[Tool Reference](tool-reference.md)** — Full list of every tool and action
- **[Architecture](architecture.md)** — How the pieces fit together
- **[Neon Shrine Demo](neon-shrine-demo.md)** — Walk through a guided demo scene
