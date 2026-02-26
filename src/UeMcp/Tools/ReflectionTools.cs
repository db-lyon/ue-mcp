using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class ReflectionTools
{
    [McpServerTool, Description(
        "Reflect a UClass from the running editor: parent chain, properties with types and defaults, " +
        "functions with signatures, class flags, and implemented interfaces. " +
        "This is the primary tool for understanding any C++ or Blueprint class. " +
        "Requires live editor connection.")]
    public static async Task<string> reflect_class(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Class name (e.g. 'Character', 'AMyCharacter', 'Actor', 'PlayerController')")] string className,
        [Description("Include inherited properties/functions from parent classes. Default: false")] bool includeInherited = false)
    {
        router.EnsureLiveMode("reflect_class");
        return await bridge.SendAndSerializeAsync("reflect_class", new()
        {
            ["className"] = className,
            ["includeInherited"] = includeInherited
        });
    }

    [McpServerTool, Description(
        "Reflect a UScriptStruct from the running editor: fields with types. " +
        "Use for understanding struct types like FVector, FHitResult, FGameplayTag, or custom structs.")]
    public static async Task<string> reflect_struct(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Struct name (e.g. 'Vector', 'HitResult', 'Transform', or full path '/Script/Engine.HitResult')")] string structName)
    {
        router.EnsureLiveMode("reflect_struct");
        return await bridge.SendAndSerializeAsync("reflect_struct", new()
        {
            ["structName"] = structName
        });
    }

    [McpServerTool, Description(
        "Reflect a UEnum from the running editor: all values with display names. " +
        "Use for understanding enum types like ECollisionChannel, EMovementMode, or custom enums.")]
    public static async Task<string> reflect_enum(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Enum name (e.g. 'ECollisionChannel', 'EMovementMode', 'ENetRole')")] string enumName)
    {
        router.EnsureLiveMode("reflect_enum");
        return await bridge.SendAndSerializeAsync("reflect_enum", new()
        {
            ["enumName"] = enumName
        });
    }

    [McpServerTool, Description(
        "List classes known to the editor, optionally filtered by parent class. " +
        "Without a filter, returns common base classes. With a filter like 'Actor', returns all Actor subclasses.")]
    public static async Task<string> list_classes(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Optional: parent class to filter by (e.g. 'Actor', 'ActorComponent', 'UserWidget')")] string? parentFilter = null,
        [Description("Maximum results. Default: 100")] int limit = 100)
    {
        router.EnsureLiveMode("list_classes");
        return await bridge.SendAndSerializeAsync("list_classes", new()
        {
            ["parentFilter"] = parentFilter,
            ["limit"] = limit
        });
    }

    [McpServerTool, Description(
        "Get the full GameplayTag hierarchy from the running editor. " +
        "Optionally filter by tag prefix (e.g. 'Item', 'Ability', 'Status').")]
    public static async Task<string> list_gameplay_tags(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Optional: filter tags by prefix (e.g. 'Item', 'Ability.Cooldown')")] string? filter = null)
    {
        router.EnsureLiveMode("list_gameplay_tags");
        return await bridge.SendAndSerializeAsync("list_gameplay_tags", new()
        {
            ["filter"] = filter ?? ""
        });
    }

    [McpServerTool, Description(
        "Add a new gameplay tag to the project. Tags are hierarchical " +
        "(e.g. 'Combat.Damage.Fire'). May require editor restart to take effect.")]
    public static async Task<string> create_gameplay_tag(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Tag to add (e.g. 'Combat.Damage.Fire')")] string tag,
        [Description("Optional: developer comment for this tag")] string comment = "")
    {
        router.EnsureLiveMode("create_gameplay_tag");
        return await bridge.SendAndSerializeAsync("create_gameplay_tag", new()
        {
            ["tag"] = tag,
            ["comment"] = comment
        });
    }
}
