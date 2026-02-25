using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class SequencerTools
{
    [McpServerTool, Description(
        "Create a new Level Sequence asset for cinematics, cutscenes, or animation. " +
        "After creation, use add_sequence_track to bind actors and add animation tracks.")]
    public static async Task<string> create_level_sequence(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path for the new sequence (e.g. '/Game/Cinematics/SEQ_Intro')")] string path)
    {
        router.EnsureLiveMode("create_level_sequence");
        return await bridge.SendAndSerializeAsync("create_level_sequence", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "Get information about a Level Sequence: display rate, bindings, tracks, and master tracks.")]
    public static async Task<string> get_sequence_info(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Level Sequence")] string path)
    {
        router.EnsureLiveMode("get_sequence_info");
        return await bridge.SendAndSerializeAsync("get_sequence_info", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "Add a track or possessable actor binding to a Level Sequence. " +
        "Provide actorLabel to bind an existing world actor, or trackType for a master track.")]
    public static async Task<string> add_sequence_track(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Level Sequence")] string path,
        [Description("Optional: Track class type for master tracks (e.g. 'MovieSceneAudioTrack')")] string? trackType = null,
        [Description("Optional: Label of an actor in the level to bind as a possessable")] string? actorLabel = null)
    {
        router.EnsureLiveMode("add_sequence_track");
        return await bridge.SendAndSerializeAsync("add_sequence_track", new()
        {
            ["path"] = path,
            ["trackType"] = trackType ?? "",
            ["actorLabel"] = actorLabel ?? ""
        });
    }

    [McpServerTool, Description(
        "Control playback of the Sequencer. Play, stop, or pause the active sequence.")]
    public static async Task<string> play_sequence(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Action: 'play', 'stop', or 'pause'")] string action)
    {
        router.EnsureLiveMode("play_sequence");
        return await bridge.SendAndSerializeAsync("play_sequence", new() { ["action"] = action });
    }
}
