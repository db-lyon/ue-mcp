// @session independent: Pure arithmetic over a tools/list answer from any MCP server; no editor involved.
/**
 * What `ue-mcp surface` reports about an MCP server: how many tools it lists,
 * how many actions those tools take, and how large their input schemas are.
 *
 * The counting rule is the same for every server, ue-mcp included, so two
 * servers can be compared on numbers anyone can reproduce:
 *
 * - A tool whose input schema has a top-level `action`, `operation` or
 *   `command` property with enumerated values counts one action per value.
 *   The first of those three names that carries values is used. Values are
 *   read from `enum`, `const`, and the `enum`/`const` of each `anyOf` or
 *   `oneOf` branch, de-duplicated.
 * - Any other tool counts as one action.
 *
 * Sizes are the compact JSON of what the server sent. Tokens are estimated as
 * characters divided by four and rounded up; the true count depends on the
 * model's tokenizer.
 */

/** The parameter names whose enumerated values count as actions, in the order they are tried. */
export const ACTION_PARAMS = ["action", "operation", "command"] as const;

export interface ListedTool {
  name: string;
  description?: string;
  inputSchema?: unknown;
}

export interface ToolSurface {
  name: string;
  /** The parameter the actions were counted from, or null when the tool counts as one. */
  actionParam: string | null;
  actions: number;
  schemaBytes: number;
}

export interface SurfaceReport {
  server: { name: string; version: string } | null;
  protocolVersion: string | null;
  tools: number;
  actions: number;
  /** Compact JSON bytes of every tool's inputSchema. */
  schemaBytes: number;
  schemaTokens: number;
  /** Compact JSON bytes of the whole tools/list array: names, descriptions and schemas. */
  toolsListBytes: number;
  toolsListTokens: number;
  /** Bytes of the initialize `instructions` field, 0 when the server sends none. */
  instructionsBytes: number;
  perTool: ToolSurface[];
}

function asRecord(value: unknown): Record<string, unknown> | undefined {
  return value !== null && typeof value === "object" && !Array.isArray(value) ? (value as Record<string, unknown>) : undefined;
}

/** The enumerated values one property schema allows, from enum, const and one level of anyOf/oneOf. */
export function enumeratedValues(schema: unknown): unknown[] {
  const s = asRecord(schema);
  if (!s) return [];
  const values: unknown[] = [];
  const take = (node: Record<string, unknown>): void => {
    if (Array.isArray(node.enum)) values.push(...node.enum);
    if ("const" in node) values.push(node.const);
  };
  take(s);
  for (const key of ["anyOf", "oneOf"]) {
    const branches = s[key];
    if (!Array.isArray(branches)) continue;
    for (const b of branches) {
      const branch = asRecord(b);
      if (branch) take(branch);
    }
  }
  const seen = new Set<string>();
  return values.filter((v) => {
    const k = JSON.stringify(v);
    if (seen.has(k)) return false;
    seen.add(k);
    return true;
  });
}

/** How many actions one tool counts for, and which parameter they came from. */
export function countToolActions(inputSchema: unknown): { actionParam: string | null; actions: number } {
  const props = asRecord(asRecord(inputSchema)?.properties);
  if (props) {
    for (const name of ACTION_PARAMS) {
      const values = enumeratedValues(props[name]);
      if (values.length > 0) return { actionParam: name, actions: values.length };
    }
  }
  return { actionParam: null, actions: 1 };
}

export function byteLength(value: unknown): number {
  return value === undefined ? 0 : Buffer.byteLength(JSON.stringify(value), "utf8");
}

/** characters / 4, rounded up: the same estimate the context-tax gate uses. */
export function estimateTokens(text: string | number): number {
  const chars = typeof text === "number" ? text : text.length;
  return Math.ceil(chars / 4);
}

export function measureSurface(
  tools: readonly ListedTool[],
  meta: { server?: { name: string; version: string } | null; protocolVersion?: string | null; instructions?: string | null } = {},
): SurfaceReport {
  const perTool = tools.map((t) => ({ name: t.name, ...countToolActions(t.inputSchema), schemaBytes: byteLength(t.inputSchema ?? {}) }));
  const schemaJson = JSON.stringify(tools.map((t) => t.inputSchema ?? {}));
  const listJson = JSON.stringify(tools);
  return {
    server: meta.server ?? null,
    protocolVersion: meta.protocolVersion ?? null,
    tools: tools.length,
    actions: perTool.reduce((n, t) => n + t.actions, 0),
    schemaBytes: Buffer.byteLength(schemaJson, "utf8"),
    schemaTokens: estimateTokens(schemaJson),
    toolsListBytes: Buffer.byteLength(listJson, "utf8"),
    toolsListTokens: estimateTokens(listJson),
    instructionsBytes: meta.instructions ? Buffer.byteLength(meta.instructions, "utf8") : 0,
    perTool,
  };
}

/** The text form `ue-mcp surface` prints. */
export function formatSurface(r: SurfaceReport, opts: { perTool?: boolean } = {}): string {
  const n = (v: number): string => v.toLocaleString("en-US");
  const lines = [
    `server         ${r.server ? `${r.server.name} ${r.server.version}` : "(no serverInfo)"}`,
    `protocol       ${r.protocolVersion ?? "?"}`,
    `tools          ${n(r.tools)}`,
    `actions        ${n(r.actions)}`,
    `input schemas  ${n(r.schemaBytes)} bytes, ~${n(r.schemaTokens)} tokens`,
    `tools/list     ${n(r.toolsListBytes)} bytes, ~${n(r.toolsListTokens)} tokens (names, descriptions, schemas)`,
    `instructions   ${n(r.instructionsBytes)} bytes`,
  ];
  if (opts.perTool) {
    const width = Math.max(4, ...r.perTool.map((t) => t.name.length));
    lines.push("", `${"tool".padEnd(width)}  actions  from       schema bytes`);
    for (const t of r.perTool) {
      lines.push(`${t.name.padEnd(width)}  ${String(t.actions).padStart(7)}  ${(t.actionParam ?? "-").padEnd(9)}  ${n(t.schemaBytes)}`);
    }
  }
  lines.push("", "actions: one per enumerated value of a tool's action, operation or command parameter; 1 for any other tool.");
  return lines.join("\n");
}
