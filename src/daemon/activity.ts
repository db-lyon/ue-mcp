/**
 * What an MCP tools/call is, for the activity feed: the category and action
 * it runs, read through the micro gateway (`tools` with action `call`) and the
 * flow tool so every client's calls are attributed alike.
 */
export interface CallLabel {
  category: string;
  action: string;
  /** The flow a flow call runs, when it runs one. */
  flowName?: string;
}

export function describeToolCall(params: unknown): CallLabel {
  const p = (params ?? {}) as { name?: unknown; arguments?: unknown };
  const name = typeof p.name === "string" ? p.name : "unknown";
  const args = (p.arguments ?? {}) as Record<string, unknown>;
  const str = (v: unknown): string | undefined => (typeof v === "string" && v ? v : undefined);
  if (name === "tools" && args.action === "call") {
    return { category: str(args.category) ?? "unknown", action: str(args.method) ?? "unknown" };
  }
  const label: CallLabel = { category: name, action: str(args.action) ?? "unknown" };
  if (name === "flow" && str(args.flowName)) label.flowName = str(args.flowName);
  return label;
}

/** The first text block of a failed tool result, trimmed for an event. */
export function resultError(result: unknown): string | undefined {
  const r = result as { isError?: boolean; content?: Array<{ text?: string }> } | undefined;
  if (!r?.isError) return undefined;
  const text = r.content?.find((c) => typeof c.text === "string")?.text ?? "error";
  return text.length > 300 ? `${text.slice(0, 300)}...` : text;
}
