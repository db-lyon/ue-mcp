using System.Text.Json;
using Microsoft.Extensions.Logging;
using UAssetAPI;
using UAssetAPI.ExportTypes;
using UAssetAPI.PropertyTypes.Structs;
using UeMcp.Core;

namespace UeMcp.Offline;

public class DataTableReader
{
    private readonly AssetService _assetService;
    private readonly ProjectContext _context;
    private readonly ILogger<DataTableReader> _logger;

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    public DataTableReader(AssetService assetService, ProjectContext context, ILogger<DataTableReader> logger)
    {
        _assetService = assetService;
        _context = context;
        _logger = logger;
    }

    public string ReadDataTable(string resolvedPath, string? rowFilter = null)
    {
        var asset = _assetService.LoadAsset(resolvedPath);
        var dtExport = asset.Exports.OfType<DataTableExport>().FirstOrDefault()
            ?? throw new InvalidOperationException($"Asset at {resolvedPath} is not a DataTable");

        var result = DescribeDataTable(asset, dtExport, resolvedPath, rowFilter);
        return JsonSerializer.Serialize(result, JsonOpts);
    }

    private Dictionary<string, object?> DescribeDataTable(UAsset asset, DataTableExport dtExport, string path, string? rowFilter)
    {
        var rows = dtExport.Table?.Data ?? new List<StructPropertyData>();

        if (!string.IsNullOrEmpty(rowFilter))
        {
            rows = rows
                .Where(r => r.Name?.ToString()?.Contains(rowFilter, StringComparison.OrdinalIgnoreCase) == true)
                .ToList();
        }

        var rowStructType = "Unknown";
        var rowStructProp = dtExport.Data?
            .FirstOrDefault(p => p.Name?.ToString() == "RowStruct");
        if (rowStructProp is UAssetAPI.PropertyTypes.Objects.ObjectPropertyData objProp)
        {
            try
            {
                rowStructType = objProp.ToImport(asset)?.ObjectName?.ToString() ?? "Unknown";
            }
            catch
            {
                rowStructType = objProp.Value?.ToString() ?? "Unknown";
            }
        }

        var columns = InferColumns(rows);

        return new Dictionary<string, object?>
        {
            ["path"] = _context.GetRelativeContentPath(path),
            ["rowStructType"] = rowStructType,
            ["rowCount"] = rows.Count,
            ["columns"] = columns,
            ["rows"] = rows.Select(row => new Dictionary<string, object?>
            {
                ["rowName"] = row.Name?.ToString(),
                ["values"] = _assetService.DescribeProperties(row.Value)
            }).ToList()
        };
    }

    private List<Dictionary<string, object?>> InferColumns(List<StructPropertyData> rows)
    {
        if (rows.Count == 0) return new();

        var firstRow = rows[0];
        if (firstRow.Value == null) return new();

        return firstRow.Value.Select(prop => new Dictionary<string, object?>
        {
            ["name"] = prop.Name?.ToString(),
            ["type"] = prop.PropertyType?.ToString() ?? prop.GetType().Name,
        }).ToList();
    }
}
