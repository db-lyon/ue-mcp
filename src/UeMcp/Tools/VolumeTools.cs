using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class VolumeTools
{
    [McpServerTool, Description(
        "Spawn a volume in the level. Supported types: trigger, blocking, painCausing, killZ, " +
        "audio, postProcess, lightmassImportance, cameraBlocking, navModifier, physics.")]
    public static async Task<string> spawn_volume(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Volume type: 'trigger', 'blocking', 'postProcess', 'audio', etc.")] string volumeType,
        [Description("World location as [x, y, z]. Default: [0,0,0]")] string? location = null,
        [Description("Scale as [x, y, z]. Default: [1,1,1]")] string? scale = null,
        [Description("Actor label")] string? label = null)
    {
        router.EnsureLiveMode("spawn_volume");
        return await bridge.SendAndSerializeAsync("spawn_volume", new()
        {
            ["volumeType"] = volumeType,
            ["location"] = ParseArray(location, [0, 0, 0]),
            ["scale"] = ParseArray(scale, [1, 1, 1]),
            ["label"] = label ?? ""
        });
    }

    [McpServerTool, Description(
        "List all volume actors in the current level with their type, location, and scale.")]
    public static async Task<string> list_volumes(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("list_volumes");
        return await bridge.SendAndSerializeAsync("list_volumes");
    }

    [McpServerTool, Description(
        "Set properties on a volume actor. Pass any editor property name/value pairs.")]
    public static async Task<string> set_volume_properties(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label of the volume")] string actorLabel,
        [Description("Properties as JSON object: {\"propertyName\": value, ...}")] string properties)
    {
        router.EnsureLiveMode("set_volume_properties");
        var parsed = JsonSerializer.Deserialize<Dictionary<string, object?>>(properties) ?? new();
        parsed["actorLabel"] = actorLabel;
        return await bridge.SendAndSerializeAsync("set_volume_properties", parsed);
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
