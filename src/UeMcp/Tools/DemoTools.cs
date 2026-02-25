using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class DemoTools
{
    [McpServerTool, Description(
        "Build a complete 'Neon Shrine' demo scene from nothing in one call. " +
        "Creates and opens a new level at /Game/Demo/DemoLevel, then fills it with: " +
        "a dark reflective floor, glowing golden sphere on a pedestal, " +
        "4 pillars with accent orbs, cyan/magenta/amber/violet neon point lights, " +
        "directional moonlight, exponential fog, post-process bloom, and camera aim. " +
        "All actors under 'Demo_Scene' outliner folder. Run demo_cleanup to tear it all down.")]
    public static async Task<string> demo_scene_from_nothing(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("demo_scene_from_nothing");
        return await bridge.SendAndSerializeAsync("demo_scene_from_nothing", new());
    }

    [McpServerTool, Description(
        "Tear down the demo: switches away from the demo level, deletes all Demo_ actors, " +
        "demo materials, and the demo level asset itself. Leaves zero trace.")]
    public static async Task<string> demo_cleanup(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("demo_cleanup");
        return await bridge.SendAndSerializeAsync("demo_cleanup");
    }
}
