/**
 * The guard, attacked rather than confirmed.
 *
 * Each case is a sequence that BROKE a previous implementation, written to
 * fail if that behaviour comes back. Where a prior version had a test that
 * passed while the bug was live, the case says so.
 */
import { describe, expect, it, vi } from "vitest";
import {
  DialogGuard,
  isModalSafeMethod,
  stampBlockedEditor,
  type GuardDeps,
} from "../../src/dialog-guard.js";
import type { DialogMode } from "../../src/user-state.js";

const DIALOG = {
  title: "Save Content",
  message: "Select Content to Save",
  buttons: ["Save Selected", "Don't Save", "Cancel"],
  choices: [{ buttonLabel: "Cancel", respondWith: "press Cancel" }],
};

const listing = (title = "Save Content") => ({
  dialogs: [{ title, message: "m", buttons: DIALOG.buttons, choices: DIALOG.choices }],
});

const empty = { dialogs: [] };

function make(over: Omit<Partial<GuardDeps>, "mode"> & { mode?: DialogMode } = {}) {
  const deps: GuardDeps = {
    mode: () => (over.mode ?? "auto") as DialogMode,
    // Default: a dialog IS up. The probe is authoritative, so a test about
    // a live dialog must have one to find; tests about it being gone pass
    // `empty` explicitly.
    probe: over.probe ?? (async () => listing()),
    press: over.press ?? (async () => ({ success: true })),
    elicit: over.elicit,
    readSnapshot: over.readSnapshot,
    isConnected: over.isConnected,
  };
  return new DialogGuard(deps);
}

const accept = (button: string) =>
  () => (vi.fn(async () => ({ action: "accept", content: { button } })) as unknown as ReturnType<NonNullable<GuardDeps["elicit"]>>);

/**
 * Get past the relay, so a case about the FORM is about the form.
 *
 * Under interactive the first gated call for a dialog hands its whole text
 * back and refuses without asking anything, because an elicitation form is a
 * few lines tall and a client may collapse the rest of it. The form goes up on
 * the next call. A case about what the person is shown therefore starts one
 * call earlier than it used to, and this makes that call rather than letting
 * every such case open with an unexplained duplicate line.
 *
 * The relay decision is returned, not swallowed: a case that wants to assert
 * on it can, and one that ignores it still pays for it visibly.
 */
async function relay(guard: DialogGuard, subject = "asset.list") {
  const decision = await guard.check(subject, "action");
  return decision;
}

describe("a failing probe never disarms the guard", () => {
  it("keeps the dialog when the probe throws but the editor is still publishing", async () => {
    // Conflating "no dialog" with "could not ask" let a dropped socket clear
    // the latch, and in-process actions then ran on a frozen editor.
    const guard = make({
      readSnapshot: () => ({ modal: null, ageSeconds: 0 }),
      probe: async () => {
        throw new Error("NOT_CONNECTED");
      },
    });
    guard.note(DIALOG);
    const decision = await guard.check("project.search_tools", "action");
    expect(decision.allow).toBe(false);
    expect(guard.current).not.toBeNull();
  });

  it("does NOT hold a dialog for an editor that is gone, or nothing could recover", async () => {
    // The wedge: a latched dialog plus an editor that went away refused every
    // action forever, including the two the refusal told you to make and the
    // relaunch. A dialog is a statement about a RUNNING editor.
    // No snapshot reader at all: with nothing to consult and no isConnected,
    // an absent snapshot is the only evidence available and the latch clears.
    const guard = make({
      probe: async () => {
        throw new Error("NOT_CONNECTED");
      },
    });
    guard.note(DIALOG);
    const decision = await guard.check("project.search_tools", "action");
    expect(decision.allow).toBe(true);
    expect(guard.current).toBeNull();
  });

  it("refuses the editor lifecycle actions too, which used to be exempt", async () => {
    // No exemption: a dialog blocks everything that acts. Answering it is the way out.
    const guard = make();
    guard.note(DIALOG);
    for (const action of ["editor.start_editor", "editor.stop_editor", "editor.restart_editor"]) {
      expect((await guard.check(action, "action")).allow, action).toBe(false);
    }
  });

  it("still lets the dialog be read and answered, which is how the block ends", async () => {
    // The reads are not an exception to blocking. They are how a person sees
    // what is being asked and answers it, and without them a modal would be a
    // dead end rather than a question.
    const guard = make();
    guard.note(DIALOG);
    for (const action of [
      "editor.list_dialogs",
      "editor.respond_to_dialog",
      "editor.get_dialog_policy",
      "editor.get_engine_state",
      "project.get_status",
    ]) {
      expect((await guard.check(action, "action")).allow, action).toBe(true);
    }
  });

  it("refreshes what it knows even for a subject it will not refuse", async () => {
    // Returning early without probing meant a cold guard never learned the
    // dialog (get_status reported a healthy editor while the thread was
    // parked) and a stale one was never corrected (the call that ANSWERED the
    // dialog came back stamped as still blocked).
    const guard = make({ probe: async () => listing("Save Content") });
    expect(guard.current).toBeNull();
    await guard.check("project.get_status", "action");
    expect(guard.current?.title).toBe("Save Content");

    const clearing = make({ probe: async () => empty });
    clearing.note(DIALOG);
    await clearing.check("editor.respond_to_dialog", "action");
    expect(clearing.current).toBeNull();
  });

  it("clears only on a probe that answered and listed nothing", async () => {
    const guard = make({ probe: async () => empty });
    guard.note(DIALOG);
    const decision = await guard.check("asset.list", "action");
    expect(decision.allow).toBe(true);
    expect(guard.current).toBeNull();
  });
});

describe("what a bridge reply is allowed to prove", () => {
  it("a modal-safe reply is not evidence the editor is running", async () => {
    // Following the refusal's own advice used to disarm the gate: read the
    // dialog with list_dialogs, and everything after ran free.
    const guard = make();
    guard.note(DIALOG);
    for (const method of [
      "list_dialogs",
      "respond_to_dialog",
      "get_dialog_policy",
      "get_engine_state",
      "get_bridge_capabilities",
    ]) {
      guard.observe(method, { success: true, dialogs: [] });
      expect(guard.current, `${method} cleared the guard`).not.toBeNull();
    }
  });

  it("an ordinary handler running IS evidence, because the plugin would have refused it", () => {
    const guard = make();
    guard.note(DIALOG);
    guard.observe("list_assets", { success: true });
    expect(guard.current).toBeNull();
  });

  it("learns the dialog from the plugin's refusal", () => {
    const guard = make();
    guard.observe("get_world_outliner", {
      success: false,
      dialogBlocking: true,
      dialogTitle: "Delete Assets",
      dialogMessage: "m",
      buttons: ["OK"],
      choices: [],
    });
    expect(guard.current?.title).toBe("Delete Assets");
  });
});

describe("what stays callable", () => {
  it("keeps reading and answering the dialog reachable", async () => {
    const guard = make();
    guard.note(DIALOG);
    for (const action of [
      "editor.list_dialogs",
      "editor.respond_to_dialog",
      "editor.get_dialog_policy",
      "editor.get_engine_state",
      "project.get_status",
    ]) {
      expect((await guard.check(action, "action")).allow, action).toBe(true);
    }
  });

  it("refuses arming a policy, which presses buttons on a live dialog", async () => {
    // Modal-safe in the plugin, so it WOULD be served. Under defer, whose
    // contract is that nothing is pressed without a person, that discards
    // unsaved work by way of the allowlist.
    const guard = make({ mode: "defer" });
    guard.note(DIALOG);
    for (const method of ["set_dialog_policy", "clear_dialog_policy"]) {
      expect(DialogGuard.bridgeAllowed(method), method).toBe(false);
      expect((await guard.check(method, "bridge")).allow, method).toBe(false);
    }
  });

  it("knows the plugin serves those two even though it refuses them", () => {
    // Modal-safe (the plugin answers) and not allowed (we refuse anyway) are
    // different questions, and conflating them is how the hole opened.
    expect(isModalSafeMethod("set_dialog_policy")).toBe(true);
    expect(DialogGuard.bridgeAllowed("set_dialog_policy")).toBe(false);
  });
});

describe("the mode decides, and only interactive presses anything", () => {
  it("interactive presses the chosen button and lets the call through", async () => {
    const press = vi.fn(async () => ({ success: true }));
    const guard = make({ mode: "interactive", press, elicit: accept("Cancel") });
    guard.note(DIALOG);
    // One call. This dialog fits a form, so there is no handover round trip
    // and nothing a caller has to know to repeat.
    const decision = await guard.check("asset.list", "action");
    expect(decision.allow).toBe(true);
    expect(press).toHaveBeenCalledWith("Cancel");
    expect(guard.current).toBeNull();
  });

  it("interactive presses nothing for a button the dialog does not offer", async () => {
    const press = vi.fn(async () => ({ success: true }));
    const guard = make({ mode: "interactive", press, elicit: accept("Format Drive") });
    guard.note(DIALOG);
    expect((await guard.check("asset.list", "action")).allow).toBe(false);
    expect(press).not.toHaveBeenCalled();
  });

  it("interactive reports the dialog when the person declines", async () => {
    const press = vi.fn(async () => ({ success: true }));
    const guard = make({
      mode: "interactive",
      press,
      elicit: () => (vi.fn(async () => ({ action: "decline" })) as never),
    });
    guard.note(DIALOG);
    expect((await guard.check("asset.list", "action")).allow).toBe(false);
    expect(press).not.toHaveBeenCalled();
  });

  it("auto hands back the press calls; defer withholds them", async () => {
    const autoGuard = make({ mode: "auto" });
    autoGuard.note(DIALOG);
    const auto = await autoGuard.check("asset.list", "action");
    expect(auto.allow).toBe(false);
    expect((auto as { refusal: Record<string, unknown> }).refusal.choices).toBeDefined();

    const deferGuard = make({ mode: "defer" });
    deferGuard.note(DIALOG);
    const defer = await deferGuard.check("asset.list", "action");
    const refusal = (defer as { refusal: Record<string, unknown> }).refusal;
    expect(refusal.choices).toBeUndefined();
    expect(refusal.buttons).toEqual(DIALOG.buttons);
    expect(String(refusal.error)).toContain("Unreal Editor window");
  });

  it("gives the same refusal whichever route asked", async () => {
    const guard = make({ mode: "auto" });
    guard.note(DIALOG);
    const viaAction = await guard.check("level.get_outliner", "action");
    const viaBridge = await guard.check("level.get_outliner", "bridge");
    expect((viaAction as { refusal: unknown }).refusal).toEqual(
      (viaBridge as { refusal: unknown }).refusal,
    );
  });
});

describe("detection does not depend on somebody making a call", () => {
  it("reads a modal out of the snapshot the plugin publishes", async () => {
    const guard = make({
      readSnapshot: () => ({
        modal: { title: "Restore Packages", message: "m", buttons: ["Yes", "No"] },
        ageSeconds: 0,
      }),
    });
    guard.startWatching(20);
    try {
      // The first read happens synchronously on startWatching.
      expect(guard.current?.title).toBe("Restore Packages");
      // And it blocks, with no call having been refused first.
      expect((await guard.check("project.search_tools", "action")).allow).toBe(false);
    } finally {
      guard.stopWatching();
    }
  });

  it("treats a snapshot with no modal as clear", async () => {
    const guard = make({ readSnapshot: () => ({ modal: null, ageSeconds: 0 }) });
    guard.note(DIALOG);
    guard.startWatching(20);
    try {
      expect(guard.current).toBeNull();
    } finally {
      guard.stopWatching();
    }
  });

  it("does not believe a snapshot nobody has refreshed", async () => {
    // A crash leaves the file behind, often recording the modal that preceded
    // it. Believing that refuses every action forever.
    const guard = make({
      readSnapshot: () => ({ modal: { title: "Save Content", message: "m", buttons: ["OK"] }, ageSeconds: 600 }),
      probe: async () => {
        throw new Error("NOT_CONNECTED");
      },
    });
    guard.note(DIALOG);
    guard.startWatching(20);
    try {
      expect(guard.current).toBeNull();
      expect((await guard.check("project.search_tools", "action")).allow).toBe(true);
    } finally {
      guard.stopWatching();
    }
  });

  it("does not let a dialog outlive the editor that raised it", async () => {
    // THE WEDGE. The plugin deletes its status file on a clean shutdown, so a
    // missing file is the normal state of an editor that is gone. Treating
    // that as "keep believing what I last saw" left a latched dialog refusing
    // every action forever, including the two the refusal names.
    const fs = await import("node:fs");
    const os = await import("node:os");
    const path = await import("node:path");
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-guard-gone-"));
    const missing = path.join(dir, "status.json");
    try {
      const guard = make({
        readSnapshot: () => (fs.existsSync(missing) ? { modal: {} } : null),
        probe: async () => {
          throw new Error("NOT_CONNECTED");
        },
      });
      guard.note(DIALOG);
      guard.startWatching(50);
      try {
        // The watcher sees no file, so the dialog is not treated as live.
        const decision = await guard.check("project.search_tools", "action");
        expect(decision.allow, "a dead editor still refused everything").toBe(true);
        expect(guard.current).toBeNull();
      } finally {
        guard.stopWatching();
      }
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it("does not believe a status file nobody has updated for a long time", async () => {
    const fs = await import("node:fs");
    const os = await import("node:os");
    const path = await import("node:path");
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-guard-stale-"));
    const file = path.join(dir, "status.json");
    fs.writeFileSync(file, JSON.stringify({ modal: { title: "Save Content", message: "m", buttons: ["OK"] } }));
    // Backdate it well past the staleness bound: a crash leaves the file
    // behind, and a crash is often preceded by the modal it last recorded.
    const old = Date.now() - 60_000;
    fs.utimesSync(file, new Date(old), new Date(old));
    try {
      const guard = make({
        // A snapshot that still names a modal, aged past the staleness bound.
        readSnapshot: () => ({ modal: { title: "Save Content" }, ageSeconds: 60 }),
        probe: async () => {
          throw new Error("NOT_CONNECTED");
        },
      });
      guard.note(DIALOG);
      guard.startWatching(50);
      try {
        const decision = await guard.check("project.search_tools", "action");
        expect(decision.allow, "a stale file wedged the server").toBe(true);
      } finally {
        guard.stopWatching();
      }
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});

describe("a route with nobody to ask never elicits", () => {
  it("reports the dialog instead of raising a form for someone who did not ask", async () => {
    // The HTTP routes share the per-session guard, whose deps were last set by
    // an MCP client. Without saying it has nobody to ask, a curl raised a form
    // in that client's UI and pressed a real button on its answer.
    const elicit = vi.fn(async () => ({ action: "accept", content: { button: "Cancel" } }));
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({
      mode: "interactive",
      press,
      elicit: () => elicit as never,
    });
    guard.note(DIALOG);
    const decision = await guard.check("flow.run", "action", { canElicit: false });
    expect(decision.allow).toBe(false);
    expect(elicit).not.toHaveBeenCalled();
    expect(press).not.toHaveBeenCalled();
    // And it is reported as defer would: named, but with no calls that press a
    // button. Handing actuation to an unattended request is the leak defer
    // exists to close.
    const refusal = (decision as { refusal: Record<string, unknown> }).refusal;
    expect(refusal.dialogMode).toBe("defer");
    expect(refusal.choices).toBeUndefined();
    expect(refusal.buttons).toEqual(DIALOG.buttons);
  });

  it("still elicits for a route that does have someone", async () => {
    const elicit = vi.fn(async () => ({ action: "accept", content: { button: "Cancel" } }));
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({ mode: "interactive", press, elicit: () => elicit as never });
    guard.note(DIALOG);
    await guard.check("level.get_outliner", "action");
    expect(elicit).toHaveBeenCalled();
  });
});

describe("being asked is not the same as having answered", () => {
  it("asks again after an elicitation that never rendered", async () => {
    // lastAsked was recorded before the ask, and askUser swallows failures, so
    // one broken form burned that dialog for the life of the process and
    // interactive silently became auto while still reporting interactive.
    let calls = 0;
    const elicit = vi.fn(async () => {
      calls += 1;
      if (calls === 1) throw new Error("no elicitation UI");
      return { action: "accept", content: { button: "Cancel" } };
    });
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({ mode: "interactive", press, elicit: () => elicit as never });
    guard.note(DIALOG);

    const first = await guard.check("asset.list", "action");
    expect(first.allow).toBe(false);
    expect(press).not.toHaveBeenCalled();

    // Same dialog, second attempt: the person must still get asked. The relay
    // is spent, so this is a second FORM rather than a second telling.
    const second = await guard.check("asset.list", "action");
    expect(elicit).toHaveBeenCalledTimes(2);
    expect(second.allow).toBe(true);
    expect(press).toHaveBeenCalledWith("Cancel");
  });

  it("does not ask twice for a dialog it already put to somebody", async () => {
    // The preflight and the bridge boundary both check one call. Asking at each
    // showed the same prompt twice and pressed two real buttons for one action.
    const elicit = vi.fn(async () => ({ action: "accept", content: { button: "Cancel" } }));
    // The press "succeeds" but the dialog stays up, as it would if the press
    // did not resolve it.
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({ mode: "interactive", press, elicit: () => elicit as never });
    guard.note(DIALOG);
    await guard.check("level.place_actor", "action");
    await guard.check("spawn_actor", "bridge");
    expect(elicit).toHaveBeenCalledTimes(1);
    expect(press).toHaveBeenCalledTimes(1);
  });
});

describe("one dialog, one ask, however many callers", () => {
  it("shares a single form across parallel calls instead of asking each", async () => {
    // Clients batch tool calls. Asking per call put three forms in front of
    // the person and pressed three real buttons for one dialog, with presses
    // two and three landing on whatever was on screen after the first.
    let forms = 0;
    const elicit = vi.fn(async () => {
      forms += 1;
      await new Promise((r) => setTimeout(r, 20));
      return { action: "accept", content: { button: "Cancel" } };
    });
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({ mode: "interactive", press, elicit: () => elicit as never });
    guard.note(DIALOG);

    await Promise.all([
      guard.check("asset.list", "action"),
      guard.check("level.get_outliner", "action"),
      guard.check("project.search_tools", "action"),
    ]);

    expect(forms).toBe(1);
    expect(press).toHaveBeenCalledTimes(1);
  });

  it("does not answer a second dialog with the first one's form", async () => {
    // The shared ask is keyed to its dialog. Keyed on nothing, a caller that
    // saw a different prompt awaited this one's form and inherited its press,
    // answering a question it was never shown.
    const shown: string[] = [];
    const elicit = vi.fn(async (req: { message: string }) => {
      shown.push(req.message);
      await new Promise((r) => setTimeout(r, 20));
      return { action: "accept", content: { button: "Cancel" } };
    });
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({ mode: "interactive", press, elicit: () => elicit as never });

    const other = { ...DIALOG, title: "Delete Assets", message: "different" };
    guard.note(DIALOG);
    // Both fit a form, so each is asked about on the call that meets it. What
    // matters is that they are asked SEPARATELY: the in-flight ask is keyed on
    // the dialog, so two concurrent callers seeing different prompts each get
    // their own form rather than one inheriting the other's answer.
    const a = guard.decideFor("asset.list", DIALOG);
    const b = guard.decideFor("asset.list", other);
    await Promise.all([a, b]);

    expect(shown.length).toBe(2);
    expect(shown.some((m) => m.includes("Save Content"))).toBe(true);
    expect(shown.some((m) => m.includes("Delete Assets"))).toBe(true);
  });
});

describe("an editor that is up but not answering is still blocked", () => {
  it("keeps the dialog when the socket is up and the probe times out", async () => {
    // The parked editor. The socket is established, so the disconnected branch
    // does not apply, but the game thread is not running so the probe never
    // comes back. Clearing on a missing status file here was a permanent miss,
    // not a race: a project whose editor has not published a snapshot yet is
    // the steady state right after launch.
    const guard = make({
      isConnected: () => true,
      probe: async () => {
        throw new Error("ETIMEDOUT");
      },
    });
    guard.note(DIALOG);
    const decision = await guard.check("level.get_outliner", "action");
    expect(decision.allow, "a parked editor let everything through").toBe(false);
    expect(guard.current).not.toBeNull();
  });

  it("still clears once the watcher proves the editor is gone", async () => {
    const guard = make({
      isConnected: () => true,
      readSnapshot: () => null,
      probe: async () => {
        throw new Error("ETIMEDOUT");
      },
    });
    guard.note(DIALOG);
    guard.startWatching(20);
    try {
      await new Promise((r) => setTimeout(r, 80));
      expect(guard.current, "the dialog outlived the editor").toBeNull();
      expect((await guard.check("level.get_outliner", "action")).allow).toBe(true);
    } finally {
      guard.stopWatching();
    }
  });
});

describe("a disconnected editor is not probed", () => {
  it("skips the probe entirely rather than burning a connection attempt", async () => {
    // Every gated call probes twice, once at the preflight and once at the
    // bridge. Against an editor that is down each of those is a connection
    // attempt that has to time out, for a question with an obvious answer:
    // there is no running editor to be blocked.
    const probe = vi.fn(async () => listing());
    const guard = make({ probe, isConnected: () => false });
    guard.note(DIALOG);
    const decision = await guard.check("level.get_outliner", "action");
    expect(probe, "probed a socket that is down").not.toHaveBeenCalled();
    // ...but a dropped socket is NOT evidence the dialog is gone. An editor can
    // be alive and sitting on a modal with its socket down, still publishing
    // that modal to its status file. Clearing here deleted what the watcher
    // knew and let everything run against a frozen editor.
    expect(decision.allow, "a dropped socket let everything through").toBe(false);
    expect(guard.current).not.toBeNull();
  });

  it("allows when the socket is down and nothing is known to be blocking", async () => {
    const probe = vi.fn(async () => listing());
    const guard = make({ probe, isConnected: () => false });
    const decision = await guard.check("level.get_outliner", "action");
    expect(decision.allow).toBe(true);
    expect(probe).not.toHaveBeenCalled();
  });

  it("releases a latched dialog once the editor stops publishing, socket down", async () => {
    // THE WEDGE, verified rather than assumed. The watcher set a flag and
    // returned WITHOUT clearing, and the socket-down path returned the latch
    // unconditionally, so a dead editor refused every action forever with no
    // way back but restarting the server.
    let alive = true;
    const guard = make({
      isConnected: () => false,
      readSnapshot: () =>
        alive
          ? { modal: { title: "Save Content", message: "m", buttons: ["Cancel"] }, ageSeconds: 0 }
          : null,
      probe: async () => {
        throw new Error("NOT_CONNECTED");
      },
    });
    guard.startWatching(20);
    try {
      expect(guard.current?.title).toBe("Save Content");
      expect((await guard.check("level.get_outliner", "action")).allow).toBe(false);

      // The editor went away, taking its published snapshot with it.
      alive = false;
      await new Promise((r) => setTimeout(r, 80));

      expect(guard.current, "the dialog outlived the editor").toBeNull();
      expect((await guard.check("level.get_outliner", "action")).allow).toBe(true);
    } finally {
      guard.stopWatching();
    }
  });

  it("still probes while the bridge is up", async () => {
    const probe = vi.fn(async () => listing());
    const guard = make({ probe, isConnected: () => true });
    const decision = await guard.check("level.get_outliner", "action");
    expect(decision.allow).toBe(false);
    expect(probe).toHaveBeenCalledTimes(1);
  });
});

describe("interactive with nobody to ask is not interactive", () => {
  it("reports and behaves as defer rather than handing over the press calls", async () => {
    // It was reporting mode "interactive" and handing the buttons to the agent
    // anyway, which is auto's behaviour under a mode whose whole contract is
    // that a PERSON chooses.
    const guard = make({ mode: "interactive", elicit: undefined });
    guard.note(DIALOG);
    const decision = await guard.check("asset.list", "action");
    const refusal = (decision as { refusal: Record<string, unknown> }).refusal;
    expect(refusal.dialogMode).toBe("defer");
    expect(refusal.choices).toBeUndefined();
    expect(String(refusal.error)).toContain("Unreal Editor window");
  });

  it("stays interactive when there IS someone to ask", async () => {
    const elicit = vi.fn(async () => ({ action: "decline" }));
    const guard = make({ mode: "interactive", elicit: () => elicit as never });
    guard.note(DIALOG);
    const decision = await guard.check("asset.list", "action");
    const refusal = (decision as { refusal: Record<string, unknown> }).refusal;
    expect(refusal.dialogMode).toBe("interactive");
  });
});

describe("stamping a result as coming from a blocked editor", () => {
  it("adds the fields to an object result", () => {
    const data: Record<string, unknown> = { success: true, dialogs: [] };
    stampBlockedEditor(data, DIALOG, "auto");
    expect(data.editorBlockedByDialog).toBe(true);
    expect(data.dialogTitle).toBe("Save Content");
    expect(String(data.dialogNote)).toContain("respond_to_dialog");
    // And leaves the payload alone.
    expect(data.success).toBe(true);
  });

  it("names the press call in auto only, because only auto may press", () => {
    // This note rides on get_status, the first call any client makes. Written
    // mode-blind it told an interactive session's agent how to answer the
    // dialog before anything had been refused, which is the whole leak: the
    // mode never gated the press, it only decided whether a form went up first.
    for (const mode of ["interactive", "defer"] as const) {
      const data: Record<string, unknown> = { success: true };
      stampBlockedEditor(data, DIALOG, mode);
      expect(String(data.dialogNote), mode).not.toContain("editor(respond_to_dialog)");
      expect(String(data.dialogNote), mode).toContain("refused");
      // It still says a dialog is up, and still says how to READ it.
      expect(data.editorBlockedByDialog, mode).toBe(true);
      expect(String(data.dialogNote), mode).toContain("list_dialogs");
    }
  });

  it("says the least when no mode was resolved", () => {
    // Omitting the mode must not leak the widest one. The default is defer.
    const data: Record<string, unknown> = { success: true };
    stampBlockedEditor(data, DIALOG);
    expect(String(data.dialogNote)).not.toContain("editor(respond_to_dialog)");
  });

  it("leaves an array alone, because the fields would be silently lost", () => {
    // typeof [] === "object", so an array took the properties and then dropped
    // them in JSON.stringify: a stamp that claims to warn and does not.
    const data: unknown[] = [{ a: 1 }];
    const out = stampBlockedEditor(data, DIALOG);
    expect(out).toBe(data);
    expect(Object.keys(data)).toEqual(["0"]);
    expect(JSON.parse(JSON.stringify(data))).toEqual([{ a: 1 }]);
  });

  it("leaves a null or primitive result alone", () => {
    expect(stampBlockedEditor(null, DIALOG)).toBeNull();
    expect(stampBlockedEditor("text", DIALOG)).toBe("text");
    expect(stampBlockedEditor(42, DIALOG)).toBe(42);
  });

  it("says nothing when no dialog is blocking", () => {
    const data: Record<string, unknown> = { success: true };
    stampBlockedEditor(data, null);
    expect(data.editorBlockedByDialog).toBeUndefined();
    expect(Object.keys(data)).toEqual(["success"]);
  });

  it("does not use dialogBlocking, which means the call was refused", () => {
    const data: Record<string, unknown> = { success: true };
    stampBlockedEditor(data, DIALOG);
    expect(data.dialogBlocking).toBeUndefined();
  });
});

describe("refreshing state is not the same as deciding", () => {
  it("re-reads what is on screen without eliciting or pressing", async () => {
    // The post-press re-probe used to call check(), which applies the mode: in
    // interactive it raised a form for whatever prompt the answer surfaced and
    // pressed a button on it, unasked, then discarded the decision.
    const elicit = vi.fn(async () => ({ action: "accept", content: { button: "Cancel" } }));
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({
      mode: "interactive",
      press,
      elicit: () => elicit as never,
      probe: async () => listing("Delete Assets"),
    });

    await guard.refresh();

    expect(elicit).not.toHaveBeenCalled();
    expect(press).not.toHaveBeenCalled();
    // And it did update what the guard knows.
    expect(guard.current?.title).toBe("Delete Assets");
  });

  it("clears through refresh when the editor reports nothing", async () => {
    const guard = make({ probe: async () => empty });
    guard.note(DIALOG);
    await guard.refresh();
    expect(guard.current).toBeNull();
  });
});

describe("who may press the button is the mode's decision, not the allow list's", () => {
  it("refuses an agent's press under interactive and defer", async () => {
    // The bug this exists for: editor.respond_to_dialog sat on the
    // always-allowed list and never consulted the mode, so an agent could
    // answer a modal under interactive. The mode decided whether a form went
    // up first, and nothing more. Allow-listed means "safe to send while the
    // game thread is parked", which is a different question from "may answer".
    for (const mode of ["interactive", "defer"] as const) {
      const guard = make({ mode, elicit: accept("Cancel") });
      const decision = await guard.check("editor.respond_to_dialog", "action");
      expect(decision.allow, mode).toBe(false);
      const refusal = (decision as { refusal: Record<string, unknown> }).refusal;
      // And the refusal does not then explain how to do it anyway.
      expect(refusal.choices, mode).toBeUndefined();
      expect(String(refusal.error), mode).toContain("refused");
    }
  });

  it("lets the agent press in auto, which is the mode that says it may", async () => {
    const guard = make({ mode: "auto" });
    expect((await guard.check("editor.respond_to_dialog", "action")).allow).toBe(true);
  });

  it("still lets the guard press the button the PERSON picked", async () => {
    // The gate is on the action route only. The guard's own press, on the
    // button chosen in the elicitation form, travels over the bridge, and
    // gating that would refuse the one press interactive exists to make.
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({ mode: "interactive", elicit: accept("Cancel"), press });
    expect((await guard.check("respond_to_dialog", "bridge")).allow).toBe(true);
    // End to end: a person answers, the button is pressed, the call proceeds.
    const guard2 = make({
      mode: "interactive",
      elicit: accept("Cancel"),
      press,
      probe: async () => listing(),
    });
    expect((await guard2.check("asset.list", "action")).allow).toBe(true);
    expect(press).toHaveBeenCalledWith("Cancel");
  });

  it("keeps reading the dialog available in every mode", async () => {
    // Withholding the press must not withhold the question. A person deciding
    // in the editor window is still entitled to have the agent tell them what
    // it says.
    for (const mode of ["interactive", "defer", "auto"] as const) {
      const guard = make({ mode });
      expect((await guard.check("editor.list_dialogs", "action")).allow, mode).toBe(true);
      expect((await guard.check("project.get_status", "action")).allow, mode).toBe(true);
    }
  });

  it("never names the press call to a mode that cannot make it", async () => {
    // Withholding `choices` and then naming editor(respond_to_dialog) in the
    // next sentence is not withholding anything. defer did exactly that.
    for (const mode of ["interactive", "defer"] as const) {
      const refusal = DialogGuard.describeRefusal("asset.list", DIALOG, mode);
      expect(String(refusal.error), mode).not.toContain("editor(respond_to_dialog)");
      expect(String(refusal.error), mode).not.toContain("editor(list_dialogs)");
      expect(refusal.choices, mode).toBeUndefined();
      // The dialog itself is still fully reported, which is what a person
      // needs to recognise the window.
      expect(refusal.dialogTitle, mode).toBe("Save Content");
      expect(refusal.buttons, mode).toEqual(DIALOG.buttons);
    }
  });
});

describe("the elicitation form leads with the question, not the boilerplate", () => {
  it("puts the title and the dialog's own text in the first two lines", async () => {
    // A client renders this message itself, and at least one keeps the opening
    // line or two and collapses the rest behind "(+N more lines)". Opening
    // with boilerplate meant the part that got collapsed was the question, and
    // the person was asked to choose a button for something they could not
    // read.
    let seen = "";
    const guard = make({
      mode: "interactive",
      probe: async () => ({
        dialogs: [{
          title: "Save Content",
          message: ["Select the assets to save.", "/Game/A", "/Game/B", "/Game/C"].join("\n"),
          buttons: DIALOG.buttons,
          choices: DIALOG.choices,
        }],
      }),
      elicit: () => (async (req: { message: string }) => {
        seen = req.message;
        return { action: "decline" };
      }) as never,
    });
    await guard.check("asset.list", "action");
    const lines = seen.split(String.fromCharCode(10));
    expect(lines[0]).toContain("Save Content");
    // The question is the SECOND line, the last one a collapsing client is
    // guaranteed to show.
    expect(lines[1]).toContain("Select the assets to save.");
    // The packages follow it a line each, rather than flattened into it.
    expect(seen).toContain("/Game/A");
  });
});

describe("dismissing the form is not the end of the session", () => {
  it("recovers as soon as the person answers the dialog in the editor window", async () => {
    // Now that no agent press can rescue a declined form, this is THE way out,
    // so it is pinned rather than left to be inferred from the clearing rules.
    //
    // The sequence: a form goes up, the person declines it, and they answer
    // the modal in Unreal's own window instead. Every gated call probes before
    // deciding, so the next one finds a clear editor and runs.
    let onScreen = true;
    const guard = make({
      mode: "interactive",
      probe: async () => (onScreen ? listing() : empty),
      elicit: () => (async () => ({ action: "decline" })) as never,
    });

    // Declined, so nothing was pressed and the call is refused.
    expect((await guard.check("asset.list", "action")).allow).toBe(false);
    expect(guard.lastPressed).toBeNull();

    // Answered by hand. The probe is what notices, and one call is enough.
    onScreen = false;
    expect((await guard.check("asset.list", "action")).allow).toBe(true);
    expect(guard.current).toBeNull();
  });

  it("asks again for the NEXT dialog, rather than staying quiet for the session", async () => {
    // The asked-once record is keyed on the dialog and reset when the editor
    // reports a clear screen. A person who declines one form must still be
    // asked about the next prompt, or declining once silently downgrades the
    // rest of the session to a mode nobody chose.
    let phase: "first" | "clear" | "second" = "first";
    const forms: string[] = [];
    const guard = make({
      mode: "interactive",
      probe: async () =>
        phase === "clear" ? empty : phase === "first" ? listing("Save Content") : listing("Delete Assets"),
      elicit: () => (async (req: { message: string }) => {
        forms.push(req.message);
        return { action: "decline" };
      }) as never,
    });

    await guard.check("asset.list", "action");
    expect(forms).toHaveLength(1);

    phase = "clear";
    expect((await guard.check("asset.list", "action")).allow).toBe(true);

    // A clear screen forgets BOTH records, so the next dialog is relayed and
    // then asked about, exactly as the first was. Forgetting only the asked-once
    // record would ask about a dialog the caller was never given.
    phase = "second";
    await guard.check("asset.list", "action");
    expect(forms, "the second dialog never reached the person").toHaveLength(2);
    expect(forms[1]).toContain("Delete Assets");
  });

  it("does not re-ask while the same dialog is still on screen", async () => {
    // The other half of the same rule. A single call is checked twice, once
    // before dispatch and once at the bridge, and asking per check put two
    // forms up and pressed two buttons for one action.
    const forms: string[] = [];
    const guard = make({
      mode: "interactive",
      probe: async () => listing(),
      elicit: () => (async (req: { message: string }) => {
        forms.push(req.message);
        return { action: "decline" };
      }) as never,
    });
    await guard.check("asset.list", "action");
    await guard.check("asset.list", "action");
    expect(forms).toHaveLength(1);
  });
});

describe("the handover is skipped for a client that renders the whole message", () => {
  /** A gate that names its client, the way the server's does. */
  const elicitAs = (name: string, forms: string[]) => () => {
    const fn = (async (req: { message: string }) => {
      forms.push(req.message);
      return { action: "decline" };
    }) as unknown as NonNullable<ReturnType<NonNullable<GuardDeps["elicit"]>>>;
    (fn as unknown as { client: () => { name: string } }).client = () => ({ name });
    return fn;
  };

  it("asks on the FIRST call, with no dialog handed over first", async () => {
    // The round trip exists to get the text past a client that collapses it.
    // Against one that draws the lot it buys nothing and delays the form.
    const forms: string[] = [];
    const guard = make({ mode: "interactive", elicit: elicitAs("pi-coding-agent", forms) });
    guard.note(DIALOG);
    await guard.check("asset.list", "action");
    expect(forms, "the form should have gone up on the first call").toHaveLength(1);
    expect(forms[0]).toContain("Save Content");
  });

  it("still hands a BIG dialog over first for a client nobody has checked", async () => {
    // Two conditions now, not one: the client collapses long messages AND this
    // message is long enough to be collapsed.
    const big = {
      ...DIALOG,
      message: ["Select Content to Save"]
        .concat(Array.from({ length: 40 }, (_, n) => `/Game/Pkg/Asset_${n}`))
        .join(String.fromCharCode(10)),
    };
    const forms: string[] = [];
    // The probe is authoritative, so a case about THIS dialog has to serve it.
    const guard = make({
      mode: "interactive",
      elicit: elicitAs("some-new-agent", forms),
      probe: async () => ({ dialogs: [big] }),
    });
    guard.note(big);
    await guard.check("asset.list", "action");
    expect(forms, "an unchecked client must not be assumed to render").toHaveLength(0);
    await guard.check("asset.list", "action");
    expect(forms).toHaveLength(1);
  });

  it("asks an unchecked client immediately when the dialog fits a form", async () => {
    // The handover leaves raising the form to whoever calls again, which is
    // not something to depend on. A prompt small enough to render whole is put
    // to the person on the spot, whatever the client.
    const forms: string[] = [];
    const guard = make({ mode: "interactive", elicit: elicitAs("some-new-agent", forms) });
    guard.note(DIALOG);
    await guard.check("asset.list", "action");
    expect(forms, "the person was not asked on the call that met the dialog").toHaveLength(1);
  });
});

describe("a dialog that asks a question per item", () => {
  const SAVE_ITEMS = {
    title: "Save Content",
    message: "Select Content to Save",
    buttons: ["Save Selected", "Don't Save", "Cancel"],
    choices: DIALOG.choices,
    items: [
      { index: 0, label: "Asset", cells: ["Asset", "File", "Type"], checked: true },
      { index: 1, label: "L_Test", cells: ["L_Test", "/Game/Maps/L_Test", "/Script/Engine.World"], checked: true },
      { index: 2, label: "M_Rock", cells: ["M_Rock", "/Game/Mat/M_Rock", "/Script/Engine.Material"], checked: true },
    ],
  };

  it("offers a toggle per tickable row, and none for the select-all header", async () => {
    let schema: any;
    const guard = make({
      mode: "interactive",
      probe: async () => ({ dialogs: [SAVE_ITEMS] }),
      elicit: () => (async (req: any) => {
        schema = req.requestedSchema;
        return { action: "decline" };
      }) as never,
    });

    await guard.check("asset.list", "action");

    // The two real rows, keyed by index, titled by the asset.
    expect(schema.properties.item_1).toMatchObject({ type: "boolean", title: "L_Test", default: true });
    expect(schema.properties.item_2).toMatchObject({ type: "boolean", title: "M_Rock" });
    // The header row carries no path, so it is not a question.
    expect(schema.properties.item_0).toBeUndefined();
    // The buttons are still the decision.
    expect(schema.properties.button.enum).toContain("Save Selected");
  });

  it("sends what the person ticked WITH the button, in one press", async () => {
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({
      mode: "interactive",
      press,
      probe: async () => ({ dialogs: [SAVE_ITEMS] }),
      elicit: () => (async () => ({
        action: "accept",
        content: { button: "Save Selected", item_1: true, item_2: false },
      })) as never,
    });

    await guard.check("asset.list", "action");

    expect(press).toHaveBeenCalledWith("Save Selected", [
      { index: 1, checked: true },
      { index: 2, checked: false },
    ]);
  });

  it("presses with one argument when the dialog has nothing to tick", async () => {
    const press = vi.fn(async () => ({ success: true, answered: true }));
    const guard = make({ mode: "interactive", press, elicit: accept("Cancel") });
    guard.note(DIALOG);

    await guard.check("asset.list", "action");

    expect(press).toHaveBeenCalledWith("Cancel");
  });
});
