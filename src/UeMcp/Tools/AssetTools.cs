using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;
using UeMcp.Offline;

namespace UeMcp.Tools;

[McpServerToolType]
public static class AssetTools
{
    [McpServerTool, Description(
        "Read an Unreal Engine asset file and return its full structure as JSON. " +
        "Works with any asset type: Blueprints, DataTables, Materials, Textures, etc. " +
        "Shows all exports, imports, and property values. " +
        "In offline mode, parses the raw binary. In live mode, uses editor reflection.")]
    public static async Task<string> read_asset(
        ModeRouter router,
        AssetService assetService,
        EditorBridge bridge,
        [Description("Path to the asset. Can be: absolute path, relative to Content dir, or a /Game/ path")] string assetPath)
    {
        router.EnsureProjectLoaded();
        var resolved = router.ResolveAssetPath(assetPath);

        if (router.CurrentMode == OperationMode.Live)
        {
            try
            {
                return await bridge.SendAndSerializeAsync("read_asset", new() { ["path"] = assetPath });
            }
            catch { /* fall through to offline */ }
        }

        return assetService.ReadAssetToJson(resolved);
    }

    [McpServerTool, Description(
        "Read specific properties from a named export within an asset. " +
        "More targeted than read_asset — use when you know which export and properties you need.")]
    public static string read_asset_properties(
        ModeRouter router,
        AssetService assetService,
        [Description("Path to the asset")] string assetPath,
        [Description("Name of the export to read (e.g. 'Default__MyBP_C')")] string exportName,
        [Description("Optional: specific property name to read. Omit to get all properties.")] string? propertyName = null)
    {
        router.EnsureProjectLoaded();
        var resolved = router.ResolveAssetPath(assetPath);
        var asset = assetService.LoadAsset(resolved);

        var export = asset.Exports
            .OfType<UAssetAPI.ExportTypes.NormalExport>()
            .FirstOrDefault(e => e.ObjectName?.ToString() == exportName)
            ?? throw new KeyNotFoundException($"Export '{exportName}' not found in asset");

        if (propertyName != null)
        {
            var prop = export.Data?.FirstOrDefault(p => p.Name?.ToString() == propertyName)
                ?? throw new KeyNotFoundException($"Property '{propertyName}' not found in export '{exportName}'");

            return JsonSerializer.Serialize(assetService.DescribeProperty(prop),
                new JsonSerializerOptions { WriteIndented = true });
        }

        return JsonSerializer.Serialize(assetService.DescribeProperties(export.Data),
            new JsonSerializerOptions { WriteIndented = true });
    }

    [McpServerTool, Description(
        "List all assets in a directory within the project's Content folder. " +
        "Optionally filter by asset class type (e.g. 'Blueprint', 'DataTable', 'Material').")]
    public static string list_assets(
        ModeRouter router,
        AssetSearch search,
        [Description("Directory path relative to Content, or absolute. Omit to list all.")] string? directory = null,
        [Description("Filter by asset class type (e.g. 'Blueprint', 'DataTable', 'Material')")] string? typeFilter = null,
        [Description("Search subdirectories. Default: true")] bool recursive = true)
    {
        router.EnsureProjectLoaded();
        return search.ListAssets(directory, typeFilter, recursive);
    }

    [McpServerTool, Description(
        "Search for assets by name or content across the entire project. " +
        "Searches file names, export names, class types, and property values.")]
    public static string search_assets(
        ModeRouter router,
        AssetSearch search,
        [Description("Search query — matches against file names, export names, and property values")] string query,
        [Description("Optional: limit search to a specific directory")] string? directory = null,
        [Description("Maximum number of results to return. Default: 50")] int maxResults = 50)
    {
        router.EnsureProjectLoaded();
        return search.SearchAssets(query, directory, maxResults);
    }

    [McpServerTool, Description(
        "Export an asset's full contents to JSON using UAssetAPI's native serialization. " +
        "This gives you the most detailed, low-level view of the asset.")]
    public static string asset_to_json(
        ModeRouter router,
        AssetService assetService,
        [Description("Path to the asset")] string assetPath)
    {
        router.EnsureProjectLoaded();
        var resolved = router.ResolveAssetPath(assetPath);
        var asset = assetService.LoadAsset(resolved);

        return asset.SerializeJson(true);
    }
}
