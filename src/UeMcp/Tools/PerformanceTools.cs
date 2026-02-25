using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class PerformanceTools
{
    [McpServerTool, Description(
        "Get editor performance stats: total actor count and top actor classes by count in the current level.")]
    public static async Task<string> get_editor_performance_stats(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("get_editor_performance_stats");
        return await bridge.SendAndSerializeAsync("get_editor_performance_stats");
    }

    [McpServerTool, Description(
        "Run a stat console command (e.g. 'stat fps', 'stat unit', 'stat scenerendering', 'stat memory'). " +
        "Toggles the stat overlay in the viewport.")]
    public static async Task<string> run_stat_command(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Stat command to run (e.g. 'stat fps', 'stat unit', 'stat memory')")] string command)
    {
        router.EnsureLiveMode("run_stat_command");
        return await bridge.SendAndSerializeAsync("run_stat_command", new() { ["command"] = command });
    }

    [McpServerTool, Description(
        "Set the scalability level for all rendering quality groups (shadows, AA, textures, etc.). " +
        "Levels: Low, Medium, High, Epic, Cinematic.")]
    public static async Task<string> set_scalability(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Quality level: 'Low', 'Medium', 'High', 'Epic', 'Cinematic'")] string level)
    {
        router.EnsureLiveMode("set_scalability");
        return await bridge.SendAndSerializeAsync("set_scalability", new() { ["level"] = level });
    }

    [McpServerTool, Description(
        "Capture a high-resolution screenshot from the active viewport.")]
    public static async Task<string> capture_screenshot(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Output filename (e.g. 'screenshot.png')")] string filename = "screenshot.png",
        [Description("Horizontal resolution. Default: 1920")] int resolutionX = 1920,
        [Description("Vertical resolution. Default: 1080")] int resolutionY = 1080)
    {
        router.EnsureLiveMode("capture_screenshot");
        return await bridge.SendAndSerializeAsync("capture_screenshot", new()
        {
            ["filename"] = filename,
            ["resolutionX"] = resolutionX,
            ["resolutionY"] = resolutionY
        });
    }

    [McpServerTool, Description(
        "Get the current editor viewport camera location and rotation.")]
    public static async Task<string> get_viewport_info(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("get_viewport_info");
        return await bridge.SendAndSerializeAsync("get_viewport_info");
    }

    [McpServerTool, Description(
        "Set the editor viewport camera position and rotation.")]
    public static async Task<string> set_viewport_camera(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Camera location as [x, y, z]")] string location,
        [Description("Camera rotation as [pitch, yaw, roll]")] string rotation)
    {
        router.EnsureLiveMode("set_viewport_camera");
        return await bridge.SendAndSerializeAsync("set_viewport_camera", new()
        {
            ["location"] = ParseArray(location),
            ["rotation"] = ParseArray(rotation)
        });
    }

    [McpServerTool, Description(
        "Focus the viewport camera on a specific actor in the level.")]
    public static async Task<string> focus_viewport_on_actor(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label to focus on")] string actorLabel)
    {
        router.EnsureLiveMode("focus_viewport_on_actor");
        return await bridge.SendAndSerializeAsync("focus_viewport_on_actor", new() { ["actorLabel"] = actorLabel });
    }

    private static double[] ParseArray(string json)
    {
        try
        {
            var arr = JsonSerializer.Deserialize<double[]>(json);
            return arr ?? [0, 0, 0];
        }
        catch { return [0, 0, 0]; }
    }
}
