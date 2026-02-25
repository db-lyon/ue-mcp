using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class InputTools
{
    [McpServerTool, Description(
        "Create a new Enhanced Input Action asset. " +
        "Value types: Bool, Axis1D, Axis2D, Axis3D.")]
    public static async Task<string> create_input_action(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path (e.g. '/Game/Input/IA_Jump')")] string path,
        [Description("Value type: 'Bool', 'Axis1D', 'Axis2D', 'Axis3D'. Default: 'Bool'")] string valueType = "Bool")
    {
        router.EnsureLiveMode("create_input_action");
        return await bridge.SendAndSerializeAsync("create_input_action", new()
        {
            ["path"] = path,
            ["valueType"] = valueType
        });
    }

    [McpServerTool, Description(
        "Create a new Input Mapping Context asset for Enhanced Input.")]
    public static async Task<string> create_input_mapping_context(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path (e.g. '/Game/Input/IMC_Default')")] string path)
    {
        router.EnsureLiveMode("create_input_mapping_context");
        return await bridge.SendAndSerializeAsync("create_input_mapping_context", new() { ["path"] = path });
    }

    [McpServerTool, Description(
        "List all Enhanced Input assets (InputAction, InputMappingContext) in a directory.")]
    public static async Task<string> list_input_assets(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/Input'). Default: '/Game'")] string directory = "/Game")
    {
        router.EnsureLiveMode("list_input_assets");
        return await bridge.SendAndSerializeAsync("list_input_assets", new() { ["directory"] = directory });
    }
}
