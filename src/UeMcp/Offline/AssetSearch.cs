using System.Text.Json;
using Microsoft.Extensions.Logging;
using UAssetAPI;
using UAssetAPI.ExportTypes;
using UAssetAPI.PropertyTypes.Objects;
using UeMcp.Core;

namespace UeMcp.Offline;

public class AssetSearch
{
    private readonly AssetService _assetService;
    private readonly ProjectContext _context;
    private readonly ILogger<AssetSearch> _logger;

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    public AssetSearch(AssetService assetService, ProjectContext context, ILogger<AssetSearch> logger)
    {
        _assetService = assetService;
        _context = context;
        _logger = logger;
    }

    public string ListAssets(string? directory = null, string? typeFilter = null, bool recursive = true)
    {
        if (_context.ContentDir == null)
            throw new InvalidOperationException("No project loaded.");

        var searchDir = directory != null
            ? _context.ResolveContentDir(directory)
            : _context.ContentDir;

        if (!Directory.Exists(searchDir))
            throw new DirectoryNotFoundException($"Directory not found: {searchDir}");

        var option = recursive ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly;
        var files = Directory.GetFiles(searchDir, "*.uasset", option)
            .Concat(Directory.GetFiles(searchDir, "*.umap", option));

        var results = files.Select(f =>
        {
            var info = new Dictionary<string, object?>
            {
                ["path"] = _context.GetRelativeContentPath(f),
                ["fileName"] = Path.GetFileNameWithoutExtension(f),
                ["extension"] = Path.GetExtension(f),
                ["size"] = new FileInfo(f).Length
            };

            if (typeFilter != null)
            {
                try
                {
                    var asset = _assetService.LoadAsset(f);
                    var classType = asset.Exports.FirstOrDefault()?.GetExportClassType()?.ToString() ?? "";
                    if (!classType.Contains(typeFilter, StringComparison.OrdinalIgnoreCase))
                        return null;
                    info["assetClass"] = classType;
                }
                catch
                {
                    return null;
                }
            }

            return info;
        })
        .Where(r => r != null)
        .Take(500)
        .ToList();

        return JsonSerializer.Serialize(new
        {
            directory = _context.GetRelativeContentPath(searchDir),
            count = results.Count,
            assets = results
        }, JsonOpts);
    }

    public string SearchAssets(string query, string? directory = null, int maxResults = 50)
    {
        if (_context.ContentDir == null)
            throw new InvalidOperationException("No project loaded.");

        var searchDir = directory != null
            ? _context.ResolveContentDir(directory)
            : _context.ContentDir;

        if (!Directory.Exists(searchDir))
            throw new DirectoryNotFoundException($"Directory not found: {searchDir}");

        var files = Directory.GetFiles(searchDir, "*.uasset", SearchOption.AllDirectories)
            .Concat(Directory.GetFiles(searchDir, "*.umap", SearchOption.AllDirectories));

        var results = new List<Dictionary<string, object?>>();

        foreach (var file in files)
        {
            if (results.Count >= maxResults) break;

            var fileName = Path.GetFileNameWithoutExtension(file);
            if (fileName.Contains(query, StringComparison.OrdinalIgnoreCase))
            {
                results.Add(new Dictionary<string, object?>
                {
                    ["path"] = _context.GetRelativeContentPath(file),
                    ["matchType"] = "filename",
                    ["fileName"] = fileName
                });
                continue;
            }

            try
            {
                var asset = _assetService.LoadAsset(file);
                var matchedExports = new List<string>();

                foreach (var export in asset.Exports)
                {
                    var objName = export.ObjectName?.ToString() ?? "";
                    var classType = export.GetExportClassType()?.ToString() ?? "";

                    if (objName.Contains(query, StringComparison.OrdinalIgnoreCase) ||
                        classType.Contains(query, StringComparison.OrdinalIgnoreCase))
                    {
                        matchedExports.Add(objName);
                    }

                    if (export is NormalExport normal && normal.Data != null)
                    {
                        foreach (var prop in normal.Data)
                        {
                            if (PropertyContainsValue(prop, query))
                            {
                                matchedExports.Add($"{objName}.{prop.Name}");
                                break;
                            }
                        }
                    }
                }

                if (matchedExports.Count > 0)
                {
                    results.Add(new Dictionary<string, object?>
                    {
                        ["path"] = _context.GetRelativeContentPath(file),
                        ["matchType"] = "content",
                        ["matches"] = matchedExports.Take(10).ToList()
                    });
                }
            }
            catch (Exception ex)
            {
                _logger.LogDebug("Failed to search asset {File}: {Error}", file, ex.Message);
            }
        }

        return JsonSerializer.Serialize(new
        {
            query,
            resultCount = results.Count,
            results
        }, JsonOpts);
    }

    private bool PropertyContainsValue(PropertyData prop, string query)
    {
        try
        {
            var rawVal = prop.RawValue?.ToString() ?? "";
            if (rawVal.Contains(query, StringComparison.OrdinalIgnoreCase))
                return true;

            if (prop is StrPropertyData str)
                return str.Value?.ToString()?.Contains(query, StringComparison.OrdinalIgnoreCase) == true;

            if (prop is NamePropertyData name)
                return name.Value?.ToString()?.Contains(query, StringComparison.OrdinalIgnoreCase) == true;

            if (prop is TextPropertyData text)
                return text.Value?.ToString()?.Contains(query, StringComparison.OrdinalIgnoreCase) == true;
        }
        catch { }

        return false;
    }
}
