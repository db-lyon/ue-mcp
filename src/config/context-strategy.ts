// @session independent: The context strategy names and the resolution of the active one from the environment and config; one answer per process.
import { readEnv } from "../core/env.js";

/**
 * How a client is seeded with the tool surface (#1172): `micro` collapses
 * everything behind one gateway tool, `lean` keeps the category tools with
 * signatures on demand, `full` lists every action's signature inline.
 */
export type ContextStrategy = "full" | "lean" | "micro";

/** The strategy a project gets when it names none (#1172). */
export const DEFAULT_CONTEXT_STRATEGY: ContextStrategy = "micro";

/**
 * Resolve the active strategy. Env var wins over config so a user can flip it
 * per-session without editing ue-mcp.yml. Anything other than "full"/"lean"
 * (case insensitive) resolves to "micro", the default since #1172: the full
 * surface cost most of a context window before the first call.
 */
export function resolveContextStrategy(configStrategy?: string): ContextStrategy {
  const raw = (readEnv("contextStrategy") ?? configStrategy ?? DEFAULT_CONTEXT_STRATEGY).trim().toLowerCase();
  return raw === "full" ? "full" : raw === "lean" ? "lean" : "micro";
}
