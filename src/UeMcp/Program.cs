using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ModelContextProtocol.Server;
using UeMcp.Core;
using UeMcp.Live;
using UeMcp.Offline;

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

await builder.Build().RunAsync();
