using System.Text.Json;
using Microsoft.Extensions.Logging;
using UAssetAPI;
using UAssetAPI.ExportTypes;
using UAssetAPI.PropertyTypes.Objects;
using UAssetAPI.PropertyTypes.Structs;
using UAssetAPI.UnrealTypes;
using UeMcp.Core;

namespace UeMcp.Offline;

public class BlueprintReader
{
    private readonly AssetService _assetService;
    private readonly ProjectContext _context;
    private readonly ILogger<BlueprintReader> _logger;

    public BlueprintReader(AssetService assetService, ProjectContext context, ILogger<BlueprintReader> logger)
    {
        _assetService = assetService;
        _context = context;
        _logger = logger;
    }

    public string ReadBlueprint(string resolvedPath)
    {
        var asset = _assetService.LoadAsset(resolvedPath);
        var result = DescribeBlueprint(asset, resolvedPath);
        return JsonSerializer.Serialize(result, new JsonSerializerOptions
        {
            WriteIndented = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        });
    }

    public Dictionary<string, object?> DescribeBlueprint(UAsset asset, string path)
    {
        var classExport = FindClassExport(asset);
        var defaultObject = FindDefaultObject(asset);

        var result = new Dictionary<string, object?>
        {
            ["path"] = _context.GetRelativeContentPath(path),
            ["className"] = classExport?.ObjectName?.ToString(),
            ["parentClass"] = ResolveParentClass(asset, classExport),
            ["interfaces"] = GetImplementedInterfaces(asset, classExport),
            ["variables"] = GetVariables(asset, classExport, defaultObject),
            ["functions"] = GetFunctions(asset),
            ["graphs"] = GetGraphNames(asset),
            ["components"] = GetComponents(asset),
            ["exportSummary"] = _assetService.GetExportList(asset)
        };

        return result;
    }

    public string ListVariables(string resolvedPath)
    {
        var asset = _assetService.LoadAsset(resolvedPath);
        var classExport = FindClassExport(asset);
        var defaultObject = FindDefaultObject(asset);
        var variables = GetVariables(asset, classExport, defaultObject);

        return JsonSerializer.Serialize(variables, new JsonSerializerOptions
        {
            WriteIndented = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        });
    }

    public string ListFunctions(string resolvedPath)
    {
        var asset = _assetService.LoadAsset(resolvedPath);
        var functions = GetFunctions(asset);

        return JsonSerializer.Serialize(functions, new JsonSerializerOptions
        {
            WriteIndented = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        });
    }

    public string ReadGraph(string resolvedPath, string graphName)
    {
        var asset = _assetService.LoadAsset(resolvedPath);
        var graph = GetGraphDetail(asset, graphName);

        return JsonSerializer.Serialize(graph, new JsonSerializerOptions
        {
            WriteIndented = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        });
    }

    private ClassExport? FindClassExport(UAsset asset)
    {
        return asset.Exports.OfType<ClassExport>().FirstOrDefault();
    }

    private NormalExport? FindDefaultObject(UAsset asset)
    {
        return asset.Exports
            .OfType<NormalExport>()
            .FirstOrDefault(e => e.ObjectName?.ToString()?.StartsWith("Default__") == true);
    }

    private string? ResolveParentClass(UAsset asset, ClassExport? classExport)
    {
        if (classExport?.SuperIndex == null) return null;

        try
        {
            var idx = classExport.SuperIndex.Index;
            if (idx < 0 && asset.Imports != null)
            {
                var import = asset.Imports[(-idx) - 1];
                return import.ObjectName?.ToString();
            }
            if (idx > 0 && idx <= asset.Exports.Count)
            {
                return asset.Exports[idx - 1].ObjectName?.ToString();
            }
        }
        catch { }
        return null;
    }

    private List<string> GetImplementedInterfaces(UAsset asset, ClassExport? classExport)
    {
        var interfaces = new List<string>();
        if (classExport?.Interfaces == null) return interfaces;

        foreach (var iface in classExport.Interfaces)
        {
            try
            {
                var idx = iface.Class;
                if (idx < 0 && asset.Imports != null)
                {
                    interfaces.Add(asset.Imports[(-idx) - 1].ObjectName?.ToString() ?? "Unknown");
                }
            }
            catch { }
        }

        return interfaces;
    }

    private List<Dictionary<string, object?>> GetVariables(UAsset asset, ClassExport? classExport, NormalExport? defaultObject)
    {
        var variables = new List<Dictionary<string, object?>>();

        if (classExport?.LoadedProperties != null)
        {
            foreach (var prop in classExport.LoadedProperties)
            {
                var varInfo = new Dictionary<string, object?>
                {
                    ["name"] = prop.Name?.ToString(),
                    ["serializedType"] = prop.SerializedType?.ToString(),
                    ["flags"] = prop.PropertyFlags.ToString(),
                };

                if (defaultObject != null)
                {
                    var defaultVal = defaultObject.Data?
                        .FirstOrDefault(p => p.Name?.ToString() == prop.Name?.ToString());

                    if (defaultVal != null)
                    {
                        varInfo["defaultValue"] = _assetService.DescribeProperty(defaultVal);
                    }
                }

                variables.Add(varInfo);
            }
        }

        return variables;
    }

    private List<Dictionary<string, object?>> GetFunctions(UAsset asset)
    {
        return asset.Exports.OfType<FunctionExport>()
            .Select(func =>
            {
                var info = new Dictionary<string, object?>
                {
                    ["name"] = func.ObjectName?.ToString(),
                    ["flags"] = func.FunctionFlags.ToString(),
                    ["superIndex"] = func.SuperIndex?.Index
                };

                if (func.LoadedProperties != null)
                {
                    info["parameters"] = func.LoadedProperties.Select(p => new Dictionary<string, object?>
                    {
                        ["name"] = p.Name?.ToString(),
                        ["type"] = p.SerializedType?.ToString(),
                        ["flags"] = p.PropertyFlags.ToString()
                    }).ToList();
                }

                if (func.ScriptBytecode != null)
                {
                    info["bytecodeSize"] = func.ScriptBytecodeSize;
                }

                return info;
            })
            .ToList();
    }

    private List<string> GetGraphNames(UAsset asset)
    {
        var graphs = new List<string>();

        foreach (var export in asset.Exports)
        {
            var className = export.GetExportClassType()?.ToString() ?? "";
            if (className.Contains("Graph", StringComparison.OrdinalIgnoreCase) ||
                className.Contains("EdGraph", StringComparison.OrdinalIgnoreCase))
            {
                graphs.Add(export.ObjectName?.ToString() ?? "Unknown");
            }
        }

        return graphs;
    }

    private Dictionary<string, object?> GetGraphDetail(UAsset asset, string graphName)
    {
        var graphExport = asset.Exports
            .FirstOrDefault(e => e.ObjectName?.ToString() == graphName);

        if (graphExport == null)
            throw new KeyNotFoundException($"Graph '{graphName}' not found in asset");

        var nodes = new List<Dictionary<string, object?>>();

        foreach (var export in asset.Exports)
        {
            if (export.OuterIndex.Index <= 0) continue;

            var outerExport = asset.Exports[export.OuterIndex.Index - 1];
            if (outerExport.ObjectName?.ToString() != graphName) continue;

            var node = new Dictionary<string, object?>
            {
                ["objectName"] = export.ObjectName?.ToString(),
                ["classType"] = export.GetExportClassType()?.ToString(),
            };

            if (export is NormalExport normal)
            {
                node["properties"] = _assetService.DescribeProperties(normal.Data);
            }

            nodes.Add(node);
        }

        return new Dictionary<string, object?>
        {
            ["graphName"] = graphName,
            ["graphClass"] = graphExport.GetExportClassType()?.ToString(),
            ["nodeCount"] = nodes.Count,
            ["nodes"] = nodes
        };
    }

    private List<Dictionary<string, object?>> GetComponents(UAsset asset)
    {
        var components = new List<Dictionary<string, object?>>();

        foreach (var export in asset.Exports)
        {
            var objName = export.ObjectName?.ToString() ?? "";
            if (objName.StartsWith("SCS_Node"))
            {
                var comp = new Dictionary<string, object?>
                {
                    ["name"] = objName,
                    ["classType"] = export.GetExportClassType()?.ToString()
                };

                if (export is NormalExport normal)
                {
                    var internalName = normal.Data?
                        .FirstOrDefault(p => p.Name?.ToString() == "InternalVariableName");
                    if (internalName != null)
                    {
                        comp["internalName"] = internalName.RawValue?.ToString();
                    }

                    var componentClass = normal.Data?
                        .FirstOrDefault(p => p.Name?.ToString() == "ComponentClass");
                    if (componentClass is ObjectPropertyData objProp)
                    {
                        comp["componentClass"] = objProp.Value?.ToString();
                    }
                }

                components.Add(comp);
            }
        }

        return components;
    }
}
