/**
 * Session-level tracker for MCP tool invocations.
 *
 * Every category-tool dispatch appends one entry; the ontology
 * InvocationProjector renders the accumulated log as a .cairn audit
 * layer at /UE/Audit/Invocations. A bounded ring (newest N kept) so
 * long-running sessions do not grow unbounded.
 */

export type InvocationStatus = "ok" | "error" | "requires_unmet";

export interface InvocationEntry {
  sequence: number;
  tool: string;
  action: string;
  status: InvocationStatus;
  durationMs: number;
  timestamp: string;
  errorCode?: string;
  errorSnippet?: string;
}

const MAX_ENTRIES = 500;
const ring: InvocationEntry[] = [];
let nextSeq = 1;

export function pushInvocation(entry: Omit<InvocationEntry, "sequence">): InvocationEntry {
  const seq = nextSeq++;
  const full: InvocationEntry = { ...entry, sequence: seq };
  ring.push(full);
  if (ring.length > MAX_ENTRIES) ring.shift();
  return full;
}

export function getInvocations(): readonly InvocationEntry[] {
  return ring;
}

export function clearInvocations(): void {
  ring.length = 0;
  nextSeq = 1;
}

export function invocationCount(): number {
  return ring.length;
}
