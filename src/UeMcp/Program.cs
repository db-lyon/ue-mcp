using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;
using UeMcp.Offline;

// Accept .uproject path as first positional arg so the project
// initializes automatically at startup — no set_project call needed.
string? projectArg = args.FirstOrDefault(a => !a.StartsWith('-'));

var builder = Host.CreateApplicationBuilder(args);

builder.Logging.AddConsole(opts =>
{
    opts.LogToStandardErrorThreshold = LogLevel.Trace;
});

builder.Services.AddSingleton<ProjectContext>();
builder.Services.AddSingleton<ModeRouter>();
builder.Services.AddSingleton<AssetService>();
builder.Services.AddSingleton<BlueprintReader>();
builder.Services.AddSingleton<DataTableReader>();
builder.Services.AddSingleton<AssetSearch>();
builder.Services.AddSingleton<ConfigReader>();
builder.Services.AddSingleton<CppHeaderParser>();
builder.Services.AddSingleton<EditorBridge>();
builder.Services.AddSingleton<BridgeDeployer>();

builder.Services
    .AddMcpServer(options =>
    {
        options.ServerInfo = new()
        {
            Name = "ue-mcp",
            Version = "0.1.0"
        };
    })
    .WithStdioServerTransport()
    .WithToolsFromAssembly();

var host = builder.Build();

if (projectArg != null)
{
    var logger = host.Services.GetRequiredService<ILogger<ProjectContext>>();
    var context = host.Services.GetRequiredService<ProjectContext>();
    var deployer = host.Services.GetRequiredService<BridgeDeployer>();
    var router = host.Services.GetRequiredService<ModeRouter>();

    try
    {
        context.SetProject(projectArg);
        logger.LogInformation("Project loaded: {Name} (engine {Version})",
            context.ProjectName, context.EngineVersion);

        var result = deployer.Deploy(context);
        logger.LogInformation("Bridge deploy: {Summary}", result.Summary);

        await router.TryConnectAsync();
        logger.LogInformation("Mode: {Mode}, editor connected: {Connected}",
            router.CurrentMode, router.IsEditorConnected);
    }
    catch (Exception ex)
    {
        logger.LogError(ex, "Failed to auto-initialize project from arg: {Path}", projectArg);
    }
}

await host.RunAsync();
