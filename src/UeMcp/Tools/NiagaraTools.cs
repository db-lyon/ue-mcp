using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class NiagaraTools
{
    [McpServerTool, Description(
        "List all Niagara Systems and Emitters in a directory.")]
    public static async Task<string> list_niagara_systems(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/VFX'). Default: '/Game'")] string directory = "/Game")
    {
        router.EnsureLiveMode("list_niagara_systems");
        return await bridge.SendAndSerializeAsync("list_niagara_systems", new() { ["directory"] = directory });
    }

    [McpServerTool, Description(
        "Get information about a Niagara System: emitter handles, class, and parameters.")]
    public static async Task<string> get_niagara_info(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Niagara System")] string path)
    {
        router.EnsureLiveMode("get_niagara_info");
        return await bridge.SendAndSerializeAsync("get_niagara_info", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "Spawn a Niagara particle system at a world location. Useful for previewing VFX placement.")]
    public static async Task<string> spawn_niagara_at_location(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Niagara System")] string systemPath,
        [Description("World location as [x, y, z]")] string? location = null,
        [Description("Rotation as [pitch, yaw, roll]")] string? rotation = null,
        [Description("Auto destroy after completion. Default: true")] bool autoDestroy = true)
    {
        router.EnsureLiveMode("spawn_niagara_at_location");
        return await bridge.SendAndSerializeAsync("spawn_niagara_at_location", new()
        {
            ["systemPath"] = systemPath,
            ["location"] = ParseArray(location, [0, 0, 0]),
            ["rotation"] = ParseArray(rotation, [0, 0, 0]),
            ["autoDestroy"] = autoDestroy
        });
    }

    [McpServerTool, Description(
        "Set a parameter on a Niagara Component attached to an actor in the level. " +
        "Supports float, bool, and vector (3-element array) values.")]
    public static async Task<string> set_niagara_parameter(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label in the level that has a NiagaraComponent")] string actorLabel,
        [Description("Name of the Niagara parameter")] string parameterName,
        [Description("Value: number, bool, or [x, y, z] array")] string value)
    {
        router.EnsureLiveMode("set_niagara_parameter");
        return await bridge.SendAndSerializeAsync("set_niagara_parameter", new()
        {
            ["actorLabel"] = actorLabel,
            ["parameterName"] = parameterName,
            ["value"] = ParseValue(value)
        });
    }

    private static double[] ParseArray(string? json, double[] fallback)
    {
        if (string.IsNullOrWhiteSpace(json)) return fallback;
        try
        {
            var arr = System.Text.Json.JsonSerializer.Deserialize<double[]>(json);
            return arr ?? fallback;
        }
        catch { return fallback; }
    }

    private static object ParseValue(string value)
    {
        if (bool.TryParse(value, out var b)) return b;
        if (double.TryParse(value, out var d)) return d;
        try
        {
            var arr = System.Text.Json.JsonSerializer.Deserialize<double[]>(value);
            if (arr != null) return arr;
        }
        catch { }
        return value;
    }
}
