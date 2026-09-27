/**
 * Tracker for execute_python workaround calls, kept per editor.
 *
 * The contents of this tracker are assembled into feedback(submit) payloads and
 * posted to a public issue tracker, and `clearWorkarounds()` fires on every
 * submit. A stack shared by every editor would carry one project's Python
 * source into another's issue, and truncate the other's record on the way out
 * (#817, plan item 6.4).
 *
 * An editor handle carries its own stack. A context whose session is only a
 * key (a test, an embedder) is partitioned by that key, and one with no
 * session reads the empty key, which the bare module-level calls share.
 */

export interface WorkaroundEntry {
  code: string;
  timestamp: string;
  resultSnippet?: string;
  /** What the caller said they were trying to do (searchable for #704 overlap report). */
  taskSummary?: string;
  /** If a dedicated action matched at execute_python time, "tool(action)". */
  suggestedTool?: string;
}

/** Anything carrying an editor session. Structural so this module needs no
 *  import of the session or context types. */
export interface WorkaroundScopeSource {
  session?: { key: string; workarounds?: WorkaroundEntry[] } | undefined;
}

const DEFAULT_SCOPE = "";

/** Stacks for sessions that are only a key. */
const keyedStacks = new Map<string, WorkaroundEntry[]>();

/** The partition key for a tool context. Undefined context means the default. */
export function workaroundScope(ctx?: WorkaroundScopeSource): string {
  return ctx?.session?.key ?? DEFAULT_SCOPE;
}

function stackFor(ctx?: WorkaroundScopeSource): WorkaroundEntry[] {
  const own = ctx?.session?.workarounds;
  if (own) return own;
  const scope = workaroundScope(ctx);
  let stack = keyedStacks.get(scope);
  if (!stack) {
    stack = [];
    keyedStacks.set(scope, stack);
  }
  return stack;
}

export function pushWorkaround(entry: WorkaroundEntry, ctx?: WorkaroundScopeSource): void {
  stackFor(ctx).push(entry);
}

export function getWorkarounds(ctx?: WorkaroundScopeSource): readonly WorkaroundEntry[] {
  return stackFor(ctx);
}

export function clearWorkarounds(ctx?: WorkaroundScopeSource): void {
  stackFor(ctx).length = 0;
}

export function workaroundCount(ctx?: WorkaroundScopeSource): number {
  return stackFor(ctx).length;
}

/** Drop every keyed partition. Test-only; an editor handle's own stack goes with the handle. */
export function resetAllWorkarounds(): void {
  keyedStacks.clear();
}
