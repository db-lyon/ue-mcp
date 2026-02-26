using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class DemoTools
{
    private static readonly string[] Steps =
    [
        "create_level",
        "materials",
        "floor",
        "pedestal",
        "hero_sphere",
        "pillars",
        "orbs",
        "neon_lights",
        "hero_light",
        "moonlight",
        "sky_light",
        "fog",
        "post_process",
        "niagara_vfx",
        "pcg_scatter",
        "orbit_rings",
        "level_sequence",
        "tuning_panel",
        "save",
    ];

    [McpServerTool, Description(
        "Build a complete 'Neon Shrine' demo scene step by step. Creates a new level, " +
        "spawns geometry/lights/atmosphere, then layers on Niagara VFX, PCG procedural scatter, " +
        "an animated orbit ring (RotatingMovementComponent — hit Play to see it spin), " +
        "a Level Sequence with actor bindings, and an Editor Utility Widget tuning panel. " +
        "Each element appears one at a time so you can watch the scene materialize.")]
    public static async Task<string> demo_scene_from_nothing(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Milliseconds to wait between steps. Default: 400")] int delayMs = 400)
    {
        router.EnsureLiveMode("demo_scene_from_nothing");

        var log = new List<string>();

        foreach (var step in Steps)
        {
            try
            {
                var response = await bridge.SendAsync("demo_step", new() { ["step"] = step });
                var msg = response.Result?.TryGetProperty("message", out var m) == true
                    ? m.GetString() : step;
                log.Add($"{step}: {msg}");
            }
            catch (Exception ex)
            {
                log.Add($"{step}: FAILED - {ex.Message}");
            }

            if (step != Steps[^1])
                await Task.Delay(delayMs);
        }

        return JsonSerializer.Serialize(new
        {
            scene = "Neon Shrine",
            level = "/Game/Demo/DemoLevel",
            stepsCompleted = log.Count(l => !l.Contains("FAILED")),
            totalSteps = Steps.Length,
            log,
        }, new JsonSerializerOptions { WriteIndented = true });
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
