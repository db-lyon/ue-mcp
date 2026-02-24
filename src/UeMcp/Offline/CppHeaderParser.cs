using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Logging;
using UeMcp.Core;

namespace UeMcp.Offline;

public class CppHeaderParser
{
    private readonly ProjectContext _context;
    private readonly ILogger<CppHeaderParser> _logger;

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    private static readonly Regex UClassRegex = new(
        @"UCLASS\(([^)]*)\)\s*class\s+(?:\w+_API\s+)?(\w+)\s*(?::\s*public\s+([\w:,\s]+))?\s*\{",
        RegexOptions.Compiled);

    private static readonly Regex UStructRegex = new(
        @"USTRUCT\(([^)]*)\)\s*struct\s+(?:\w+_API\s+)?(\w+)\s*(?::\s*public\s+(\w+))?\s*\{",
        RegexOptions.Compiled);

    private static readonly Regex UEnumRegex = new(
        @"UENUM\(([^)]*)\)\s*enum\s+(?:class\s+)?(\w+)",
        RegexOptions.Compiled);

    private static readonly Regex UPropertyRegex = new(
        @"UPROPERTY\(([^)]*)\)\s*(?:(?:TArray|TMap|TSet|TSubclassOf|TSoftObjectPtr|TObjectPtr|TWeakObjectPtr)<[^>]+>|[\w:*&]+)\s+(\w+)",
        RegexOptions.Compiled);

    private static readonly Regex UFunctionRegex = new(
        @"UFUNCTION\(([^)]*)\)\s*(?:virtual\s+)?(?:static\s+)?([\w:*&<>]+)\s+(\w+)\s*\(",
        RegexOptions.Compiled);

    private static readonly Regex EnumValueRegex = new(
        @"(\w+)\s*(?:=\s*[^,]+)?\s*(?:UMETA\(([^)]*)\))?\s*,?",
        RegexOptions.Compiled);

    private static readonly Regex PropertyTypeRegex = new(
        @"UPROPERTY\([^)]*\)\s*((?:TArray|TMap|TSet|TSubclassOf|TSoftObjectPtr|TObjectPtr|TWeakObjectPtr)<[^>]+>|[\w:*&]+)\s+(\w+)",
        RegexOptions.Compiled);

    public CppHeaderParser(ProjectContext context, ILogger<CppHeaderParser> logger)
    {
        _context = context;
        _logger = logger;
    }

    public string ReadHeader(string headerPath)
    {
        var path = ResolveSourcePath(headerPath);
        if (!File.Exists(path))
            throw new FileNotFoundException($"Header not found: {path}");

        var content = File.ReadAllText(path);
        var result = ParseHeader(content, path);

        return JsonSerializer.Serialize(result, JsonOpts);
    }

    public string ReadModule(string moduleName)
    {
        var sourceDir = GetSourceDir();
        var buildCs = FindFile(sourceDir, $"{moduleName}.Build.cs");
        if (buildCs == null)
            throw new FileNotFoundException($"Module build file not found: {moduleName}.Build.cs");

        var content = File.ReadAllText(buildCs);
        var deps = ExtractModuleDependencies(content);
        var moduleDir = Path.GetDirectoryName(buildCs)!;

        var headers = Directory.GetFiles(moduleDir, "*.h", SearchOption.AllDirectories)
            .Select(h => Path.GetRelativePath(moduleDir, h).Replace("\\", "/"))
            .ToList();

        var sources = Directory.GetFiles(moduleDir, "*.cpp", SearchOption.AllDirectories)
            .Select(s => Path.GetRelativePath(moduleDir, s).Replace("\\", "/"))
            .ToList();

        return JsonSerializer.Serialize(new
        {
            moduleName,
            buildFile = buildCs,
            publicDependencies = deps.Public,
            privateDependencies = deps.Private,
            headerCount = headers.Count,
            sourceCount = sources.Count,
            headers,
            sources
        }, JsonOpts);
    }

    public string ListModules()
    {
        var sourceDir = GetSourceDir();
        var buildFiles = Directory.GetFiles(sourceDir, "*.Build.cs", SearchOption.AllDirectories);

        var modules = buildFiles.Select(bf =>
        {
            var name = Path.GetFileNameWithoutExtension(bf).Replace(".Build", "");
            var content = File.ReadAllText(bf);
            var deps = ExtractModuleDependencies(content);
            var moduleDir = Path.GetDirectoryName(bf)!;

            var headerCount = Directory.GetFiles(moduleDir, "*.h", SearchOption.AllDirectories).Length;
            var sourceCount = Directory.GetFiles(moduleDir, "*.cpp", SearchOption.AllDirectories).Length;

            var moduleType = "Runtime";
            if (content.Contains("\"Editor\"", StringComparison.OrdinalIgnoreCase) ||
                content.Contains("ModuleType.Editor", StringComparison.OrdinalIgnoreCase))
                moduleType = "Editor";

            return new Dictionary<string, object?>
            {
                ["name"] = name,
                ["type"] = moduleType,
                ["headerCount"] = headerCount,
                ["sourceCount"] = sourceCount,
                ["publicDeps"] = deps.Public,
                ["privateDeps"] = deps.Private
            };
        }).ToList();

        return JsonSerializer.Serialize(new
        {
            sourceDirectory = sourceDir,
            moduleCount = modules.Count,
            modules
        }, JsonOpts);
    }

    public string SearchCpp(string query, string? directory = null)
    {
        var sourceDir = directory != null ? ResolveSourcePath(directory) : GetSourceDir();
        if (!Directory.Exists(sourceDir))
            throw new DirectoryNotFoundException($"Source directory not found: {sourceDir}");

        var results = new List<Dictionary<string, object?>>();

        foreach (var file in Directory.GetFiles(sourceDir, "*.h", SearchOption.AllDirectories)
            .Concat(Directory.GetFiles(sourceDir, "*.cpp", SearchOption.AllDirectories)))
        {
            if (results.Count >= 100) break;

            var lines = File.ReadAllLines(file);
            for (int i = 0; i < lines.Length; i++)
            {
                if (lines[i].Contains(query, StringComparison.OrdinalIgnoreCase))
                {
                    var contextStart = Math.Max(0, i - 2);
                    var contextEnd = Math.Min(lines.Length - 1, i + 2);
                    var context = string.Join("\n", lines[contextStart..(contextEnd + 1)]);

                    results.Add(new()
                    {
                        ["file"] = Path.GetRelativePath(sourceDir, file).Replace("\\", "/"),
                        ["line"] = i + 1,
                        ["context"] = context
                    });
                    break;
                }
            }
        }

        return JsonSerializer.Serialize(new
        {
            query,
            sourceDirectory = sourceDir,
            resultCount = results.Count,
            results
        }, JsonOpts);
    }

    private Dictionary<string, object?> ParseHeader(string content, string path)
    {
        var classes = new List<Dictionary<string, object?>>();
        var structs = new List<Dictionary<string, object?>>();
        var enums = new List<Dictionary<string, object?>>();

        foreach (Match m in UClassRegex.Matches(content))
        {
            var specifiers = m.Groups[1].Value.Trim();
            var className = m.Groups[2].Value;
            var parents = m.Groups[3].Success ? m.Groups[3].Value.Split(',').Select(p => p.Trim()).ToList() : new List<string>();

            var classBody = ExtractBracedBlock(content, m.Index + m.Length - 1);
            var properties = ExtractUProperties(classBody);
            var functions = ExtractUFunctions(classBody);

            classes.Add(new()
            {
                ["name"] = className,
                ["specifiers"] = specifiers,
                ["parents"] = parents,
                ["properties"] = properties,
                ["functions"] = functions
            });
        }

        foreach (Match m in UStructRegex.Matches(content))
        {
            var specifiers = m.Groups[1].Value.Trim();
            var structName = m.Groups[2].Value;
            var parent = m.Groups[3].Success ? m.Groups[3].Value : null;

            var structBody = ExtractBracedBlock(content, m.Index + m.Length - 1);
            var properties = ExtractUProperties(structBody);

            structs.Add(new()
            {
                ["name"] = structName,
                ["specifiers"] = specifiers,
                ["parent"] = parent,
                ["properties"] = properties
            });
        }

        foreach (Match m in UEnumRegex.Matches(content))
        {
            var specifiers = m.Groups[1].Value.Trim();
            var enumName = m.Groups[2].Value;

            var enumBody = ExtractBracedBlock(content, content.IndexOf('{', m.Index + m.Length));
            var values = new List<string>();
            foreach (Match ev in EnumValueRegex.Matches(enumBody))
            {
                var val = ev.Groups[1].Value.Trim();
                if (!string.IsNullOrEmpty(val) && val != "}" && !val.StartsWith("//"))
                    values.Add(val);
            }

            enums.Add(new()
            {
                ["name"] = enumName,
                ["specifiers"] = specifiers,
                ["values"] = values
            });
        }

        var includes = Regex.Matches(content, @"#include\s+[""<]([^"">\n]+)["">\n]")
            .Select(m => m.Groups[1].Value).ToList();

        return new()
        {
            ["path"] = path,
            ["includes"] = includes,
            ["classCount"] = classes.Count,
            ["structCount"] = structs.Count,
            ["enumCount"] = enums.Count,
            ["classes"] = classes,
            ["structs"] = structs,
            ["enums"] = enums
        };
    }

    private static List<Dictionary<string, object?>> ExtractUProperties(string body)
    {
        return PropertyTypeRegex.Matches(body)
            .Select(m => new Dictionary<string, object?>
            {
                ["type"] = m.Groups[1].Value.Trim(),
                ["name"] = m.Groups[2].Value.Trim(),
                ["specifiers"] = ExtractSpecifiers(body, m.Index)
            })
            .ToList();
    }

    private static List<Dictionary<string, object?>> ExtractUFunctions(string body)
    {
        return UFunctionRegex.Matches(body)
            .Select(m => new Dictionary<string, object?>
            {
                ["specifiers"] = m.Groups[1].Value.Trim(),
                ["returnType"] = m.Groups[2].Value.Trim(),
                ["name"] = m.Groups[3].Value.Trim()
            })
            .ToList();
    }

    private static string ExtractSpecifiers(string body, int propertyIndex)
    {
        var before = body[..propertyIndex];
        var lastUprop = before.LastIndexOf("UPROPERTY(", StringComparison.Ordinal);
        if (lastUprop < 0) return "";
        var end = before.IndexOf(')', lastUprop);
        if (end < 0) return "";
        return before[(lastUprop + 10)..end].Trim();
    }

    private static string ExtractBracedBlock(string content, int openBraceIndex)
    {
        if (openBraceIndex < 0 || openBraceIndex >= content.Length) return "";
        int depth = 0;
        int start = openBraceIndex;

        for (int i = openBraceIndex; i < content.Length; i++)
        {
            if (content[i] == '{') depth++;
            else if (content[i] == '}') depth--;

            if (depth == 0)
                return content[start..(i + 1)];
        }

        return content[start..];
    }

    private (List<string> Public, List<string> Private) ExtractModuleDependencies(string buildCsContent)
    {
        var publicDeps = ExtractStringArray(buildCsContent, "PublicDependencyModuleNames");
        var privateDeps = ExtractStringArray(buildCsContent, "PrivateDependencyModuleNames");
        return (publicDeps, privateDeps);
    }

    private static List<string> ExtractStringArray(string content, string arrayName)
    {
        var pattern = $@"{arrayName}\s*\.AddRange\s*\(\s*new\s+string\[\]\s*\{{([^}}]+)\}}";
        var match = Regex.Match(content, pattern);
        if (!match.Success)
        {
            pattern = $@"{arrayName}\s*\.Add\s*\(\s*""([^""]+)""";
            return Regex.Matches(content, pattern).Select(m => m.Groups[1].Value).ToList();
        }

        return Regex.Matches(match.Groups[1].Value, @"""([^""]+)""")
            .Select(m => m.Groups[1].Value).ToList();
    }

    private string GetSourceDir()
    {
        if (_context.ProjectPath == null)
            throw new InvalidOperationException("No project loaded.");
        var sourceDir = Path.Combine(Path.GetDirectoryName(_context.ProjectPath)!, "Source");
        if (!Directory.Exists(sourceDir))
            throw new DirectoryNotFoundException($"Source directory not found: {sourceDir}");
        return sourceDir;
    }

    private string ResolveSourcePath(string path)
    {
        if (Path.IsPathRooted(path)) return path;
        if (_context.ProjectPath == null)
            throw new InvalidOperationException("No project loaded.");
        return Path.Combine(Path.GetDirectoryName(_context.ProjectPath)!, path);
    }

    private static string? FindFile(string directory, string fileName)
    {
        var files = Directory.GetFiles(directory, fileName, SearchOption.AllDirectories);
        return files.Length > 0 ? files[0] : null;
    }
}
