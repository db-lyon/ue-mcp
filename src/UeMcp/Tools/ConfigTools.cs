using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Offline;

namespace UeMcp.Tools;

[McpServerToolType]
public static class ConfigTools
{
    [McpServerTool, Description(
        "Read a UE config/INI file and return parsed sections and key-value pairs. " +
        "Accepts config name (e.g. 'Engine', 'Game', 'Input', 'GameplayTags') which resolves to " +
        "Default<Name>.ini, or a direct path. Config files control physics, rendering, input, tags, etc.")]
    public static string read_config(
        ModeRouter router,
        ConfigReader reader,
        [Description("Config name (e.g. 'Engine', 'Game', 'Input') or path to .ini file")] string configName)
    {
        router.EnsureProjectLoaded();
        return reader.ReadConfig(configName);
    }

    [McpServerTool, Description(
        "Search across all project config files for a key, value, or section name. " +
        "Returns matching lines with file, section, and line number context.")]
    public static string search_config(
        ModeRouter router,
        ConfigReader reader,
        [Description("Search query — matches against keys, values, and section names")] string query)
    {
        router.EnsureProjectLoaded();
        return reader.SearchConfig(query);
    }

    [McpServerTool, Description(
        "List GameplayTags defined in the project's config files. " +
        "Returns the flat tag list and a hierarchical tree structure. " +
        "In live mode, prefer list_gameplay_tags from ReflectionTools for runtime-registered tags.")]
    public static string list_config_tags(
        ModeRouter router,
        ConfigReader reader)
    {
        router.EnsureProjectLoaded();
        return reader.ListGameplayTags();
    }
}
