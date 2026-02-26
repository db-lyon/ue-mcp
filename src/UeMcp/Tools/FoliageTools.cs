using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class FoliageTools
{
    [McpServerTool, Description(
        "List all foliage types used in the level: mesh reference, instance count, key settings.")]
    public static async Task<string> list_foliage_types(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("list_foliage_types");
        return await bridge.SendAndSerializeAsync("list_foliage_types", new());
    }

    [McpServerTool, Description(
        "Read full settings for a foliage type: mesh, density, scale range, ground slope angle, " +
        "height range, cull distance, collision, clustering, random rotation, landscape layer filter.")]
    public static async Task<string> get_foliage_type_settings(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the foliage type")] string foliageTypePath)
    {
        router.EnsureLiveMode("get_foliage_type_settings");
        return await bridge.SendAndSerializeAsync("get_foliage_type_settings", new()
        {
            ["foliageTypePath"] = foliageTypePath
        });
    }

    [McpServerTool, Description(
        "Query foliage instances in a region. Returns instance count and transforms. " +
        "Use to verify density before/after painting.")]
    public static async Task<string> sample_foliage(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Center point as JSON: {\"x\": 0, \"y\": 0, \"z\": 0}")] string center,
        [Description("Search radius in world units")] float radius = 1000,
        [Description("Optional: filter by foliage type name or mesh path")] string? foliageType = null,
        [Description("Maximum instances to return. Default: 100")] int limit = 100)
    {
        router.EnsureLiveMode("sample_foliage");
        return await bridge.SendAndSerializeAsync("sample_foliage", new()
        {
            ["center"] = JsonSerializer.Deserialize<object>(center),
            ["radius"] = radius,
            ["foliageType"] = foliageType,
            ["limit"] = limit
        });
    }

    [McpServerTool, Description(
        "Add foliage instances in a radius. Optionally restrict to specific landscape paint layers.")]
    public static async Task<string> paint_foliage(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Foliage type name or mesh asset path")] string foliageType,
        [Description("World location as JSON: {\"x\": 0, \"y\": 0, \"z\": 0}")] string location,
        [Description("Brush radius in world units")] float radius = 500,
        [Description("Density of instances to paint")] float density = 100,
        [Description("Optional: only paint on these landscape layers (JSON array of names)")] string? paintLayers = null)
    {
        router.EnsureLiveMode("paint_foliage");
        var parameters = new Dictionary<string, object?>
        {
            ["foliageType"] = foliageType,
            ["location"] = JsonSerializer.Deserialize<object>(location),
            ["radius"] = radius,
            ["density"] = density,
        };
        if (paintLayers != null)
            parameters["paintLayers"] = JsonSerializer.Deserialize<object>(paintLayers);
        return await bridge.SendAndSerializeAsync("paint_foliage", parameters);
    }

    [McpServerTool, Description(
        "Remove foliage instances in a radius. Optionally filter by foliage type.")]
    public static async Task<string> erase_foliage(
        ModeRouter router,
        EditorBridge bridge,
        [Description("World location as JSON: {\"x\": 0, \"y\": 0, \"z\": 0}")] string location,
        [Description("Erase radius in world units")] float radius = 500,
        [Description("Optional: only erase this foliage type")] string? foliageType = null)
    {
        router.EnsureLiveMode("erase_foliage");
        return await bridge.SendAndSerializeAsync("erase_foliage", new()
        {
            ["location"] = JsonSerializer.Deserialize<object>(location),
            ["radius"] = radius,
            ["foliageType"] = foliageType
        });
    }

    [McpServerTool, Description(
        "Create a new FoliageType asset from a StaticMesh. " +
        "After creation, use paint_foliage to scatter instances in the level.")]
    public static async Task<string> create_foliage_type(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset name (e.g. 'FT_TreeOak')")] string name = "FT_New",
        [Description("Package path (e.g. '/Game/Foliage')")] string packagePath = "/Game/Foliage",
        [Description("Optional: path to the StaticMesh to use")] string? meshPath = null)
    {
        router.EnsureLiveMode("create_foliage_type");
        return await bridge.SendAndSerializeAsync("create_foliage_type", new()
        {
            ["name"] = name,
            ["packagePath"] = packagePath,
            ["meshPath"] = meshPath ?? ""
        });
    }

    [McpServerTool, Description(
        "Modify settings on a foliage type: density, scale range, alignment, cull distance, etc. " +
        "Partial update — only specified keys change.")]
    public static async Task<string> set_foliage_type_settings(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the foliage type")] string foliageTypePath,
        [Description("Settings to update as JSON (partial update)")] string settings)
    {
        router.EnsureLiveMode("set_foliage_type_settings");
        return await bridge.SendAndSerializeAsync("set_foliage_type_settings", new()
        {
            ["foliageTypePath"] = foliageTypePath,
            ["settings"] = JsonSerializer.Deserialize<object>(settings)
        });
    }
}
