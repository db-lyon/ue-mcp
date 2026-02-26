using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;
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

    [McpServerTool, Description(
        "Write a value to a project config/INI file (e.g. DefaultEngine.ini, DefaultGame.ini). " +
        "Creates the section if it doesn't exist. Use for changing project settings like " +
        "physics, rendering, packaging, etc.")]
    public static async Task<string> set_config(
        ModeRouter router,
        EditorBridge bridge,
        [Description("INI section name (e.g. '/Script/Engine.PhysicsSettings', '/Script/Engine.RendererSettings')")] string section,
        [Description("Key name")] string key,
        [Description("Value to set")] string value,
        [Description("Config file name (e.g. 'DefaultEngine.ini', 'DefaultGame.ini'). Default: 'DefaultEngine.ini'")] string configFile = "DefaultEngine.ini")
    {
        router.EnsureLiveMode("set_config");
        return await bridge.SendAndSerializeAsync("set_config", new()
        {
            ["configFile"] = configFile,
            ["section"] = section,
            ["key"] = key,
            ["value"] = value
        });
    }
}
