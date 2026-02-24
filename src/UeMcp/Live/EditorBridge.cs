using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging;

namespace UeMcp.Live;

public class EditorBridge : IDisposable
{
    private readonly ILogger<EditorBridge> _logger;
    private ClientWebSocket? _ws;
    private CancellationTokenSource? _cts;
    private Task? _receiveTask;
    private readonly ConcurrentDictionary<string, TaskCompletionSource<BridgeResponse>> _pending = new();

    public string Host { get; set; } = "localhost";
    public int Port { get; set; } = 9877;
    public bool IsConnected => _ws?.State == WebSocketState.Open;

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    public EditorBridge(ILogger<EditorBridge> logger)
    {
        _logger = logger;
    }

    public async Task ConnectAsync(CancellationToken ct = default)
    {
        if (IsConnected) return;

        _ws?.Dispose();
        _ws = new ClientWebSocket();
        _cts = CancellationTokenSource.CreateLinkedTokenSource(ct);

        var uri = new Uri($"ws://{Host}:{Port}");
        _logger.LogDebug("Connecting to editor bridge at {Uri}", uri);

        using var timeoutCts = new CancellationTokenSource(TimeSpan.FromSeconds(3));
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(_cts.Token, timeoutCts.Token);

        await _ws.ConnectAsync(uri, linked.Token);
        _logger.LogInformation("Connected to editor bridge at {Uri}", uri);

        _receiveTask = Task.Run(() => ReceiveLoop(_cts.Token), _cts.Token);
    }

    public async Task DisconnectAsync()
    {
        if (_ws == null) return;

        _cts?.Cancel();

        if (_ws.State == WebSocketState.Open)
        {
            try
            {
                await _ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "Disconnecting", CancellationToken.None);
            }
            catch { }
        }

        _ws.Dispose();
        _ws = null;
    }

    public async Task<BridgeResponse> SendAsync(string method, Dictionary<string, object?>? parameters = null, int timeoutMs = 30000)
    {
        if (!IsConnected)
            throw new InvalidOperationException("Not connected to editor bridge");

        var request = new BridgeRequest
        {
            Method = method,
            Params = parameters
        };

        var tcs = new TaskCompletionSource<BridgeResponse>();
        _pending[request.Id] = tcs;

        var json = JsonSerializer.Serialize(request, JsonOpts);
        var bytes = Encoding.UTF8.GetBytes(json);
        await _ws!.SendAsync(bytes, WebSocketMessageType.Text, true, _cts?.Token ?? CancellationToken.None);

        _logger.LogDebug("Sent bridge request: {Method} (id={Id})", method, request.Id);

        using var timeoutCts = new CancellationTokenSource(timeoutMs);
        timeoutCts.Token.Register(() => tcs.TrySetCanceled());

        try
        {
            return await tcs.Task;
        }
        finally
        {
            _pending.TryRemove(request.Id, out _);
        }
    }

    public async Task<string> SendAndSerializeAsync(string method, Dictionary<string, object?>? parameters = null)
    {
        var response = await SendAsync(method, parameters);

        if (!response.IsSuccess)
            throw new InvalidOperationException($"Bridge error: {response.Error?.Message}");

        return response.Result?.GetRawText() ?? "null";
    }

    private async Task ReceiveLoop(CancellationToken ct)
    {
        var buffer = new byte[64 * 1024];
        var messageBuffer = new List<byte>();

        while (!ct.IsCancellationRequested && _ws?.State == WebSocketState.Open)
        {
            try
            {
                var result = await _ws.ReceiveAsync(buffer, ct);

                if (result.MessageType == WebSocketMessageType.Close)
                {
                    _logger.LogInformation("Editor bridge closed connection");
                    break;
                }

                messageBuffer.AddRange(buffer.AsSpan(0, result.Count).ToArray());

                if (result.EndOfMessage)
                {
                    var json = Encoding.UTF8.GetString(messageBuffer.ToArray());
                    messageBuffer.Clear();
                    HandleMessage(json);
                }
            }
            catch (OperationCanceledException) { break; }
            catch (WebSocketException ex)
            {
                _logger.LogWarning("WebSocket error: {Message}", ex.Message);
                break;
            }
        }

        foreach (var pending in _pending.Values)
        {
            pending.TrySetException(new InvalidOperationException("Bridge connection lost"));
        }
        _pending.Clear();
    }

    private void HandleMessage(string json)
    {
        try
        {
            var response = JsonSerializer.Deserialize<BridgeResponse>(json, JsonOpts);
            if (response?.Id != null && _pending.TryRemove(response.Id, out var tcs))
            {
                tcs.TrySetResult(response);
            }
            else
            {
                _logger.LogDebug("Received unmatched bridge message: {Json}", json[..Math.Min(200, json.Length)]);
            }
        }
        catch (Exception ex)
        {
            _logger.LogWarning("Failed to parse bridge message: {Error}", ex.Message);
        }
    }

    public void Dispose()
    {
        _cts?.Cancel();
        _ws?.Dispose();
        GC.SuppressFinalize(this);
    }
}
