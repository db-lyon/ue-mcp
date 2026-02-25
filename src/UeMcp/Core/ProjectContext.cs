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
        if (ContentDir == null)
            throw new InvalidOperationException("No project loaded. Call set_project first.");

        if (IsGamePath(assetPath))
        {
            var stripped = StripGamePrefix(assetPath);
            if (!stripped.EndsWith(".uasset", StringComparison.OrdinalIgnoreCase) &&
                !stripped.EndsWith(".umap", StringComparison.OrdinalIgnoreCase))
                stripped += ".uasset";
            return Path.Combine(ContentDir, stripped.Replace("/", Path.DirectorySeparatorChar.ToString()));
        }

        if (Path.IsPathRooted(assetPath))
            return assetPath;

        var normalized = assetPath.Replace("\\", "/");
        if (!normalized.EndsWith(".uasset", StringComparison.OrdinalIgnoreCase) &&
            !normalized.EndsWith(".umap", StringComparison.OrdinalIgnoreCase))
        {
            normalized += ".uasset";
        }

        return Path.Combine(ContentDir, normalized.Replace("/", Path.DirectorySeparatorChar.ToString()));
    }

    public string ResolveContentDir(string directoryPath)
    {
        if (ContentDir == null)
            throw new InvalidOperationException("No project loaded. Call set_project first.");

        if (IsGamePath(directoryPath))
        {
            var stripped = StripGamePrefix(directoryPath).TrimEnd('/');
            return Path.Combine(ContentDir, stripped.Replace("/", Path.DirectorySeparatorChar.ToString()));
        }

        if (Path.IsPathRooted(directoryPath))
            return directoryPath;

        var normalized = directoryPath
            .Replace("\\", "/")
            .TrimEnd('/');

        return Path.Combine(ContentDir, normalized.Replace("/", Path.DirectorySeparatorChar.ToString()));
    }

    private static bool IsGamePath(string path) =>
        path.StartsWith("/Game/", StringComparison.OrdinalIgnoreCase) ||
        path.Equals("/Game", StringComparison.OrdinalIgnoreCase);

    private static string StripGamePrefix(string path) =>
        path.StartsWith("/Game/", StringComparison.OrdinalIgnoreCase)
            ? path[6..]
            : path.Equals("/Game", StringComparison.OrdinalIgnoreCase)
                ? ""
                : path;

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
    public static readonly EngineVersion LatestKnown;

    static EngineVersionResolver()
    {
        // Discover the highest non-sentinel value in UAssetAPI's EngineVersion enum
        LatestKnown = Enum.GetValues<EngineVersion>()
            .Where(v => v != EngineVersion.UNKNOWN
                     && v != EngineVersion.VER_UE4_AUTOMATIC_VERSION_PLUS_ONE
                     && v != EngineVersion.VER_UE4_AUTOMATIC_VERSION)
            .Max();
    }

    public static EngineVersion Resolve(string? engineAssociation)
    {
        if (string.IsNullOrEmpty(engineAssociation))
            return EngineVersion.UNKNOWN;

        // Try direct enum lookup: "5.7" -> "VER_UE5_7", "4.27" -> "VER_UE4_27"
        if (TryResolveFromVersion(engineAssociation, out var version))
            return version;

        // "5.7.1" -> try "5.7"
        var parts = engineAssociation.Split('.');
        if (parts.Length > 2 && TryResolveFromVersion($"{parts[0]}.{parts[1]}", out var partial))
            return partial;

        // Custom engine builds use a GUID — default to latest known
        if (Guid.TryParse(engineAssociation, out _))
            return LatestKnown;

        return EngineVersion.UNKNOWN;
    }

    private static bool TryResolveFromVersion(string versionString, out EngineVersion result)
    {
        var parts = versionString.Split('.');
        if (parts.Length != 2 || !int.TryParse(parts[0], out var major) || !int.TryParse(parts[1], out var minor))
        {
            result = EngineVersion.UNKNOWN;
            return false;
        }

        var enumName = major >= 5 ? $"VER_UE5_{minor}" : $"VER_UE4_{minor}";
        return Enum.TryParse(enumName, out result);
    }
}
