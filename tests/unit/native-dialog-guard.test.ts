/**
 * A native Win32 dialog of the editor process, through the dialog guard and
 * the bridge boundary.
 *
 * While such a box is up the bridge cannot answer anything, so every case here
 * gives the guard a probe and a bridge that would hang or fail if they were
 * asked: the native dialog has to be known, refused on, put to the person,
 * listed and pressed without them.
 */
import { describe, expect, it, vi } from "vitest";
import {
  DialogGuard,
  forgetGuard,
  guardFor,
  nativeAsBlocking,
  onDialogEvent,
  type BlockingDialog,
  type DialogEvent,
  type GuardDeps,
} from "../../src/editor/dialog-guard.js";
import { interpretNativeDialog } from "../../src/editor/native-dialogs.js";
import { GuardedBridge } from "../../src/flow/guarded-bridge.js";
import { GuardRegistry } from "../../src/flow/guard.js";
import type { EditorSession } from "../../src/sessions/session.js";
import type { IBridge } from "../../src/bridge/bridge.js";

const GPU: BlockingDialog = nativeAsBlocking(interpretNativeDialog({
  hwnd: "4242",
  pid: 77,
  commandLine: "UnrealEditor.exe C:\\P\\P.uproject",
  className: "#32770",
  title: "Error",
  controls: [
    { className: "Static", text: "GPU Crashed or D3D Device Removed.\nCheck log for GPU state information.", id: 65535 },
    { className: "Button", text: "OK", id: 2 },
  ],
})!);

const hang = () => new Promise<never>(() => {});

function deps(native: () => BlockingDialog[], over: Partial<GuardDeps> = {}): GuardDeps {
  return {
    mode: () => "auto",
    probe: vi.fn(hang),
    press: vi.fn(hang),
    isConnected: () => true,
    nativeDialogs: native,
    pressNative: vi.fn(async (_d: BlockingDialog, button: string) => ({ pressed: true, closed: true, button })),
    ...over,
  };
}

function events(): { seen: DialogEvent[]; stop: () => void } {
  const seen: DialogEvent[] = [];
  const stop = onDialogEvent((e) => seen.push(e));
  return { seen, stop };
}

describe("nativeAsBlocking", () => {
  it("carries the handle, process, kind and summary, with a respond_to_dialog choice per button", () => {
    expect(GPU.title).toBe("Error");
    expect(GPU.buttons).toEqual(["OK"]);
    expect(GPU.native).toMatchObject({ hwnd: "4242", pid: 77, kind: "gpu-crash" });
    expect(GPU.choices[0].respondWith).toBe("editor(action='respond_to_dialog', buttonLabel='OK')");
  });
});

describe("the guard and a native dialog", () => {
  it("refuses without asking the bridge, and says what the dialog means", async () => {
    const d = deps(() => [GPU]);
    const guard = new DialogGuard(d);
    const decision = await guard.check("level.get_outliner", "action");
    expect(decision.allow).toBe(false);
    const refusal = (decision as { refusal: Record<string, unknown> }).refusal;
    expect(refusal).toMatchObject({ dialogBlocking: true, nativeDialog: true, dialogKind: "gpu-crash", dialogTitle: "Error", buttons: ["OK"] });
    expect(String(refusal.error)).toMatch(/native Windows dialog/);
    expect(String(refusal.error)).toMatch(/GPU device was lost/);
    expect(d.probe).not.toHaveBeenCalled();
  });

  it("reports opened with the native fields, and closed when the window goes", async () => {
    let up = [GPU];
    const guard = new DialogGuard(deps(() => up));
    guard.label = "Vale";
    const { seen, stop } = events();
    try {
      await guard.refresh();
      expect(seen.map((e) => e.type)).toEqual(["opened"]);
      expect(seen[0]).toMatchObject({ editor: "Vale", title: "Error", native: { hwnd: "4242", kind: "gpu-crash" } });
      up = [];
      // With the window gone the guard asks the bridge again; give it a clear answer.
      guard.setDeps(deps(() => up, { probe: async () => ({ dialogs: [] }) }));
      await guard.refresh();
      expect(seen.map((e) => e.type)).toEqual(["opened", "closed"]);
      expect(guard.current).toBeNull();
    } finally {
      stop();
    }
  });

  it("does not flap when the status file reads clear while the box is up", async () => {
    const guard = new DialogGuard(deps(() => [GPU], { readSnapshot: () => ({ modal: null, ageSeconds: 0 }) }));
    const { seen, stop } = events();
    try {
      guard.startWatching(20);
      await new Promise((r) => setTimeout(r, 120));
      expect(seen.filter((e) => e.type === "opened")).toHaveLength(1);
      expect(seen.filter((e) => e.type === "closed")).toHaveLength(0);
      expect(guard.current?.native?.hwnd).toBe("4242");
    } finally {
      guard.stopWatching();
      stop();
    }
  });

  it("puts it to the extension's prompt with the native fields, and presses the chosen button by handle", async () => {
    const prompt = vi.fn(async () => ({ action: "accept" as const, content: { button: "OK" } }));
    let up = [GPU];
    const d = deps(() => up, {
      mode: () => "interactive",
      prompt: () => prompt,
      pressNative: vi.fn(async (_x: BlockingDialog, button: string) => {
        up = [];
        return { pressed: true, closed: true, button };
      }),
    });
    const guard = new DialogGuard(d);
    const { seen, stop } = events();
    const savedMin = process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    try {
      const decision = await guard.check("level.get_outliner", "action");
      expect(decision.allow).toBe(true);
      const req = (prompt.mock.calls[0] as unknown as [{ message: string; dialog: { native?: unknown } }])[0];
      expect(req.dialog.native).toMatchObject({ hwnd: "4242", kind: "gpu-crash" });
      expect(req.message).toMatch(/GPU device was lost/);
      expect(d.pressNative).toHaveBeenCalledWith(expect.objectContaining({ title: "Error" }), "OK");
      expect(d.press).not.toHaveBeenCalled();
      expect(seen.find((e) => e.type === "answered")).toMatchObject({ button: "OK", via: "prompt" });
    } finally {
      if (savedMin === undefined) delete process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;
      else process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = savedMin;
      stop();
    }
  });

  it("a person's press reports answered with via person", async () => {
    const guard = new DialogGuard(deps(() => [GPU]));
    const { seen, stop } = events();
    try {
      const r = await guard.pressNativeButton(GPU, "OK", "person");
      expect(r).toMatchObject({ pressed: true, closed: true });
      expect(seen.find((e) => e.type === "answered")).toMatchObject({ via: "person", native: { hwnd: "4242" } });
    } finally {
      stop();
    }
  });
});

function boundary(native: () => BlockingDialog[], inner: Partial<IBridge> = {}, over: Partial<GuardDeps> = {}) {
  const session = {} as unknown as EditorSession;
  const bridge = {
    isConnected: true,
    call: vi.fn(hang),
    connect: async () => {},
    retargetProject: () => ({}) as never,
    getTarget: () => ({ projectPath: "C:\\P\\P.uproject", port: 1, portSource: "test" }) as never,
    ...inner,
  } as unknown as IBridge;
  const d = deps(native, over);
  const guard = guardFor(session, d);
  return { session, bridge, guard, deps: d, guarded: new GuardedBridge(bridge, new GuardRegistry(), () => null, session) };
}

describe("the bridge boundary while a native dialog is up", () => {
  it("serves list_dialogs from the window scan, without the editor", async () => {
    const { session, bridge, guarded } = boundary(() => [GPU]);
    try {
      const listed = (await guarded.call("list_dialogs", {})) as { dialogs: Array<Record<string, unknown>> };
      expect(listed.dialogs[0]).toMatchObject({ title: "Error", buttons: ["OK"], blocksGameThread: true, detectedBy: "nativeWindow", native: { hwnd: "4242" } });
      expect(bridge.call).not.toHaveBeenCalled();
    } finally {
      forgetGuard(session);
    }
  });

  it("presses respond_to_dialog's button on the window, by label, index or response keyword", async () => {
    for (const params of [{ buttonLabel: "OK" }, { buttonIndex: 0 }, { response: "ok" }]) {
      const { session, bridge, guarded, deps: d } = boundary(() => [GPU]);
      try {
        const r = (await guarded.call("respond_to_dialog", params)) as Record<string, unknown>;
        expect(r).toMatchObject({ success: true, native: true, pressed: "OK", closed: true });
        expect(String(r.note)).toMatch(/exits now/);
        expect(d.pressNative).toHaveBeenCalledWith(expect.anything(), "OK");
        expect(bridge.call).not.toHaveBeenCalled();
      } finally {
        forgetGuard(session);
      }
    }
  });

  it("refuses closing a native dialog instead of answering it", async () => {
    const { session, guarded } = boundary(() => [GPU]);
    try {
      const r = (await guarded.call("respond_to_dialog", { dialogAction: "close" })) as Record<string, unknown>;
      expect(r.success).toBe(false);
      expect(String(r.error)).toMatch(/answered with one of its own buttons/);
    } finally {
      forgetGuard(session);
    }
  });

  it("tells a call already waiting on the editor the moment a native dialog appears", async () => {
    let up: BlockingDialog[] = [];
    // The editor answers the gate's probe (nothing up yet), then never answers the call.
    const { session, guard, guarded } = boundary(() => up, {}, { probe: async () => ({ dialogs: [] }) });
    try {
      const pending = guarded.call("spawn_actor", { actorClass: "StaticMeshActor" });
      await new Promise((r) => setTimeout(r, 20));
      up = [GPU];
      await guard.refresh();
      const r = (await pending) as Record<string, unknown>;
      expect(r).toMatchObject({ dialogBlocking: true, nativeDialog: true, inFlight: true, refusedMethod: "spawn_actor" });
      expect(String(r.error)).toMatch(/already been sent/);
    } finally {
      forgetGuard(session);
    }
  });

  it("goes to the editor as usual when no native dialog is up", async () => {
    const { session, bridge, guarded } = boundary(() => [], { call: vi.fn(async () => ({ success: true, dialogs: [] })) });
    try {
      await guarded.call("list_dialogs", {});
      expect(bridge.call).toHaveBeenCalledTimes(1);
    } finally {
      forgetGuard(session);
    }
  });
});
