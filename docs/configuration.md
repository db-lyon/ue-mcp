# Configuration

## MCP Client Configuration

UE-MCP runs as a standard MCP server over stdio. Configure it in your AI client by pointing at the built `dist/index.js` and passing your `.uproject` path.

### Basic Configuration

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

### Where to Put This

| Client | Config File |
|--------|-------------|
| Claude Code | `.mcp.json` in project root, or `~/.claude/` global config |
| Claude Desktop | `claude_desktop_config.json` |
| Cursor | `mcp.json` in `.cursor/` or project root |

### Without a Project Path

You can start the server without a `.uproject` argument. It will run in a limited mode — you can then use `project(action="set_project", projectPath="...")` at runtime to attach to a project.

## Project Configuration (`.ue-mcp.json`)

Place a `.ue-mcp.json` file in your UE project root (next to the `.uproject`) to customize behavior.

```json
{
  "contentRoots": [
    "/Game/",
    "/MyPlugin/"
  ]
}
```

### Options

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `contentRoots` | `string[]` | `["/Game/"]` | Content paths to search when using `asset(action="search")`. Add plugin content roots here if your project uses plugins with their own assets. |

## Bridge Connection

The C++ plugin listens on **`ws://localhost:9877`** (currently hardcoded). The MCP server auto-connects on startup and reconnects every 15 seconds if the connection drops.

### Connection States

| State | Meaning |
|-------|---------|
| **Connected** | Bridge is active, all tools available |
| **Disconnected** | Editor not running or plugin not loaded. Filesystem tools still work (INI parsing, C++ headers, asset listing) |
| **Reconnecting** | Connection lost, auto-retry in progress |

Check the current state with `project(action="get_status")`.

## Plugin Deployment

On first run with a project path, the server automatically:

1. Copies `plugin/ue_mcp_bridge/` → `<Project>/Plugins/UE_MCP_Bridge/`
2. Adds `UE_MCP_Bridge` to the `.uproject` plugins list
3. Enables `PythonScriptPlugin` if not already enabled (needed for `execute_python` escape hatch)

The plugin is editor-only and has no runtime footprint.

### Plugin Dependencies

The C++ bridge plugin enables these UE plugins (adding them to `.uproject` if missing):

- `PythonScriptPlugin` — for `editor(action="execute_python")`
- `EnhancedInput` — for input action/mapping creation
- `GameplayAbilities` — for GAS tools
- `Niagara` — for VFX tools
- `PCG` — for procedural generation tools

## Editor Lifecycle

The server can manage the editor process:

| Command | Description |
|---------|-------------|
| `editor(action="start_editor")` | Launch UE with the current project |
| `editor(action="stop_editor")` | Gracefully stop the editor |
| `editor(action="restart_editor")` | Stop and relaunch |
