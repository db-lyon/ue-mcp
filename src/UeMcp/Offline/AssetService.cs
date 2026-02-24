using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Extensions.Logging;
using UAssetAPI;
using UAssetAPI.ExportTypes;
using UAssetAPI.PropertyTypes.Objects;
using UAssetAPI.PropertyTypes.Structs;
using UAssetAPI.UnrealTypes;
using UeMcp.Core;

namespace UeMcp.Offline;

public class AssetService
{
    private readonly ProjectContext _context;
    private readonly ILogger<AssetService> _logger;

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    public AssetService(ProjectContext context, ILogger<AssetService> logger)
    {
        _context = context;
        _logger = logger;
    }

    public UAsset LoadAsset(string resolvedPath)
    {
        if (!File.Exists(resolvedPath))
            throw new FileNotFoundException($"Asset not found: {resolvedPath}");

        var version = _context.EngineVersion;
        if (version == EngineVersion.UNKNOWN)
            version = EngineVersion.VER_UE5_4;

        _logger.LogDebug("Loading asset {Path} with engine version {Version}", resolvedPath, version);
        return new UAsset(resolvedPath, version);
    }

    public string ReadAssetToJson(string resolvedPath)
    {
        var asset = LoadAsset(resolvedPath);
        var result = DescribeAsset(asset, resolvedPath);
        return JsonSerializer.Serialize(result, JsonOpts);
    }

    public Dictionary<string, object?> DescribeAsset(UAsset asset, string path)
    {
        var exports = new List<Dictionary<string, object?>>();

        for (int i = 0; i < asset.Exports.Count; i++)
        {
            var export = asset.Exports[i];
            var info = new Dictionary<string, object?>
            {
                ["index"] = i,
                ["objectName"] = export.ObjectName?.ToString(),
                ["classType"] = export.GetExportClassType()?.ToString(),
                ["outerIndex"] = export.OuterIndex?.Index,
                ["superIndex"] = export.SuperIndex?.Index,
                ["type"] = export.GetType().Name
            };

            if (export is NormalExport normal)
            {
                info["properties"] = DescribeProperties(normal.Data);
            }

            if (export is DataTableExport dt && dt.Table != null)
            {
                info["rows"] = DescribeDataTableRows(dt.Table);
            }

            exports.Add(info);
        }

        return new Dictionary<string, object?>
        {
            ["path"] = _context.GetRelativeContentPath(path),
            ["absolutePath"] = path,
            ["packageName"] = asset.FilePath,
            ["engineVersion"] = _context.EngineVersion.ToString(),
            ["exportCount"] = asset.Exports.Count,
            ["importCount"] = asset.Imports?.Count ?? 0,
            ["nameCount"] = asset.GetNameMapIndexList()?.Count ?? 0,
            ["exports"] = exports
        };
    }

    public List<Dictionary<string, object?>> DescribeProperties(List<PropertyData>? properties)
    {
        if (properties == null) return new();

        return properties.Select(prop => DescribeProperty(prop)).ToList();
    }

    public Dictionary<string, object?> DescribeProperty(PropertyData prop)
    {
        var result = new Dictionary<string, object?>
        {
            ["name"] = prop.Name?.ToString(),
            ["type"] = prop.PropertyType?.ToString() ?? prop.GetType().Name,
            ["arrayIndex"] = prop.ArrayIndex > 0 ? prop.ArrayIndex : null
        };

        switch (prop)
        {
            case BoolPropertyData b:
                result["value"] = b.Value;
                break;
            case IntPropertyData i:
                result["value"] = i.Value;
                break;
            case UInt32PropertyData u32:
                result["value"] = u32.Value;
                break;
            case Int64PropertyData i64:
                result["value"] = i64.Value;
                break;
            case FloatPropertyData f:
                result["value"] = f.Value;
                break;
            case DoublePropertyData d:
                result["value"] = d.Value;
                break;
            case StrPropertyData s:
                result["value"] = s.Value?.ToString();
                break;
            case NamePropertyData n:
                result["value"] = n.Value?.ToString();
                break;
            case TextPropertyData t:
                result["value"] = t.Value?.ToString();
                break;
            case ObjectPropertyData o:
                result["value"] = o.Value?.Index;
                result["objectPath"] = ResolveObjectPath(o);
                break;
            case SoftObjectPropertyData so:
                result["value"] = so.Value.ToString();
                break;
            case EnumPropertyData e:
                result["value"] = e.Value?.ToString();
                result["enumType"] = e.EnumType?.ToString();
                break;
            case BytePropertyData by:
                result["value"] = by.RawValue?.ToString();
                result["byteType"] = by.ByteType.ToString();
                break;
            case SetPropertyData set:
                result["value"] = set.Value?.Select(p => DescribeProperty(p)).ToList();
                break;
            case ArrayPropertyData arr:
                result["arrayType"] = arr.ArrayType?.ToString();
                result["value"] = arr.Value?.Select(p => DescribeProperty(p)).ToList();
                break;
            case MapPropertyData map:
                result["value"] = DescribeMapProperty(map);
                break;
            case StructPropertyData str:
                result["structType"] = str.StructType?.ToString();
                result["value"] = DescribeProperties(str.Value);
                break;
            default:
                result["value"] = prop.RawValue?.ToString();
                break;
        }

        return result;
    }

    private List<Dictionary<string, object?>>? DescribeMapProperty(MapPropertyData map)
    {
        if (map.Value == null) return null;

        return map.Value.Select(kvp => new Dictionary<string, object?>
        {
            ["key"] = DescribeProperty(kvp.Key),
            ["value"] = DescribeProperty(kvp.Value)
        }).ToList();
    }

    private List<Dictionary<string, object?>> DescribeDataTableRows(UDataTable table)
    {
        return table.Data.Select(row =>
        {
            var rowData = new Dictionary<string, object?>
            {
                ["rowName"] = row.Name?.ToString(),
                ["properties"] = DescribeProperties(row.Value)
            };
            return rowData;
        }).ToList();
    }

    private string? ResolveObjectPath(ObjectPropertyData obj)
    {
        try
        {
            return obj.Value?.ToString();
        }
        catch
        {
            return null;
        }
    }

    public List<Dictionary<string, object?>> GetExportList(UAsset asset)
    {
        return asset.Exports.Select((exp, i) => new Dictionary<string, object?>
        {
            ["index"] = i,
            ["objectName"] = exp.ObjectName?.ToString(),
            ["classType"] = exp.GetExportClassType()?.ToString(),
            ["type"] = exp.GetType().Name,
            ["serialSize"] = exp.SerialSize,
        }).ToList();
    }

    public List<Dictionary<string, object?>> GetImportList(UAsset asset)
    {
        if (asset.Imports == null) return new();

        return asset.Imports.Select((imp, i) => new Dictionary<string, object?>
        {
            ["index"] = -(i + 1),
            ["classPackage"] = imp.ClassPackage?.ToString(),
            ["className"] = imp.ClassName?.ToString(),
            ["objectName"] = imp.ObjectName?.ToString(),
        }).ToList();
    }
}
