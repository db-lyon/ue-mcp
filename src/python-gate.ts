/**
 * The execute_python gate: matching a caller's rulings against the candidate
 * actions a tool search returned (#960, #938).
 *
 * The gate is the front door to this project's feedback pipeline. Every Python
 * escape hatch is a recorded gap, and the record only exists if the agent gets
 * through the gate and runs the code. Two separate reporters found that it
 * could not be satisfied at all, for two different reasons:
 *
 *  1. The refusal printed the qualified spelling, `editor(invoke_function)`,
 *     and then matched what came back against the bare `invoke_function`.
 *     Passing back exactly the strings the tool had just printed did not
 *     satisfy it. Matching is now spelling-insensitive: bare, `tool(action)`
 *     and `tool.action` all resolve to the same key.
 *
 *  2. The candidate set is recomputed from the taskSummary on every call, so
 *     rewording the summary swapped in candidates that then had to be ruled out
 *     as well. One reporter needed five re-justifications for one unchanged
 *     intent. Accepted rulings are now remembered for the session, so a reword
 *     can only ever add the candidates that are genuinely new, and the refusal
 *     hands back the exact `ruledOut` array to send.
 *
 * A reason shorter than MIN_REASON_LENGTH used to be dropped in silence, which
 * left the caller re-sending an entry the gate had thrown away without saying
 * so. Those are now reported back by name.
 *
 * A candidate the connected plugin does not register cannot do the task, so
 * it is never owed a ruling. It is listed apart with the upgrade pointer
 * instead (#1167).
 */
import { PLUGIN_UPGRADE_POINTER } from "./bridge/bridge.js";
import { workaroundScope, type WorkaroundScopeSource } from "./workaround-tracker.js";

/** A ruling has to say something. Twelve characters is the long-standing bar. */
export const MIN_REASON_LENGTH = 12;

/** How many rulings one session remembers. Oldest are dropped first. */
const RULING_HISTORY = 64;

/** A candidate action the tool search returned for the caller's taskSummary. */
export interface GateCandidate {
  tool: string;
  action: string;
  description?: string;
  score?: number;
}

/** An entry the gate refused to count, and why, so the caller can fix it. */
export interface RejectedRuling {
  entry: string;
  why: string;
}

export interface ParsedRulings {
  /** Normalized action key to the reason given. */
  accepted: Map<string, string>;
  rejected: RejectedRuling[];
}

/**
 * Reduce any spelling of an action reference to the bare action name.
 *
 * Accepted: `invoke_function`, `editor(invoke_function)`, `editor.invoke_function`,
 * `editor:invoke_function`, `editor/invoke_function`, with any surrounding
 * whitespace and in any case. Deliberately liberal: this is a gate that has to
 * be passable, and an agent that names the right action in the wrong shape has
 * done the thinking the gate exists to force.
 */
export function ruledOutKey(raw: unknown): string {
  let s = String(raw ?? "").trim().toLowerCase();
  if (!s) return "";
  // tool(action) - keep what is inside the parentheses.
  const call = /^[^()]*\(\s*([^()]*?)\s*\)$/.exec(s);
  if (call) s = call[1].trim();
  // tool.action / tool:action / tool/action - keep the last segment.
  const cut = Math.max(s.lastIndexOf("."), s.lastIndexOf(":"), s.lastIndexOf("/"));
  if (cut >= 0) s = s.slice(cut + 1);
  return s.trim();
}


/** Read the caller's `ruledOut` parameter, separating what counts from what does not. */
export function parseRulings(raw: unknown): ParsedRulings {
  const accepted = new Map<string, string>();
  const rejected: RejectedRuling[] = [];
  if (!Array.isArray(raw)) return { accepted, rejected };

  for (const item of raw) {
    // A bare string is a common spelling. It carries no reason, so it cannot
    // pass, but saying so beats ignoring it.
    if (typeof item === "string") {
      rejected.push({
        entry: item,
        why: `no reason given - send {action: "${item}", reason: "<why it does not fit>"}`,
      });
      continue;
    }
    const record = (item ?? {}) as Record<string, unknown>;
    const label = String(record.action ?? record.tool ?? "").trim();
    const key = ruledOutKey(label);
    const reason = String(record.reason ?? "").trim();
    if (!key) {
      rejected.push({ entry: label || JSON.stringify(item), why: "no 'action' named" });
      continue;
    }
    if (reason.length < MIN_REASON_LENGTH) {
      rejected.push({
        entry: label,
        why: `reason is ${reason.length} characters, at least ${MIN_REASON_LENGTH} are required`,
      });
      continue;
    }
    accepted.set(key, reason);
  }
  return { accepted, rejected };
}

// ── Session-scoped memory ──────────────────────────────────────────────────────
// Keyed the same way the workaround stack is, so one editor's rulings never
// answer for another's.

const remembered = new Map<string, Map<string, string>>();

function storeFor(scope: string): Map<string, string> {
  let store = remembered.get(scope);
  if (!store) {
    store = new Map();
    remembered.set(scope, store);
  }
  return store;
}

/**
 * Fold this call's rulings into what the session already knows, and return the
 * union. This is what makes the gate converge: a reworded taskSummary produces
 * a different candidate set, and without this every earlier justification would
 * have to be typed again.
 */
function recordRulings(
  ctx: WorkaroundScopeSource | undefined,
  rulings: Map<string, string>,
): Map<string, string> {
  const store = storeFor(workaroundScope(ctx));
  for (const [key, reason] of rulings) {
    // Re-insert so a repeated key moves to the newest end of the eviction order.
    store.delete(key);
    store.set(key, reason);
  }
  while (store.size > RULING_HISTORY) {
    const oldest = store.keys().next();
    if (oldest.done) break;
    store.delete(oldest.value);
  }
  return new Map(store);
}


/** Drop every partition. Test-only. */
export function resetRulings(): void {
  remembered.clear();
}

// ── The gate itself ────────────────────────────────────────────────────────────

export interface GateVerdict {
  /** Candidates with no accepted ruling. Empty means Python may run. */
  unresolved: GateCandidate[];
  /** Advertised candidates the running plugin does not register. Owed no ruling. */
  notInPlugin: GateCandidate[];
  /** Entries the caller sent that did not count, with the reason they did not. */
  rejected: RejectedRuling[];
  /** Candidates already covered, in the spelling the caller will recognise. */
  satisfied: string[];
  /** The exact array to send back, one entry per unresolved candidate. */
  ruledOutTemplate: Array<{ action: string; reason: string }>;
}

/**
 * Decide whether the caller has cleared the gate, and if not, say precisely
 * what is still needed.
 */
export function evaluateGate(
  candidates: GateCandidate[],
  rawRuledOut: unknown,
  ctx?: WorkaroundScopeSource,
  notRegistered: (c: GateCandidate) => boolean = () => false,
): GateVerdict {
  const parsed = parseRulings(rawRuledOut);
  const known = recordRulings(ctx, parsed.accepted);

  const notInPlugin = candidates.filter((c) => notRegistered(c));
  const unresolved = candidates.filter((c) => !notRegistered(c) && !known.has(ruledOutKey(c.action)));
  const satisfied = candidates
    .filter((c) => known.has(ruledOutKey(c.action)))
    .map((c) => `${c.tool}(${c.action})`);

  return {
    unresolved,
    notInPlugin,
    rejected: parsed.rejected,
    satisfied,
    ruledOutTemplate: unresolved.map((c) => ({
      action: `${c.tool}(${c.action})`,
      reason: `<why ${c.tool}(${c.action}) does not do this task>`,
    })),
  };
}

/**
 * The refusal text. It has one job: make the next call the successful one, so
 * it states the accepted spellings, names what is still missing, and shows the
 * array to send rather than describing it.
 */
export function gateRefusalMessage(
  taskSummary: string,
  candidates: GateCandidate[],
  verdict: GateVerdict,
): string {
  const lines: string[] = [
    `execute_python is GATED. A tool search for "${taskSummary}" returned ${candidates.length} candidate action(s).`,
    `Still need a reason for: ${verdict.unresolved.map((c) => `${c.tool}(${c.action})`).join(", ")}.`,
  ];
  if (verdict.notInPlugin.length > 0) {
    lines.push(
      `Advertised but not in the running plugin, so no reason is needed: ${verdict.notInPlugin.map((c) => `${c.tool}(${c.action})`).join(", ")}. ${PLUGIN_UPGRADE_POINTER}`,
    );
  }
  if (verdict.satisfied.length > 0) {
    lines.push(`Already ruled out this session (no need to repeat): ${verdict.satisfied.join(", ")}.`);
  }
  if (verdict.rejected.length > 0) {
    lines.push(
      `These entries did not count: ${verdict.rejected.map((r) => `${r.entry} (${r.why})`).join("; ")}.`,
    );
  }
  lines.push(
    `Re-call with the SAME taskSummary plus this ruledOut array, replacing each placeholder with a real reason of at least ${MIN_REASON_LENGTH} characters:`,
    JSON.stringify({ ruledOut: verdict.ruledOutTemplate }),
    `The action field accepts any of "${candidates[0].action}", "${candidates[0].tool}(${candidates[0].action})" or "${candidates[0].tool}.${candidates[0].action}".`,
    `Rulings are remembered for this session, so rewording the taskSummary does not ask you to justify the same action twice.`,
    `If one of these actually does the task, call it instead of Python.`,
  );
  return lines.join(" ");
}
