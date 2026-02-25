using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class AudioTools
{
    [McpServerTool, Description(
        "List all sound assets (SoundWave, SoundCue, MetaSoundSource, SoundMix, SoundClass) in a directory.")]
    public static async Task<string> list_sound_assets(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/Audio'). Default: '/Game'")] string directory = "/Game")
    {
        router.EnsureLiveMode("list_sound_assets");
        return await bridge.SendAndSerializeAsync("list_sound_assets", new() { ["directory"] = directory });
    }

    [McpServerTool, Description(
        "Play a sound asset at a world location. Useful for previewing audio placement.")]
    public static async Task<string> play_sound_at_location(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the sound (SoundWave or SoundCue)")] string soundPath,
        [Description("World location as [x, y, z]. Default: [0,0,0]")] string? location = null,
        [Description("Volume multiplier. Default: 1.0")] double volume = 1.0,
        [Description("Pitch multiplier. Default: 1.0")] double pitch = 1.0)
    {
        router.EnsureLiveMode("play_sound_at_location");
        return await bridge.SendAndSerializeAsync("play_sound_at_location", new()
        {
            ["soundPath"] = soundPath,
            ["location"] = ParseArray(location, [0, 0, 0]),
            ["volume"] = volume,
            ["pitch"] = pitch
        });
    }

    [McpServerTool, Description(
        "Spawn an AmbientSound actor in the level with a given sound asset.")]
    public static async Task<string> spawn_ambient_sound(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the sound")] string soundPath,
        [Description("World location as [x, y, z]. Default: [0,0,0]")] string? location = null,
        [Description("Actor label")] string? label = null,
        [Description("Volume multiplier. Default: 1.0")] double volume = 1.0)
    {
        router.EnsureLiveMode("spawn_ambient_sound");
        return await bridge.SendAndSerializeAsync("spawn_ambient_sound", new()
        {
            ["soundPath"] = soundPath,
            ["location"] = ParseArray(location, [0, 0, 0]),
            ["label"] = label ?? "",
            ["volume"] = volume
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
