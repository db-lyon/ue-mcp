using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class BehaviorTreeTools
{
    [McpServerTool, Description(
        "List all Behavior Tree and Blackboard assets in a directory.")]
    public static async Task<string> list_behavior_trees(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/AI'). Default: '/Game'")] string directory = "/Game")
    {
        router.EnsureLiveMode("list_behavior_trees");
        return await bridge.SendAndSerializeAsync("list_behavior_trees", new() { ["directory"] = directory });
    }

    [McpServerTool, Description(
        "Get information about a Behavior Tree: associated Blackboard and its keys.")]
    public static async Task<string> get_behavior_tree_info(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Behavior Tree")] string path)
    {
        router.EnsureLiveMode("get_behavior_tree_info");
        return await bridge.SendAndSerializeAsync("get_behavior_tree_info", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "Create a new Blackboard Data asset for AI Behavior Trees.")]
    public static async Task<string> create_blackboard(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path (e.g. '/Game/AI/BB_Enemy')")] string path)
    {
        router.EnsureLiveMode("create_blackboard");
        return await bridge.SendAndSerializeAsync("create_blackboard", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "Create a new Behavior Tree asset. Optionally link it to an existing Blackboard.")]
    public static async Task<string> create_behavior_tree(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path (e.g. '/Game/AI/BT_Enemy')")] string path,
        [Description("Optional: asset path to a Blackboard to associate with")] string? blackboardPath = null)
    {
        router.EnsureLiveMode("create_behavior_tree");
        return await bridge.SendAndSerializeAsync("create_behavior_tree", new()
        {
            ["path"] = path,
            ["blackboardPath"] = blackboardPath ?? ""
        });
    }
}
