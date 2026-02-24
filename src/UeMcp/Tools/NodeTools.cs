using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class NodeTools
{
    [McpServerTool, Description(
        "List available Blueprint node types by category. Returns K2Node class names with descriptions " +
        "so you know what to pass to add_blueprint_node. Categories: Flow Control, Events, Functions, " +
        "Variables, Casting & Type, Object Lifecycle, Struct, Math, Utility. " +
        "In live mode with includeFunctions=true, also discovers BlueprintCallable library functions.")]
    public static async Task<string> list_node_types(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Optional: filter by category name (e.g. 'Flow', 'Events', 'Functions')")] string? category = null,
        [Description("Include discoverable BlueprintCallable functions from library classes. Requires live mode.")] bool includeFunctions = false)
    {
        if (bridge.IsConnected)
        {
            return await bridge.SendAndSerializeAsync("list_node_types", new()
            {
                ["category"] = category,
                ["includeFunctions"] = includeFunctions
            });
        }

        throw new InvalidOperationException(
            "list_node_types requires a live editor connection for the full catalog. " +
            "The node type database is hosted in the Python bridge plugin.");
    }

    [McpServerTool, Description(
        "Search for Blueprint node types by name, class, or description. " +
        "Use when you need a specific kind of node but don't know the exact K2Node class name. " +
        "Example queries: 'spawn', 'delay', 'branch', 'cast', 'array', 'print'.")]
    public static async Task<string> search_node_types(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Search query — matches against node names, K2Node class names, and descriptions")] string query)
    {
        if (bridge.IsConnected)
        {
            return await bridge.SendAndSerializeAsync("search_node_types", new()
            {
                ["query"] = query
            });
        }

        throw new InvalidOperationException(
            "search_node_types requires a live editor connection. " +
            "The node type database is hosted in the Python bridge plugin.");
    }
}
