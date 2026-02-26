using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;
using UeMcp.Offline;

namespace UeMcp.Tools;

[McpServerToolType]
public static class BlueprintTools
{
    [McpServerTool, Description(
        "Read a Blueprint asset's full structure: parent class, interfaces, variables with defaults, " +
        "functions with parameters, graph names, and Simple Construction Script components. " +
        "This is the primary tool for understanding a Blueprint's architecture.")]
    public static async Task<string> read_blueprint(
        ModeRouter router,
        BlueprintReader reader,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath)
    {
        router.EnsureProjectLoaded();

        if (router.CurrentMode == OperationMode.Live)
        {
            try
            {
                return await bridge.SendAndSerializeAsync("read_blueprint", new() { ["path"] = assetPath });
            }
            catch { /* fall through */ }
        }

        var resolved = router.ResolveAssetPath(assetPath);
        return reader.ReadBlueprint(resolved);
    }

    [McpServerTool, Description(
        "List all variables defined in a Blueprint, including their types, flags, and default values.")]
    public static string list_blueprint_variables(
        ModeRouter router,
        BlueprintReader reader,
        [Description("Path to the Blueprint asset")] string assetPath)
    {
        router.EnsureProjectLoaded();
        var resolved = router.ResolveAssetPath(assetPath);
        return reader.ListVariables(resolved);
    }

    [McpServerTool, Description(
        "List all functions defined in a Blueprint, including their parameters, flags, and bytecode size.")]
    public static string list_blueprint_functions(
        ModeRouter router,
        BlueprintReader reader,
        [Description("Path to the Blueprint asset")] string assetPath)
    {
        router.EnsureProjectLoaded();
        var resolved = router.ResolveAssetPath(assetPath);
        return reader.ListFunctions(resolved);
    }

    [McpServerTool, Description(
        "Read a specific graph within a Blueprint (e.g. EventGraph, a custom function graph). " +
        "Returns the nodes in the graph with their types and property data.")]
    public static string read_blueprint_graph(
        ModeRouter router,
        BlueprintReader reader,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Name of the graph to read (e.g. 'EventGraph', 'MyFunction')")] string graphName)
    {
        router.EnsureProjectLoaded();
        var resolved = router.ResolveAssetPath(assetPath);
        return reader.ReadGraph(resolved, graphName);
    }

    [McpServerTool, Description(
        "Set properties on a Blueprint variable: instance editable (public/private), " +
        "blueprint read only, category, tooltip, replication. Requires live editor connection.")]
    public static async Task<string> set_blueprint_variable_properties(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Name of the variable to modify")] string name,
        [Description("Make variable editable per-instance in the editor")] bool? instanceEditable = null,
        [Description("Make variable read-only in Blueprint graphs")] bool? blueprintReadOnly = null,
        [Description("Variable category in the details panel")] string? category = null,
        [Description("Tooltip text")] string? tooltip = null,
        [Description("Replication: 'none', 'replicated', or 'repNotify'")] string? replicationType = null)
    {
        router.EnsureLiveMode("set_blueprint_variable_properties");
        var parameters = new Dictionary<string, object?>
        {
            ["path"] = assetPath,
            ["name"] = name,
        };
        if (instanceEditable.HasValue) parameters["instanceEditable"] = instanceEditable.Value;
        if (blueprintReadOnly.HasValue) parameters["blueprintReadOnly"] = blueprintReadOnly.Value;
        if (category != null) parameters["category"] = category;
        if (tooltip != null) parameters["tooltip"] = tooltip;
        if (replicationType != null) parameters["replicationType"] = replicationType;

        return await bridge.SendAndSerializeAsync("set_variable_properties", parameters);
    }

    [McpServerTool, Description(
        "Create a new function graph in a Blueprint. After creation, use add_blueprint_node " +
        "and connect_blueprint_pins to build the function logic. Requires live editor connection.")]
    public static async Task<string> create_blueprint_function(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Name for the new function")] string functionName)
    {
        router.EnsureLiveMode("create_blueprint_function");
        return await bridge.SendAndSerializeAsync("create_function", new()
        {
            ["path"] = assetPath,
            ["functionName"] = functionName,
        });
    }

    [McpServerTool, Description(
        "Delete a function graph from a Blueprint. Requires live editor connection.")]
    public static async Task<string> delete_blueprint_function(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Name of the function to delete")] string functionName)
    {
        router.EnsureLiveMode("delete_blueprint_function");
        return await bridge.SendAndSerializeAsync("delete_function", new()
        {
            ["path"] = assetPath,
            ["functionName"] = functionName,
        });
    }

    [McpServerTool, Description(
        "Rename a function or graph in a Blueprint. Requires live editor connection.")]
    public static async Task<string> rename_blueprint_function(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Current name of the function")] string oldName,
        [Description("New name for the function")] string newName)
    {
        router.EnsureLiveMode("rename_blueprint_function");
        return await bridge.SendAndSerializeAsync("rename_function", new()
        {
            ["path"] = assetPath,
            ["oldName"] = oldName,
            ["newName"] = newName,
        });
    }

    [McpServerTool, Description(
        "Delete a node from a Blueprint graph. Requires live editor connection.")]
    public static async Task<string> delete_blueprint_node(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Name of the graph containing the node")] string graphName,
        [Description("Name of the node to delete")] string nodeName)
    {
        router.EnsureLiveMode("delete_blueprint_node");
        return await bridge.SendAndSerializeAsync("delete_node", new()
        {
            ["path"] = assetPath,
            ["graphName"] = graphName,
            ["nodeName"] = nodeName,
        });
    }

    [McpServerTool, Description(
        "Set a property on a Blueprint graph node. Requires live editor connection.")]
    public static async Task<string> set_blueprint_node_property(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Name of the graph containing the node")] string graphName,
        [Description("Name of the node")] string nodeName,
        [Description("Property name to set")] string propertyName,
        [Description("New value (as JSON)")] string value)
    {
        router.EnsureLiveMode("set_blueprint_node_property");
        return await bridge.SendAndSerializeAsync("set_node_property", new()
        {
            ["path"] = assetPath,
            ["graphName"] = graphName,
            ["nodeName"] = nodeName,
            ["propertyName"] = propertyName,
            ["value"] = JsonSerializer.Deserialize<object>(value),
        });
    }

    [McpServerTool, Description(
        "Add a component to a Blueprint (e.g. StaticMeshComponent, BoxCollisionComponent, " +
        "AudioComponent). Requires live editor connection.")]
    public static async Task<string> add_blueprint_component(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Component class name (e.g. 'StaticMeshComponent', 'BoxCollisionComponent')")] string componentClass,
        [Description("Optional: name for the component")] string? componentName = null)
    {
        router.EnsureLiveMode("add_blueprint_component");
        return await bridge.SendAndSerializeAsync("add_component", new()
        {
            ["path"] = assetPath,
            ["componentClass"] = componentClass,
            ["componentName"] = componentName ?? componentClass,
        });
    }

    [McpServerTool, Description(
        "Create a Blueprint Interface asset. Blueprint Interfaces define function signatures " +
        "that multiple Blueprints can implement, enabling polymorphic communication.")]
    public static async Task<string> create_blueprint_interface(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path for the interface (e.g. '/Game/Interfaces/BPI_Interactable')")] string path)
    {
        router.EnsureLiveMode("create_blueprint_interface");
        return await bridge.SendAndSerializeAsync("create_blueprint_interface", new()
        {
            ["path"] = path
        });
    }

    [McpServerTool, Description(
        "Add a Blueprint Interface to a Blueprint's implemented interfaces list. " +
        "After adding, the Blueprint can implement the interface's functions.")]
    public static async Task<string> add_blueprint_interface(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint")] string blueprintPath,
        [Description("Path to the Blueprint Interface to implement")] string interfacePath)
    {
        router.EnsureLiveMode("add_blueprint_interface");
        return await bridge.SendAndSerializeAsync("add_blueprint_interface", new()
        {
            ["blueprintPath"] = blueprintPath,
            ["interfacePath"] = interfacePath
        });
    }

    [McpServerTool, Description(
        "Add an event dispatcher (multicast delegate) to a Blueprint. " +
        "Event dispatchers allow Blueprints to broadcast events that other objects can subscribe to.")]
    public static async Task<string> add_event_dispatcher(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint")] string blueprintPath,
        [Description("Name for the event dispatcher")] string name)
    {
        router.EnsureLiveMode("add_event_dispatcher");
        return await bridge.SendAndSerializeAsync("add_event_dispatcher", new()
        {
            ["blueprintPath"] = blueprintPath,
            ["name"] = name
        });
    }
}
