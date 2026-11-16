/**
 * The Anthropic model provider for ue-mcp flow agent steps (`class_path: agent`,
 * `agent_prompt` and the built-in playtest flow), as a daemon extension.
 *
 * Install it with `ue-mcp install anthropic`. It reads, from the daemon's
 * environment:
 *   ANTHROPIC_API_KEY    the key; without it nothing is registered
 *   ANTHROPIC_BASE_URL   another endpoint for the Messages API
 *   UE_MCP_FLOW_MODEL    the model a step uses when it names none
 *
 * It calls the Messages API over fetch, so it ships as this one file.
 */

export const DEFAULT_MODEL = "claude-opus-5-5";
const DEFAULT_BASE_URL = "https://api.anthropic.com";
const API_VERSION = "2023-06-01";

/** flowkit's neutral request as a Messages API body, less the model and limits. */
export function toMessages(req) {
  const source = req.messages ?? [{ role: "user", content: req.prompt ?? "" }];
  const systems = req.system ? [req.system] : [];
  const messages = [];
  for (const m of source) {
    if (m.role === "system") {
      systems.push(m.content);
    } else if (m.role === "tool") {
      messages.push({ role: "user", content: [{ type: "tool_result", tool_use_id: m.toolCallId ?? "", content: m.content }] });
    } else if (m.role === "assistant") {
      const blocks = [];
      if (m.content) blocks.push({ type: "text", text: m.content });
      for (const call of m.toolCalls ?? []) blocks.push({ type: "tool_use", id: call.id, name: call.name, input: call.arguments });
      messages.push({ role: "assistant", content: blocks.length ? blocks : "" });
    } else {
      messages.push({ role: "user", content: m.content });
    }
  }
  return { ...(systems.length ? { system: systems.join("\n\n") } : {}), messages };
}

function finishReason(stop) {
  if (stop === "end_turn" || stop === "stop_sequence") return "stop";
  if (stop === "max_tokens") return "length";
  return stop ?? undefined;
}

/**
 * A provider over the Messages API. `fetchImpl` is for tests.
 * @param {{ apiKey: string, baseUrl?: string, model?: string, fetchImpl?: typeof fetch }} opts
 */
export function anthropicProvider({ apiKey, baseUrl, model, fetchImpl = fetch }) {
  const endpoint = `${(baseUrl || DEFAULT_BASE_URL).replace(/\/+$/, "")}/v1/messages`;
  return {
    async complete(req) {
      const { system, messages } = toMessages(req);
      const body = {
        model: req.model ?? (model || DEFAULT_MODEL),
        max_tokens: req.maxTokens ?? 16000,
        ...(system ? { system } : {}),
        messages,
        ...(req.stop?.length ? { stop_sequences: req.stop } : {}),
        ...(req.tools?.length
          ? {
              tools: req.tools.map((t) => ({
                name: t.name,
                ...(t.description ? { description: t.description } : {}),
                input_schema: t.parameters,
              })),
            }
          : {}),
      };
      const res = await fetchImpl(endpoint, {
        method: "POST",
        headers: { "content-type": "application/json", "x-api-key": apiKey, "anthropic-version": API_VERSION },
        body: JSON.stringify(body),
        ...(req.signal ? { signal: req.signal } : {}),
      });
      const text = await res.text();
      if (!res.ok) {
        let detail = text;
        try {
          detail = JSON.parse(text)?.error?.message ?? text;
        } catch {
          // Not JSON: the body as sent.
        }
        throw new Error(`The Anthropic Messages API answered HTTP ${res.status}: ${detail}`);
      }
      const json = JSON.parse(text);
      let out = "";
      const toolCalls = [];
      for (const block of json.content ?? []) {
        if (block.type === "text") out += block.text;
        else if (block.type === "tool_use") toolCalls.push({ id: block.id, name: block.name, arguments: block.input ?? {} });
      }
      const finish = finishReason(json.stop_reason);
      return {
        text: out,
        ...(toolCalls.length ? { toolCalls } : {}),
        ...(finish ? { finishReason: finish } : {}),
        usage: { inputTokens: json.usage?.input_tokens, outputTokens: json.usage?.output_tokens },
        model: json.model,
      };
    },
  };
}

export async function activate(api, env = process.env) {
  if (!api.models) {
    api.log.warn("this daemon has no models.register (extension API 2 or later); update ue-mcp. No model registered.");
    return;
  }
  const apiKey = env.ANTHROPIC_API_KEY?.trim();
  if (!apiKey) {
    api.log.warn("ANTHROPIC_API_KEY is not set in the daemon's environment, so no model is registered. Set it where the MCP client starts ue-mcp, then restart the daemon (ue-mcp daemon stop).");
    return;
  }
  const dispose = api.models.register(
    anthropicProvider({ apiKey, baseUrl: env.ANTHROPIC_BASE_URL, model: env.UE_MCP_FLOW_MODEL?.trim() }),
  );
  return { dispose };
}
