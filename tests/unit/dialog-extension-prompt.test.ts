/**
 * A daemon extension answering the dialogs its own in-process calls meet.
 *
 * The extension's prompt stands where an MCP elicitation form stands: same
 * form, plus the dialog itself, plus one more answer (delegate) that hands a
 * single dialog to the agent. The MCP route must not change at all.
 */
import { afterEach, describe, expect, it, vi } from "vitest";
import {
  DialogGuard,
  onDialogEvent,
  type DialogEvent,
  type GuardDeps,
} from "../../src/editor/dialog-guard.js";
import type { DialogMode } from "../../src/config/user-state.js";
import type { DialogPromptFn, DialogPromptRequest } from "../../src/editor/dialog-types.js";

const BUTTONS = ["Save Selected", "Don't Save", "Cancel"];
const CHOICES = BUTTONS.map((b) => ({ buttonLabel: b, respondWith: `editor(respond_to_dialog, buttonLabel="${b}")` }));
const listing = (title = "Save Content") => ({ dialogs: [{ title, message: "Save these?", buttons: BUTTONS, choices: CHOICES }] });
const empty = { dialogs: [] };

const saved = { ...process.env };
afterEach(() => {
  process.env = { ...saved };
});

function make(over: Partial<GuardDeps> & { modeIs?: DialogMode } = {}) {
  const deps: GuardDeps = {
    mode: () => over.modeIs ?? "interactive",
    probe: over.probe ?? (async () => listing()),
    press: over.press ?? (async () => ({ success: true })),
    elicit: over.elicit,
    prompt: over.prompt,
    askWaitMs: over.askWaitMs,
  };
  const guard = new DialogGuard(deps);
  guard.label = "Scratch";
  return { guard, deps };
}

/** A prompt answering after a person's delay, so the min-human check passes. */
function answering(result: Awaited<ReturnType<DialogPromptFn>>, afterMs = 0) {
  return vi.fn(async (_req: DialogPromptRequest) => {
    if (afterMs) await new Promise((r) => setTimeout(r, afterMs));
    return result;
  });
}

describe("an extension's prompt answers its own call's dialog", () => {
  it("is given the dialog itself, not only the form text", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const prompt = answering({ action: "accept", content: { button: "Cancel" } });
    const { guard } = make({ prompt: () => prompt });
    await guard.check("editor.stop_editor", "action");
    const req = prompt.mock.calls[0][0];
    expect(req.dialog).toMatchObject({
      editor: "Scratch",
      title: "Save Content",
      message: "Save these?",
      buttons: BUTTONS,
      subject: "editor.stop_editor",
      minHumanMs: 0,
    });
    // The same form an MCP client would get.
    expect(req.requestedSchema.properties.button).toMatchObject({ type: "string" });
    expect(req.message).toContain("Save Content");
  });

  it("presses the button the person picked and lets the call through", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const press = vi.fn(async () => ({ success: true }));
    const prompt = answering({ action: "accept", content: { button: "Cancel" } });
    const { guard } = make({ prompt: () => prompt, press });
    const decision = await guard.check("editor.stop_editor", "action");
    expect(press).toHaveBeenCalledWith("Cancel");
    expect(decision.allow).toBe(true);
  });

  it("makes interactive available with no MCP elicitation at all", async () => {
    const { guard } = make({ prompt: () => answering({ action: "decline" }) });
    expect(guard.canElicit).toBe(true);
    expect(DialogGuard.effectiveMode("interactive", guard.canElicit)).toBe("interactive");
  });

  it("is preferred over the MCP form when both are there", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const elicit = vi.fn(async () => ({ action: "accept" as const, content: { button: "Save Selected" } }));
    const prompt = answering({ action: "accept", content: { button: "Cancel" } });
    const press = vi.fn(async () => ({ success: true }));
    const { guard } = make({ prompt: () => prompt, elicit: () => elicit, press });
    await guard.check("editor.stop_editor", "action");
    expect(elicit).not.toHaveBeenCalled();
    expect(press).toHaveBeenCalledWith("Cancel");
  });

  it("does not take an answer faster than a person as a person's", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "5000";
    const press = vi.fn(async () => ({ success: true }));
    const prompt = answering({ action: "accept", content: { button: "Don't Save" } });
    const { guard } = make({ prompt: () => prompt, press });
    const decision = await guard.check("editor.stop_editor", "action");
    expect(press).not.toHaveBeenCalled();
    expect(decision.allow).toBe(false);
    // Not a used-up chance: the next call asks again.
    await guard.check("editor.stop_editor", "action");
    expect(prompt).toHaveBeenCalledTimes(2);
  });

  it("returns the refusal when the wait runs out, and still presses the later answer", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const press = vi.fn(async () => ({ success: true }));
    let answer!: (v: Awaited<ReturnType<DialogPromptFn>>) => void;
    const prompt = vi.fn(() => new Promise<Awaited<ReturnType<DialogPromptFn>>>((r) => (answer = r)));
    const { guard } = make({ prompt: () => prompt, press, askWaitMs: 20 });
    const decision = await guard.check("editor.stop_editor", "action");
    expect(decision.allow).toBe(false);
    if (!decision.allow) expect(decision.refusal.dialogMode).toBe("interactive");
    answer({ action: "accept", content: { button: "Cancel" } });
    await vi.waitFor(() => expect(press).toHaveBeenCalledWith("Cancel"));
  });

  it("never relays a long dialog first, since the prompt draws it whole", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const long = { dialogs: [{ title: "Save Content", message: "line\n".repeat(40), buttons: BUTTONS, choices: CHOICES }] };
    const prompt = answering({ action: "accept", content: { button: "Cancel" } });
    const { guard } = make({ prompt: () => prompt, probe: async () => long });
    await guard.check("editor.stop_editor", "action");
    expect(prompt).toHaveBeenCalledTimes(1);
  });
});

describe("delegate hands one dialog to the agent", () => {
  it("answers the waiting call with auto's refusal, press calls included", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const press = vi.fn(async () => ({ success: true }));
    const { guard } = make({ prompt: () => answering({ action: "delegate" }), press });
    const decision = await guard.check("editor.stop_editor", "action");
    expect(press).not.toHaveBeenCalled();
    expect(decision.allow).toBe(false);
    if (decision.allow) return;
    expect(decision.refusal.dialogMode).toBe("auto");
    expect(decision.refusal.dialogDelegated).toBe(true);
    expect(decision.refusal.choices).toEqual(CHOICES);
  });

  it("lets the agent's press through for that dialog, which interactive otherwise refuses", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const { guard } = make({ prompt: () => answering({ action: "delegate" }) });
    expect((await guard.check("editor.respond_to_dialog", "action")).allow).toBe(false);
    await guard.check("editor.stop_editor", "action");
    expect((await guard.check("editor.respond_to_dialog", "action")).allow).toBe(true);
    expect(guard.modeFor(guard.current)).toBe("auto");
  });

  it("does not carry over to the next dialog", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    let screen: object = listing();
    const prompt = vi.fn(async () => ({ action: "delegate" as const }));
    const { guard } = make({ prompt: () => prompt, probe: async () => screen });
    await guard.check("editor.stop_editor", "action");
    screen = empty;
    await guard.check("project.get_status", "action");
    screen = listing();
    expect((await guard.check("editor.respond_to_dialog", "action")).allow).toBe(false);
  });

  it("is not accepted faster than a person could give it", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "5000";
    const { guard } = make({ prompt: () => answering({ action: "delegate" }) });
    const decision = await guard.check("editor.stop_editor", "action");
    if (!decision.allow) expect(decision.refusal.dialogMode).toBe("interactive");
    expect(guard.isDelegated(guard.current)).toBe(false);
  });
});

describe("the MCP route is untouched", () => {
  it("elicits without a dialog field, which a client was never sent", async () => {
    const elicit = vi.fn(async (_p: unknown) => ({ action: "accept" as const, content: { button: "Cancel" } }));
    const { guard } = make({ elicit: () => elicit });
    await guard.check("editor.stop_editor", "action");
    expect(elicit).toHaveBeenCalledTimes(1);
    expect(elicit.mock.calls[0][0]).not.toHaveProperty("dialog");
  });

  it("takes a fast MCP answer exactly as before, with no min-human gate", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "5000";
    const press = vi.fn(async () => ({ success: true }));
    const elicit = vi.fn(async () => ({ action: "accept" as const, content: { button: "Cancel" } }));
    const { guard } = make({ elicit: () => elicit, press });
    expect((await guard.check("editor.stop_editor", "action")).allow).toBe(true);
    expect(press).toHaveBeenCalledWith("Cancel");
  });
});

describe("dialog events", () => {
  function collect() {
    const seen: DialogEvent[] = [];
    const off = onDialogEvent((e) => seen.push(e));
    return { seen, off };
  }

  it("reports a dialog opening once, however often it is seen", async () => {
    const { seen, off } = collect();
    try {
      const { guard } = make({ modeIs: "auto" });
      await guard.check("project.get_status", "action");
      await guard.check("project.get_status", "action");
      guard.note({ title: "Save Content", message: "flattened differently", buttons: BUTTONS, choices: [] });
      expect(seen.filter((e) => e.type === "opened")).toHaveLength(1);
      expect(seen[0]).toMatchObject({ type: "opened", editor: "Scratch", title: "Save Content", buttons: BUTTONS, mode: "auto" });
    } finally {
      off();
    }
  });

  it("reports the press, the route it came by, and the close", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const { seen, off } = collect();
    try {
      let screen: object = listing();
      const { guard } = make({
        probe: async () => screen,
        prompt: () => answering({ action: "accept", content: { button: "Cancel" } }),
        press: async () => {
          screen = empty;
          return { success: true };
        },
      });
      await guard.check("editor.stop_editor", "action");
      expect(seen.map((e) => e.type)).toEqual(["opened", "answered", "closed"]);
      expect(seen[1]).toMatchObject({ button: "Cancel", via: "prompt" });
    } finally {
      off();
    }
  });

  it("reports a press made by an action call, and a delegation", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const { seen, off } = collect();
    try {
      const { guard } = make({ prompt: () => answering({ action: "delegate" }) });
      await guard.check("editor.stop_editor", "action");
      guard.notePress("Don't Save", guard.current);
      expect(seen.map((e) => e.type)).toEqual(["opened", "delegated", "answered"]);
      expect(seen[2]).toMatchObject({ button: "Don't Save", via: "action" });
    } finally {
      off();
    }
  });

  it("keeps the guard working when a watcher throws", async () => {
    const off = onDialogEvent(() => {
      throw new Error("broken watcher");
    });
    try {
      const { guard } = make({ modeIs: "auto" });
      expect((await guard.check("project.search_tools", "action")).allow).toBe(false);
    } finally {
      off();
    }
  });
});
