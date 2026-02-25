using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class SplineTools
{
    [McpServerTool, Description(
        "Get information about a spline component on an actor: point count, length, " +
        "whether it's closed, and all point locations.")]
    public static async Task<string> get_spline_info(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label of the actor with a SplineComponent")] string actorLabel)
    {
        router.EnsureLiveMode("get_spline_info");
        return await bridge.SendAndSerializeAsync("get_spline_info", new() { ["actorLabel"] = actorLabel });
    }

    [McpServerTool, Description(
        "Set the spline points on an actor's SplineComponent. Replaces all existing points. " +
        "Points are specified as an array of [x, y, z] arrays in world space.")]
    public static async Task<string> set_spline_points(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label of the actor with a SplineComponent")] string actorLabel,
        [Description("Array of points as [[x,y,z], [x,y,z], ...] in JSON")] string points)
    {
        router.EnsureLiveMode("set_spline_points");
        var parsed = JsonSerializer.Deserialize<double[][]>(points) ?? [];
        return await bridge.SendAndSerializeAsync("set_spline_points", new()
        {
            ["actorLabel"] = actorLabel,
            ["points"] = parsed
        });
    }
}
