/**
 * Read-only session mode through the daemon (#1484): two MCP clients on one
 * editor, one switched to read-only over HTTP, the other still writable.
 */
import * as path from "node:path";
import WebSocket from "ws";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { base, bearer, call, readSse, startDaemonCli, type Discovery } from "./daemon-fakes.js";

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
let project: string;

beforeAll(async () => {
  sb = new Sandbox();
  project = sb.makeProject("Sessions");
  bridge = await FakeBridge.start({ projectDir: path.dirname(project) });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

type Session = { id: string; client: string; access: string };
const sessions = async (): Promise<Session[]> => (await call(d, "GET", "/v1/sessions")).body.sessions as Session[];
const command = (server: McpServer, cmd: string) =>
  server.call("tools", { action: "call", category: "editor", method: "execute_command", args: { command: cmd } });

describe("session access mode", () => {
  it("switches one session to read-only and leaves another on the same editor writable", async () => {
    covers("daemon.sessions-access");
    expect((await call(d, "GET", "/v1/health")).body.features).toContain("sessions.access");

    const one = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    const two = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    try {
      await waitFor(async () => (await sessions()).length === 2, 10_000, "both sessions to be listed");
      const listed = await sessions();
      expect(listed.every((s) => s.access === "read-write")).toBe(true);
      // The list does not say which socket is which, so exactly one of the two calls must be refused.
      const [first, second] = listed;

      const before = (await call(d, "GET", "/v1/health")).body.lastEventId as number;
      const set = await call(d, "PUT", `/v1/sessions/${first.id}/access`, { mode: "read-only" });
      expect(set.status, set.text).toBe(200);
      expect(set.body).toEqual({ id: first.id, access: "read-only" });
      expect(await call(d, "GET", `/v1/sessions/${first.id}/access`)).toMatchObject({ status: 200, body: { id: first.id, access: "read-only" } });
      expect(await call(d, "GET", `/v1/sessions/${second.id}/access`)).toMatchObject({ status: 200, body: { access: "read-write" } });
      const changed = await readSse(`${base(d)}/v1/events`, { ...bearer(d), "last-event-id": String(before) }, (es) => es.some((e) => e.type === "session.access"));
      expect(changed.find((e) => e.type === "session.access")!.data).toMatchObject({ session: first.id, access: "read-only" });

      const results = [await command(one, "stat one"), await command(two, "stat two")];
      const refused = results.filter((r) => r.isError && /read-only session mode refuses editor\(execute_command\)/.test(r.text));
      const ran = results.filter((r) => !r.text.includes("read-only session mode"));
      expect(refused).toHaveLength(1);
      expect(ran).toHaveLength(1);
      const sent = bridge.calls.filter((c) => c.method === "execute_command").map((c) => c.params.command);
      expect(sent).toHaveLength(1);
      expect(["stat one", "stat two"]).toContain(sent[0]);

      // A read still runs in the read-only session.
      const reads = await Promise.all([one, two].map((s) => s.call("tools", { action: "call", category: "project", method: "get_status" })));
      expect(reads.every((r) => !r.text.includes("read-only session mode"))).toBe(true);

      // Back to read-write: both run.
      expect((await call(d, "PUT", `/v1/sessions/${first.id}/access`, { mode: "read-write" })).body.access).toBe("read-write");
      expect((await command(one, "stat again")).text).not.toContain("read-only session mode");
      expect((await command(two, "stat again")).text).not.toContain("read-only session mode");
    } finally {
      // The client alone: McpServer.close also stops the sandbox's daemons.
      await one.client.close();
      await two.client.close();
    }
    await waitFor(async () => (await sessions()).length === 0, 10_000, "closed sessions to drop off the list");
  });

  it("refuses a mode it does not know and a session that is not open", async () => {
    covers("daemon.sessions-access");
    expect((await call(d, "PUT", "/v1/sessions/nope/access", { mode: "read-only" })).status).toBe(404);
    const ws = new WebSocket(`ws://127.0.0.1:${d.port}/v1/mcp`, { headers: { ...bearer(d), "x-ue-mcp-access": "read-only" } });
    const id = await new Promise<string>((resolve, reject) => {
      ws.on("upgrade", (res) => resolve(String(res.headers["x-ue-mcp-session"])));
      ws.on("error", reject);
    });
    try {
      // A socket can open read-only, and is told its id on the upgrade.
      await waitFor(async () => (await sessions()).some((s) => s.id === id), 10_000, "the socket's session to be listed");
      expect((await call(d, "GET", `/v1/sessions/${id}/access`)).body.access).toBe("read-only");
      const bad = await call(d, "PUT", `/v1/sessions/${id}/access`, { mode: "readonly" });
      expect(bad.status).toBe(400);
      expect(bad.body.error).toContain("read-write, read-only");
    } finally {
      ws.close();
    }
  });
});
