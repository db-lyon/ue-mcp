// @live-editor interactive
/**
 * A native Win32 message box in a real editor, seen and answered from outside it.
 *
 * The box is raised the way the RHI raises its device-lost box: MessageBoxW
 * on the game thread, from Python, so the bridge cannot answer while it is up.
 * The editor kind is interactive because -unattended suppresses message boxes.
 *
 * Asserted: a call already waiting on the editor is told at once rather than
 * timing out, the window scan lists the box with its title, text and buttons
 * read from its child controls, every other call is refused naming it, and
 * respond_to_dialog presses OK on that window by handle, which closes it and
 * lets the editor run again.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson, type LiveCallResult } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();
let server: LiveServer;

const TITLE = `UeMcpNativeProbe_${process.pid}`;
const TEXT = "UE-MCP live test: a native message box raised on the game thread.";

beforeAll(async () => {
  server = await LiveServer.start({
    projects: [target.uproject],
    env: { UE_MCP_DIALOG_MODE: "auto" },
  });
}, 240_000);

afterAll(async () => {
  try {
    // Never leave the box up: answer it if a failed assertion left it there.
    const listed = resultJson<any>(await server.call("editor", { action: "list_dialogs" }));
    if (listed?.dialogs?.[0]?.native) await server.call("editor", { action: "respond_to_dialog", buttonLabel: "Cancel" });
  } catch {
    // The editor may be gone; nothing to answer then.
  }
  await server?.close();
  closeLiveBridges();
}, 120_000);

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** execute_python through the server's gate: rule out whatever it names, then run. */
async function runPython(code: string, taskSummary: string): Promise<LiveCallResult> {
  const first = await server.call("editor", { action: "execute_python", code, taskSummary });
  const text = first.blocks.join("\n");
  const m = /\{"ruledOut":\[.*?\]\}/.exec(text);
  if (!m) return first;
  const template = (JSON.parse(m[0]) as { ruledOut: Array<{ action: string; reason: string }> }).ruledOut;
  const ruledOut = template.map((r) => ({ action: r.action, reason: "It does not raise a native Win32 message box, which this test needs." }));
  return server.call("editor", { action: "execute_python", code, taskSummary, ruledOut, timeoutMs: 120_000 });
}

async function nativeListed(timeoutMs = 30_000): Promise<any> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const listed = resultJson<any>(await server.call("editor", { action: "list_dialogs" }));
    const d = listed?.dialogs?.[0];
    if (d?.native && d.title === TITLE) return d;
    if (Date.now() > deadline) throw new Error(`no native dialog listed: ${JSON.stringify(listed)}`);
    await sleep(250);
  }
}

describe("a native message box in the editor", () => {
  it("is seen with its buttons, refuses calls, tells a waiting call at once, and is closed by pressing OK", async () => {
    const before = resultJson<any>(await server.call("editor", { action: "list_dialogs" }));
    expect(before.dialogs?.some((d: any) => d.native)).toBeFalsy();

    // MB_OKCANCEL | MB_ICONERROR, after a moment, so the call is already waiting on the editor when the box goes up.
    const code = [
      "import ctypes, time",
      "time.sleep(2)",
      `ctypes.windll.user32.MessageBoxW(None, ${JSON.stringify(TEXT)}, ${JSON.stringify(TITLE)}, 0x1 | 0x10)`,
    ].join("\n");
    const startedAt = Date.now();
    const pending = runPython(code, "raise a native Win32 message box on the game thread for the native dialog live test");

    const dialog = await nativeListed();
    expect(dialog).toMatchObject({ title: TITLE, buttons: ["OK", "Cancel"], detectedBy: "nativeWindow", blocksGameThread: true });
    expect(dialog.message).toContain(TEXT);
    expect(dialog.native).toMatchObject({ kind: "message" });
    expect(dialog.native.pid).toBeGreaterThan(0);
    expect(dialog.native.hwnd).toMatch(/^\d+$/);

    // The call that was waiting is told now, not at its timeout.
    const told = resultJson<any>(await pending);
    expect(told).toMatchObject({ dialogBlocking: true, nativeDialog: true, dialogTitle: TITLE });
    expect(Date.now() - startedAt).toBeLessThan(60_000);

    // Anything else is refused naming the box, without reaching the editor.
    const refused = resultJson<any>(await server.call("asset", { action: "list", directory: "/Game", limit: 1 }));
    expect(refused).toMatchObject({ dialogBlocking: true, nativeDialog: true, dialogTitle: TITLE, buttons: ["OK", "Cancel"] });

    const pressed = resultJson<any>(await server.call("editor", { action: "respond_to_dialog", buttonLabel: "OK" }));
    expect(pressed).toMatchObject({ success: true, native: true, pressed: "OK", closed: true });

    const after = resultJson<any>(await server.call("editor", { action: "list_dialogs" }));
    expect(after.dialogs?.some((d: any) => d.native)).toBeFalsy();
    // The editor runs again.
    const listed = resultJson<any>(await server.call("asset", { action: "list", directory: "/Game", limit: 1 }));
    expect(listed.dialogBlocking).toBeUndefined();
  }, 240_000);
});
