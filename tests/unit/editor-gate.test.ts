/**
 * Untargeted-call gating and response attribution (#817, plan 5.2 and 5.3).
 */
import { describe, it, expect } from "vitest";
import {
  refuseUntargetedCall,
  editorAttribution,
  EDITOR_ATTRIBUTION_PREFIX,
} from "../../src/editor-gate.js";

const TWO = { editors: ["alpha", "beta"], activeEditor: "alpha", targetParam: "editor" };

describe("write gating beyond one editor", () => {
  it("refuses an untargeted mutation and names every editor", () => {
    const refusal = refuseUntargetedCall({ taskName: "asset.delete", ...TWO });
    expect(refusal).toBeTruthy();
    expect(refusal).toContain("asset.delete");
    expect(refusal).toContain("alpha");
    expect(refusal).toContain("beta");
    expect(refusal).toContain('editor="<name>"');
  });

  it("says which editor the call would have gone to", () => {
    const refusal = refuseUntargetedCall({ taskName: "editor.stop_editor", ...TWO })!;
    // The whole point of the refusal is that the fall-through is not obvious.
    expect(refusal).toContain("'alpha'");
    expect(refusal).toContain("beta");
  });

  it("lets a read through", () => {
    expect(refuseUntargetedCall({ taskName: "asset.list", ...TWO })).toBeNull();
    expect(refuseUntargetedCall({ taskName: "project.get_status", ...TWO })).toBeNull();
    expect(refuseUntargetedCall({ taskName: "flow.plan", ...TWO })).toBeNull();
  });

  it("refuses an unclassifiable call rather than guessing it is a read", () => {
    const refusal = refuseUntargetedCall({ taskName: "epic.call_tool", ...TWO });
    expect(refusal).toContain("does whatever its parameters say");
  });

  it("refuses a flow run, which is whatever its steps are", () => {
    expect(refuseUntargetedCall({ taskName: "flow.run", ...TWO })).toBeTruthy();
  });

  it("refuses an action from a category nobody classified", () => {
    // A plugin-provided category is not in the native surface, so nothing can
    // vouch for it. Conservative means refused, not allowed.
    expect(refuseUntargetedCall({ taskName: "someplugin.frobnicate", ...TWO })).toBeTruthy();
  });
});

describe("response attribution", () => {
  it("is absent at one editor", () => {
    expect(editorAttribution({ name: "alpha", projectPath: "C:/a/Alpha.uproject" }, 1)).toBeNull();
    expect(editorAttribution({ name: "default", projectPath: null }, 0)).toBeNull();
  });

  it("names the serving editor beyond one", () => {
    const line = editorAttribution({ name: "beta", projectPath: "C:/b/Beta.uproject" }, 2)!;
    expect(line.startsWith(EDITOR_ATTRIBUTION_PREFIX)).toBe(true);
    expect(JSON.parse(line.slice(EDITOR_ATTRIBUTION_PREFIX.length))).toEqual({
      editor: "beta",
      project: "C:/b/Beta.uproject",
    });
  });
});
