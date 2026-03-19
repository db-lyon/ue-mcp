# Getting Started

## Prerequisites

- [Node.js](https://nodejs.org/) 18+
- Unreal Engine 5.4–5.7 (any version with `PythonScriptPlugin`)
- An MCP-capable AI client (Claude Code, Claude Desktop, Cursor, etc.)

## 1. Clone and Build

```bash
git clone https://github.com/db-lyon/ue-mcp.git
cd ue-mcp
npm install
npm run build
```

## 2. Configure Your MCP Client

Add to your MCP configuration, passing your `.uproject` path as an argument.

=== "Claude Code"

    `.mcp.json` or project settings:
    ```json
    {
      "mcpServers": {
        "ue-mcp": {
          "command": "node",
          "args": [
            "C:/path/to/ue-mcp/dist/index.js",
            "C:/Users/you/UnrealProjects/MyGame/MyGame.uproject"
          ]
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
          "command": "node",
          "args": [
            "C:/path/to/ue-mcp/dist/index.js",
            "C:/Users/you/UnrealProjects/MyGame/MyGame.uproject"
          ]
        }
      }
    }
    ```

=== "Cursor"

    `mcp.json`:
    ```json
    {
      "mcpServers": {
        "ue-mcp": {
          "command": "node",
          "args": [
            "C:/path/to/ue-mcp/dist/index.js",
            "C:/Users/you/UnrealProjects/MyGame/MyGame.uproject"
          ]
        }
      }
    }
    ```

## 3. First Run

On first run, the server automatically:

1. Detects the engine version from `.uproject`
2. Enables `PythonScriptPlugin` if needed
3. Copies the C++ bridge plugin to `<Project>/Plugins/UE_MCP_Bridge/`
4. Enables the plugin in `.uproject`
5. Connects to the editor if it's running

!!! warning "Restart Required"
    After the first launch, **restart the editor once** so the C++ bridge plugin loads. From then on it starts automatically with the editor.

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

## Next Steps

- **[Tool Reference](tool-reference.md)** — Full list of every tool and action
- **[Architecture](architecture.md)** — How the pieces fit together
- **[Neon Shrine Demo](neon-shrine-demo.md)** — Walk through a guided demo scene
