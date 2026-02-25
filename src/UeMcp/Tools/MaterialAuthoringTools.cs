using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class MaterialAuthoringTools
{
    [McpServerTool, Description(
        "Create a new base Material asset. After creation, use set_material_base_color and " +
        "connect_texture_to_material to build the material graph.")]
    public static async Task<string> create_material(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path for the new material (e.g. '/Game/Materials/M_NewMaterial')")] string path)
    {
        router.EnsureLiveMode("create_material");
        return await bridge.SendAndSerializeAsync("create_material", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "Set the shading model on a Material. Options: DefaultLit, Unlit, Subsurface, " +
        "ClearCoat, SubsurfaceProfile, TwoSidedFoliage.")]
    public static async Task<string> set_material_shading_model(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the material")] string path,
        [Description("Shading model name")] string shadingModel)
    {
        router.EnsureLiveMode("set_material_shading_model");
        return await bridge.SendAndSerializeAsync("set_material_shading_model", new()
        {
            ["path"] = path,
            ["shadingModel"] = shadingModel
        });
    }

    [McpServerTool, Description(
        "Set the base color of a Material to a constant RGBA value. Creates a Constant4Vector " +
        "expression and connects it to the BaseColor pin.")]
    public static async Task<string> set_material_base_color(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the material")] string path,
        [Description("Color as [R, G, B] or [R, G, B, A] with values 0.0-1.0")] string color)
    {
        router.EnsureLiveMode("set_material_base_color");
        var parsed = JsonSerializer.Deserialize<double[]>(color) ?? [1, 1, 1];
        return await bridge.SendAndSerializeAsync("set_material_base_color", new()
        {
            ["path"] = path,
            ["color"] = parsed
        });
    }

    [McpServerTool, Description(
        "Connect a texture to a material input. Creates a TextureSample expression and wires it to " +
        "the specified property: BaseColor, Normal, Roughness, Metallic, Emissive, Opacity, or AO.")]
    public static async Task<string> connect_texture_to_material(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the material")] string materialPath,
        [Description("Asset path to the texture")] string texturePath,
        [Description("Material property to connect: 'BaseColor', 'Normal', 'Roughness', 'Metallic', 'Emissive', 'Opacity', 'AO'")] string property = "BaseColor")
    {
        router.EnsureLiveMode("connect_texture_to_material");
        return await bridge.SendAndSerializeAsync("connect_texture_to_material", new()
        {
            ["materialPath"] = materialPath,
            ["texturePath"] = texturePath,
            ["property"] = property
        });
    }
}
