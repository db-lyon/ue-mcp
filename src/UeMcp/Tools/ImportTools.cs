using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class ImportTools
{
    [McpServerTool, Description(
        "Import a static mesh from an FBX or OBJ file. Imports materials and textures alongside the mesh.")]
    public static async Task<string> import_static_mesh(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Absolute filesystem path to the FBX/OBJ file")] string filePath,
        [Description("Destination content path (e.g. '/Game/Meshes')")] string destinationPath = "/Game/Meshes",
        [Description("Optional: asset name (defaults to filename without extension)")] string? name = null)
    {
        router.EnsureLiveMode("import_static_mesh");
        return await bridge.SendAndSerializeAsync("import_static_mesh", new()
        {
            ["filePath"] = filePath,
            ["destinationPath"] = destinationPath,
            ["name"] = name ?? ""
        });
    }

    [McpServerTool, Description(
        "Import a skeletal mesh from an FBX file. Optionally target an existing skeleton for retargeting.")]
    public static async Task<string> import_skeletal_mesh(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Absolute filesystem path to the FBX file")] string filePath,
        [Description("Destination content path (e.g. '/Game/Characters')")] string destinationPath = "/Game/Meshes",
        [Description("Optional: asset name")] string? name = null,
        [Description("Optional: path to existing skeleton to use")] string? skeletonPath = null)
    {
        router.EnsureLiveMode("import_skeletal_mesh");
        return await bridge.SendAndSerializeAsync("import_skeletal_mesh", new()
        {
            ["filePath"] = filePath,
            ["destinationPath"] = destinationPath,
            ["name"] = name ?? "",
            ["skeletonPath"] = skeletonPath ?? ""
        });
    }

    [McpServerTool, Description(
        "Import an animation from an FBX file. Optionally target an existing skeleton.")]
    public static async Task<string> import_animation(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Absolute filesystem path to the FBX file")] string filePath,
        [Description("Destination content path (e.g. '/Game/Animations')")] string destinationPath = "/Game/Animations",
        [Description("Optional: asset name")] string? name = null,
        [Description("Optional: path to existing skeleton to use")] string? skeletonPath = null)
    {
        router.EnsureLiveMode("import_animation");
        return await bridge.SendAndSerializeAsync("import_animation", new()
        {
            ["filePath"] = filePath,
            ["destinationPath"] = destinationPath,
            ["name"] = name ?? "",
            ["skeletonPath"] = skeletonPath ?? ""
        });
    }
}
