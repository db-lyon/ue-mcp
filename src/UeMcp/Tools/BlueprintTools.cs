using System.ComponentModel;
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
}
