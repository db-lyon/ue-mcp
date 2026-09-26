import { describe, it, expect } from "vitest";
import { z } from "zod";
import { bp, categoryTool } from "../../src/category-tool.js";
import { ALL_TOOLS } from "../../src/tools.js";

const probe = (limit: z.ZodType) => categoryTool("probe", "Probe.", { list: bp("read", "List.", "list") }, { limit });

describe("categoryTool bounds limit", () => {
  it("refuses a page of zero or fewer rows and keeps the description", () => {
    const limit = probe(z.number().optional().describe("Rows per page")).schema.limit;
    expect(limit.safeParse(0).success).toBe(false);
    expect(limit.safeParse(-3).success).toBe(false);
    expect(limit.safeParse(5).success).toBe(true);
    expect(limit.safeParse(undefined).success).toBe(true);
    expect(limit.description).toBe("Rows per page");
  });

  it("keeps an integer check and a bound the category stated itself", () => {
    const int = probe(z.number().int().optional()).schema.limit;
    expect(int.safeParse(2.5).success).toBe(false);
    const own = probe(z.number().int().min(1).max(100).optional()).schema.limit;
    expect(own.safeParse(101).success).toBe(false);
    expect(own.safeParse(1).success).toBe(true);
  });

  it("applies to every shipped category that declares limit", () => {
    const withLimit = ALL_TOOLS.filter((t) => t.schema.limit);
    expect(withLimit.length).toBeGreaterThan(0);
    for (const tool of withLimit) {
      expect(tool.schema.limit.safeParse(0).success, tool.name).toBe(false);
    }
  });
});
