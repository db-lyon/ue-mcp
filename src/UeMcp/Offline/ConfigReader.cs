using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Logging;
using UeMcp.Core;

namespace UeMcp.Offline;

public class ConfigReader
{
    private readonly ProjectContext _context;
    private readonly ILogger<ConfigReader> _logger;

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    public ConfigReader(ProjectContext context, ILogger<ConfigReader> logger)
    {
        _context = context;
        _logger = logger;
    }

    public string ReadConfig(string configName)
    {
        var path = ResolveConfigPath(configName);
        if (!File.Exists(path))
            throw new FileNotFoundException($"Config file not found: {path}");

        var sections = ParseIni(File.ReadAllLines(path));

        return JsonSerializer.Serialize(new
        {
            path,
            configName,
            sectionCount = sections.Count,
            sections
        }, JsonOpts);
    }

    public string SearchConfig(string query)
    {
        if (_context.ProjectPath == null)
            throw new InvalidOperationException("No project loaded.");

        var configDir = Path.Combine(Path.GetDirectoryName(_context.ProjectPath)!, "Config");
        if (!Directory.Exists(configDir))
            throw new DirectoryNotFoundException($"Config directory not found: {configDir}");

        var results = new List<Dictionary<string, object?>>();

        foreach (var file in Directory.GetFiles(configDir, "*.ini", SearchOption.AllDirectories))
        {
            var lines = File.ReadAllLines(file);
            var currentSection = "";
            var fileName = Path.GetFileName(file);

            for (int i = 0; i < lines.Length; i++)
            {
                var line = lines[i].Trim();

                if (line.StartsWith("[") && line.EndsWith("]"))
                {
                    currentSection = line[1..^1];
                    continue;
                }

                if (line.Contains(query, StringComparison.OrdinalIgnoreCase))
                {
                    results.Add(new()
                    {
                        ["file"] = fileName,
                        ["section"] = currentSection,
                        ["line"] = i + 1,
                        ["content"] = line
                    });
                }
            }
        }

        return JsonSerializer.Serialize(new
        {
            query,
            resultCount = results.Count,
            results = results.Take(200).ToList()
        }, JsonOpts);
    }

    public string ListGameplayTags()
    {
        if (_context.ProjectPath == null)
            throw new InvalidOperationException("No project loaded.");

        var configDir = Path.Combine(Path.GetDirectoryName(_context.ProjectPath)!, "Config");
        var tags = new SortedSet<string>();

        foreach (var file in Directory.GetFiles(configDir, "*.ini", SearchOption.AllDirectories))
        {
            var lines = File.ReadAllLines(file);
            var inTagSection = false;

            foreach (var line in lines)
            {
                var trimmed = line.Trim();

                if (trimmed.StartsWith("[") && trimmed.EndsWith("]"))
                {
                    inTagSection = trimmed.Contains("GameplayTag", StringComparison.OrdinalIgnoreCase);
                    continue;
                }

                if (inTagSection)
                {
                    var match = Regex.Match(trimmed, @"Tag=""?([^""]+)""?");
                    if (match.Success)
                    {
                        tags.Add(match.Groups[1].Value);
                    }
                    else if (trimmed.StartsWith("+GameplayTagList="))
                    {
                        var tagMatch = Regex.Match(trimmed, @"TagName=""([^""]+)""");
                        if (tagMatch.Success)
                            tags.Add(tagMatch.Groups[1].Value);
                    }
                }
            }
        }

        var tagTree = BuildTagTree(tags);

        return JsonSerializer.Serialize(new
        {
            source = "config_files",
            count = tags.Count,
            tags = tags.ToList(),
            tree = tagTree
        }, JsonOpts);
    }

    private string ResolveConfigPath(string configName)
    {
        if (Path.IsPathRooted(configName))
            return configName;

        if (_context.ProjectPath == null)
            throw new InvalidOperationException("No project loaded.");

        var configDir = Path.Combine(Path.GetDirectoryName(_context.ProjectPath)!, "Config");

        if (!configName.EndsWith(".ini", StringComparison.OrdinalIgnoreCase))
            configName += ".ini";

        if (!configName.StartsWith("Default", StringComparison.OrdinalIgnoreCase))
        {
            var defaultPath = Path.Combine(configDir, $"Default{configName}");
            if (File.Exists(defaultPath)) return defaultPath;
        }

        return Path.Combine(configDir, configName);
    }

    private static Dictionary<string, Dictionary<string, string>> ParseIni(string[] lines)
    {
        var sections = new Dictionary<string, Dictionary<string, string>>();
        var currentSection = "Global";

        foreach (var line in lines)
        {
            var trimmed = line.Trim();
            if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith(";") || trimmed.StartsWith("#"))
                continue;

            if (trimmed.StartsWith("[") && trimmed.EndsWith("]"))
            {
                currentSection = trimmed[1..^1];
                if (!sections.ContainsKey(currentSection))
                    sections[currentSection] = new();
                continue;
            }

            if (!sections.ContainsKey(currentSection))
                sections[currentSection] = new();

            var eqIdx = trimmed.IndexOf('=');
            if (eqIdx > 0)
            {
                var key = trimmed[..eqIdx].TrimStart('+', '-', '.');
                var value = trimmed[(eqIdx + 1)..];
                sections[currentSection][key] = value;
            }
        }

        return sections;
    }

    private static Dictionary<string, object> BuildTagTree(SortedSet<string> tags)
    {
        var tree = new Dictionary<string, object>();

        foreach (var tag in tags)
        {
            var parts = tag.Split('.');
            var current = tree;

            foreach (var part in parts)
            {
                if (!current.ContainsKey(part))
                    current[part] = new Dictionary<string, object>();
                current = (Dictionary<string, object>)current[part];
            }
        }

        return tree;
    }
}
