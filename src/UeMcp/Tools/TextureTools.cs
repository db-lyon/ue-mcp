using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class TextureTools
{
    [McpServerTool, Description(
        "List all texture assets in a directory: Texture2D, TextureCube, render targets, media textures.")]
    public static async Task<string> list_textures(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/Textures'). Default: '/Game'")] string directory = "/Game")
    {
        router.EnsureLiveMode("list_textures");
        return await bridge.SendAndSerializeAsync("list_textures", new() { ["directory"] = directory });
    }

    [McpServerTool, Description(
        "Get information about a texture: compression settings, LOD group, sRGB, streaming settings.")]
    public static async Task<string> get_texture_info(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the texture")] string path)
    {
        router.EnsureLiveMode("get_texture_info");
        return await bridge.SendAndSerializeAsync("get_texture_info", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "Set texture settings: sRGB, compression, LOD group, streaming. " +
        "Compression options: Default, Normalmap, Masks, HDR, VectorDisplacementmap. " +
        "LOD groups: World, Character, UI, Lightmap.")]
    public static async Task<string> set_texture_settings(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the texture")] string path,
        [Description("sRGB enabled")] bool? srgb = null,
        [Description("Never stream this texture")] bool? neverStream = null,
        [Description("Compression: 'Default', 'Normalmap', 'Masks', 'HDR', 'VectorDisplacementmap'")] string? compressionSettings = null,
        [Description("LOD group: 'World', 'Character', 'UI', 'Lightmap'")] string? lodGroup = null)
    {
        router.EnsureLiveMode("set_texture_settings");
        var parameters = new Dictionary<string, object?> { ["path"] = path };
        if (srgb.HasValue) parameters["srgb"] = srgb.Value;
        if (neverStream.HasValue) parameters["neverStream"] = neverStream.Value;
        if (compressionSettings != null) parameters["compressionSettings"] = compressionSettings;
        if (lodGroup != null) parameters["lodGroup"] = lodGroup;

        return await bridge.SendAndSerializeAsync("set_texture_settings", parameters);
    }

    [McpServerTool, Description(
        "Import a texture file from disk into the project. Supports PNG, TGA, BMP, EXR, HDR, JPEG.")]
    public static async Task<string> import_texture(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Absolute path to the image file on disk")] string filePath,
        [Description("Destination content path (e.g. '/Game/Textures'). Default: '/Game/Textures'")] string destination = "/Game/Textures")
    {
        router.EnsureLiveMode("import_texture");
        return await bridge.SendAndSerializeAsync("import_texture", new()
        {
            ["filePath"] = filePath,
            ["destination"] = destination
        });
    }
}
