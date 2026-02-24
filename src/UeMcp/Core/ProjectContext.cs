using System.Text.Json;
using UAssetAPI.UnrealTypes;

namespace UeMcp.Core;

public class ProjectContext
{
    public string? ProjectPath { get; private set; }
    public string? ProjectName { get; private set; }
    public string? ContentDir { get; private set; }
    public EngineVersion EngineVersion { get; private set; } = EngineVersion.UNKNOWN;
    public string? EngineAssociation { get; private set; }
    public bool IsLoaded => ProjectPath != null;

    public void SetProject(string path)
    {
        if (path.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
        {
            ProjectPath = Path.GetFullPath(path);
        }
        else
        {
            var files = Directory.GetFiles(path, "*.uproject");
            ProjectPath = files.Length > 0
                ? Path.GetFullPath(files[0])
                : throw new FileNotFoundException($"No .uproject file found in {path}");
        }

        ProjectName = Path.GetFileNameWithoutExtension(ProjectPath);
        ContentDir = Path.Combine(Path.GetDirectoryName(ProjectPath)!, "Content");
        ParseUProject();
    }

    public string ResolveContentPath(string assetPath)
    {
        if (Path.IsPathRooted(assetPath))
            return assetPath;

        if (ContentDir == null)
            throw new InvalidOperationException("No project loaded. Call set_project first.");

        var normalized = assetPath
            .Replace("/Game/", "")
            .Replace("\\", "/");

        if (!normalized.EndsWith(".uasset", StringComparison.OrdinalIgnoreCase) &&
            !normalized.EndsWith(".umap", StringComparison.OrdinalIgnoreCase))
        {
            normalized += ".uasset";
        }

        return Path.Combine(ContentDir, normalized.Replace("/", Path.DirectorySeparatorChar.ToString()));
    }

    public string GetRelativeContentPath(string absolutePath)
    {
        if (ContentDir == null) return absolutePath;
        if (absolutePath.StartsWith(ContentDir, StringComparison.OrdinalIgnoreCase))
        {
            var relative = absolutePath[(ContentDir.Length + 1)..];
            return "/Game/" + relative.Replace("\\", "/").Replace(".uasset", "");
        }
        return absolutePath;
    }

    private void ParseUProject()
    {
        if (ProjectPath == null) return;

        try
        {
            var json = File.ReadAllText(ProjectPath);
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            if (root.TryGetProperty("EngineAssociation", out var engineProp))
            {
                EngineAssociation = engineProp.GetString();
                EngineVersion = EngineVersionResolver.Resolve(EngineAssociation);
            }
        }
        catch
        {
            EngineVersion = EngineVersion.UNKNOWN;
        }
    }
}

public static class EngineVersionResolver
{
    private static readonly Dictionary<string, EngineVersion> VersionMap = new(StringComparer.OrdinalIgnoreCase)
    {
        ["4.18"] = EngineVersion.VER_UE4_18,
        ["4.19"] = EngineVersion.VER_UE4_19,
        ["4.20"] = EngineVersion.VER_UE4_20,
        ["4.21"] = EngineVersion.VER_UE4_21,
        ["4.22"] = EngineVersion.VER_UE4_22,
        ["4.23"] = EngineVersion.VER_UE4_23,
        ["4.24"] = EngineVersion.VER_UE4_24,
        ["4.25"] = EngineVersion.VER_UE4_25,
        ["4.26"] = EngineVersion.VER_UE4_26,
        ["4.27"] = EngineVersion.VER_UE4_27,
        ["5.0"] = EngineVersion.VER_UE5_0,
        ["5.1"] = EngineVersion.VER_UE5_1,
        ["5.2"] = EngineVersion.VER_UE5_2,
        ["5.3"] = EngineVersion.VER_UE5_3,
        ["5.4"] = EngineVersion.VER_UE5_4,
        ["5.5"] = EngineVersion.VER_UE5_5,
    };

    public static EngineVersion Resolve(string? engineAssociation)
    {
        if (string.IsNullOrEmpty(engineAssociation))
            return EngineVersion.UNKNOWN;

        if (VersionMap.TryGetValue(engineAssociation, out var version))
            return version;

        // Custom engine builds use a GUID — default to latest known
        if (Guid.TryParse(engineAssociation, out _))
            return EngineVersion.VER_UE5_5;

        // Try partial match (e.g. "5.4.1" → "5.4")
        var parts = engineAssociation.Split('.');
        if (parts.Length >= 2)
        {
            var majorMinor = $"{parts[0]}.{parts[1]}";
            if (VersionMap.TryGetValue(majorMinor, out var partial))
                return partial;
        }

        return EngineVersion.UNKNOWN;
    }
}
