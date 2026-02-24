using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class LandscapeTools
{
    [McpServerTool, Description(
        "Read the level's landscape setup: component count, section size, resolution, material, location.")]
    public static async Task<string> get_landscape_info(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("get_landscape_info");
        return await bridge.SendAndSerializeAsync("get_landscape_info", new());
    }

    [McpServerTool, Description(
        "List all paint/weight layers on the landscape with their layer info, physical materials, and blend type.")]
    public static async Task<string> list_landscape_layers(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("list_landscape_layers");
        return await bridge.SendAndSerializeAsync("list_landscape_layers", new());
    }

    [McpServerTool, Description(
        "Sample the landscape at world coordinates. Returns height, normal, and layer weights. " +
        "Essential for understanding terrain without visual inspection. Accepts a single point or array.")]
    public static async Task<string> sample_landscape(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Single point as JSON {\"x\": 0, \"y\": 0} or array of points [{\"x\": 0, \"y\": 0}, ...]")] string points)
    {
        router.EnsureLiveMode("sample_landscape");
        try
        {
            var parsed = JsonSerializer.Deserialize<object>(points);
            if (parsed is JsonElement elem && elem.ValueKind == JsonValueKind.Array)
                return await bridge.SendAndSerializeAsync("sample_landscape", new() { ["points"] = parsed });
            else
                return await bridge.SendAndSerializeAsync("sample_landscape", new() { ["point"] = parsed });
        }
        catch
        {
            return await bridge.SendAndSerializeAsync("sample_landscape", new() { ["point"] = JsonSerializer.Deserialize<object>(points) });
        }
    }

    [McpServerTool, Description(
        "Read landscape spline data: control points with positions and tangents.")]
    public static async Task<string> list_landscape_splines(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("list_landscape_splines");
        return await bridge.SendAndSerializeAsync("list_landscape_splines", new());
    }

    [McpServerTool, Description(
        "Inspect a specific landscape component by grid coordinates: heightmap range, layers, LOD.")]
    public static async Task<string> get_landscape_component(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Section X coordinate")] int sectionX,
        [Description("Section Y coordinate")] int sectionY)
    {
        router.EnsureLiveMode("get_landscape_component");
        return await bridge.SendAndSerializeAsync("get_landscape_component", new()
        {
            ["sectionX"] = sectionX,
            ["sectionY"] = sectionY
        });
    }

    [McpServerTool, Description(
        "Sculpt the landscape heightmap at a location. Operations: raise, lower, smooth, flatten, noise.")]
    public static async Task<string> sculpt_landscape(
        ModeRouter router,
        EditorBridge bridge,
        [Description("World location as JSON: {\"x\": 0, \"y\": 0}")] string location,
        [Description("Brush radius in world units")] float radius = 500,
        [Description("Brush strength (0.0 to 1.0)")] float strength = 0.5f,
        [Description("Operation: 'raise', 'lower', 'smooth', 'flatten', 'noise'")] string operation = "raise",
        [Description("Falloff (0.0 to 1.0). Default: 0.5")] float falloff = 0.5f)
    {
        router.EnsureLiveMode("sculpt_landscape");
        return await bridge.SendAndSerializeAsync("sculpt_landscape", new()
        {
            ["location"] = JsonSerializer.Deserialize<object>(location),
            ["radius"] = radius,
            ["strength"] = strength,
            ["operation"] = operation,
            ["falloff"] = falloff
        });
    }

    [McpServerTool, Description(
        "Paint a weight layer on the landscape at a location. Use list_landscape_layers to find valid layer names.")]
    public static async Task<string> paint_landscape_layer(
        ModeRouter router,
        EditorBridge bridge,
        [Description("World location as JSON: {\"x\": 0, \"y\": 0}")] string location,
        [Description("Layer name to paint")] string layerName,
        [Description("Brush radius in world units")] float radius = 500,
        [Description("Brush strength (0.0 to 1.0)")] float strength = 1.0f,
        [Description("Falloff (0.0 to 1.0). Default: 0.5")] float falloff = 0.5f)
    {
        router.EnsureLiveMode("paint_landscape_layer");
        return await bridge.SendAndSerializeAsync("paint_landscape_layer", new()
        {
            ["location"] = JsonSerializer.Deserialize<object>(location),
            ["layerName"] = layerName,
            ["radius"] = radius,
            ["strength"] = strength,
            ["falloff"] = falloff
        });
    }

    [McpServerTool, Description(
        "Set the landscape's material or material instance.")]
    public static async Task<string> set_landscape_material(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the material")] string materialPath)
    {
        router.EnsureLiveMode("set_landscape_material");
        return await bridge.SendAndSerializeAsync("set_landscape_material", new()
        {
            ["materialPath"] = materialPath
        });
    }

    [McpServerTool, Description(
        "Register a new paint layer on the landscape with a layer info asset.")]
    public static async Task<string> add_landscape_layer_info(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Name for the new layer")] string layerName,
        [Description("Optional: physical material asset path")] string? physMaterialPath = null)
    {
        router.EnsureLiveMode("add_landscape_layer_info");
        return await bridge.SendAndSerializeAsync("add_landscape_layer_info", new()
        {
            ["layerName"] = layerName,
            ["physMaterialPath"] = physMaterialPath
        });
    }

    [McpServerTool, Description(
        "Import a heightmap from a raw/png file and apply it to the landscape.")]
    public static async Task<string> import_landscape_heightmap(
        ModeRouter router,
        EditorBridge bridge,
        [Description("File path to the heightmap (raw or png)")] string filePath)
    {
        router.EnsureLiveMode("import_landscape_heightmap");
        return await bridge.SendAndSerializeAsync("import_landscape_heightmap", new()
        {
            ["filePath"] = filePath
        });
    }
}
