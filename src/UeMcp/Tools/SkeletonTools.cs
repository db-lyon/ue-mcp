using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class SkeletonTools
{
    [McpServerTool, Description(
        "Get information about a Skeleton or SkeletalMesh asset: skeleton path, sockets, bone count.")]
    public static async Task<string> get_skeleton_info(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Skeleton or SkeletalMesh")] string path)
    {
        router.EnsureLiveMode("get_skeleton_info");
        return await bridge.SendAndSerializeAsync("get_skeleton_info", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "List all sockets on a Skeleton or SkeletalMesh with their bone names, relative locations, and rotations.")]
    public static async Task<string> list_sockets(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the Skeleton or SkeletalMesh")] string path)
    {
        router.EnsureLiveMode("list_sockets");
        return await bridge.SendAndSerializeAsync("list_sockets", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "List all SkeletalMesh, Skeleton, and PhysicsAsset assets in a directory.")]
    public static async Task<string> list_skeletal_meshes(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/Characters'). Default: '/Game'")] string directory = "/Game")
    {
        router.EnsureLiveMode("list_skeletal_meshes");
        return await bridge.SendAndSerializeAsync("list_skeletal_meshes", new() { ["directory"] = directory });
    }

    [McpServerTool, Description(
        "Get information about a Physics Asset: body setups and their associated bone names.")]
    public static async Task<string> get_physics_asset_info(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the PhysicsAsset")] string path)
    {
        router.EnsureLiveMode("get_physics_asset_info");
        return await bridge.SendAndSerializeAsync("get_physics_asset_info", new() { ["path"] = path });
    }
}
