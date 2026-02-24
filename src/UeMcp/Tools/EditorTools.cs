using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class EditorTools
{
    [McpServerTool, Description(
        "Execute a console command in the Unreal Editor. Requires a live editor connection. " +
        "Use for any editor automation: loading levels, running commandlets, toggling settings, etc.")]
    public static async Task<string> editor_execute(
        ModeRouter router,
        EditorBridge bridge,
        [Description("The console command to execute in the editor")] string command)
    {
        router.EnsureLiveMode("editor_execute");
        return await bridge.SendAndSerializeAsync("execute_command", new() { ["command"] = command });
    }

    [McpServerTool, Description(
        "Set a property value on an asset through the editor. Requires live editor connection. " +
        "Changes are made through the editor's property system with full undo support.")]
    public static async Task<string> set_property(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the asset")] string assetPath,
        [Description("Name of the export/object to modify")] string objectName,
        [Description("Name of the property to set")] string propertyName,
        [Description("New value for the property (as JSON)")] string value)
    {
        router.EnsureLiveMode("set_property");
        return await bridge.SendAndSerializeAsync("set_property", new()
        {
            ["path"] = assetPath,
            ["objectName"] = objectName,
            ["propertyName"] = propertyName,
            ["value"] = JsonSerializer.Deserialize<object>(value)
        });
    }

    [McpServerTool, Description(
        "Compile a Blueprint in the editor and return compilation results including any errors or warnings. " +
        "Requires live editor connection.")]
    public static async Task<string> compile_blueprint(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset to compile")] string assetPath)
    {
        router.EnsureLiveMode("compile_blueprint");
        return await bridge.SendAndSerializeAsync("compile_blueprint", new() { ["path"] = assetPath });
    }

    [McpServerTool, Description(
        "Create a new Blueprint asset in the editor. Requires live editor connection. " +
        "Specify the parent class and destination path.")]
    public static async Task<string> create_blueprint(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Desired asset path (e.g. '/Game/Blueprints/BP_MyActor')")] string assetPath,
        [Description("Parent class name (e.g. 'Actor', 'Character', 'Pawn')")] string parentClass)
    {
        router.EnsureLiveMode("create_blueprint");
        return await bridge.SendAndSerializeAsync("create_blueprint", new()
        {
            ["path"] = assetPath,
            ["parentClass"] = parentClass
        });
    }

    [McpServerTool, Description(
        "Add a variable to a Blueprint. Requires live editor connection. " +
        "The Blueprint will need to be compiled after adding variables.")]
    public static async Task<string> add_blueprint_variable(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Name for the new variable")] string variableName,
        [Description("Variable type (e.g. 'bool', 'int', 'float', 'FString', 'FVector', 'UObject*')")] string variableType,
        [Description("Optional: default value as JSON")] string? defaultValue = null)
    {
        router.EnsureLiveMode("add_blueprint_variable");
        return await bridge.SendAndSerializeAsync("add_variable", new()
        {
            ["path"] = assetPath,
            ["name"] = variableName,
            ["type"] = variableType,
            ["defaultValue"] = defaultValue
        });
    }

    [McpServerTool, Description(
        "Control Play-in-Editor (PIE) sessions. Start, stop, or check status of PIE. " +
        "Requires live editor connection.")]
    public static async Task<string> play_in_editor(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Action: 'start', 'stop', or 'status'")] string action)
    {
        router.EnsureLiveMode("play_in_editor");
        return await bridge.SendAndSerializeAsync("pie_control", new() { ["action"] = action });
    }

    [McpServerTool, Description(
        "Get a runtime value from an actor or component during a Play-in-Editor session. " +
        "Requires live editor connection with an active PIE session.")]
    public static async Task<string> get_runtime_value(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor name or path in the world")] string actorPath,
        [Description("Property name to read")] string propertyName)
    {
        router.EnsureLiveMode("get_runtime_value");
        return await bridge.SendAndSerializeAsync("get_runtime_value", new()
        {
            ["actorPath"] = actorPath,
            ["propertyName"] = propertyName
        });
    }

    [McpServerTool, Description(
        "Undo the last editor action(s). Reverts Blueprint modifications, property changes, " +
        "actor placement, etc. Use to recover from mistakes.")]
    public static async Task<string> undo(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Number of actions to undo. Default: 1")] int count = 1)
    {
        router.EnsureLiveMode("undo");
        return await bridge.SendAndSerializeAsync("undo", new() { ["count"] = count });
    }

    [McpServerTool, Description(
        "Redo the last undone editor action(s). Reapplies previously undone changes.")]
    public static async Task<string> redo(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Number of actions to redo. Default: 1")] int count = 1)
    {
        router.EnsureLiveMode("redo");
        return await bridge.SendAndSerializeAsync("redo", new() { ["count"] = count });
    }

    [McpServerTool, Description(
        "Save one or all modified assets in the editor. Requires live editor connection.")]
    public static async Task<string> save_asset(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the asset to save, or 'all' to save all modified assets")] string assetPath = "all")
    {
        router.EnsureLiveMode("save_asset");
        return await bridge.SendAndSerializeAsync("save_asset", new() { ["path"] = assetPath });
    }

    [McpServerTool, Description(
        "Add a node to a Blueprint graph. Requires live editor connection. " +
        "Returns the new node's ID for use in wiring connections.")]
    public static async Task<string> add_blueprint_node(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Name of the graph (e.g. 'EventGraph')")] string graphName,
        [Description("Node class type (e.g. 'K2Node_CallFunction', 'K2Node_Event', 'K2Node_IfThenElse')")] string nodeClass,
        [Description("Optional: additional parameters as JSON for node configuration")] string? nodeParams = null)
    {
        router.EnsureLiveMode("add_blueprint_node");
        var parameters = new Dictionary<string, object?>
        {
            ["path"] = assetPath,
            ["graphName"] = graphName,
            ["nodeClass"] = nodeClass
        };

        if (nodeParams != null)
            parameters["nodeParams"] = JsonSerializer.Deserialize<object>(nodeParams);

        return await bridge.SendAndSerializeAsync("add_node", parameters);
    }

    [McpServerTool, Description(
        "Connect two pins between Blueprint nodes. Requires live editor connection.")]
    public static async Task<string> connect_blueprint_pins(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Path to the Blueprint asset")] string assetPath,
        [Description("Source node name")] string sourceNode,
        [Description("Source pin name")] string sourcePin,
        [Description("Target node name")] string targetNode,
        [Description("Target pin name")] string targetPin)
    {
        router.EnsureLiveMode("connect_blueprint_pins");
        return await bridge.SendAndSerializeAsync("connect_pins", new()
        {
            ["path"] = assetPath,
            ["sourceNode"] = sourceNode,
            ["sourcePin"] = sourcePin,
            ["targetNode"] = targetNode,
            ["targetPin"] = targetPin
        });
    }
}
