/**
 * The context-tax budgets (#1172), measured on the real server.
 *
 * The same measurement is the `context-tax` release gate; this puts it in the
 * unit run as well, so a change that fattens the seed fails where it was made
 * rather than at release time.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { beforeAll, describe, expect, it } from "vitest";
import {
  BUDGETS,
  DEFAULT_SEED,
  ENGINE_SEED,
  TAX_MARKER_FILES,
  applyTaxMarkers,
  budgetProblems,
  engineCeilingProblems,
  formatTokens,
  measureContextTax,
  parseStamp,
  staleStamps,
} from "../../scripts/context-tax.mjs";
import { ENGINE_TOOL_SEARCH_INSTRUCTIONS, ENGINE_TOOL_SEARCH_TOOLS } from "../fixtures/engine-tool-search-surface.mjs";

const REPO = path.resolve(__dirname, "..", "..");

type Rows = Record<string, { what: string; tokens: number; chars: number }>;

describe("budget arithmetic", () => {
  const rows = (tokens: Partial<Record<keyof typeof BUDGETS, number>>): Rows =>
    Object.fromEntries(Object.entries(tokens).map(([k, t]) => [k, { what: k, tokens: t!, chars: t! * 4 }]));

  it("passes numbers at their budget and fails one token over", () => {
    expect(budgetProblems(rows({ ...BUDGETS }))).toEqual([]);
    expect(budgetProblems(rows({ ...BUDGETS, full: BUDGETS.full + 1 }))).toEqual([
      `full: ${BUDGETS.full + 1} tokens, over its ${BUDGETS.full} budget`,
    ]);
  });

  it("fails a default seed one token over the engine's surface", () => {
    const at = { [DEFAULT_SEED]: { what: "default", tokens: 327, chars: 0 }, [ENGINE_SEED]: { what: "engine", tokens: 327, chars: 0 } };
    expect(engineCeilingProblems(at)).toEqual([]);
    expect(engineCeilingProblems({ ...at, [DEFAULT_SEED]: { what: "default", tokens: 328, chars: 0 } })).toEqual([
      "default: 328 tokens, over the 327 of engine",
    ]);
    expect(engineCeilingProblems({ [ENGINE_SEED]: at[ENGINE_SEED] })).toEqual(["default or engine seed: not measured"]);
  });

  it("measures the engine surface exactly as reconstructed", () => {
    const payload = JSON.stringify({ instructions: ENGINE_TOOL_SEARCH_INSTRUCTIONS, tools: ENGINE_TOOL_SEARCH_TOOLS });
    expect(ENGINE_TOOL_SEARCH_TOOLS.map((t) => t.name)).toEqual(["list_toolsets", "describe_toolset", "call_tool"]);
    expect(payload.length).toBe(1307);
  });

  it("fails a number that was never measured", () => {
    const { search: _search, ...rest } = BUDGETS;
    expect(budgetProblems(rows(rest))).toEqual(["search: not measured"]);
  });

  it("stamps and reads back the same numbers", () => {
    expect(formatTokens(50_798)).toBe("~51k");
    expect(formatTokens(2_079)).toBe("~2.1k");
    expect(formatTokens(560)).toBe("~560");
    expect(parseStamp("~51k")).toBe(51_000);
    expect(parseStamp("~2.1k")).toBe(2_100);
    const measured = rows({ full: 50_798 });
    const text = applyTaxMarkers("seed <!-- tax:full -->~45k<!-- /tax -->, budget <!-- tax:fullBudget -->x<!-- /tax -->", measured);
    expect(text).toBe("seed <!-- tax:full -->~51k<!-- /tax -->, budget <!-- tax:fullBudget -->~60k<!-- /tax -->");
    expect(staleStamps(text, measured, "doc")).toEqual([]);
    expect(staleStamps("<!-- tax:full -->~45k<!-- /tax -->", measured, "doc")).toEqual(["doc: tax:full says ~45k, measured 50798"]);
  });
});

describe("the shipped surface", () => {
  let rows: Rows;
  beforeAll(async () => {
    rows = await measureContextTax();
  }, 240_000);

  it("keeps every seed and discovery answer within its budget", () => {
    expect(budgetProblems(rows)).toEqual([]);
  });

  it("keeps the default seed at or below the engine's tool-search surface", () => {
    expect(engineCeilingProblems(rows)).toEqual([]);
  });

  it("measures the Epic catalog, which the budgets were set with", () => {
    // Micro describe of the largest category reaches the wrapped engine tools.
    expect(rows.describe.what).toMatch(/\(\d{3,} actions\)/);
  });

  it("matches the numbers the docs print", () => {
    const stale = TAX_MARKER_FILES.flatMap((rel) => {
      const file = path.join(REPO, rel);
      return fs.existsSync(file) ? staleStamps(fs.readFileSync(file, "utf8"), rows, rel) : [];
    });
    expect(stale, "run node scripts/context-tax.mjs --stamp").toEqual([]);
  });
});
