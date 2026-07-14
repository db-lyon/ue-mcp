import { describe, it, expect } from "vitest";
import { validateSubmission } from "../../src/tools/feedback.js";

const realPy = "import unreal\nfoo = unreal.do_thing()";
const realSummary =
  "blueprint.set_class_default marks the asset dirty but never saves it, forcing python flushes.";
const realTitle = "blueprint.set_class_default does not save asset";

describe("validateSubmission", () => {
  it("accepts a real feedback submission", () => {
    expect(
      validateSubmission(realTitle, realSummary, realPy, "blueprint(set_class_default)", 2),
    ).toBeNull();
  });

  it("rejects short placeholder titles via length check", () => {
    for (const t of ["noop", "test", "x", "ignore", "dummy", "stop", "tmp", "n/a", "..."]) {
      const r = validateSubmission(t, realSummary, realPy, undefined, 1);
      expect(r?.code).toBe("title_too_short");
    }
  });

  it("rejects placeholder titles that clear the length threshold", () => {
    // "placeholder" (11) and "accidental" (10) clear the length floor but are still placeholders
    for (const t of ["placeholder", "accidental"]) {
      const r = validateSubmission(t, realSummary, realPy, undefined, 1);
      expect(r?.code).toBe("placeholder_title");
    }
  });

  it("rejects short titles", () => {
    const r = validateSubmission("bp bug", realSummary, realPy, undefined, 1);
    expect(r?.code).toBe("title_too_short");
  });

  it("rejects meta/apology submissions", () => {
    const r = validateSubmission(
      "Accidental feedback submission cleanup needed",
      "please ignore previous noop issues filed in error",
      realPy,
      undefined,
      0,
    );
    expect(r?.code).toBe("meta_apology");
  });

  it("rejects when summary is too short", () => {
    const r = validateSubmission(realTitle, "too short", realPy, undefined, 1);
    expect(r?.code).toBe("summary_too_short");
  });

  it("rejects when summary just duplicates the title", () => {
    const long = "a".repeat(60);
    const r = validateSubmission(long, long, realPy, undefined, 1);
    expect(r?.code).toBe("summary_duplicates_title");
  });

  it("accepts a substantive report with no python workaround and no idealTool", () => {
    // A python workaround is not required. A specific title + summary is enough
    // to file (e.g. a crash report, where there is nothing to work around).
    expect(validateSubmission(realTitle, realSummary, undefined, undefined, 0)).toBeNull();
  });

  it("accepts when idealTool is set but no python ran this session", () => {
    expect(
      validateSubmission(realTitle, realSummary, undefined, "blueprint(set_class_default)", 0),
    ).toBeNull();
  });

  it("accepts a crash report with neither workaround nor session evidence", () => {
    expect(
      validateSubmission(
        "create_niagara_system_from_spec crashes the editor",
        "Adding a GraphSource-less emitter to a system via the spec handler hard-crashes the editor with an access violation. No workaround exists.",
        undefined,
        undefined,
        0,
      ),
    ).toBeNull();
  });
});
