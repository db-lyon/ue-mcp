// @session independent: Declares the cursor/limit parameters; the editor holds the collection and the C++ half holds the cursor.
import { z } from "zod";

/**
 * Cursor pagination, TypeScript half (T3).
 *
 * The editor holds the data, so the paging itself happens in C++ - one shared
 * implementation in `plugin/ue_mcp_bridge/.../Private/HandlerPagination.h`,
 * which owns the cursor format and the rules for what happens when the
 * collection changes between pages. This module is the half that lives on this
 * side of the bridge: the shared `cursor` declaration.
 *
 * ── The rule ──
 *
 * A category whose handlers page declares `cursor` (CURSOR_PARAM) in its shape,
 * because the MCP layer strips an undeclared key and the call then succeeds on
 * a page that was never paged. `limit` is the category's own; `categoryTool`
 * bounds any optional numeric `limit` to at least one row. A paged action
 * declares `cursor` and `limit` among its parameters like any other.
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
