/**
 * Backslash repair on path parameters.
 *
 * The risk here is not the repair, it is over-reach: a rule that rewrites a
 * value it should not have touched turns a working call into a broken one, and
 * does it on the parameter the caller is least able to inspect. So most of
 * these assert what is LEFT ALONE.
 */
import { describe, it, expect } from "vitest";
import { isPathParam, normalizePathParams, attachPathRepairs } from "../../src/path-params.js";
import { categoryTool } from "../../src/types.js";
import type { ToolContext } from "../../src/types.js";

describe("isPathParam", () => {
  it("recognises the path spellings the surface uses", () => {
    for (const name of [
      "path", "paths", "assetPath", "assetPaths", "packagePath", "levelPath",
      "outputPath", "filePath", "headerPath", "directory", "outputDirectory",
      "folderPath", "sourcePath", "destinationPath", "subdirectory",
    ]) {
      expect(isPathParam(name), name).toBe(true);
    }
  });

  it("does not mistake a word that merely contains one for a path", () => {
    // These are real parameter names on the surface. Rewriting the value of
    // any of them would corrupt a call that was correct.
    for (const name of [
      "focusDirection", "pathFilter", "pathfindingContext", "profileName",
      "direction", "pathMode", "filterText",
    ]) {
      expect(isPathParam(name), name).toBe(false);
    }
  });
});

describe("normalizePathParams", () => {
  it("repairs a mount-rooted path written with backslashes", () => {
    const { params, repairs } = normalizePathParams({ assetPath: "\\Game\\UI\\WBP_Menu" });
    expect(params.assetPath).toBe("/Game/UI/WBP_Menu");
    expect(repairs).toEqual([
      { param: "assetPath", from: "\\Game\\UI\\WBP_Menu", to: "/Game/UI/WBP_Menu" },
    ]);
  });

  it("repairs a path that mixes both separators", () => {
    const { params } = normalizePathParams({ assetPath: "/Game/UI\\WBP_Menu" });
    expect(params.assetPath).toBe("/Game/UI/WBP_Menu");
  });

  it("repairs each mangled entry of a path array and leaves the clean ones", () => {
    const { params, repairs } = normalizePathParams({
      assetPaths: ["/Game/A", "\\Game\\B"],
    });
    expect(params.assetPaths).toEqual(["/Game/A", "/Game/B"]);
    expect(repairs).toHaveLength(1);
  });

  it("leaves a UNC path alone, where the leading pair is the syntax", () => {
    const value = "\\\\build-server\\share\\Content";
    const { params, repairs } = normalizePathParams({ outputPath: value });
    expect(params.outputPath).toBe(value);
    expect(repairs).toEqual([]);
  });

  it("leaves a parameter that is not a path alone, whatever it contains", () => {
    const { params, repairs } = normalizePathParams({ value: "C:\\literal\\string" });
    expect(params.value).toBe("C:\\literal\\string");
    expect(repairs).toEqual([]);
  });

  it("returns the original object when there is nothing to repair", () => {
    // Identity, not just equality: a clean call must allocate nothing and be
    // byte-identical to what it was before this existed.
    const input = { assetPath: "/Game/UI/WBP_Menu", limit: 5 };
    const { params, repairs } = normalizePathParams(input);
    expect(params).toBe(input);
    expect(repairs).toEqual([]);
  });

  it("does not touch a non-string value under a path name", () => {
    const { params, repairs } = normalizePathParams({ path: 42, paths: null });
    expect(params).toEqual({ path: 42, paths: null });
    expect(repairs).toEqual([]);
  });
});

describe("attachPathRepairs", () => {
  const repairs = [{ param: "assetPath", from: "\\Game\\A", to: "/Game/A" }];

  it("adds the report to an object result", () => {
    const out = attachPathRepairs({ success: true }, repairs) as Record<string, unknown>;
    expect(out.success).toBe(true);
    expect((out.pathsRepaired as { repairs: unknown[] }).repairs).toEqual(repairs);
  });

  it("returns the result untouched when nothing was repaired", () => {
    const result = { success: true };
    expect(attachPathRepairs(result, [])).toBe(result);
  });

  it("leaves an array or scalar result alone rather than reshaping it", () => {
    expect(attachPathRepairs([1, 2], repairs)).toEqual([1, 2]);
    expect(attachPathRepairs("done", repairs)).toBe("done");
    expect(attachPathRepairs(null, repairs)).toBe(null);
  });

  it("puts the report on a directive's payload, not on the envelope", () => {
    const directive = { __directive: true, directive: "do the thing", result: { success: true } };
    const out = attachPathRepairs(directive, repairs) as typeof directive & Record<string, unknown>;
    expect(out.pathsRepaired).toBeUndefined();
    expect((out.result as Record<string, unknown>).pathsRepaired).toBeDefined();
  });

  it("does not overwrite a handler's own account of what it repaired", () => {
    const result = { pathsRepaired: "mine" };
    expect(attachPathRepairs(result, repairs)).toBe(result);
  });
});

describe("dispatch integration", () => {
  const ctx = {} as ToolContext;

  it("repairs before the handler sees the parameter, and reports it after", async () => {
    let seen: unknown;
    const tool = categoryTool("probe", "test", {
      go: {
        kind: "handler",
        effect: "read",
        description: "Params: assetPath",
        handler: async (_c, p) => {
          seen = p.assetPath;
          return { ok: true };
        },
      },
    });

    const out = await tool.handler(ctx, { action: "go", assetPath: "\\Game\\UI\\X" }) as Record<string, unknown>;
    expect(seen).toBe("/Game/UI/X");
    expect(out.ok).toBe(true);
    expect(out.pathsRepaired).toBeDefined();
  });

  it("adds nothing to the result of a call whose paths were already correct", async () => {
    const tool = categoryTool("probe", "test", {
      go: { kind: "handler", effect: "read", description: "Params: assetPath", handler: async () => ({ ok: true }) },
    });
    const out = await tool.handler(ctx, { action: "go", assetPath: "/Game/UI/X" }) as Record<string, unknown>;
    expect(out).toEqual({ ok: true });
  });

  it("repairs before the category's own normalizeParams runs", async () => {
    // A category that reads a path in normalizeParams has to see the repaired
    // one, or it makes its decision on the broken spelling.
    let seenByCategory: unknown;
    const tool = categoryTool(
      "probe", "test",
      { go: { kind: "handler", effect: "read", description: "Params: assetPath", handler: async () => ({ ok: true }) } },
      undefined,
      undefined,
      {
        normalizeParams: (p) => {
          seenByCategory = p.assetPath;
          return p;
        },
      },
    );
    await tool.handler(ctx, { action: "go", assetPath: "\\Game\\A" });
    expect(seenByCategory).toBe("/Game/A");
  });

  it("suggests the closest action on an unknown one instead of listing them all", async () => {
    const tool = categoryTool("probe", "test", {
      sculpt: { kind: "handler", effect: "read", description: "Params: none", handler: async () => ({}) },
      paint_layer: { kind: "handler", effect: "read", description: "Params: none", handler: async () => ({}) },
    });
    await expect(tool.handler(ctx, { action: "sculp" })).rejects.toThrow(/Did you mean: sculpt\?/);
  });
});
