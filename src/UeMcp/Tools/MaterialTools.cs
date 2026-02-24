using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class MaterialTools
{
    [McpServerTool, Description(
        "Read a material or material instance: parent, shading model, blend mode, parameter overrides, " +
        "and expression types. Works on both base Materials and MaterialInstanceConstants.")]
    public static async Task<string> read_material(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the material (e.g. '/Game/Materials/M_Base', '/Game/Materials/MI_Wood')")] string assetPath)
    {
        router.EnsureLiveMode("read_material");
        return await bridge.SendAndSerializeAsync("read_material", new()
        {
            ["assetPath"] = assetPath
        });
    }

    [McpServerTool, Description(
        "List all overridable parameters on a material: scalar, vector, and texture parameters " +
        "with their current values. Works on both base Materials and MaterialInstances.")]
    public static async Task<string> list_material_parameters(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the material")] string assetPath)
    {
        router.EnsureLiveMode("list_material_parameters");
        return await bridge.SendAndSerializeAsync("list_material_parameters", new()
        {
            ["assetPath"] = assetPath
        });
    }

    [McpServerTool, Description(
        "Set a parameter on a material instance. Supports scalar (number), vector ({r,g,b,a}), " +
        "and texture (asset path string) parameters. Only works on MaterialInstances, not base Materials.")]
    public static async Task<string> set_material_parameter(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the material instance")] string assetPath,
        [Description("Parameter name to set")] string parameterName,
        [Description("Value: number for scalar, JSON {r,g,b,a} for vector, asset path string for texture")] string value)
    {
        router.EnsureLiveMode("set_material_parameter");

        object? parsedValue = ParseParameterValue(value);

        return await bridge.SendAndSerializeAsync("set_material_parameter", new()
        {
            ["assetPath"] = assetPath,
            ["parameterName"] = parameterName,
            ["value"] = parsedValue
        });
    }

    [McpServerTool, Description(
        "Create a new material instance from a parent material. " +
        "The instance inherits all parameters and can override individual values.")]
    public static async Task<string> create_material_instance(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the parent material")] string parentPath,
        [Description("Desired asset path for the new instance (e.g. '/Game/Materials/MI_Wood_Dark')")] string instancePath)
    {
        router.EnsureLiveMode("create_material_instance");
        return await bridge.SendAndSerializeAsync("create_material_instance", new()
        {
            ["parentPath"] = parentPath,
            ["instancePath"] = instancePath
        });
    }

    private static object? ParseParameterValue(string value)
    {
        if (double.TryParse(value, out var number))
            return number;

        try
        {
            var doc = System.Text.Json.JsonDocument.Parse(value);
            var root = doc.RootElement;
            if (root.TryGetProperty("r", out _))
            {
                return new Dictionary<string, object?>
                {
                    ["r"] = root.GetProperty("r").GetDouble(),
                    ["g"] = root.GetProperty("g").GetDouble(),
                    ["b"] = root.GetProperty("b").GetDouble(),
                    ["a"] = root.TryGetProperty("a", out var a) ? a.GetDouble() : 1.0
                };
            }
        }
        catch { }

        return value;
    }
}
