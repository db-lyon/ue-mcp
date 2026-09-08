import { describe, it, expect } from "vitest";
import { dialogModeGuidance } from "../../src/editor-control.js";
import { DialogGuard } from "../../src/dialog-guard.js";
import type { DialogMode } from "../../src/user-state.js";

const MODES: DialogMode[] = ["interactive", "auto", "defer"];

/**
 * The prose a caller reads and the decision the guard made are the same
 * statement. This branched on the configured mode and carried its own copy of
 * "interactive with nobody to ask behaves as defer", so a client that could
 * not be asked anything was told to answer a form that was never shown.
 */
describe("the guidance says what the guard decided", () => {
  for (const mode of MODES) {
    for (const canElicit of [true, false]) {
      const label = `${mode}, ${canElicit ? "a client that can be asked" : "nobody to ask"}`;

      it(`describes the effective mode under ${label}`, () => {
        const text = dialogModeGuidance({ mode, source: "default" }, canElicit);
        const effective = DialogGuard.effectiveMode(mode, canElicit);
        // The configured mode is always reported, so the reader can see what
        // is set even when it could not be honoured.
        expect(text).toContain(`Dialog handling mode: ${mode}`);
        if (effective === "auto") expect(text).toContain("handed back");
        if (effective === "defer") expect(text).toMatch(/Unreal Editor window/);
        if (effective === "interactive") expect(text).toContain("Answer the dialog,");
      });

      it(`never tells the caller to press a button it will not be handed under ${label}`, () => {
        const text = dialogModeGuidance({ mode, source: "default" }, canElicit);
        if (DialogGuard.handsOverPressCalls(mode, canElicit)) return;
        // A no-actuation mode may still name the setting that turns actuation
        // on, and that sentence carries the call it would enable. What it must
        // never do is tell the caller to press THIS dialog's button now.
        expect(text, "guidance offered a press under a no-actuation mode").not.toContain(
          "the call listed beside it",
        );
      });
    }
  }

  it("explains the downgrade rather than pretending the form was shown", () => {
    const text = dialogModeGuidance({ mode: "interactive", source: "env" }, false);
    expect(text).toContain("did not advertise that capability");
    expect(text).not.toContain("Answer the dialog, then call");
  });
});
