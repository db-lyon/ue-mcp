// @session independent: Reads one editor operation step's result text. Pure logic over its arguments.
/**
 * Whether one step of a daemon editor operation (start, stop, restart,
 * rebuild) failed, and why.
 *
 * The dispatch marks a call as an error only when it threw or was refused. A
 * handler that ran and reports failure RETURNS `success: false`, so a step is
 * read off both (#1384). Nothing to do is not a failure: a stop that found the
 * editor already down, or a start that found it up and answering.
 */

/** The JSON object a step's text starts with; notes the dispatch adds follow it. */
export function stepPayload(text: string): Record<string, unknown> | null {
  const parse = (t: string): Record<string, unknown> | null => {
    try {
      const v: unknown = JSON.parse(t);
      return v && typeof v === "object" && !Array.isArray(v) ? (v as Record<string, unknown>) : null;
    } catch {
      return null;
    }
  };
  const whole = parse(text);
  if (whole) return whole;
  for (let i = text.indexOf("\n}"); i >= 0; i = text.indexOf("\n}", i + 1)) {
    const head = parse(text.slice(0, i + 2));
    if (head) return head;
  }
  return null;
}

/** Null when the step went through, else its reason in one paragraph. */
export function editorStepFailure(isError: boolean, text: string, max = 1200): string | null {
  const payload = stepPayload(text);
  if (!isError) {
    if (!payload || payload.success !== false) return null;
    if (payload.alreadyStopped === true) return null;
    if (payload.alreadyRunning === true && payload.bridgeReady === true) return null;
  }
  const message = typeof payload?.message === "string" ? payload.message : typeof payload?.error === "string" ? payload.error : null;
  const reason = (message ?? text).trim() || "the step failed";
  return reason.length > max ? reason.slice(0, max) : reason;
}
