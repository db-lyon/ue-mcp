using System.ComponentModel;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class LevelTools
{
    [McpServerTool, Description(
        "Get all actors in the current level (world outliner). " +
        "Returns name, label, class, location, rotation, and folder for each actor. " +
        "Optionally filter by class name or actor name/label.")]
    public static async Task<string> get_world_outliner(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Optional: filter by class name (e.g. 'StaticMeshActor', 'PointLight', 'Character')")] string? classFilter = null,
        [Description("Optional: filter by actor name or label")] string? nameFilter = null,
        [Description("Maximum actors to return. Default: 500")] int limit = 500)
    {
        router.EnsureLiveMode("get_world_outliner");
        return await bridge.SendAndSerializeAsync("get_world_outliner", new()
        {
            ["classFilter"] = classFilter,
            ["nameFilter"] = nameFilter,
            ["limit"] = limit
        });
    }

    [McpServerTool, Description(
        "Place a new actor in the current level. Specify the class, position, rotation, " +
        "and optional label/folder. The actor is spawned immediately and visible in the viewport.")]
    public static async Task<string> place_actor(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor class (e.g. 'StaticMeshActor', 'PointLight', 'CameraActor', or a Blueprint path)")] string className,
        [Description("World location as JSON: {\"x\": 0, \"y\": 0, \"z\": 0}")] string? location = null,
        [Description("World rotation as JSON: {\"pitch\": 0, \"yaw\": 0, \"roll\": 0}")] string? rotation = null,
        [Description("Optional: human-readable label for the actor")] string? label = null,
        [Description("Optional: outliner folder path (e.g. 'Lighting/Fill')")] string? folder = null)
    {
        router.EnsureLiveMode("place_actor");

        var locDict = ParseJsonOrDefault(location, new Dictionary<string, object?> { ["x"] = 0.0, ["y"] = 0.0, ["z"] = 0.0 });
        var rotDict = ParseJsonOrDefault(rotation, new Dictionary<string, object?> { ["pitch"] = 0.0, ["yaw"] = 0.0, ["roll"] = 0.0 });

        return await bridge.SendAndSerializeAsync("place_actor", new()
        {
            ["className"] = className,
            ["location"] = locDict,
            ["rotation"] = rotDict,
            ["label"] = label,
            ["folder"] = folder
        });
    }

    [McpServerTool, Description(
        "Delete an actor from the current level by name or label.")]
    public static async Task<string> delete_actor(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor name or label to delete")] string actorName)
    {
        router.EnsureLiveMode("delete_actor");
        return await bridge.SendAndSerializeAsync("delete_actor", new()
        {
            ["actorName"] = actorName
        });
    }

    [McpServerTool, Description(
        "Get detailed information about a specific actor: all components, transform, tags, and properties.")]
    public static async Task<string> get_actor_details(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor name or label")] string actorName)
    {
        router.EnsureLiveMode("get_actor_details");
        return await bridge.SendAndSerializeAsync("get_actor_details", new()
        {
            ["actorName"] = actorName
        });
    }

    [McpServerTool, Description(
        "Move, rotate, or scale an actor in the level. Only specify the transforms you want to change.")]
    public static async Task<string> move_actor(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor name or label")] string actorName,
        [Description("New location as JSON: {\"x\": 0, \"y\": 0, \"z\": 0}. Omit to keep current.")] string? location = null,
        [Description("New rotation as JSON: {\"pitch\": 0, \"yaw\": 0, \"roll\": 0}. Omit to keep current.")] string? rotation = null,
        [Description("New scale as JSON: {\"x\": 1, \"y\": 1, \"z\": 1}. Omit to keep current.")] string? scale = null)
    {
        router.EnsureLiveMode("move_actor");
        return await bridge.SendAndSerializeAsync("move_actor", new()
        {
            ["actorName"] = actorName,
            ["location"] = location != null ? ParseJsonOrDefault(location, null) : null,
            ["rotation"] = rotation != null ? ParseJsonOrDefault(rotation, null) : null,
            ["scale"] = scale != null ? ParseJsonOrDefault(scale, null) : null
        });
    }

    private static Dictionary<string, object?>? ParseJsonOrDefault(string? json, Dictionary<string, object?>? defaultValue)
    {
        if (string.IsNullOrWhiteSpace(json)) return defaultValue;
        try
        {
            var doc = System.Text.Json.JsonDocument.Parse(json);
            var result = new Dictionary<string, object?>();
            foreach (var prop in doc.RootElement.EnumerateObject())
            {
                result[prop.Name] = prop.Value.ValueKind switch
                {
                    System.Text.Json.JsonValueKind.Number => prop.Value.GetDouble(),
                    System.Text.Json.JsonValueKind.String => prop.Value.GetString(),
                    System.Text.Json.JsonValueKind.True => true,
                    System.Text.Json.JsonValueKind.False => false,
                    _ => prop.Value.ToString()
                };
            }
            return result;
        }
        catch
        {
            return defaultValue;
        }
    }
}
