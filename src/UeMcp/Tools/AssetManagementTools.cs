using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class AssetManagementTools
{
    [McpServerTool, Description(
        "Duplicate an asset to a new path. Preserves all references within the duplicate.")]
    public static async Task<string> duplicate_asset(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Source asset path (e.g. '/Game/Meshes/SM_Rock')")] string sourcePath,
        [Description("Destination asset path (e.g. '/Game/Meshes/SM_Rock_Copy')")] string destinationPath)
    {
        router.EnsureLiveMode("duplicate_asset");
        return await bridge.SendAndSerializeAsync("duplicate_asset", new()
        {
            ["sourcePath"] = sourcePath,
            ["destinationPath"] = destinationPath
        });
    }

    [McpServerTool, Description(
        "Rename an asset (move it to a new path). Updates references automatically.")]
    public static async Task<string> rename_asset(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Current asset path")] string sourcePath,
        [Description("New asset path")] string destinationPath)
    {
        router.EnsureLiveMode("rename_asset");
        return await bridge.SendAndSerializeAsync("rename_asset", new()
        {
            ["sourcePath"] = sourcePath,
            ["destinationPath"] = destinationPath
        });
    }

    [McpServerTool, Description(
        "Move an asset to a different directory. Updates references automatically.")]
    public static async Task<string> move_asset(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Current asset path")] string sourcePath,
        [Description("New asset path (full path including new name)")] string destinationPath)
    {
        router.EnsureLiveMode("move_asset");
        return await bridge.SendAndSerializeAsync("move_asset", new()
        {
            ["sourcePath"] = sourcePath,
            ["destinationPath"] = destinationPath
        });
    }

    [McpServerTool, Description(
        "Delete an asset. Will fail if other assets reference it. " +
        "Use search_assets to check for references first.")]
    public static async Task<string> delete_asset(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the asset to delete")] string path)
    {
        router.EnsureLiveMode("delete_asset");
        return await bridge.SendAndSerializeAsync("delete_asset", new()
        {
            ["path"] = path
        });
    }
}
