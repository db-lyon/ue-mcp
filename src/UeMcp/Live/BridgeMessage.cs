using System.Text.Json;
using System.Text.Json.Serialization;

namespace UeMcp.Live;

public class BridgeRequest
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = Guid.NewGuid().ToString("N")[..8];

    [JsonPropertyName("method")]
    public string Method { get; set; } = "";

    [JsonPropertyName("params")]
    public Dictionary<string, object?>? Params { get; set; }
}

public class BridgeResponse
{
    [JsonPropertyName("id")]
    public string? Id { get; set; }

    [JsonPropertyName("result")]
    public JsonElement? Result { get; set; }

    [JsonPropertyName("error")]
    public BridgeError? Error { get; set; }

    public bool IsSuccess => Error == null;
}

public class BridgeError
{
    [JsonPropertyName("code")]
    public int Code { get; set; }

    [JsonPropertyName("message")]
    public string Message { get; set; } = "";
}
