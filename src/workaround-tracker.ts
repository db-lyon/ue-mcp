/**
 * Tracker for execute_python workaround calls, partitioned per editor session.
 *
 * The contents of this tracker are assembled into feedback(submit) payloads and
 * posted to a public issue tracker, and `clearWorkarounds()` fires on every
 * submit. While the stack was a single module-level array, both of those were
 * cross-project: a submit from one editor would carry another project's Python
 * source into a public issue, and would truncate the other editor's record on
 * the way out. So the stack is keyed by session (#817, plan item 6.4).
 *
 * The key is the session's registry key (its normalized project root). A
 * context with no session resolves to the empty key, which is also the key the
 * project-less default session uses and the one the bare module-level
 * functions read, so a single-editor server and a direct caller land on the
 * same partition they always did.
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

/** Anything carrying an editor session. Structural so this module stays free of
 *  a cycle through types.ts, which imports the tool graph. */
export interface WorkaroundScopeSource {
  session?: { key: string } | undefined;
}

const DEFAULT_SCOPE = "";

const stacks = new Map<string, WorkaroundEntry[]>();

/** The partition key for a tool context. Undefined context means the default. */
export function workaroundScope(ctx?: WorkaroundScopeSource): string {
  return ctx?.session?.key ?? DEFAULT_SCOPE;
}

function stackFor(scope: string): WorkaroundEntry[] {
  let stack = stacks.get(scope);
  if (!stack) {
    stack = [];
    stacks.set(scope, stack);
  }
  return stack;
}

export function pushWorkaround(entry: WorkaroundEntry, ctx?: WorkaroundScopeSource): void {
  stackFor(workaroundScope(ctx)).push(entry);
}

export function getWorkarounds(ctx?: WorkaroundScopeSource): readonly WorkaroundEntry[] {
  return stackFor(workaroundScope(ctx));
}

export function clearWorkarounds(ctx?: WorkaroundScopeSource): void {
  stackFor(workaroundScope(ctx)).length = 0;
}

export function workaroundCount(ctx?: WorkaroundScopeSource): number {
  return stackFor(workaroundScope(ctx)).length;
}

/** Drop every partition. Test-only; production never wants a global reset. */
export function resetAllWorkarounds(): void {
  stacks.clear();
}
