import { z } from "zod";

/**
 * Cursor pagination, TypeScript half (T3).
 *
 * The editor holds the data, so the paging itself happens in C++ - one shared
 * implementation in `plugin/ue_mcp_bridge/.../Public/HandlerPagination.h`,
 * which owns the cursor format and the rules for what happens when the
 * collection changes between pages. This module is the half that lives on this
 * side of the bridge: it declares `cursor` and `limit` identically for every
 * category that pages, keeps each action's documentation in step with that
 * declaration, and reads the paging fields back off a result.
 *
 * ── Why the declaration is not optional ──
 *
 * A category's zod shape is ONE FLAT BAG shared by every action in it, and the
 * MCP layer strips keys the shape does not declare. A parameter an action
 * documents, or forwards in its own `mapParams`, but which the category never
 * declares does not fail: it arrives at the handler as `undefined` and the call
 * returns an ordinary success for a page that was never paged. So a category
 * whose handlers accept `cursor` and `limit` MUST spread `PAGINATION_SCHEMA`
 * into its shape. `tests/unit/action-schema.test.ts` fails on the drift, which
 * is the backstop rather than the plan.
 *
 * ── The cursor is opaque ──
 *
 * Pass back exactly the `nextCursor` string the previous page returned, and
 * nothing else. Do not build one, parse one, or edit one: the encoding is a
 * bridge implementation detail and a cursor from an older build is refused by
 * version rather than misread. An invalid or stale cursor is an ERROR that
 * names the problem and says how to restart, never an empty page.
 */

export const CURSOR_PARAM = z
  .string()
  .optional()
  .describe(
    "Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified. "
    + "Omit it for the first page. A cursor is only valid while every other parameter stays "
    + "the same, and an invalid one is refused with instructions rather than returning nothing.",
  );

export const LIMIT_PARAM = z
  .number()
  .int()
  .positive()
  .optional()
  .describe(
    "Rows to return on this page. Each action names its own default and maximum, and refuses "
    + "a value outside that range rather than silently clamping.",
  );

/** The two parameters, ready to spread into a category's zod shape. */
export const PAGINATION_SCHEMA: Record<string, z.ZodType> = {
  cursor: CURSOR_PARAM,
  limit: LIMIT_PARAM,
};

/** The names this module declares, in the order `paged()` documents them. */
export const PAGINATION_PARAM_NAMES = ["cursor", "limit"] as const;

/**
 * Mark an action description as paged, by adding `cursor?, limit?` to its
 * `Params:` clause.
 *
 * The clause is what `project(describe_action)` reads and what the drift test
 * compares against the declared shape, so documenting the two parameters by
 * hand in each description is a drift waiting to happen. This inserts them at
 * the END of the clause and nowhere else: after the action's own parameters,
 * before a `Returns` section or the sentence that resumes the prose, and before
 * a trailing issue reference, all of which sit outside the clause.
 *
 * A description with no `Params:` clause gets one.
 *
 * An action whose clause is `Params: none` ends up with `none, cursor?,
 * limit?`, which reads oddly but says the truth: the action has no parameters
 * of its own and two for paging. The reader that matters is
 * `parseParams()`, which treats `none` as a sentinel and carries on through
 * the rest of the clause rather than stopping at it - stopping is what once
 * hid the paging parameters of every action in this shape, leaving page two
 * unreachable from the advertised schema.
 */
export function paged(description: string): string {
  const marker = /\bParams:/.exec(description);
  if (!marker) {
    const base = description.trimEnd();
    const separator = base.endsWith(".") || base.length === 0 ? " " : ". ";
    return `${base}${separator}Params: ${PAGINATION_PARAM_NAMES.map((n) => `${n}?`).join(", ")}`;
  }

  const clauseStart = marker.index + marker[0].length;
  const clauseEnd = clauseStart + clauseLength(description.slice(clauseStart));
  const clause = description.slice(clauseStart, clauseEnd);

  // Already paged: leave it exactly as it is rather than listing the
  // parameters twice.
  if (/\bcursor\b/.test(clause) && /\blimit\b/.test(clause)) return description;

  // A trailing `(#123)` belongs to the sentence, not to the parameter list.
  const trailingRef = /(\s*\(#[\d/#\s,]+\))\s*$/.exec(clause);
  const insertAt = trailingRef ? clauseEnd - trailingRef[1].length : clauseEnd;
  const addition = PAGINATION_PARAM_NAMES
    .filter((name) => !new RegExp(`\\b${name}\\b`).test(clause))
    .map((name) => `${name}?`)
    .join(", ");
  if (addition.length === 0) return description;

  return `${description.slice(0, insertAt)}, ${addition}${description.slice(insertAt)}`;
}

/**
 * How much of `text` is still the `Params:` clause.
 *
 * Mirrors the reader in `action-schema.ts`: the clause runs until a `Returns`
 * section or a sentence break, and only at bracket depth zero, so
 * `(e.g. 'foot_l')` and `{op:'set'}` stay attached to the parameter they
 * document instead of ending the list early.
 */
function clauseLength(text: string): number {
  const stop = /\.\s+|\bReturns\b|\bReturn:/g;
  let depth = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth = Math.max(0, depth - 1);
    else if (depth === 0) {
      stop.lastIndex = i;
      const m = stop.exec(text);
      if (m && m.index === i) return i;
    }
  }
  return text.length;
}

/** The paging fields a paged bridge result carries. */
export interface PageInfo {
  /** Rows on this page. */
  count: number;
  /** Index of this page's first row in the full enumeration. */
  pageOffset: number;
  /** Whether another page exists. */
  hasMore: boolean;
  /** Pass this back as `cursor` to read the next page. Absent on the last one. */
  nextCursor?: string;
  /** Rows in the whole collection, when the handler could count it. */
  total?: number;
  /** False when the handler could not count the collection, so `total` is absent. */
  totalKnown: boolean;
  /** True when the collection moved underneath a resumed page. */
  collectionChanged: boolean;
  /** Plain-language account of that change, straight from the handler. */
  cursorNote?: string;
}

/**
 * Read the paging fields off a bridge result, or `undefined` when the result is
 * not a paged one. Never throws on a shape it does not recognise: a caller
 * probing an arbitrary result must be able to ask without guarding first.
 */
export function readPage(result: unknown): PageInfo | undefined {
  if (typeof result !== "object" || result === null) return undefined;
  const row = result as Record<string, unknown>;
  if (typeof row.hasMore !== "boolean") return undefined;

  const num = (key: string): number | undefined =>
    typeof row[key] === "number" && Number.isFinite(row[key] as number) ? (row[key] as number) : undefined;

  return {
    count: num("count") ?? 0,
    pageOffset: num("pageOffset") ?? 0,
    hasMore: row.hasMore,
    nextCursor: typeof row.nextCursor === "string" ? row.nextCursor : undefined,
    total: num("total"),
    totalKnown: row.totalKnown === true,
    collectionChanged: row.collectionChanged === true,
    cursorNote: typeof row.cursorNote === "string" ? row.cursorNote : undefined,
  };
}

/**
 * One line telling the caller how to read the rest, written for the agent that
 * has to act on it rather than for a log. Returns an empty string when there is
 * nothing left to say, so it can be concatenated unconditionally.
 *
 * NOTHING IN src/ CALLS THIS, and nothing calls readPage above it either. The
 * "Showing rows X-Y, for the next page: cursor=..." line is not emitted
 * anywhere: callers read `nextCursor`, `hasMore` and `cursorNote` straight off
 * the raw result the bridge returned, which is already in every paged response.
 * Both functions are kept as the one place that decodes that shape, with their
 * own unit tests, for a caller that wants the decoded form rather than the raw
 * fields. Read this as the answer to "why do I not see this line": it is not
 * dead by accident and it is not wired by accident.
 */
export function pageHint(tool: string, action: string, page: PageInfo | undefined): string {
  if (!page) return "";
  const parts: string[] = [];
  if (page.hasMore && page.nextCursor) {
    const seen = page.pageOffset + page.count;
    const of = page.total !== undefined ? ` of ${page.total}` : "";
    parts.push(
      `Showing rows ${page.pageOffset + 1}-${seen}${of}. `
      + `For the next page: ${tool}(action="${action}", cursor="${page.nextCursor}").`,
    );
  }
  if (page.collectionChanged && page.cursorNote) parts.push(page.cursorNote);
  return parts.join(" ");
}
