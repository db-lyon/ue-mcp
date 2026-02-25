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
        "Places a dark reflective floor, a glowing golden sphere on a pedestal, " +
        "4 pillars with accent orbs, cyan/magenta/amber/violet neon point lights, " +
        "directional moonlight, exponential fog, post-process bloom, and aims the camera. " +
        "All actors are organized under the 'Demo_Scene' outliner folder. " +
        "Run demo_cleanup to tear it all down.")]
    public static async Task<string> demo_scene_from_nothing(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Remove previous Demo_ actors before building. Default: true")] bool clean = true)
    {
        router.EnsureLiveMode("demo_scene_from_nothing");
        return await bridge.SendAndSerializeAsync("demo_scene_from_nothing", new()
        {
            ["clean"] = clean
        });
    }

    [McpServerTool, Description(
        "Remove all actors and materials created by demo_scene_from_nothing. " +
        "Deletes every actor whose label starts with 'Demo_' and the demo material assets.")]
    public static async Task<string> demo_cleanup(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("demo_cleanup");
        return await bridge.SendAndSerializeAsync("demo_cleanup");
    }
}
