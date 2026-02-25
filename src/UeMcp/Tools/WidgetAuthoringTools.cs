using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class WidgetAuthoringTools
{
    [McpServerTool, Description(
        "Create a new Widget Blueprint (UMG) asset for UI.")]
    public static async Task<string> create_widget_blueprint(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path (e.g. '/Game/UI/WBP_MainMenu')")] string path,
        [Description("Parent class: 'UserWidget'. Default: 'UserWidget'")] string parentClass = "UserWidget")
    {
        router.EnsureLiveMode("create_widget_blueprint");
        return await bridge.SendAndSerializeAsync("create_widget_blueprint", new()
        {
            ["path"] = path,
            ["parentClass"] = parentClass
        });
    }

    [McpServerTool, Description(
        "List all Widget Blueprint assets in a directory.")]
    public static async Task<string> list_widget_blueprints(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/UI'). Default: '/Game'")] string directory = "/Game")
    {
        router.EnsureLiveMode("list_widget_blueprints");
        return await bridge.SendAndSerializeAsync("list_widget_blueprints", new() { ["directory"] = directory });
    }

    [McpServerTool, Description(
        "Get the widget hierarchy tree of a Widget Blueprint: root widget, classes, and structure.")]
    public static async Task<string> get_widget_tree(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Widget Blueprint")] string path)
    {
        router.EnsureLiveMode("get_widget_tree");
        return await bridge.SendAndSerializeAsync("get_widget_tree", new() { ["path"] = path });
    }
}
