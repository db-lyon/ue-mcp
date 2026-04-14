import Anthropic from "@anthropic-ai/sdk";
import type {
  LLMProvider,
  LLMCompletionRequest,
  LLMCompletionResponse,
} from "@db-lyon/flowkit";

const DEFAULT_MODEL = "claude-opus-4-5";
const DEFAULT_MAX_TOKENS = 1024;

/**
 * Anthropic-backed LLMProvider for use by the `agent_prompt` task.
 *
 * Attach to the flow context as `ctx.llm`. Requires ANTHROPIC_API_KEY
 * (or explicit apiKey in constructor). Enables prompt caching on the
 * system prompt so recurrent on_failure triage is cheap.
 */
export class AnthropicProvider implements LLMProvider {
  private client: Anthropic;

  constructor(opts: { apiKey?: string } = {}) {
    const apiKey = opts.apiKey ?? process.env.ANTHROPIC_API_KEY;
    if (!apiKey) {
      throw new Error(
        "AnthropicProvider: no API key. Set ANTHROPIC_API_KEY or pass { apiKey } explicitly.",
      );
    }
    this.client = new Anthropic({ apiKey });
  }

  async complete(req: LLMCompletionRequest): Promise<LLMCompletionResponse> {
    const model = req.model ?? DEFAULT_MODEL;
    const max_tokens = req.maxTokens ?? DEFAULT_MAX_TOKENS;

    let userPrompt = req.prompt;
    if (req.schema) {
      userPrompt +=
        "\n\nRespond ONLY with a JSON object matching this schema:\n" +
        JSON.stringify(req.schema) +
        "\n\nOutput just the JSON, no prose, no markdown fences.";
    }

    const systemBlocks = req.system
      ? [{ type: "text" as const, text: req.system, cache_control: { type: "ephemeral" as const } }]
      : undefined;

    const response = await this.client.messages.create({
      model,
      max_tokens,
      system: systemBlocks,
      messages: [{ role: "user", content: userPrompt }],
    });

    const text = response.content
      .map((block) => (block.type === "text" ? block.text : ""))
      .join("")
      .trim();

    const out: LLMCompletionResponse = {
      text,
      usage: {
        inputTokens: response.usage?.input_tokens,
        outputTokens: response.usage?.output_tokens,
      },
    };

    if (req.schema) {
      const parsed = tryParseJson(text);
      if (parsed !== undefined) out.parsed = parsed;
    }

    return out;
  }
}

function tryParseJson(text: string): unknown {
  // Strip common markdown fences the model might add despite instructions.
  const cleaned = text
    .replace(/^```(?:json)?\s*/i, "")
    .replace(/```$/, "")
    .trim();
  try {
    return JSON.parse(cleaned);
  } catch {
    return undefined;
  }
}
