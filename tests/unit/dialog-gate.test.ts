import { describe, it, expect, vi } from "vitest";
import {
  buildRefusal,
  clearDialogBlocking,
  gateForDialog,
  isDialogRefusal,
  latchedDialog,
  noteDialogBlocking,
} from "../../src/dialog-gate.js";
import type { EditorSession } from "../../src/session.js";

const session = () => ({}) as unknown as EditorSession;

const INFO = {
  dialogTitle: "Save Content",
  dialogMessage: "Select Content to Save",
  buttons: ["Save Selected", "Don't Save", "Cancel"],
  choices: [{ buttonLabel: "Don't Save", respondWith: "editor(action='respond_to_dialog', buttonLabel=\"Don't Save\")" }],
};

const listing = (title: string) => ({
  dialogs: [{ title, message: "m", buttons: ["OK"], choices: [{ buttonLabel: "OK", respondWith: "x" }] }],
});

describe("recognising the plugin's refusal", () => {
  it("keys off dialogBlocking, not off success", () => {
    expect(isDialogRefusal({ dialogBlocking: true })).toBe(true);
    expect(isDialogRefusal({ success: false })).toBe(false);
    expect(isDialogRefusal(null)).toBe(false);
    expect(isDialogRefusal("blocked")).toBe(false);
  });
});

describe("the session latch", () => {
  it("is per editor, so one project's modal does not gate another", async () => {
    const a = session();
    const b = session();
    noteDialogBlocking(a, INFO);
    expect(latchedDialog(a)?.dialogTitle).toBe("Save Content");
    expect(latchedDialog(b)).toBeUndefined();
    expect(await gateForDialog(b, "asset.list", async () => listing("x"))).toBeNull();
  });

  it("does not probe at all when nothing is latched", async () => {
    const probe = vi.fn(async () => listing("Save Content"));
    expect(await gateForDialog(session(), "asset.list", probe)).toBeNull();
    expect(probe).not.toHaveBeenCalled();
  });
});

describe("gating a call", () => {
  it("refuses in the same shape the plugin emits", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const refusal = await gateForDialog(s, "asset.list", async () => listing("Save Content"));
    expect(refusal).toMatchObject({
      success: false,
      dialogBlocking: true,
      refusedMethod: "asset.list",
      dialogTitle: "Save Content",
    });
    expect(isDialogRefusal(refusal)).toBe(true);
  });

  it("lets the dialog actions through, or the modal could never be answered", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    for (const allowed of [
      "editor.respond_to_dialog",
      "editor.list_dialogs",
      "editor.set_dialog_policy",
      "editor.get_engine_state",
      "project.get_status",
    ]) {
      expect(await gateForDialog(s, allowed, async () => listing("Save Content"))).toBeNull();
    }
  });

  it("clears itself when the dialog is gone, without being told", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    // Answered in the editor window by hand: the probe comes back empty.
    expect(await gateForDialog(s, "asset.list", async () => ({ dialogs: [] }))).toBeNull();
    expect(latchedDialog(s)).toBeUndefined();
  });

  it("refreshes from the probe, so a second dialog behind the first is the one reported", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const refusal = await gateForDialog(s, "asset.list", async () => listing("Delete Assets"));
    expect(refusal?.dialogTitle).toBe("Delete Assets");
    expect(latchedDialog(s)?.dialogTitle).toBe("Delete Assets");
  });

  it("lets the call through when the probe fails, because that is a different fault", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const refusal = await gateForDialog(s, "asset.list", async () => {
      throw new Error("editor down");
    });
    expect(refusal).toBeNull();
  });

  it("names the refused method in the message the caller reads", () => {
    const built = buildRefusal("level.place_actor", INFO);
    expect(built.error).toContain("level.place_actor");
    expect(built.error).toContain("Every other action returns this same refusal");
  });

  it("stops gating once cleared", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    clearDialogBlocking(s);
    expect(await gateForDialog(s, "asset.list", async () => listing("Save Content"))).toBeNull();
  });
});
