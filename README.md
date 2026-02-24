# UE-MCP — Unreal Engine Model Context Protocol Server

A hybrid MCP server that gives AI assistants deep read/write access to Unreal Engine projects. Works in two modes:

- **Offline mode** — Parses raw `.uasset` / `.umap` binaries using [UAssetAPI](https://github.com/atenfyr/UAssetAPI). No Unreal Editor required.
- **Live mode** — Connects to a running Unreal Editor via a WebSocket bridge plugin for full read/write access with undo, compilation, and PIE introspection.

```
┌─────────────────────────────────────┐
│          MCP Server (C#)            │
│                                     │
│  ┌───────────┐   ┌──────────────┐  │
│  │ UAssetAPI  │   │  UE Bridge   │  │
│  │ (offline)  │   │  (live)      │  │
│  └─────┬─────┘   └──────┬───────┘  │
│        │                 │          │
│        └────────┬────────┘          │
│           Mode Router               │
│   "Is the editor connected? Use     │
│    bridge. Otherwise, parse raw."   │
└─────────────────────────────────────┘
         ▲                    ▲
         │ MCP Protocol       │ TCP/WebSocket
         │ (stdio)            │ (ws://localhost:9877)
         ▼                    ▼
    AI Assistant         UE Editor (Python plugin)
```

## Prerequisites

- [.NET 8.0 SDK](https://dotnet.microsoft.com/download/dotnet/8.0) or later
- Git (for cloning with submodules)
- Unreal Engine 4.13+ or 5.x project (for offline mode)
- Unreal Engine with Python plugin enabled (for live mode)

## Quick Start

### 1. Clone and build

```bash
git clone --recursive https://github.com/YOUR_USERNAME/ue-mcp.git
cd ue-mcp
dotnet build
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
dotnet build
```

### 2. Configure your MCP client

Add to your MCP configuration (e.g., Cursor `mcp.json` or Claude Desktop `claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "ue-mcp": {
      "command": "dotnet",
      "args": ["run", "--project", "/absolute/path/to/ue-mcp/src/UeMcp/UeMcp.csproj"]
    }
  }
}
```

Or after publishing:

```json
{
  "mcpServers": {
    "ue-mcp": {
      "command": "/path/to/ue-mcp/publish/ue-mcp"
    }
  }
}
```

### 3. Point it at your UE project

Once the AI assistant is connected, the first thing it should do:

```
set_project("C:/Users/you/Unreal Projects/MyGame/MyGame.uproject")
```

This loads the project, detects the engine version, and attempts to connect to a running editor.

## Tools Reference

### Status & Project

| Tool | Mode | Description |
|------|------|-------------|
| `get_status` | Both | Server mode, connection status, loaded project info |
| `set_project` | Both | Point the server at a UE project directory |
| `get_project_info` | Both | Read the `.uproject` file contents |

### Asset Reading

| Tool | Mode | Description |
|------|------|-------------|
| `read_asset` | Both | Read any asset's full structure as JSON |
| `read_asset_properties` | Offline | Read specific properties from a named export |
| `list_assets` | Both | List assets in a directory with optional type filter |
| `search_assets` | Both | Search assets by name/content across the project |
| `asset_to_json` | Offline | Export full asset via UAssetAPI's native JSON serializer |

### Blueprints

| Tool | Mode | Description |
|------|------|-------------|
| `read_blueprint` | Both | Full Blueprint structure: parent class, variables, functions, graphs, components |
| `list_blueprint_variables` | Both | All variables with types, flags, and default values |
| `list_blueprint_functions` | Both | All functions with parameters and bytecode size |
| `read_blueprint_graph` | Both | Nodes and connections within a specific graph |

### DataTables

| Tool | Mode | Description |
|------|------|-------------|
| `read_datatable` | Both | Read DataTable rows and column structure with optional row filter |

### Editor (Live Mode Only)

| Tool | Mode | Description |
|------|------|-------------|
| `editor_execute` | Live | Run a console command in the editor |
| `set_property` | Live | Set a property value with undo support |
| `compile_blueprint` | Live | Compile a Blueprint and get error feedback |
| `create_blueprint` | Live | Create a new Blueprint with specified parent class |
| `add_blueprint_variable` | Live | Add a variable to a Blueprint |
| `add_blueprint_node` | Live | Add a node to a Blueprint graph |
| `connect_blueprint_pins` | Live | Wire two pins between nodes |
| `play_in_editor` | Live | Start/stop/query PIE sessions |
| `get_runtime_value` | Live | Read actor property values during PIE |
| `save_asset` | Live | Save one or all modified assets |

## Live Mode Setup

Live mode requires a small Python plugin running inside the Unreal Editor.

### Install the bridge plugin

1. **Enable the Python Editor Script Plugin** in your UE project (Edit → Plugins → search "Python")

2. **Install websockets** in UE's Python:

   ```bash
   # Windows
   "<UE_INSTALL>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -m pip install websockets

   # macOS/Linux
   "<UE_INSTALL>/Engine/Binaries/ThirdParty/Python3/Linux/bin/python3" -m pip install websockets
   ```

3. **Copy the plugin** to your project:

   ```bash
   cp -r plugin/ue_mcp_bridge <YOUR_PROJECT>/Content/Python/
   ```

4. **Start the bridge** in the UE Python console:

   ```python
   import ue_mcp_bridge
   ue_mcp_bridge.start()
   ```

### Auto-start on editor launch

Add to your project's `DefaultEngine.ini`:

```ini
[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
+StartupScripts=/Game/Python/ue_mcp_bridge/startup_script.py
```

Or use an Editor Utility Blueprint/Widget that runs the Python command on startup.

## How the Mode Router Works

1. On startup, the MCP server begins in **offline mode**
2. When `set_project` is called, it:
   - Parses the `.uproject` to detect the engine version
   - Attempts a WebSocket connection to `ws://localhost:9877`
   - If the editor bridge is running → switches to **live mode**
   - If not → stays in **offline mode**, retries every 15 seconds
3. In live mode, read operations first try the bridge (richer data from editor reflection), falling back to offline parsing on failure
4. Write operations (compile, create, modify) are **only available in live mode** — they require the editor to execute safely

## Project Structure

```
ue-mcp/
├── src/UeMcp/                    # C# MCP Server
│   ├── Program.cs                # Entry point & DI setup
│   ├── Core/
│   │   ├── ModeRouter.cs         # Offline/live mode routing
│   │   └── ProjectContext.cs     # Project state & version detection
│   ├── Offline/
│   │   ├── AssetService.cs       # UAssetAPI wrapper for asset reading
│   │   ├── BlueprintReader.cs    # Blueprint structure parsing
│   │   ├── DataTableReader.cs    # DataTable parsing
│   │   └── AssetSearch.cs        # Project-wide asset search
│   ├── Live/
│   │   ├── EditorBridge.cs       # WebSocket client to UE editor
│   │   └── BridgeMessage.cs      # Bridge protocol message types
│   └── Tools/
│       ├── StatusTools.cs        # get_status, set_project
│       ├── AssetTools.cs         # read_asset, list_assets, search_assets
│       ├── BlueprintTools.cs     # read_blueprint, list_variables/functions
│       ├── DataTableTools.cs     # read_datatable
│       └── EditorTools.cs        # Live editor operations
├── plugin/ue_mcp_bridge/         # UE Editor Python plugin
│   ├── bridge_server.py          # WebSocket server
│   ├── handlers/
│   │   ├── asset.py              # Asset reading via editor reflection
│   │   ├── blueprint.py          # Blueprint CRUD operations
│   │   ├── editor.py             # Console commands, property setting
│   │   └── pie.py                # Play-in-Editor control
│   └── startup_script.py         # Auto-start for editor
└── lib/UAssetAPI/                # Git submodule
```

## Supported Engine Versions

Offline mode (via UAssetAPI) supports: **UE 4.13 through 5.5+**

Live mode supports whatever version of Unreal Editor you're running — it uses the editor's own reflection system.

## Publishing

Build a self-contained executable:

```bash
dotnet publish src/UeMcp/UeMcp.csproj -c Release -o publish/ --self-contained -r win-x64
```

Replace `win-x64` with `linux-x64` or `osx-arm64` as needed.

## License

MIT
