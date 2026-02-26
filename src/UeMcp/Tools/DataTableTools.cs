using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;
using UeMcp.Offline;

namespace UeMcp.Tools;

[McpServerToolType]
public static class DataTableTools
{
    [McpServerTool, Description(
        "Read a DataTable asset and return all rows with their column structure. " +
        "Shows the row struct type, column definitions, and all row data. " +
        "Optionally filter rows by name.")]
    public static async Task<string> read_datatable(
        ModeRouter router,
        DataTableReader reader,
        EditorBridge bridge,
        [Description("Path to the DataTable asset")] string assetPath,
        [Description("Optional: filter rows by name (substring match)")] string? rowFilter = null)
    {
        router.EnsureProjectLoaded();

        if (router.CurrentMode == OperationMode.Live)
        {
            try
            {
                return await bridge.SendAndSerializeAsync("read_datatable", new()
                {
                    ["path"] = assetPath,
                    ["rowFilter"] = rowFilter
                });
            }
            catch { /* fall through */ }
        }

        var resolved = router.ResolveAssetPath(assetPath);
        return reader.ReadDataTable(resolved, rowFilter);
    }

    [McpServerTool, Description(
        "Create a new DataTable asset with a given row struct. " +
        "After creation, use reimport_datatable to populate it with row data.")]
    public static async Task<string> create_datatable(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Row struct name or path (e.g. 'InventoryItem', '/Script/MyGame.FMyRowStruct')")] string rowStruct,
        [Description("Asset name (e.g. 'DT_Items')")] string name = "DT_New",
        [Description("Package path (e.g. '/Game/Data')")] string packagePath = "/Game/Data")
    {
        router.EnsureLiveMode("create_datatable");
        return await bridge.SendAndSerializeAsync("create_datatable", new()
        {
            ["rowStruct"] = rowStruct,
            ["name"] = name,
            ["packagePath"] = packagePath
        });
    }

    [McpServerTool, Description(
        "Reimport a DataTable from a JSON file or JSON string. Replaces all rows in the DataTable " +
        "with the contents of the JSON. The JSON format must match UE's DataTable JSON schema " +
        "(array of objects with row names as keys). Requires live editor connection. " +
        "Automatically saves the asset after a successful import.")]
    public static async Task<string> reimport_datatable(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the DataTable asset (e.g. '/Game/Data/DT_Items')")] string assetPath,
        [Description("Absolute filesystem path to a JSON file to import from")] string? jsonPath = null,
        [Description("Raw JSON string to import (alternative to jsonPath)")] string? jsonString = null)
    {
        router.EnsureLiveMode("reimport_datatable");
        return await bridge.SendAndSerializeAsync("reimport_datatable", new()
        {
            ["path"] = assetPath,
            ["jsonPath"] = jsonPath,
            ["jsonString"] = jsonString
        });
    }
}
