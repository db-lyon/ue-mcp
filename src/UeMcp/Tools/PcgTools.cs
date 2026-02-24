using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;

namespace UeMcp.Tools;

[McpServerToolType]
public static class PcgTools
{
    [McpServerTool, Description(
        "List PCG graph assets in a directory. Returns graph-relevant metadata.")]
    public static async Task<string> list_pcg_graphs(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Content directory to search (e.g. '/Game/', '/Game/PCG/')")] string directory = "/Game/",
        [Description("Search subdirectories. Default: true")] bool recursive = true)
    {
        router.EnsureLiveMode("list_pcg_graphs");
        return await bridge.SendAndSerializeAsync("list_pcg_graphs", new()
        {
            ["directory"] = directory,
            ["recursive"] = recursive
        });
    }

    [McpServerTool, Description(
        "Read a PCG graph's full structure: all nodes with types, edges between them, parameters. " +
        "The PCG equivalent of read_blueprint — the primary tool for understanding a PCG graph.")]
    public static async Task<string> read_pcg_graph(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the PCG graph")] string graphPath)
    {
        router.EnsureLiveMode("read_pcg_graph");
        return await bridge.SendAndSerializeAsync("read_pcg_graph", new()
        {
            ["graphPath"] = graphPath
        });
    }

    [McpServerTool, Description(
        "Read detailed settings of a specific node in a PCG graph. PCG nodes are config-heavy " +
        "(density, bounds, filters, attribute operations). Use when you need one node's full parameter set.")]
    public static async Task<string> read_pcg_node_settings(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the PCG graph")] string graphPath,
        [Description("Name of the node to inspect")] string nodeName)
    {
        router.EnsureLiveMode("read_pcg_node_settings");
        return await bridge.SendAndSerializeAsync("read_pcg_node_settings", new()
        {
            ["graphPath"] = graphPath,
            ["nodeName"] = nodeName
        });
    }

    [McpServerTool, Description(
        "List all PCG components in the current level: actor, graph reference, generation trigger, seed.")]
    public static async Task<string> get_pcg_components(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Optional: filter by graph name")] string? graphFilter = null,
        [Description("Optional: filter by actor name")] string? nameFilter = null)
    {
        router.EnsureLiveMode("get_pcg_components");
        return await bridge.SendAndSerializeAsync("get_pcg_components", new()
        {
            ["graphFilter"] = graphFilter,
            ["nameFilter"] = nameFilter
        });
    }

    [McpServerTool, Description(
        "Deep inspect a specific PCG component: full settings, managed resources, generation state.")]
    public static async Task<string> get_pcg_component_details(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor name or label containing the PCG component")] string actorName)
    {
        router.EnsureLiveMode("get_pcg_component_details");
        return await bridge.SendAndSerializeAsync("get_pcg_component_details", new()
        {
            ["actorName"] = actorName
        });
    }

    [McpServerTool, Description(
        "Create a new PCG graph asset at a specified path.")]
    public static async Task<string> create_pcg_graph(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Desired asset path (e.g. '/Game/PCG/PCG_RockScatter')")] string graphPath)
    {
        router.EnsureLiveMode("create_pcg_graph");
        return await bridge.SendAndSerializeAsync("create_pcg_graph", new()
        {
            ["graphPath"] = graphPath
        });
    }

    [McpServerTool, Description(
        "Add a node to a PCG graph. Returns the node ID for wiring. Common types: " +
        "PCGSurfaceSampler, PCGStaticMeshSpawner, PCGDensityFilter, PCGPointFilter, " +
        "PCGTransformPoints, PCGBoundsModifier, PCGAttributeNoise.")]
    public static async Task<string> add_pcg_node(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the PCG graph")] string graphPath,
        [Description("Node type class name (e.g. 'PCGSurfaceSampler', 'PCGStaticMeshSpawner')")] string nodeType,
        [Description("Optional: initial settings as JSON")] string? settings = null)
    {
        router.EnsureLiveMode("add_pcg_node");
        var parameters = new Dictionary<string, object?>
        {
            ["graphPath"] = graphPath,
            ["nodeType"] = nodeType,
        };
        if (settings != null)
            parameters["settings"] = JsonSerializer.Deserialize<object>(settings);

        return await bridge.SendAndSerializeAsync("add_pcg_node", parameters);
    }

    [McpServerTool, Description(
        "Connect an output pin of one PCG node to an input pin of another.")]
    public static async Task<string> connect_pcg_nodes(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the PCG graph")] string graphPath,
        [Description("Source node name")] string sourceNode,
        [Description("Source pin name (default: 'Out')")] string sourcePin = "Out",
        [Description("Target node name")] string targetNode = "",
        [Description("Target pin name (default: 'In')")] string targetPin = "In")
    {
        router.EnsureLiveMode("connect_pcg_nodes");
        return await bridge.SendAndSerializeAsync("connect_pcg_nodes", new()
        {
            ["graphPath"] = graphPath,
            ["sourceNode"] = sourceNode,
            ["sourcePin"] = sourcePin,
            ["targetNode"] = targetNode,
            ["targetPin"] = targetPin
        });
    }

    [McpServerTool, Description(
        "Set parameters on an existing PCG node. Partial update — only specified keys change.")]
    public static async Task<string> set_pcg_node_settings(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the PCG graph")] string graphPath,
        [Description("Node name to modify")] string nodeName,
        [Description("Settings to update as JSON (partial update)")] string settings)
    {
        router.EnsureLiveMode("set_pcg_node_settings");
        return await bridge.SendAndSerializeAsync("set_pcg_node_settings", new()
        {
            ["graphPath"] = graphPath,
            ["nodeName"] = nodeName,
            ["settings"] = JsonSerializer.Deserialize<object>(settings)
        });
    }

    [McpServerTool, Description(
        "Remove a node and its connections from a PCG graph.")]
    public static async Task<string> remove_pcg_node(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the PCG graph")] string graphPath,
        [Description("Node name to remove")] string nodeName)
    {
        router.EnsureLiveMode("remove_pcg_node");
        return await bridge.SendAndSerializeAsync("remove_pcg_node", new()
        {
            ["graphPath"] = graphPath,
            ["nodeName"] = nodeName
        });
    }

    [McpServerTool, Description(
        "Trigger regeneration of a PCG component in the level. Returns generation stats.")]
    public static async Task<string> execute_pcg_graph(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Actor name or label with the PCG component")] string actorName,
        [Description("Optional: seed override")] int? seed = null)
    {
        router.EnsureLiveMode("execute_pcg_graph");
        return await bridge.SendAndSerializeAsync("execute_pcg_graph", new()
        {
            ["actorName"] = actorName,
            ["seed"] = seed
        });
    }

    [McpServerTool, Description(
        "Place a PCG volume in the level with a graph, bounds, and seed. " +
        "Combines place_actor + PCG component setup in one call.")]
    public static async Task<string> add_pcg_volume(
        ModeRouter router,
        EditorBridge bridge,
        [Description("Asset path to the PCG graph to assign")] string graphPath,
        [Description("World location as JSON: {\"x\": 0, \"y\": 0, \"z\": 0}")] string? location = null,
        [Description("Bounds extents as JSON: {\"x\": 1000, \"y\": 1000, \"z\": 500}")] string? bounds = null,
        [Description("Random seed. Default: 42")] int seed = 42,
        [Description("Optional: label for the volume actor")] string? label = null)
    {
        router.EnsureLiveMode("add_pcg_volume");
        return await bridge.SendAndSerializeAsync("add_pcg_volume", new()
        {
            ["graphPath"] = graphPath,
            ["location"] = ParseJson(location, new Dictionary<string, object?> { ["x"] = 0.0, ["y"] = 0.0, ["z"] = 0.0 }),
            ["bounds"] = ParseJson(bounds, new Dictionary<string, object?> { ["x"] = 1000.0, ["y"] = 1000.0, ["z"] = 500.0 }),
            ["seed"] = seed,
            ["label"] = label
        });
    }

    private static object? ParseJson(string? json, object? defaultValue)
    {
        if (string.IsNullOrWhiteSpace(json)) return defaultValue;
        try { return JsonSerializer.Deserialize<object>(json); }
        catch { return defaultValue; }
    }
}
