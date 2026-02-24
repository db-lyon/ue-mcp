# UE-MCP — Unreal Engine Model Context Protocol Server

A hybrid MCP server that gives AI assistants deep read/write access to Unreal Engine projects. Works in two modes:

- **Offline mode** — Parses raw `.uasset` / `.umap` binaries using [UAssetAPI](https://github.com/atenfyr/UAssetAPI). Reads C++ headers, config files, and module structure. No Unreal Editor required.
- **Live mode** — Connects to a running Unreal Editor via a WebSocket bridge plugin for full read/write access with undo, compilation, runtime reflection, and PIE introspection.

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

### Reflection (Live Mode)

| Tool | Mode | Description |
|------|------|-------------|
| `reflect_class` | Live | Reflect a UClass: parent chain, properties, functions, flags, interfaces |
| `reflect_struct` | Live | Reflect a UScriptStruct: fields with types |
| `reflect_enum` | Live | Reflect a UEnum: all values with display names |
| `list_classes` | Live | List classes, optionally filtered by parent class |
| `list_gameplay_tags` | Live | Full GameplayTag hierarchy, filterable by prefix |

### Node Discovery (Live Mode)

| Tool | Mode | Description |
|------|------|-------------|
| `list_node_types` | Live | List Blueprint node types by category (Flow Control, Events, Functions, etc.) |
| `search_node_types` | Live | Search for node types by name or description |

### Config / INI

| Tool | Mode | Description |
|------|------|-------------|
| `read_config` | Offline | Parse a config/INI file: sections and key-value pairs |
| `search_config` | Offline | Search all config files for keys, values, or sections |
| `list_config_tags` | Offline | GameplayTags defined in config files (offline alternative to `list_gameplay_tags`) |

### C++ Source

| Tool | Mode | Description |
|------|------|-------------|
| `read_cpp_header` | Offline | Parse a .h file for UCLASS, USTRUCT, UENUM, UPROPERTY, UFUNCTION |
| `read_module` | Offline | Module structure: Build.cs deps, headers, sources |
| `list_modules` | Offline | All C++ modules with types, file counts, dependencies |
| `search_cpp` | Offline | Search C++ source for symbols, macros, or text |

### Level / World (Live Mode)

| Tool | Mode | Description |
|------|------|-------------|
| `get_world_outliner` | Live | List all actors in the level with class, transform, folder |
| `place_actor` | Live | Spawn an actor with class, position, rotation, label |
| `delete_actor` | Live | Remove an actor by name or label |
| `get_actor_details` | Live | Detailed actor info: components, tags, properties |
| `move_actor` | Live | Relocate, rotate, or rescale an actor |

### Materials (Live Mode)

| Tool | Mode | Description |
|------|------|-------------|
| `read_material` | Live | Read material structure: parent, shading model, parameters |
| `list_material_parameters` | Live | All overridable parameters with current values |
| `set_material_parameter` | Live | Set scalar, vector, or texture parameter on a material instance |
| `create_material_instance` | Live | Create a new material instance from a parent |

### Editor (Live Mode)

| Tool | Mode | Description |
|------|------|-------------|
| `editor_execute` | Live | Run a console command in the editor |
| `set_property` | Live | Set a property value with undo support |
| `undo` | Live | Undo last editor action(s) — reverts any mutation |
| `redo` | Live | Redo last undone action(s) |
| `compile_blueprint` | Live | Compile a Blueprint and get error feedback |
| `create_blueprint` | Live | Create a new Blueprint with specified parent class |
| `add_blueprint_variable` | Live | Add a variable to a Blueprint |
| `add_blueprint_node` | Live | Add a node to a Blueprint graph |
| `connect_blueprint_pins` | Live | Wire two pins between nodes |
| `play_in_editor` | Live | Start/stop/query PIE sessions |
| `get_runtime_value` | Live | Read actor property values during PIE |
| `save_asset` | Live | Save one or all modified assets |

## Ontology

The `.kantext/` directory contains a compositional ontology that models UE concepts, the MCP's tool surface, cross-cutting traits, and development workflows:

| File | Purpose |
|------|---------|
| `Kantext.kant` | Root config + MCP identity + signal definitions |
| `UEConcepts.kant` | Asset taxonomy, type system, relationships, module system, config system |
| `BlueprintOntology.kant` | Blueprint internal anatomy + editor state machines |
| `Traits.kant` | Cross-cutting concerns: replication, serialization, GC, threading, Blueprint exposure |
| `Workflows.kant` | Common development workflows as tool-call sequences |
| `McpSurface.kant` | Tool surface with discovery links back to concepts and workflows |

The ontology follows a reflection-first design — concepts are organized around what the tools discover, with `discover_via` links connecting concepts to the tools that reveal them.

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
│   │   ├── AssetSearch.cs        # Project-wide asset search
│   │   ├── ConfigReader.cs       # Config/INI file parser
│   │   └── CppHeaderParser.cs   # C++ header UE macro parser
│   ├── Live/
│   │   ├── EditorBridge.cs       # WebSocket client to UE editor
│   │   └── BridgeMessage.cs      # Bridge protocol message types
│   └── Tools/
│       ├── StatusTools.cs        # get_status, set_project
│       ├── AssetTools.cs         # read_asset, list_assets, search_assets
│       ├── BlueprintTools.cs     # read_blueprint, list_variables/functions
│       ├── DataTableTools.cs     # read_datatable
│       ├── ReflectionTools.cs    # reflect_class, reflect_struct, reflect_enum
│       ├── ConfigTools.cs        # read_config, search_config, list_config_tags
│       ├── CppTools.cs           # read_cpp_header, read_module, list_modules
│       ├── NodeTools.cs          # list_node_types, search_node_types
│       ├── LevelTools.cs         # get_world_outliner, place_actor, move_actor
│       ├── MaterialTools.cs      # read_material, set_material_parameter
│       └── EditorTools.cs        # Live editor ops, undo/redo
├── plugin/ue_mcp_bridge/         # UE Editor Python plugin
│   ├── bridge_server.py          # WebSocket server
│   ├── handlers/
│   │   ├── asset.py              # Asset reading via editor reflection
│   │   ├── blueprint.py          # Blueprint CRUD operations
│   │   ├── editor.py             # Console commands, property setting
│   │   ├── pie.py                # Play-in-Editor control
│   │   ├── reflection.py         # Live type reflection (classes, structs, enums, tags)
│   │   ├── nodes.py              # Blueprint node type discovery
│   │   ├── level.py              # World outliner, actor placement/manipulation
│   │   └── material.py           # Material reading, parameter setting
│   └── startup_script.py         # Auto-start for editor
├── .kantext/                     # Ontology (Kantext compositional language)
│   ├── Kantext.kant              # Root config + signals
│   ├── UEConcepts.kant           # Asset taxonomy + type system + modules + config
│   ├── BlueprintOntology.kant    # Blueprint anatomy + editor states
│   ├── Traits.kant               # Cross-cutting: replication, serialization, GC, threading
│   ├── Workflows.kant            # Development workflow patterns
│   └── McpSurface.kant           # Tool surface with discovery links
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
