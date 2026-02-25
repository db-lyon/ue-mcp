using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;
using UeMcp.Core;

namespace UeMcp.Tools;

[McpServerToolType]
public static class StatusTools
{
    [McpServerTool, Description(
        "Get the current status of the UE MCP server including operation mode (offline/live), " +
        "loaded project info, engine version, and editor connection state. " +
        "Call this first to understand what capabilities are available.")]
    public static string get_status(ModeRouter router, ProjectContext context)
    {
        return JsonSerializer.Serialize(new
        {
            mode = router.CurrentMode.ToString().ToLower(),
            editorConnected = router.IsEditorConnected,
            project = context.IsLoaded ? new
            {
                name = context.ProjectName,
                path = context.ProjectPath,
                contentDir = context.ContentDir,
                engineVersion = context.EngineVersion.ToString(),
                engineAssociation = context.EngineAssociation
            } : null
        }, new JsonSerializerOptions { WriteIndented = true });
    }

    [McpServerTool, Description(
        "Set the Unreal Engine project to work with. Provide the path to the .uproject file " +
        "or the directory containing it. This must be called before using any asset/blueprint tools. " +
        "Automatically detects the engine version, deploys the editor bridge plugin if needed, " +
        "and attempts to connect to a running editor.")]
    public static async Task<string> set_project(
        ModeRouter router,
        ProjectContext context,
        BridgeDeployer deployer,
        [Description("Absolute path to the .uproject file or directory containing it")] string projectPath)
    {
        context.SetProject(projectPath);

        var deployResult = deployer.Deploy(context);

        await router.TryConnectAsync();

        return JsonSerializer.Serialize(new
        {
            success = true,
            projectName = context.ProjectName,
            contentDir = context.ContentDir,
            engineVersion = context.EngineVersion.ToString(),
            engineAssociation = context.EngineAssociation,
            mode = router.CurrentMode.ToString().ToLower(),
            editorConnected = router.IsEditorConnected,
            bridgeSetup = deployResult.Summary
        }, new JsonSerializerOptions { WriteIndented = true });
    }

    [McpServerTool, Description(
        "Get detailed information from the .uproject file including plugins, target platforms, " +
        "modules, and engine association.")]
    public static string get_project_info(
        ModeRouter router,
        ProjectContext context)
    {
        router.EnsureProjectLoaded();

        var json = File.ReadAllText(context.ProjectPath!);
        using var doc = JsonDocument.Parse(json);

        return JsonSerializer.Serialize(new
        {
            projectName = context.ProjectName,
            engineVersion = context.EngineVersion.ToString(),
            engineAssociation = context.EngineAssociation,
            contentDir = context.ContentDir,
            uprojectContents = doc.RootElement
        }, new JsonSerializerOptions { WriteIndented = true });
    }
}
