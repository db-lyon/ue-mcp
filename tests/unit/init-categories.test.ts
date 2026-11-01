/**
 * `init` offers its categories from a hand-written list with labels and
 * descriptions. Every registered category tool must be on it, or the prompt
 * silently leaves that tool always on with no way to opt out.
 */
import { describe, expect, it } from "vitest";
import { CATEGORIES } from "../../src/cli/init-core.js";
import { ALL_TOOLS } from "../../src/tools.js";

describe("init categories", () => {
  it("lists every registered category tool, and nothing else", () => {
    const listed = CATEGORIES.map((c) => c.name).sort();
    const registered = ALL_TOOLS.map((t) => t.name).sort();
    expect(listed).toEqual(registered);
  });

  it("describes every optional category", () => {
    const undescribed = CATEGORIES.filter((c) => !c.alwaysOn && !c.description).map((c) => c.name);
    expect(undescribed).toEqual([]);
  });
});
