/**
 * The model behind flow agent steps (`class_path: agent` and `agent_prompt`).
 *
 * A daemon extension registers a provider through `api.models.register`; the
 * most recent registration that is still live wins. Without one, a key in
 * ANTHROPIC_API_KEY is used. With neither, an agent step fails with
 * NO_MODEL_MESSAGE rather than reaching for a service nobody configured.
 */
import Anthropic from "@anthropic-ai/sdk";
import type { LLMCompletionRequest, LLMCompletionResponse, LLMMessage, LLMProvider } from "@db-lyon/flowkit";

/** The provider contract: flowkit's neutral completion request and response. */
export type ModelProvider = LLMProvider;
export type { LLMCompletionRequest as ModelRequest, LLMCompletionResponse as ModelResponse };

export const NO_MODEL_MESSAGE = "No model is configured for flow agent steps.";

/** The model an env-key run uses when a step names none. UE_MCP_FLOW_MODEL overrides it. */
export const DEFAULT_FLOW_MODEL = "claude-opus-5-5";

const registered: Array<{ provider: ModelProvider }> = [];

/** Register a provider for agent steps. Returns the function that removes it again. */
export function registerModelProvider(provider: ModelProvider): () => void {
  if (!provider || typeof provider.complete !== "function") {
    throw new TypeError("A model provider needs a complete(request) function.");
  }
  const entry = { provider };
  registered.push(entry);
  return () => {
    const i = registered.indexOf(entry);
    if (i !== -1) registered.splice(i, 1);
  };
}

/** Where the model for the next agent step comes from, without building it. */
export function modelSource(env: NodeJS.ProcessEnv = process.env): "extension" | "env" | null {
  if (registered.length > 0) return "extension";
  if (env.ANTHROPIC_API_KEY?.trim()) return "env";
  return null;
}

/** The provider an agent step runs on: a registered one, else the env key, else null. */
export function resolveModelProvider(env: NodeJS.ProcessEnv = process.env): ModelProvider | null {
  const last = registered[registered.length - 1];
  if (last) return last.provider;
  const key = env.ANTHROPIC_API_KEY?.trim();
  if (!key) return null;
  return anthropicProvider(key, env);
}

type AnthropicMessage = Anthropic.MessageParam;

function toAnthropic(req: LLMCompletionRequest): { system?: string; messages: AnthropicMessage[] } {
  const source: LLMMessage[] = req.messages ?? [{ role: "user", content: req.prompt ?? "" }];
  const systems = req.system ? [req.system] : [];
  const messages: AnthropicMessage[] = [];
  for (const m of source) {
    if (m.role === "system") {
      systems.push(m.content);
    } else if (m.role === "tool") {
      messages.push({
        role: "user",
        content: [{ type: "tool_result", tool_use_id: m.toolCallId ?? "", content: m.content }],
      });
    } else if (m.role === "assistant") {
      const blocks: Anthropic.ContentBlockParam[] = [];
      if (m.content) blocks.push({ type: "text", text: m.content });
      for (const call of m.toolCalls ?? []) {
        blocks.push({ type: "tool_use", id: call.id, name: call.name, input: call.arguments });
      }
      messages.push({ role: "assistant", content: blocks.length ? blocks : "" });
    } else {
      messages.push({ role: "user", content: m.content });
    }
  }
  return { ...(systems.length ? { system: systems.join("\n\n") } : {}), messages };
}

function finishReason(stop: string | null | undefined): string | undefined {
  if (stop === "end_turn" || stop === "stop_sequence") return "stop";
  if (stop === "max_tokens") return "length";
  return stop ?? undefined;
}

/** A provider over the Anthropic Messages API, for the env-key path. */
export function anthropicProvider(apiKey: string, env: NodeJS.ProcessEnv = process.env): ModelProvider {
  const client = new Anthropic({ apiKey, ...(env.ANTHROPIC_BASE_URL ? { baseURL: env.ANTHROPIC_BASE_URL } : {}) });
  return {
    async complete(req: LLMCompletionRequest): Promise<LLMCompletionResponse> {
      const { system, messages } = toAnthropic(req);
      const res = await client.messages.create(
        {
          model: req.model ?? (env.UE_MCP_FLOW_MODEL?.trim() || DEFAULT_FLOW_MODEL),
          max_tokens: req.maxTokens ?? 16000,
          ...(system ? { system } : {}),
          messages,
          ...(req.stop?.length ? { stop_sequences: req.stop } : {}),
          ...(req.tools?.length
            ? {
                tools: req.tools.map((t) => ({
                  name: t.name,
                  ...(t.description ? { description: t.description } : {}),
                  input_schema: t.parameters as Anthropic.Tool.InputSchema,
                })),
              }
            : {}),
        },
        req.signal ? { signal: req.signal } : undefined,
      );
      let text = "";
      const toolCalls: NonNullable<LLMCompletionResponse["toolCalls"]> = [];
      for (const block of res.content) {
        if (block.type === "text") text += block.text;
        else if (block.type === "tool_use") toolCalls.push({ id: block.id, name: block.name, arguments: (block.input ?? {}) as Record<string, unknown> });
      }
      return {
        text,
        ...(toolCalls.length ? { toolCalls } : {}),
        ...(finishReason(res.stop_reason) ? { finishReason: finishReason(res.stop_reason) } : {}),
        usage: { inputTokens: res.usage.input_tokens, outputTokens: res.usage.output_tokens },
        model: res.model,
      };
    },
  };
}
