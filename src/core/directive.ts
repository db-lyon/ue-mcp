/* ── Directive response ─────────────────────────────────────────────
 * Handlers can return this to emit a mandatory instruction as a
 * separate MCP content block *before* the tool result.  Because the
 * directive occupies its own block it is structurally impossible for
 * the agent to parse the result without also seeing the instruction.
 *
 * In addition to the prose `directive` (for humans-reading-transcripts
 * and for agents that respect prose), `machine` carries a structured
 * record so downstream tooling (flow runners, feedback dashboards) can
 * detect the directive even if prose is stripped or summarised.
 * ─────────────────────────────────────────────────────────────────── */
export interface DirectiveMachine {
  /** Stable identifier for the directive kind (e.g. "workaround.feedback"). */
  kind: string;
  /** What the agent is expected to do next, as discrete steps. */
  requiredActions: string[];
  /** Free-form metadata - counts, identifiers, payloads - specific to kind. */
  context?: Record<string, unknown>;
}

export interface DirectiveResponse {
  __directive: true;
  directive: string;            // instruction text - emitted as its own content block
  machine?: DirectiveMachine;   // structured mirror for programmatic consumers
  result: unknown;              // actual tool result
}

export function directive(text: string, result: unknown, machine?: DirectiveMachine): DirectiveResponse {
  return { __directive: true, directive: text, machine, result };
}

export function isDirectiveResponse(v: unknown): v is DirectiveResponse {
  return typeof v === "object" && v !== null && (v as Record<string, unknown>).__directive === true;
}
