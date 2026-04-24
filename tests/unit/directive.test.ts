import { describe, it, expect } from "vitest";
import { directive, isDirectiveResponse } from "../../src/types.js";

describe("directive()", () => {
  it("emits the legacy prose shape by default", () => {
    const d = directive("please read this", { ok: true });
    expect(isDirectiveResponse(d)).toBe(true);
    expect(d.directive).toContain("please read this");
    expect(d.result).toEqual({ ok: true });
    expect(d.machine).toBeUndefined();
  });

  it("carries a machine-readable mirror when supplied", () => {
    const d = directive("prose", "res", {
      kind: "workaround.feedback",
      requiredActions: ["step1", "step2"],
      context: { workaroundCount: 3 },
    });
    expect(d.machine?.kind).toBe("workaround.feedback");
    expect(d.machine?.requiredActions).toEqual(["step1", "step2"]);
    expect(d.machine?.context?.workaroundCount).toBe(3);
  });

  it("isDirectiveResponse distinguishes directives from plain objects", () => {
    expect(isDirectiveResponse({ result: 1 })).toBe(false);
    expect(isDirectiveResponse(null)).toBe(false);
    expect(isDirectiveResponse("string")).toBe(false);
    expect(isDirectiveResponse(directive("x", "y"))).toBe(true);
  });
});
