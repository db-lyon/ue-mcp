using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class AnimationTools
{
    [McpServerTool, Description(
        "Read an Animation Blueprint's structure: target skeleton, parent class, state machines, " +
        "variables, and groups. The entry point for understanding any AnimBP.")]
    public static async Task<string> read_anim_blueprint(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Anim Blueprint (e.g. '/Game/Characters/ABP_Hero')")] string assetPath)
    {
        router.EnsureLiveMode("read_anim_blueprint");
        return await bridge.SendAndSerializeAsync("read_anim_blueprint", new()
        {
            ["assetPath"] = assetPath
        });
    }

    [McpServerTool, Description(
        "Read an Animation Montage: sections with next-section links, notifies with timing, " +
        "slot anim tracks with segments, blend in/out times, rate scale.")]
    public static async Task<string> read_anim_montage(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Anim Montage")] string assetPath)
    {
        router.EnsureLiveMode("read_anim_montage");
        return await bridge.SendAndSerializeAsync("read_anim_montage", new()
        {
            ["assetPath"] = assetPath
        });
    }

    [McpServerTool, Description(
        "Read an Animation Sequence: length, frame count, rate, skeleton, notifies, curve names, " +
        "and whether it's additive.")]
    public static async Task<string> read_anim_sequence(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Anim Sequence")] string assetPath)
    {
        router.EnsureLiveMode("read_anim_sequence");
        return await bridge.SendAndSerializeAsync("read_anim_sequence", new()
        {
            ["assetPath"] = assetPath
        });
    }

    [McpServerTool, Description(
        "Read a BlendSpace: axis names, sample points with animations and coordinates, skeleton.")]
    public static async Task<string> read_blendspace(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the BlendSpace")] string assetPath)
    {
        router.EnsureLiveMode("read_blendspace");
        return await bridge.SendAndSerializeAsync("read_blendspace", new()
        {
            ["assetPath"] = assetPath
        });
    }

    [McpServerTool, Description(
        "List animation assets in a directory: montages, sequences, blendspaces, and anim Blueprints. " +
        "Use to survey the animation content in a project or folder.")]
    public static async Task<string> list_anim_assets(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/', '/Game/Characters/Animations/')")] string directory = "/Game/",
        [Description("Search subdirectories. Default: true")] bool recursive = true)
    {
        router.EnsureLiveMode("list_anim_assets");
        return await bridge.SendAndSerializeAsync("list_anim_assets", new()
        {
            ["directory"] = directory,
            ["recursive"] = recursive
        });
    }

    [McpServerTool, Description(
        "Add a notify event to an animation montage or sequence at a specific time. " +
        "Notifies trigger gameplay events during animation playback (footsteps, VFX, damage windows).")]
    public static async Task<string> add_anim_notify(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the animation")] string assetPath,
        [Description("Notify name")] string notifyName,
        [Description("Time in seconds when the notify triggers")] float triggerTime,
        [Description("Optional: notify class (e.g. 'AnimNotify_PlaySound', 'AnimNotify_PlayParticleEffect')")] string? notifyClass = null)
    {
        router.EnsureLiveMode("add_anim_notify");
        return await bridge.SendAndSerializeAsync("add_anim_notify", new()
        {
            ["assetPath"] = assetPath,
            ["notifyName"] = notifyName,
            ["triggerTime"] = triggerTime,
            ["notifyClass"] = notifyClass
        });
    }
}
