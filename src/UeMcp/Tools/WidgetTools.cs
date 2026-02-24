using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class WidgetTools
{
    [McpServerTool, Description(
        "Read a Widget Blueprint's full widget tree: hierarchy of panels, buttons, text blocks, images, " +
        "etc. with class, visibility, and slot info. Also returns UMG animations and Blueprint variables.")]
    public static async Task<string> read_widget_tree(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Widget Blueprint (e.g. '/Game/UI/WBP_HUD')")] string assetPath,
        [Description("Maximum depth to traverse. Default: 20")] int maxDepth = 20)
    {
        router.EnsureLiveMode("read_widget_tree");
        return await bridge.SendAndSerializeAsync("read_widget_tree", new()
        {
            ["assetPath"] = assetPath,
            ["maxDepth"] = maxDepth
        });
    }

    [McpServerTool, Description(
        "Get detailed information about a specific widget in a Widget Blueprint: all properties, " +
        "type-specific data (text content, font size, image, colors, progress percent), and visibility.")]
    public static async Task<string> get_widget_details(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Widget Blueprint")] string assetPath,
        [Description("Name of the widget to inspect")] string widgetName)
    {
        router.EnsureLiveMode("get_widget_details");
        return await bridge.SendAndSerializeAsync("get_widget_details", new()
        {
            ["assetPath"] = assetPath,
            ["widgetName"] = widgetName
        });
    }

    [McpServerTool, Description(
        "Set a property on a widget in a Widget Blueprint. Works for visibility, text, colors, " +
        "opacity, and any editor-exposed property.")]
    public static async Task<string> set_widget_property(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Widget Blueprint")] string assetPath,
        [Description("Name of the widget to modify")] string widgetName,
        [Description("Property name to set")] string propertyName,
        [Description("New value (JSON for complex types, string/number for simple)")] string value)
    {
        router.EnsureLiveMode("set_widget_property");

        object? parsedValue;
        try { parsedValue = JsonSerializer.Deserialize<object>(value); }
        catch { parsedValue = value; }

        return await bridge.SendAndSerializeAsync("set_widget_property", new()
        {
            ["assetPath"] = assetPath,
            ["widgetName"] = widgetName,
            ["propertyName"] = propertyName,
            ["value"] = parsedValue
        });
    }

    [McpServerTool, Description(
        "List all Widget Blueprints in a directory. Use to discover the UI assets in a project.")]
    public static async Task<string> list_widget_blueprints(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/', '/Game/UI/')")] string directory = "/Game/",
        [Description("Search subdirectories. Default: true")] bool recursive = true)
    {
        router.EnsureLiveMode("list_widget_blueprints");
        return await bridge.SendAndSerializeAsync("list_widget_blueprints", new()
        {
            ["directory"] = directory,
            ["recursive"] = recursive
        });
    }

    [McpServerTool, Description(
        "Read all UMG widget animations in a Widget Blueprint: animation names, lengths, " +
        "looping state, and which widgets/properties they animate.")]
    public static async Task<string> read_widget_animations(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Widget Blueprint")] string assetPath)
    {
        router.EnsureLiveMode("read_widget_animations");
        return await bridge.SendAndSerializeAsync("read_widget_animations", new()
        {
            ["assetPath"] = assetPath
        });
    }
}
