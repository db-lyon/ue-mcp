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

    [McpServerTool, Description(
        "Select actors in the editor by label. Use to highlight actors or prepare for bulk operations.")]
    public static async Task<string> select_actors(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Array of actor labels to select (JSON array, e.g. '[\"Light1\", \"Wall2\"]')")] string labels,
        [Description("Add to existing selection instead of replacing it. Default: false")] bool addToSelection = false)
    {
        router.EnsureLiveMode("select_actors");
        var labelArray = System.Text.Json.JsonSerializer.Deserialize<string[]>(labels) ?? [];
        return await bridge.SendAndSerializeAsync("select_actors", new()
        {
            ["labels"] = labelArray,
            ["addToSelection"] = addToSelection
        });
    }

    [McpServerTool, Description(
        "Get the currently selected actors in the editor. Returns label, class, and location for each.")]
    public static async Task<string> get_selected_actors(
        ModeRouter router,
        EditorBridge bridge)
    {
        router.EnsureLiveMode("get_selected_actors");
        return await bridge.SendAndSerializeAsync("get_selected_actors", new());
    }

    [McpServerTool, Description(
        "Add a component to an actor already placed in the level. " +
        "For example, add a PointLightComponent, AudioComponent, or BoxCollisionComponent to any actor.")]
    public static async Task<string> add_component_to_actor(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label in the level")] string actorLabel,
        [Description("Component class name (e.g. 'PointLightComponent', 'AudioComponent', 'BoxComponent')")] string componentClass,
        [Description("Optional: name for the new component")] string? componentName = null)
    {
        router.EnsureLiveMode("add_component_to_actor");
        return await bridge.SendAndSerializeAsync("add_component_to_actor", new()
        {
            ["actorLabel"] = actorLabel,
            ["componentClass"] = componentClass,
            ["componentName"] = componentName ?? ""
        });
    }

    [McpServerTool, Description(
        "Set a property on a specific component of an actor in the level.")]
    public static async Task<string> set_component_property(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor label in the level")] string actorLabel,
        [Description("Property name to set")] string propertyName,
        [Description("New value (JSON)")] string value,
        [Description("Optional: component class to target (e.g. 'StaticMeshComponent'). If omitted, uses first component.")] string? componentClass = null)
    {
        router.EnsureLiveMode("set_component_property");
        return await bridge.SendAndSerializeAsync("set_component_property", new()
        {
            ["actorLabel"] = actorLabel,
            ["componentClass"] = componentClass ?? "",
            ["propertyName"] = propertyName,
            ["value"] = System.Text.Json.JsonSerializer.Deserialize<object>(value)
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
