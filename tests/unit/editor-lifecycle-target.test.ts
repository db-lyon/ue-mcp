/**
 * Stopping an editor must reach the editor that was addressed and no other
 * (#817, #819).
 *
 * A port is an address, not an identity: a collision walk, a pinned port or a
 * stale lockfile can all put another project's editor at the address this one
 * resolved to. These tests stand up a fake bridge that answers the identity
 * probe with a project of its own choosing and assert what stopEditor does
 * with each answer, including whether the quit request is sent at all.
 */
import { once } from "node:events";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { AddressInfo } from "node:net";
import { WebSocketServer } from "ws";
import { afterEach, describe, expect, it } from "vitest";
import { stopEditor } from "../../src/editor-control.js";

interface FakeBridge {
  port: number;
  /** Python bodies the fake editor was asked to run, in order. */
  received: string[];
  close: () => Promise<void>;
}

/**
 * A bridge that reports `reportedProjectDir` as the project it has open, and
 * shuts itself down when asked to quit (which is what a real editor exiting
 * looks like from outside).
 */
async function fakeEditorBridge(reportedProjectDir: string | null): Promise<FakeBridge> {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  const received: string[] = [];
  let closed = false;

  const close = async (): Promise<void> => {
    if (closed) return;
    closed = true;
    for (const client of server.clients) client.terminate();
    await new Promise<void>((resolve) => server.close(() => resolve()));
  };

  server.on("connection", (socket) => {
    socket.on("message", (data) => {
      const msg = JSON.parse(data.toString()) as { id: string; params?: { code?: string; resultVariable?: string } };
      const code = msg.params?.code ?? "";
      received.push(code);
      if (msg.params?.resultVariable) {
        if (reportedProjectDir === null) {
          socket.send(JSON.stringify({ id: msg.id, error: { code: 1, message: "Python scripting is not available" } }));
          return;
        }
        socket.send(JSON.stringify({
          id: msg.id,
          result: { success: true, result: `'${reportedProjectDir}'`, resultVariableResolved: true },
        }));
        return;
      }
      // A quit request: answer, then go quiet like an editor that exited.
      socket.send(JSON.stringify({ id: msg.id, result: { success: true } }));
      void close();
    });
  });

  await once(server, "listening");
  return { port: (server.address() as AddressInfo).port, received, close };
}

const cleanups: Array<() => Promise<void>> = [];
afterEach(async () => {
  for (const c of cleanups.splice(0)) await c();
});

function makeProjectDir(name: string): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `ue-mcp-${name}-`));
  fs.writeFileSync(path.join(dir, `${name}.uproject`), "{}", "utf-8");
  cleanups.push(async () => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

describe("stopEditor targeting", () => {
  it("refuses when the editor on that port has another project open", async () => {
    const mine = makeProjectDir("Mine");
    const theirs = makeProjectDir("Theirs");
    const bridge = await fakeEditorBridge(theirs);
    cleanups.push(bridge.close);

    const result = await stopEditor(false, mine, { port: bridge.port });

    expect(result.success).toBe(false);
    expect(result.message).toMatch(/Refusing to stop/);
    expect(result.message).toContain(theirs);
    // The decisive assertion: no quit request ever reached that editor.
    expect(bridge.received.some((code) => code.includes("quit_editor"))).toBe(false);
  });

  it("quits the editor that reports the addressed project", async () => {
    const mine = makeProjectDir("Mine");
    const bridge = await fakeEditorBridge(mine);
    cleanups.push(bridge.close);

    const result = await stopEditor(false, mine, { port: bridge.port });

    expect(result.success).toBe(true);
    expect(bridge.received.some((code) => code.includes("quit_editor"))).toBe(true);
  });

  it("tolerates a bridge that cannot answer the probe while one editor is registered", async () => {
    const mine = makeProjectDir("Mine");
    const bridge = await fakeEditorBridge(null);
    cleanups.push(bridge.close);

    const result = await stopEditor(false, mine, { port: bridge.port });

    expect(result.success).toBe(true);
    expect(bridge.received.some((code) => code.includes("quit_editor"))).toBe(true);
  });

  it("refuses an unidentifiable editor once more than one is registered", async () => {
    const mine = makeProjectDir("Mine");
    const bridge = await fakeEditorBridge(null);
    cleanups.push(bridge.close);

    const result = await stopEditor(false, mine, { port: bridge.port, strict: true });

    expect(result.success).toBe(false);
    expect(result.message).toMatch(/could not confirm which project/);
    expect(bridge.received.some((code) => code.includes("quit_editor"))).toBe(false);
  });

  it("matches project directories that differ only in slash style or case", async () => {
    const mine = makeProjectDir("Mine");
    const reported = mine.replace(/\\/g, "/").toUpperCase();
    const bridge = await fakeEditorBridge(reported);
    cleanups.push(bridge.close);

    const result = await stopEditor(false, mine, { port: bridge.port, strict: true });

    expect(result.success).toBe(true);
  });
});
