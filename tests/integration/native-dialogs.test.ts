/**
 * A native Win32 dialog of the editor process, end to end through the real
 * server and daemon: the window scan is a fixture file (UE_MCP_NATIVE_DIALOG_FIXTURE),
 * the editor a fake bridge that would answer if it were asked.
 *
 * The daemon reports the dialog on /v1/events and /v1/dialogs/native, a call
 * is refused with what the dialog means and never reaches the editor,
 * list_dialogs and respond_to_dialog serve it from outside the editor, and a
 * press by window handle answers it.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, Sandbox, json, stopSandboxDaemons, waitFor } from "./harness.js";
import { base, bearer, call, readSse, startDaemonCli, type Discovery } from "./daemon-fakes.js";

let sb: Sandbox;
let project: string;
let fixture: string;

function gpuBox(uproject: string, hwnd = "4242") {
  return {
    hwnd,
    pid: 4321,
    processName: "UnrealEditor",
    commandLine: `"C:\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor.exe" "${uproject}"`,
    className: "#32770",
    title: "Error",
    hung: false,
    controls: [
      { className: "Static", text: "GPU Crashed or D3D Device Removed.\r\n\r\nCheck log for GPU state information.", id: 65535, visible: true, enabled: true },
      { className: "Button", text: "OK", id: 2, visible: true, enabled: true },
    ],
  };
}

function presses(): Array<Record<string, unknown>> {
  const file = `${fixture}.presses.jsonl`;
  if (!fs.existsSync(file)) return [];
  return fs.readFileSync(file, "utf-8").trim().split("\n").filter(Boolean).map((l) => JSON.parse(l));
}

beforeAll(() => {
  sb = new Sandbox();
  project = sb.makeProject("Vale");
  fixture = sb.path("windows.json");
  // The editor log the device-loss details are read from.
  fs.mkdirSync(path.join(path.dirname(project), "Saved", "Logs"), { recursive: true });
  fs.writeFileSync(
    path.join(path.dirname(project), "Saved", "Logs", "Vale.log"),
    "LogInit: Display: Starting\nLogD3D12RHI: Error: GPU crash detected: DXGI_ERROR_DEVICE_REMOVED\nLogD3D12RHI: Error: DRED: Breadcrumb 4: BasePass\n",
  );
});

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("the in-process server", () => {
  it("refuses with what the dialog means, and serves list_dialogs and respond_to_dialog from outside the editor", async () => {
    covers("server.native-dialogs");
    fs.writeFileSync(fixture, "[]");
    const bridge = await FakeBridge.start({ projectDir: path.dirname(project) });
    const server = await McpServer.start(sb, {
      projects: [project],
      strategy: "full",
      port: null,
      env: { UE_MCP_NATIVE_DIALOG_FIXTURE: fixture, UE_MCP_DIALOG_MODE: "auto" },
    });
    try {
      // The editor answers while nothing is up.
      await waitFor(async () => !(await server.call("project", { action: "get_status" })).isError, 20_000, "the server to reach the fake editor");

      fs.writeFileSync(fixture, JSON.stringify([gpuBox(project)]));
      const sent = (method: string) => bridge.calls.filter((c) => c.method === method).length;
      const refused = (await waitFor(async () => {
        const r = json<Record<string, unknown>>(await server.call("level", { action: "get_outliner", limit: 1 }));
        return r.nativeDialog === true ? r : null;
      }, 20_000, "the native dialog to be seen"))!;
      expect(refused).toMatchObject({ dialogBlocking: true, dialogKind: "gpu-crash", dialogTitle: "Error", buttons: ["OK"], dialogMode: "auto" });
      expect(String(refused.dialogSummary)).toMatch(/GPU device was lost/);
      expect(refused.dialogDetails).toEqual(expect.arrayContaining([expect.stringMatching(/DXGI_ERROR_DEVICE_REMOVED/)]));
      expect(sent("get_outliner")).toBe(0);

      // Nothing reaches the editor while the box is up, not even the dialog reads.
      const probes = sent("list_dialogs");
      const listed = json<{ dialogs: Array<Record<string, unknown>> }>(await server.call("editor", { action: "list_dialogs" }));
      expect(listed.dialogs[0]).toMatchObject({ title: "Error", buttons: ["OK"], detectedBy: "nativeWindow", native: { hwnd: "4242", pid: 4321, kind: "gpu-crash" } });
      expect(sent("list_dialogs")).toBe(probes);

      const pressed = json<Record<string, unknown>>(await server.call("editor", { action: "respond_to_dialog", buttonLabel: "OK" }));
      expect(pressed).toMatchObject({ success: true, native: true, pressed: "OK" });
      expect(presses()).toEqual([{ hwnd: "4242", pid: 4321, button: "OK" }]);
      expect(sent("respond_to_dialog")).toBe(0);
    } finally {
      await server.close();
      await bridge.stop();
      fs.rmSync(`${fixture}.presses.jsonl`, { force: true });
    }
  }, 120_000);

  it("never counts a dialog of another project's editor", async () => {
    fs.writeFileSync(fixture, JSON.stringify([gpuBox(path.join(sb.root, "Other", "Other.uproject"))]));
    const server = await McpServer.start(sb, { projects: [project], strategy: "full", env: { UE_MCP_NATIVE_DIALOG_FIXTURE: fixture } });
    try {
      await new Promise((r) => setTimeout(r, 1500));
      const r = json<Record<string, unknown>>(await server.call("project", { action: "get_info" }));
      expect(r.nativeDialog).toBeUndefined();
      expect(r.dialogBlocking).toBeUndefined();
    } finally {
      await server.close();
    }
  }, 60_000);
});

describe("the daemon", () => {
  let d: Discovery;

  beforeAll(async () => {
    fs.writeFileSync(fixture, "[]");
    d = await startDaemonCli(sb, project, { UE_MCP_NATIVE_DIALOG_FIXTURE: fixture });
  }, 60_000);

  it("reports the dialog on its events and its route, and presses it by window handle", async () => {
    covers("daemon.native-dialogs");
    const health = (await call(d, "GET", "/v1/health")).body as { features: string[]; lastEventId: number };
    expect(health.features).toContain("dialogs.native");

    const stream = readSse(
      `${base(d)}/v1/events`,
      { ...bearer(d), "last-event-id": String(health.lastEventId) },
      (es) => es.some((e) => e.type === "dialog.closed"),
      45_000,
    );

    fs.writeFileSync(fixture, JSON.stringify([gpuBox(project, "5151")]));
    const listed = (await waitFor(async () => {
      const r = (await call(d, "GET", "/v1/dialogs/native")).body as { dialogs: Array<Record<string, unknown>> };
      return r.dialogs.length > 0 ? r.dialogs : null;
    }, 20_000, "the route to list the dialog"))!;
    expect(listed[0]).toMatchObject({ editor: "Vale", hwnd: "5151", pid: 4321, title: "Error", buttons: ["OK"], kind: "gpu-crash", hung: false });
    expect(listed[0].details).toEqual(expect.arrayContaining([expect.stringMatching(/BasePass/)]));

    const wrong = await call(d, "POST", "/v1/dialogs/native/press", { hwnd: "5151", button: "Cancel" });
    expect(wrong.status).toBe(409);
    expect(String((wrong.body as { error: string }).error)).toMatch(/no button "Cancel"/);
    const missing = await call(d, "POST", "/v1/dialogs/native/press", { button: "OK" });
    expect(missing.status).toBe(400);

    const pressed = await call(d, "POST", "/v1/dialogs/native/press", { hwnd: "5151", pid: 4321, button: "OK" });
    expect(pressed.status).toBe(200);
    expect(pressed.body).toMatchObject({ pressed: true, closed: true, button: "OK" });
    expect(presses()).toEqual([{ hwnd: "5151", pid: 4321, button: "OK" }]);

    const events = (await stream).filter((e) => e.type.startsWith("dialog."));
    expect(events.map((e) => e.type)).toEqual(["dialog.opened", "dialog.answered", "dialog.closed"]);
    expect(events[0].data).toMatchObject({ editor: "Vale", title: "Error", buttons: ["OK"], native: { hwnd: "5151", kind: "gpu-crash" } });
    expect(events[1].data).toMatchObject({ button: "OK", via: "person" });
    expect((await call(d, "GET", "/v1/dialogs/native")).body).toEqual({ dialogs: [] });
  }, 90_000);
});
