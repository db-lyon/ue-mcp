/**
 * The dialog gate, end to end, against a real modal in the disposable project.
 *
 * The modal is raised the way a user hits it in practice: a dirty map plus a
 * shutdown request. "Cancel" on that prompt aborts the shutdown, so the editor
 * is still there for the rest of the tier.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();
let server: LiveServer;

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
}, 240_000);

afterAll(async () => {
  await server?.close();
  closeLiveBridges();
});

/** How many dialogs the editor reports right now. list_dialogs is modal-safe. */
async function dialogCount(): Promise<number> {
  const listed = resultJson<any>(await server.call("editor", { action: "list_dialogs" }));
  return Array.isArray(listed.dialogs) ? listed.dialogs.length : 0;
}

/**
 * Wait for the modal to appear or go, rather than assuming either is
 * instantaneous. The quit is scheduled on the game thread and the prompt comes
 * up a frame or two later, so a call sent immediately after scheduling can
 * genuinely arrive before there is anything to block it.
 */
async function waitForDialogs(want: "some" | "none", timeoutMs = 30_000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const n = await dialogCount();
    if (want === "some" ? n > 0 : n === 0) return;
    if (Date.now() > deadline) throw new Error(`timed out waiting for ${want} dialogs (saw ${n})`);
    await new Promise((r) => setTimeout(r, 250));
  }
}

/** Dirty the map, ask to quit, and leave the Save Content prompt standing. */
async function raiseModal(): Promise<void> {
  await waitForDialogs("none");
  resultJson<any>(await server.call("level", {
    action: "place_actor",
    actorClass: "StaticMeshActor",
    label: `DialogGateProbe_${Date.now()}`,
    staticMesh: "/Engine/BasicShapes/Cube.Cube",
  }));
  resultJson<any>(await server.call("editor", {
    action: "request_editor_shutdown",
    requireClean: false,
  }));
  await waitForDialogs("some");
}

/** Cancel aborts the scheduled quit, so the editor outlives the test. */
async function clearModal(): Promise<void> {
  await server.call("editor", { action: "respond_to_dialog", buttonLabel: "Cancel" });
  await waitForDialogs("none");
}

describe("a modal blocks every action, whatever raised it", () => {
  it("refuses editor-bound calls, offline calls and other categories alike", async () => {
    await raiseModal();
    try {
      // An editor-bound read. The plugin's gate catches this one.
      const outliner = resultJson<any>(await server.call("level", { action: "get_outliner", limit: 1 }));
      expect(outliner.dialogBlocking).toBe(true);
      expect(outliner.refusedMethod).toBe("get_world_outliner");
      expect(outliner.dialogTitle).toBe("Save Content");
      expect(outliner.buttons).toContain("Don't Save");
      expect(outliner.choices.some((c: any) => c.respondWith.includes("respond_to_dialog"))).toBe(true);

      // A different category, to show it is not one handler's behaviour.
      const assets = resultJson<any>(await server.call("asset", { action: "list", directory: "/Game", limit: 1 }));
      expect(assets.dialogBlocking).toBe(true);

      // An action served in-process that never reaches the bridge at all.
      // Before the server half existed this answered normally while Unreal
      // sat on the modal, which is the hole the plugin gate cannot see.
      const info = resultJson<any>(await server.call("project", { action: "get_info" }));
      expect(info.dialogBlocking).toBe(true);
      expect(info.refusedMethod).toBe("project.get_info");

      // A write, which is the case that matters most.
      const write = resultJson<any>(await server.call("level", {
        action: "place_actor",
        actorClass: "StaticMeshActor",
        label: "ShouldNeverSpawn",
      }));
      expect(write.dialogBlocking).toBe(true);
    } finally {
      await clearModal();
    }
  }, 180_000);

  it("keeps the dialog actions reachable, or the modal could never be answered", async () => {
    await raiseModal();
    try {
      const listed = resultJson<any>(await server.call("editor", { action: "list_dialogs" }));
      expect(listed.dialogBlocking).toBeUndefined();
      expect(listed.dialogs[0].title).toBe("Save Content");

      // Reading status has to work too, or a caller cannot orient itself.
      const status = resultJson<any>(await server.call("project", { action: "get_status" }));
      expect(status.dialogBlocking).toBeUndefined();
      expect(status.editorConnected).toBe(true);
    } finally {
      await clearModal();
    }
  }, 180_000);

  it("lets everything through again the moment the dialog is answered", async () => {
    await raiseModal();
    const blocked = resultJson<any>(await server.call("asset", { action: "list", directory: "/Game", limit: 1 }));
    expect(blocked.dialogBlocking).toBe(true);

    await clearModal();

    const after = resultJson<any>(await server.call("asset", { action: "list", directory: "/Game", limit: 1 }));
    expect(after.dialogBlocking).toBeUndefined();
    expect(after.success).not.toBe(false);

    // And the editor is still up, because Cancel aborted the quit.
    const status = resultJson<any>(await server.call("project", { action: "get_status" }));
    expect(status.editorConnected).toBe(true);
  }, 180_000);
});
