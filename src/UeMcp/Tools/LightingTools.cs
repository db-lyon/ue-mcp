using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class LightingTools
{
    [McpServerTool, Description(
        "Spawn a light actor in the level. Supports point, spot, directional, rect, and sky lights.")]
    public static async Task<string> spawn_light(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Light type: 'point', 'spot', 'directional', 'rect', or 'sky'")] string lightType,
        [Description("World location as [x, y, z]. Default: [0,0,0]")] string? location = null,
        [Description("Rotation as [pitch, yaw, roll]. Default: [0,0,0]")] string? rotation = null,
        [Description("Light intensity value")] double? intensity = null,
        [Description("Light color as [R, G, B] (0-255)")] string? color = null,
        [Description("Actor label")] string? label = null)
    {
        router.EnsureLiveMode("spawn_light");
        var parameters = new Dictionary<string, object?>
        {
            ["lightType"] = lightType,
            ["location"] = ParseArray(location, [0, 0, 0]),
            ["rotation"] = ParseArray(rotation, [0, 0, 0]),
            ["label"] = label ?? ""
        };
        if (intensity.HasValue) parameters["intensity"] = intensity.Value;
        if (color != null) parameters["color"] = ParseArray(color, [255, 255, 255]);

        return await bridge.SendAndSerializeAsync("spawn_light", parameters);
    }

    [McpServerTool, Description(
        "Set properties on a light actor: intensity, color, temperature, attenuation radius, " +
        "shadows, cone angles. Specify only the properties you want to change.")]
    public static async Task<string> set_light_properties(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label of the light")] string actorLabel,
        [Description("Light intensity")] double? intensity = null,
        [Description("Color as [R, G, B] (0-255)")] string? color = null,
        [Description("Color temperature in Kelvin")] double? temperature = null,
        [Description("Attenuation radius")] double? attenuationRadius = null,
        [Description("Enable shadow casting")] bool? castShadows = null,
        [Description("Inner cone angle for spot lights")] double? innerConeAngle = null,
        [Description("Outer cone angle for spot lights")] double? outerConeAngle = null)
    {
        router.EnsureLiveMode("set_light_properties");
        var parameters = new Dictionary<string, object?> { ["actorLabel"] = actorLabel };
        if (intensity.HasValue) parameters["intensity"] = intensity.Value;
        if (color != null) parameters["color"] = ParseArray(color, [255, 255, 255]);
        if (temperature.HasValue) parameters["temperature"] = temperature.Value;
        if (attenuationRadius.HasValue) parameters["attenuationRadius"] = attenuationRadius.Value;
        if (castShadows.HasValue) parameters["castShadows"] = castShadows.Value;
        if (innerConeAngle.HasValue) parameters["innerConeAngle"] = innerConeAngle.Value;
        if (outerConeAngle.HasValue) parameters["outerConeAngle"] = outerConeAngle.Value;

        return await bridge.SendAndSerializeAsync("set_light_properties", parameters);
    }

    [McpServerTool, Description(
        "Build/rebuild lighting for the current level. Quality options: Preview, Medium, High, Production.")]
    public static async Task<string> build_lighting(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Build quality: 'Preview', 'Medium', 'High', or 'Production'. Default: 'Preview'")] string quality = "Preview")
    {
        router.EnsureLiveMode("build_lighting");
        return await bridge.SendAndSerializeAsync("build_lighting", new() { ["quality"] = quality });
    }

    private static double[] ParseArray(string? json, double[] fallback)
    {
        if (string.IsNullOrWhiteSpace(json)) return fallback;
        try
        {
            var arr = JsonSerializer.Deserialize<double[]>(json);
            return arr ?? fallback;
        }
        catch { return fallback; }
    }
}
