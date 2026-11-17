// @session independent: One process-wide model for flow agent steps, registered by a daemon extension. No editor is involved.
/**
 * The model behind flow agent steps (`class_path: agent` and `agent_prompt`).
 *
 * A daemon extension registers a provider through `api.models.register`; the
 * most recent registration that is still live wins. Core ships no provider of
 * its own, since a model is a third-party service with its own credential.
 * Without one, an agent step fails with NO_MODEL_MESSAGE and NO_MODEL_WAY_FORWARD.
 */
import type { LLMProvider } from "@db-lyon/flowkit";

/**
 * The provider: flowkit's neutral completion request and response. An
 * extension registers the contract's ModelProvider, which is checked against
 * this where the daemon hands it over.
 */
export type ModelProvider = LLMProvider;

export const NO_MODEL_MESSAGE = "No model is configured for flow agent steps.";

/** What to do about NO_MODEL_MESSAGE. The provider extensions load in the daemon, not in an in-process server. */
export const NO_MODEL_WAY_FORWARD =
  "Install a model provider extension, for example `ue-mcp install anthropic` (it reads ANTHROPIC_API_KEY), "
  + "and serve the project through its daemon (the default; not server.mode: in-process), where extensions load.";

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

/** The provider an agent step runs on: the newest live registration, else null. */
export function resolveModelProvider(): ModelProvider | null {
  return registered[registered.length - 1]?.provider ?? null;
}
