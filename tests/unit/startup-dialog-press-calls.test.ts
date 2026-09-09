import { describe, it, expect, beforeEach, afterEach } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { waitForEditorReady } from "../../src/editor-control.js";

/**
 * A dialog raised during startup goes through the same rule as one raised any
 * other time: whether the report carries the calls that press its buttons is
 * the guard's decision, not each route's. The startup path had its own default
 * and no test, so a mode that promises no actuation could still hand the
 * buttons over on the one route a caller reaches while nothing else is up.
 */
describe("a dialog met during startup is named, never pressed", () => {
  let dir: string;
  let projectPath: string;

  const publishModal = (): void => {
    const saved = path.join(dir, "Saved", "UE_MCP_Bridge");
    fs.mkdirSync(saved, { recursive: true });
    fs.writeFileSync(
      path.join(saved, "status.json"),
      JSON.stringify({
        phase: "waiting_for_input",
        ready: false,
        modal: {
          title: "Restore Packages",
          message: "Some packages were not saved.",
          buttons: ["Restore", "Discard", ""],
        },
      }),
    );
  };

  beforeEach(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-startup-dialog-"));
    projectPath = path.join(dir, "p.uproject");
    fs.writeFileSync(projectPath, "{}");
    publishModal();
  });

  afterEach(() => {
    fs.rmSync(dir, { recursive: true, force: true });
  });

  it("names the dialog so the launch failure says what is holding it", async () => {
    const r = await waitForEditorReady(projectPath, dir, 5, { showProgress: false });
    expect(r.ready).toBe(false);
    expect(r.reason).toContain("Restore Packages");
    expect(r.reason).toContain("Some packages were not saved.");
    expect(r.reason).toContain("Restore");
  });

  it("carries nothing that presses it, under every mode", async () => {
    // A startup report never hands back a press call. There is no bridge yet,
    // so the call it would name cannot be delivered, and answering a prompt
    // this early is dialogPolicy's job.
    const r = await waitForEditorReady(projectPath, dir, 5, { showProgress: false });
    expect(r.reason, "a startup dialog leaked the press call").not.toContain("respond_to_dialog");
  });

  it("drops the blank button the editor pads its list with", async () => {
    const r = await waitForEditorReady(projectPath, dir, 5, { showProgress: false });
    expect(r.reason).not.toMatch(/,\s*,/);
    expect(r.reason).not.toMatch(/Buttons: ,/);
  });
});
