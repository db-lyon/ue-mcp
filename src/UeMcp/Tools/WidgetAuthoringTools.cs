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

    [McpServerTool, Description(
        "Create an Editor Utility Widget — a UMG panel that runs inside the editor as a docked tab. " +
        "Use for building custom tool windows, tuning dashboards, asset inspectors, and batch operation UIs.")]
    public static async Task<string> create_editor_utility_widget(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path (e.g. '/Game/EditorTools/EUW_TuningPanel')")] string path)
    {
        router.EnsureLiveMode("create_editor_utility_widget");
        return await bridge.SendAndSerializeAsync("create_editor_utility_widget", new()
        {
            ["path"] = path
        });
    }

    [McpServerTool, Description(
        "Open an Editor Utility Widget as a docked tab in the editor. " +
        "The widget must already exist as an EditorUtilityWidgetBlueprint asset.")]
    public static async Task<string> run_editor_utility_widget(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Editor Utility Widget")] string path)
    {
        router.EnsureLiveMode("run_editor_utility_widget");
        return await bridge.SendAndSerializeAsync("run_editor_utility_widget", new()
        {
            ["path"] = path
        });
    }

    [McpServerTool, Description(
        "Create an Editor Utility Blueprint — a headless editor automation script. " +
        "Unlike EUWs, these have no UI and run logic directly. " +
        "Common parent classes: 'EditorUtilityObject', 'ActorActionUtility', 'AssetActionUtility'.")]
    public static async Task<string> create_editor_utility_blueprint(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path (e.g. '/Game/EditorTools/EUB_BatchRenamer')")] string path,
        [Description("Parent class. Default: 'EditorUtilityObject'. Also: 'ActorActionUtility', 'AssetActionUtility'")] string parentClass = "EditorUtilityObject")
    {
        router.EnsureLiveMode("create_editor_utility_blueprint");
        return await bridge.SendAndSerializeAsync("create_editor_utility_blueprint", new()
        {
            ["path"] = path,
            ["parentClass"] = parentClass
        });
    }

    [McpServerTool, Description(
        "Execute an Editor Utility Blueprint (headless editor script). " +
        "Optionally call a specific function by name.")]
    public static async Task<string> run_editor_utility_blueprint(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Editor Utility Blueprint")] string path,
        [Description("Optional: specific function to call")] string? functionName = null)
    {
        router.EnsureLiveMode("run_editor_utility_blueprint");
        return await bridge.SendAndSerializeAsync("run_editor_utility_blueprint", new()
        {
            ["path"] = path,
            ["functionName"] = functionName ?? ""
        });
    }
}
