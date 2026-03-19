# Troubleshooting

## Connection Issues

### "Editor not connected" / Bridge not running

**Symptoms:** `project(action="get_status")` shows disconnected. Tools that require the editor return errors.

**Fixes:**

1. **Is the editor running?** The C++ bridge plugin only runs when the editor is open.
2. **Was the editor restarted after first setup?** The plugin is deployed on first run but needs an editor restart to load.
3. **Check the Output Log.** In the editor: **Window > Developer Tools > Output Log**, filter on `LogMCPBridge`. You should see:
   ```
   LogMCPBridge: UE-MCP Bridge server started on ws://localhost:9877
   ```
4. **Port conflict.** If another process is using port 9877, the bridge can't start. Check with:

    === "Windows"
        ```bash
        netstat -ano | findstr 9877
        ```

    === "macOS / Linux"
        ```bash
        lsof -i :9877
        ```

### Connection drops / reconnecting

The MCP server auto-reconnects every 15 seconds. If the editor is restarted, the connection will restore automatically.

If the connection is flapping (connecting then immediately disconnecting), check the editor's Output Log for errors in the `LogMCPBridge` category.

## Plugin Build Issues

### Plugin fails to compile

The C++ bridge links against many UE modules. If compilation fails:

1. **Missing plugins.** Ensure these are enabled in your `.uproject`:
    - `PythonScriptPlugin`
    - `EnhancedInput`
    - `GameplayAbilities`
    - `Niagara`
    - `PCG`

2. **UE version mismatch.** The plugin is tested with UE 5.4–5.7. Older versions may have API differences. Check the build log for specific errors.

3. **Rebuild from clean.** Delete `<Project>/Plugins/UE_MCP_Bridge/Binaries/` and `<Project>/Plugins/UE_MCP_Bridge/Intermediate/`, then rebuild.

### Plugin not loading

If the editor starts but the bridge doesn't appear in the Output Log:

1. Check **Edit > Plugins** in the editor — search for "UE_MCP_Bridge" and ensure it's enabled.
2. Check that the plugin is listed in your `.uproject`:
   ```json
   { "Name": "UE_MCP_Bridge", "Enabled": true }
   ```

## MCP Server Issues

### Server won't start

1. **Node.js version.** Requires Node 18+. Check with `node --version`.
2. **Build step.** Make sure you ran `npm run build` — the server runs from `dist/index.js`, not source.
3. **Path to .uproject.** The path must be absolute and point to a valid `.uproject` file.

### Tools return errors

- **"Bridge not connected"** — the editor isn't running or the plugin isn't loaded. See connection issues above.
- **"Handler not found"** — the action name might be wrong. Check the [Tool Reference](tool-reference.md) for valid action names.
- **"Asset not found"** — asset paths should use the `/Game/` prefix (e.g., `/Game/Blueprints/BP_Player`), not filesystem paths.
- **Timeout** — the default timeout is 30 seconds. Long operations (build lighting, cook content) may need patience.

## Asset Path Issues

UE-MCP expects Unreal-style asset paths:

| Format | Example |
|--------|---------|
| Content path | `/Game/Blueprints/BP_Player` |
| Plugin content | `/MyPlugin/Assets/SomeAsset` |
| Full object path | `/Game/Blueprints/BP_Player.BP_Player_C` |

!!! warning "Common mistakes"
    - Using filesystem paths (`C:/Users/.../Content/...`) — use `/Game/...` instead
    - Including file extensions (`.uasset`) — omit the extension
    - Missing the leading slash — `/Game/Foo`, not `Game/Foo`

## Search Not Finding Assets

If `asset(action="search")` misses assets in plugin directories:

1. Add the content root to `.ue-mcp.json`:
   ```json
   { "contentRoots": ["/Game/", "/MyPlugin/"] }
   ```
2. Wildcards work in search queries: `asset(action="search", query="/Game/Characters/*")`

## Logs

### Bridge logs
```
editor(action="get_log", category="LogMCPBridge")
```

### Full output log
```
editor(action="get_log")
```

### Search logs
```
editor(action="search_log", query="error")
```
