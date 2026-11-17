/** #1384: a step a handler failed by returning success: false fails the daemon's editor operation. */
import { describe, expect, it } from "vitest";
import { editorStepFailure } from "../../src/daemon/editor-step.js";
import { failed, refused, succeeded } from "../../src/dispatch/call.js";

describe("a step of a daemon editor operation", () => {
  it("failed when the pipeline says so, or when its handler returned success: false", () => {
    expect(editorStepFailure(failed("TASK_FAILED", "boom", true))).toBe("Error [TASK_FAILED]: boom");
    expect(editorStepFailure(succeeded({ success: false, exitCode: 6, message: "Build failed with exit code 6" }, [], true))).toBe("Build failed with exit code 6");
    expect(editorStepFailure(refused({ success: false, dialogBlocking: true, error: "A dialog is up" }, true))).toBe("A dialog is up");
    expect(editorStepFailure(succeeded({ success: true }, [], true))).toBeNull();
  });

  it("went through when there was nothing to do", () => {
    expect(editorStepFailure(succeeded({ success: false, alreadyStopped: true, message: "Editor is not running" }, [], true))).toBeNull();
    expect(editorStepFailure(succeeded({ success: false, alreadyRunning: true, bridgeReady: true }, [], true))).toBeNull();
    expect(editorStepFailure(succeeded({ success: false, alreadyRunning: true, bridgeReady: false, message: "up, no bridge" }, [], true))).toBe("up, no bridge");
  });
});
