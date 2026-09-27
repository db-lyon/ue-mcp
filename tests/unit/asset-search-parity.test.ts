/**
 * asset.search answers exactly as the handler it replaced: that handler, as
 * it stood before the move to a composite, is kept below.
 */
import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { callOwnBridgeMethod } from "../../src/flow/action-call.js";
import { McpError, ErrorCode } from "../../src/core/errors.js";
import { expectParity, type Answer, type LegacyHandler } from "../helpers/composite-parity.js";

const legacySearch: LegacyHandler = async (ctx, p) => {
  const { action: _, maxResults, ...rest } = p;
  const limit = (p.limit as number | undefined) ?? (maxResults as number | undefined);
  const call = { ...rest, ...(limit !== undefined ? { limit } : {}) };
  const roots = ctx.project.config.contentRoots;
  if (!p.directory && roots && roots.length > 0) {
    if (typeof p.cursor === "string" && p.cursor !== "") {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        `A cursor names one content root, and this call searches ${roots.length} of them (${roots.join(", ")}). `
        + "Pass 'directory' set to the root the cursor came from, alongside the cursor, to continue that root. "
        + "Each root's own nextCursor is reported per root on the first page.",
      );
    }
    const cap = limit ?? 50;
    const allResults: Array<Record<string, unknown>> = [];
    const perRoot: Array<Record<string, unknown>> = [];
    for (const root of roots) {
      const res = await callOwnBridgeMethod(ctx, "search_assets", { ...call, directory: root }) as Record<string, unknown>;
      if (res.results && Array.isArray(res.results)) {
        allResults.push(...(res.results as Array<Record<string, unknown>>));
      }
      perRoot.push({
        directory: root,
        count: res.count ?? 0,
        total: res.total,
        hasMore: res.hasMore === true,
        nextCursor: res.nextCursor,
      });
      if (allResults.length >= cap) break;
    }
    const searched = perRoot.length;
    return {
      query: p.query ?? "",
      searchScope: roots,
      roots: perRoot,
      rootsSearched: searched,
      rootsUnsearched: roots.length - searched,
      resultCount: Math.min(allResults.length, cap),
      results: allResults.slice(0, cap),
      hasMore: allResults.length > cap || perRoot.some((r) => r.hasMore === true) || searched < roots.length,
      success: true,
    };
  }
  return callOwnBridgeMethod(ctx, "search_assets", call);
};

/** One scripted page per content root. */
function pages(perRoot: Record<string, { rows: number; hasMore?: boolean }>): Answer {
  return (_m, params) => {
    const dir = String(params.directory ?? "/Game/");
    const page = perRoot[dir] ?? { rows: 0 };
    return {
      success: true,
      count: page.rows,
      total: page.rows + (page.hasMore ? 10 : 0),
      hasMore: page.hasMore === true,
      ...(page.hasMore ? { nextCursor: `${dir}#next` } : {}),
      results: Array.from({ length: page.rows }, (_, i) => ({ path: `${dir}A${i}` })),
    };
  };
}

const parity = (answer: Answer, params: Record<string, unknown>, roots?: string[]) =>
  expectParity(assetTool, "search", legacySearch, answer, params, roots);

describe("asset.search answers as the handler did", () => {
  it("with no content roots configured, as one bridge page", async () => {
    await parity(pages({ "/Game/": { rows: 3, hasMore: true } }), { query: "door", maxResults: 3 });
    await parity(() => ({ success: false, error: "registry busy" }), { query: "door" });
  });

  it("across roots, one page per root, stopping at the cap", async () => {
    const roots = ["/Game/", "/PluginA/", "/PluginB/"];
    const answer = pages({ "/Game/": { rows: 2 }, "/PluginA/": { rows: 4, hasMore: true }, "/PluginB/": { rows: 5 } });
    const capped = await parity(answer, { query: "door", limit: 5 }, roots);
    expect(capped.data).toMatchObject({ rootsSearched: 2, rootsUnsearched: 1, resultCount: 5, hasMore: true });
    await parity(answer, { query: "door", limit: 50 }, roots);
    await parity(answer, { query: "door", directory: "/PluginA/", cursor: "/PluginA/#next" }, roots);
  });

  it("refusing a cursor that cannot say which root it came from", async () => {
    await parity(pages({}), { query: "door", cursor: "x" }, ["/Game/", "/PluginA/"]);
  });

  it("when a root's page fails outright", async () => {
    await parity(() => { throw new Error("socket closed"); }, { query: "door" }, ["/Game/", "/PluginA/"]);
  });
});
