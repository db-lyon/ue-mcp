using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class NavigationTools
{
    [McpServerTool, Description(
        "Rebuild the navigation mesh for the current level. Triggers a full navmesh regeneration.")]
    public static async Task<string> rebuild_navigation(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("rebuild_navigation");
        return await bridge.SendAndSerializeAsync("rebuild_navigation");
    }

    [McpServerTool, Description(
        "Get information about the current navigation system and nav data.")]
    public static async Task<string> get_navmesh_info(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("get_navmesh_info");
        return await bridge.SendAndSerializeAsync("get_navmesh_info");
    }

    [McpServerTool, Description(
        "Project a point onto the navigation mesh. Returns the nearest navigable location. " +
        "Useful for verifying AI-walkable positions.")]
    public static async Task<string> project_point_to_navigation(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Location as [x, y, z]")] string location,
        [Description("Query extent as [x, y, z]. Default: [100,100,100]")] string? queryExtent = null)
    {
        router.EnsureLiveMode("project_point_to_navigation");
        return await bridge.SendAndSerializeAsync("project_point_to_navigation", new()
        {
            ["location"] = ParseArray(location, [0, 0, 0]),
            ["queryExtent"] = ParseArray(queryExtent, [100, 100, 100])
        });
    }

    [McpServerTool, Description(
        "Spawn a Nav Modifier Volume in the level. Used to mark areas as walkable, " +
        "unwalkable, or with custom navigation cost.")]
    public static async Task<string> spawn_nav_modifier_volume(
        ModeRouter router,
        EditorBridge bridge,
        [Description("World location as [x, y, z]")] string? location = null,
        [Description("Volume extent as [x, y, z] in units")] string? extent = null,
        [Description("Actor label")] string? label = null)
    {
        router.EnsureLiveMode("spawn_nav_modifier_volume");
        return await bridge.SendAndSerializeAsync("spawn_nav_modifier_volume", new()
        {
            ["location"] = ParseArray(location, [0, 0, 0]),
            ["extent"] = ParseArray(extent, [200, 200, 200]),
            ["label"] = label ?? ""
        });
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
