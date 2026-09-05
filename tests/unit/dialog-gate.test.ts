import { describe, it, expect, vi } from "vitest";
import {
  buildRefusal,
  clearDialogBlocking,
  gateForDialog,
  isDialogRefusal,
  latchedDialog,
  noteDialogBlocking,
  type GateOptions,
} from "../../src/dialog-gate.js";
import type { EditorSession } from "../../src/session.js";

const session = () => ({}) as unknown as EditorSession;

const INFO = {
  dialogTitle: "Save Content",
  dialogMessage: "Select Content to Save",
  buttons: ["Save Selected", "Don't Save", "Cancel"],
  choices: [
    { buttonLabel: "Don't Save", respondWith: "editor(action='respond_to_dialog', buttonLabel=\"Don't Save\")" },
  ],
};

const listing = (title: string, buttons = ["Save Selected", "Don't Save", "Cancel"]) => ({
  dialogs: [
    {
      title,
      message: "m",
      buttons,
      choices: buttons.map((b) => ({ buttonLabel: b, respondWith: `press ${b}` })),
    },
  ],
});

/** Gate options with everything stubbed; override what a case is about. */
function opts(over: Partial<GateOptions> = {}): GateOptions {
  return {
    mode: "auto",
    probe: async () => listing("Save Content"),
    press: async () => ({ success: true }),
    ...over,
  };
}

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
    expect(await gateForDialog(b, "asset.list", opts())).toBeNull();
  });

  it("does not probe at all when nothing is latched", async () => {
    const probe = vi.fn(async () => listing("Save Content"));
    expect(await gateForDialog(session(), "asset.list", opts({ probe }))).toBeNull();
    expect(probe).not.toHaveBeenCalled();
  });
});

describe("gating a call", () => {
  it("refuses in the same shape the plugin emits", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const refusal = await gateForDialog(s, "asset.list", opts());
    expect(refusal).toMatchObject({
      success: false,
      dialogBlocking: true,
      refusedMethod: "asset.list",
      dialogTitle: "Save Content",
      dialogMode: "auto",
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
      expect(await gateForDialog(s, allowed, opts())).toBeNull();
    }
  });

  it("clears itself when the dialog is gone, without being told", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const gone = await gateForDialog(s, "asset.list", opts({ probe: async () => ({ dialogs: [] }) }));
    expect(gone).toBeNull();
    expect(latchedDialog(s)).toBeUndefined();
  });

  it("refreshes from the probe, so a second dialog behind the first is reported", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const refusal = await gateForDialog(s, "asset.list", opts({ probe: async () => listing("Delete Assets") }));
    expect(refusal?.dialogTitle).toBe("Delete Assets");
    expect(latchedDialog(s)?.dialogTitle).toBe("Delete Assets");
  });

  it("lets the call through when the probe fails, because that is a different fault", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const refusal = await gateForDialog(s, "asset.list", opts({
      probe: async () => {
        throw new Error("editor down");
      },
    }));
    expect(refusal).toBeNull();
  });

  it("stops gating once cleared", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    clearDialogBlocking(s);
    expect(await gateForDialog(s, "asset.list", opts())).toBeNull();
  });
});

describe("the mode governs every call, not just the lifecycle actions", () => {
  const accept = (button: string) =>
    vi.fn(async () => ({ action: "accept", content: { button } })) as unknown as GateOptions["elicit"];

  it("interactive asks the person and presses only their choice", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const press = vi.fn(async () => ({ success: true }));
    const gated = await gateForDialog(s, "asset.list", opts({
      mode: "interactive",
      press,
      elicit: accept("Don't Save"),
    }));
    // Answered, so the call that tripped the gate goes on to run.
    expect(gated).toBeNull();
    expect(press).toHaveBeenCalledWith("Don't Save");
    expect(latchedDialog(s)).toBeUndefined();
  });

  it("interactive reports the dialog when the person declines", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const press = vi.fn(async () => ({ success: true }));
    const gated = await gateForDialog(s, "asset.list", opts({
      mode: "interactive",
      press,
      elicit: vi.fn(async () => ({ action: "decline" })) as unknown as GateOptions["elicit"],
    }));
    expect(gated).toMatchObject({ dialogBlocking: true });
    expect(press).not.toHaveBeenCalled();
  });

  it("interactive presses nothing for a button the dialog does not offer", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const press = vi.fn(async () => ({ success: true }));
    const gated = await gateForDialog(s, "asset.list", opts({
      mode: "interactive",
      press,
      elicit: accept("Format Hard Drive"),
    }));
    expect(gated).toMatchObject({ dialogBlocking: true });
    expect(press).not.toHaveBeenCalled();
  });

  it("interactive falls back to reporting when the client cannot be asked", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const gated = await gateForDialog(s, "asset.list", opts({ mode: "interactive", elicit: undefined }));
    expect(gated).toMatchObject({ dialogBlocking: true, dialogMode: "interactive" });
  });

  it("auto hands back the press calls and presses nothing", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const press = vi.fn(async () => ({ success: true }));
    const gated = await gateForDialog(s, "asset.list", opts({ mode: "auto", press }));
    expect(gated?.choices).toBeDefined();
    expect(press).not.toHaveBeenCalled();
  });

  it("defer withholds the press calls, or it would differ from auto in wording only", async () => {
    const s = session();
    noteDialogBlocking(s, INFO);
    const press = vi.fn(async () => ({ success: true }));
    const gated = await gateForDialog(s, "asset.list", opts({ mode: "defer", press }));
    expect(gated).toMatchObject({ dialogBlocking: true, dialogMode: "defer" });
    expect(gated?.choices).toBeUndefined();
    expect(String(gated?.error)).toContain("Unreal Editor window");
    expect(press).not.toHaveBeenCalled();
    // The dialog is still fully described, so it can be recognised on screen.
    expect(gated?.buttons).toEqual(["Save Selected", "Don't Save", "Cancel"]);
  });

  it("names the refused method in the message the caller reads", () => {
    const built = buildRefusal("level.place_actor", INFO, "auto");
    expect(built.error).toContain("level.place_actor");
    expect(built.error).toContain("Every other action returns this same refusal");
  });
});
