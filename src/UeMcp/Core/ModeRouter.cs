using Microsoft.Extensions.Logging;
using UeMcp.Live;

namespace UeMcp.Core;

public enum OperationMode
{
    Offline,
    Live
}

public class ModeRouter
{
    private readonly ProjectContext _context;
    private readonly EditorBridge _bridge;
    private readonly ILogger<ModeRouter> _logger;
    private readonly Timer _reconnectTimer;

    public OperationMode CurrentMode { get; private set; } = OperationMode.Offline;
    public bool IsEditorConnected => _bridge.IsConnected;

    public ModeRouter(ProjectContext context, EditorBridge bridge, ILogger<ModeRouter> logger)
    {
        _context = context;
        _bridge = bridge;
        _logger = logger;

        _reconnectTimer = new Timer(AttemptReconnect, null, TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(15));
    }

    public async Task TryConnectAsync()
    {
        try
        {
            await _bridge.ConnectAsync();
            if (_bridge.IsConnected)
            {
                CurrentMode = OperationMode.Live;
                _logger.LogInformation("Editor bridge connected — live mode active");
            }
        }
        catch (Exception ex)
        {
            _logger.LogDebug("Editor bridge not available: {Message}", ex.Message);
            CurrentMode = OperationMode.Offline;
        }
    }

    public void TryConnect()
    {
        _ = Task.Run(async () => await TryConnectAsync());
    }

    public string ResolveAssetPath(string assetPath)
    {
        return _context.ResolveContentPath(assetPath);
    }

    public void EnsureProjectLoaded()
    {
        if (!_context.IsLoaded)
            throw new InvalidOperationException("No project loaded. Call set_project with the path to your .uproject file first.");
    }

    public void EnsureLiveMode(string operation)
    {
        if (CurrentMode != OperationMode.Live)
            throw new InvalidOperationException(
                $"Operation '{operation}' requires a live editor connection. Start Unreal Editor with the MCP bridge plugin enabled.");
    }

    private void AttemptReconnect(object? state)
    {
        if (_bridge.IsConnected || !_context.IsLoaded) return;

        _ = Task.Run(async () =>
        {
            try
            {
                await _bridge.ConnectAsync();
                if (_bridge.IsConnected)
                {
                    CurrentMode = OperationMode.Live;
                    _logger.LogInformation("Editor bridge reconnected — switching to live mode");
                }
            }
            catch { /* silent retry */ }
        });
    }
}
