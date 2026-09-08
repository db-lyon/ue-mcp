/**
 * The bridge boundary refuses on its own.
 *
 * This is the half of the guard that has no preflight in front of it: a flow
 * step, or a handler making its own bridge calls, is never vetted under its own
 * name. The preflight saw `flow.run`, not `editor.set_dialog_policy`.
 *
 * It needs its own test because the end-to-end cases cannot see it. The plugin
 * refuses ordinary methods anyway, and the preflight catches anything named as
 * a tool action, so an auditor deleted `guardCall` and every wiring test stayed
 * green.
 */
import { describe, expect, it, vi } from "vitest";
import { GuardedBridge } from "../../src/flow/guarded-bridge.js";
import { GuardRegistry } from "../../src/flow/guard.js";
import { DialogGuard, guardFor, forgetGuard, withoutDialogActuation } from "../../src/dialog-guard.js";
import type { EditorSession } from "../../src/session.js";
import type { IBridge } from "../../src/bridge.js";

const DIALOG = {
  title: "Save Content",
  message: "Select Content to Save",
  buttons: ["Save Selected", "Don't Save", "Cancel"],
  choices: [{ buttonLabel: "Cancel", respondWith: "press Cancel" }],
};

function harness(mode: "auto" | "defer" | "interactive" = "auto") {
  const session = {} as unknown as EditorSession;
  const inner = {
    isConnected: true,
    call: vi.fn(async () => ({ success: true })),
    connect: async () => {},
    retargetProject: () => ({}) as never,
    getTarget: () => ({ projectPath: "p", port: 1, portSource: "test" }) as never,
  } as unknown as IBridge;

  const guard = guardFor(session, {
    mode: () => mode,
    probe: async () => ({ dialogs: [{ ...DIALOG }] }),
    press: async () => ({ success: true, answered: true }),
  });
  guard.note(DIALOG);

  const bridge = new GuardedBridge(inner, new GuardRegistry(), async () => null, session);
  return { session, inner, guard, bridge };
}

describe("guardCall, the boundary a flow step crosses", () => {
  it("refuses a step's bridge call without sending it", async () => {
    const { session, inner, bridge } = harness();
    try {
      const result = await bridge.call("spawn_actor", { actorClass: "StaticMeshActor" });
      expect(result).toMatchObject({ dialogBlocking: true, refusedMethod: "spawn_actor" });
      expect(inner.call).not.toHaveBeenCalled();
    } finally {
      forgetGuard(session);
    }
  });

  it("refuses arming a policy from a step, which the plugin WOULD have served", async () => {
    // set_dialog_policy is modal-safe in the plugin, so nothing downstream
    // stops it, and an armed policy presses buttons on the dialog already on
    // screen. Under defer that answers the prompt with no person involved.
    const { session, inner, bridge } = harness("defer");
    try {
      const result = await bridge.call("set_dialog_policy", { pattern: "Save", response: "no" });
      expect(result).toMatchObject({ dialogBlocking: true });
      expect(inner.call).not.toHaveBeenCalled();
    } finally {
      forgetGuard(session);
    }
  });

  it("lets the dialog methods through, or a step could never clear one", async () => {
    const { session, inner, bridge } = harness();
    try {
      for (const method of ["list_dialogs", "respond_to_dialog", "get_engine_state"]) {
        await bridge.call(method, {});
      }
      expect(inner.call).toHaveBeenCalledTimes(3);
    } finally {
      forgetGuard(session);
    }
  });

  it("sends normally once the editor reports the dialog gone", async () => {
    const session = {} as unknown as EditorSession;
    const inner = {
      isConnected: true,
      call: vi.fn(async () => ({ success: true })),
      connect: async () => {},
      retargetProject: () => ({}) as never,
      getTarget: () => ({ projectPath: "p", port: 1, portSource: "test" }) as never,
    } as unknown as IBridge;
    guardFor(session, {
      mode: () => "auto",
      probe: async () => ({ dialogs: [] }),
      press: async () => ({ success: true }),
    });
    const bridge = new GuardedBridge(inner, new GuardRegistry(), async () => null, session);
    try {
      await bridge.call("spawn_actor", {});
      expect(inner.call).toHaveBeenCalledTimes(1);
    } finally {
      forgetGuard(session);
    }
  });

  it("builds a guard for a session that has none, rather than refusing it forever", async () => {
    // The gate fails closed, which is only safe because no session can reach
    // it without a guard. Guards used to be created in one startup pass, so a
    // session registered any other way had none and every call it ever made
    // was refused. Now the boundary makes one from the session itself.
    const { forgetGuard: forget, existingGuard: existing } = await import("../../src/dialog-guard.js");
    const inner = {
      isConnected: true,
      call: vi.fn(async () => ({ success: true })),
      connect: async () => {},
      retargetProject: () => ({}) as never,
      getTarget: () => ({ projectPath: null, port: 1, portSource: "test" }) as never,
    } as unknown as IBridge;
    const session = {
      projectDir: null,
      project: { projectPath: null },
      bridge: inner,
      guarded: inner,
    } as unknown as EditorSession;
    try {
      expect(existing(session), "started with a guard, so this proves nothing").toBeUndefined();
      const bridge = new GuardedBridge(inner, new GuardRegistry(), async () => null, session);
      const result = await bridge.call("spawn_actor", {});
      // Nothing is blocking, so the call runs; what matters is that it was
      // decided by a guard rather than refused for the lack of one.
      expect(result).toMatchObject({ success: true });
      expect(inner.call).toHaveBeenCalled();
      expect(existing(session), "no guard was created for the session").toBeDefined();
    } finally {
      existing(session)?.stopWatching();
      forget(session);
    }
  });

  it("does not let a modal-safe reply clear the guard", async () => {
    const { session, guard, bridge } = harness();
    try {
      // list_dialogs answers whether or not a modal is up, so its success
      // proves nothing about the game thread.
      await bridge.call("list_dialogs", {});
      expect(guard.current).not.toBeNull();
    } finally {
      forgetGuard(session);
    }
  });
});

describe("a plugin guard task cannot answer the dialog for you", () => {
  it("refuses arming a policy on the raw bridge while a modal is up", async () => {
    // Guard tasks run on the RAW bridge on purpose: routing them through the
    // guarded one would re-enter the pipeline running them. That left a hole,
    // because set_dialog_policy is modal-safe and the plugin WILL serve it, so
    // a guard task could arm a policy that answers the modal already on screen.
    const { session, inner, guard } = harness("defer");
    try {
      expect(guard.current).not.toBeNull();
      const safe = withoutDialogActuation(session, inner);
      for (const method of ["set_dialog_policy", "clear_dialog_policy"]) {
        const res = await safe.call(method, { pattern: "Save", response: "no" });
        expect(res, method).toMatchObject({ dialogBlocking: true });
      }
      expect(inner.call).not.toHaveBeenCalled();
    } finally {
      forgetGuard(session);
    }
  });

  it("passes everything else straight through, with no extra round trip", async () => {
    const { session, inner } = harness();
    try {
      const safe = withoutDialogActuation(session, inner);
      await safe.call("spawn_actor", {});
      await safe.call("list_dialogs", {});
      expect(inner.call).toHaveBeenCalledTimes(2);
    } finally {
      forgetGuard(session);
    }
  });

  it("allows arming a policy when nothing is on screen", async () => {
    const session = {} as unknown as EditorSession;
    const inner = {
      isConnected: true,
      call: vi.fn(async () => ({ success: true })),
      connect: async () => {},
      retargetProject: () => ({}) as never,
      getTarget: () => ({ projectPath: "p", port: 1, portSource: "test" }) as never,
    } as unknown as IBridge;
    guardFor(session, {
      mode: () => "defer",
      probe: async () => ({ dialogs: [] }),
      press: async () => ({ success: true }),
    });
    try {
      const safe = withoutDialogActuation(session, inner);
      await safe.call("set_dialog_policy", { pattern: "Save", response: "no" });
      expect(inner.call).toHaveBeenCalledTimes(1);
    } finally {
      forgetGuard(session);
    }
  });
});

describe("the actuation block lives on the session's own bridge", () => {
  it("refuses a policy on session.bridge, the one-line escape from the wrapper", async () => {
    // The proxy used to wrap only the bridge handed to guard tasks, but
    // `ctx.session.bridge` is the raw one and reachable from any handler or
    // task. Wrapping one caller left the escape a single line away, so the
    // block is applied where the session builds its bridge instead.
    const { SessionRegistry } = await import("../../src/session.js");
    const { GuardRegistry } = await import("../../src/flow/guard.js");
    const registry = new SessionRegistry(new GuardRegistry());
    const session = registry.register({});
    try {
      const guard = guardFor(session, {
        mode: () => "defer",
        probe: async () => ({ dialogs: [{ ...DIALOG }] }),
        press: async () => ({ success: true }),
      });
      guard.note(DIALOG);

      // The raw bridge, reached exactly as a handler would reach it.
      const viaSession = await session.bridge.call("set_dialog_policy", {
        pattern: "Save",
        response: "no",
      });
      expect(viaSession).toMatchObject({ dialogBlocking: true });

      // And through the guarded one, which wraps the same protected bridge.
      const viaGuarded = await session.guarded.call("clear_dialog_policy", {});
      expect(viaGuarded).toMatchObject({ dialogBlocking: true });
    } finally {
      forgetGuard(session);
    }
  });
});
