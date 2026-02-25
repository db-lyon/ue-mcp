using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class LevelManagementTools
{
    [McpServerTool, Description(
        "Get information about the currently loaded level: path, name, and streaming sublevels with their load/visibility state.")]
    public static async Task<string> get_current_level(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("get_current_level");
        return await bridge.SendAndSerializeAsync("get_current_level");
    }

    [McpServerTool, Description(
        "Load/open a level in the editor by its asset path.")]
    public static async Task<string> load_level(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path of the level to load (e.g. '/Game/Maps/MainLevel')")] string path)
    {
        router.EnsureLiveMode("load_level");
        return await bridge.SendAndSerializeAsync("load_level", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "Save the currently loaded level.")]
    public static async Task<string> save_current_level(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("save_current_level");
        return await bridge.SendAndSerializeAsync("save_current_level");
    }

    [McpServerTool, Description(
        "List all level/map assets (World type) in a directory.")]
    public static async Task<string> list_levels(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/Maps'). Default: '/Game'")] string directory = "/Game")
    {
        router.EnsureLiveMode("list_levels");
        return await bridge.SendAndSerializeAsync("list_levels", new() { ["directory"] = directory });
    }

    [McpServerTool, Description(
        "Create a new empty level.")]
    public static async Task<string> create_new_level(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path for the new level")] string path,
        [Description("Level template: 'Default'. Default: 'Default'")] string template = "Default")
    {
        router.EnsureLiveMode("create_new_level");
        return await bridge.SendAndSerializeAsync("create_new_level", new()
        {
            ["path"] = path,
            ["template"] = template
        });
    }
}
