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
}
