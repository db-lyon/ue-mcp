/**
 * Consent before a session takes the shared editor away (#2075), through the
 * real daemon: several MCP sessions on one fake editor, an external
 * participant driven over HTTP, people deciding through the routes and the
 * CLI, and a lone session asked about its own subagents.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, Sandbox, runCli, stopSandboxDaemons, waitFor } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
let project: string;

beforeAll(async () => {
  sb = new Sandbox();
  project = sb.makeProject("Consent", { engine: "5.9", content: true });
  bridge = await FakeBridge.start({ projectDir: path.dirname(project) });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

const editor = (server: McpServer, method: string, args: Record<string, unknown> = {}) =>
  server.call("tools", { action: "call", category: "editor", method, args });
const hotReload = (server: McpServer, args: Record<string, unknown> = {}) => editor(server, "hot_reload", args);
const sessions = async () => (await call(d, "GET", "/v1/sessions")).body.sessions as Array<{ id: string }>;
const requestIdOf = (text: string): string => /request (r-[0-9a-f]+)/.exec(text)?.[1] ?? "";
const hotReloads = () => bridge.calls.filter((c) => c.method === "hot_reload");
const open = (n: number) => Promise.all(Array.from({ length: n }, () => McpServer.start(sb, { projects: [project], mode: "daemon", port: null })));
const closeAll = (servers: McpServer[]) => Promise.all(servers.map((s) => s.client.close()));
const waitSessions = (n: number) => waitFor(async () => (await sessions()).length === n, 10_000, `${n} sessions to be listed`);

describe("a lone session", () => {
  it("is asked about its own subagents once, then goes straight through", async () => {
    covers("daemon.editor-consent");
    const [a] = await open(1);
    try {
      await waitSessions(1);
      const before = hotReloads().length;
      const asked = await hotReload(a);
      expect(asked.isError).toBe(true);
      expect(asked.text).toMatch(/^Error \[EDITOR_IN_USE\]: Not run yet: request r-[0-9a-f]+ .* own subagents/);
      expect(asked.text).toContain('subagents="paused"');
      expect(hotReloads().length).toBe(before);

      const ran = await hotReload(a, { subagents: "none" });
      expect(ran.text).not.toContain("EDITOR_IN_USE");
      expect(hotReloads().length, ran.text).toBe(before + 1);
      // The attestation is the gate's, never sent to the editor.
      expect(hotReloads().at(-1)!.params).not.toHaveProperty("subagents");
    } finally {
      await closeAll([a]);
    }
  });
});

describe("several sessions", () => {
  it("waits on an active session, tells it in its next response, and follows its block, wait and go", async () => {
    covers("daemon.editor-consent");
    const [a, b] = await open(2);
    try {
      await waitSessions(2);
      await editor(b, "disruption_status");
      const before = hotReloads().length;

      const pending = await hotReload(a, { subagents: "paused" });
      expect(pending.text).toContain("EDITOR_IN_USE");
      expect(pending.text).toContain("waiting on the other sessions");
      expect(pending.text).toContain("no answer yet");
      const id = requestIdOf(pending.text);
      expect(id).not.toBe("");

      // B learns of it on its next call, once.
      const told = await editor(b, "disruption_status", { requestId: id });
      expect(told.blocks.at(-1)).toContain(`[EDITOR DISRUPTION REQUEST ${id}]`);
      expect((await editor(b, "disruption_status")).text).not.toContain("EDITOR DISRUPTION REQUEST");

      // The requester cannot answer its own request.
      const own = await editor(a, "disruption_vote", { requestId: id, vote: "go" });
      expect(own.isError).toBe(true);
      expect(own.text).toContain("own");

      await editor(b, "disruption_vote", { requestId: id, vote: "block", reason: "sensitive operations underway" });
      const blocked = await hotReload(a);
      expect(blocked.text).toContain("blocked");
      expect(blocked.text).toContain("sensitive operations underway");

      await editor(b, "disruption_vote", { requestId: id, vote: "wait_until_released", reason: "pausing my subagents" });
      expect((await hotReload(a)).text).toContain("pausing my subagents");
      expect(hotReloads().length).toBe(before);

      // Released: the wait becomes a go, and the same request now runs.
      await editor(b, "release");
      const ran = await hotReload(a);
      expect(ran.text).not.toContain("EDITOR_IN_USE");
      expect(hotReloads().length).toBe(before + 1);
    } finally {
      await closeAll([a, b]);
    }
  });

  it("lets a person approve a request a hold keeps waiting, from the CLI", async () => {
    covers("daemon.editor-consent");
    covers("cli.editor");
    const [a, b] = await open(2);
    try {
      await waitSessions(2);
      await editor(b, "hold", { reason: "unsaved level edits" });
      const pending = await hotReload(a, { subagents: "none" });
      expect(pending.text).toContain("unsaved level edits");
      const id = requestIdOf(pending.text);

      const listed = await runCli(sb, ["editor", "requests", "--project", project, "--json"]);
      expect(listed.code, listed.stderr).toBe(0);
      expect((JSON.parse(listed.stdout) as { requests: Array<{ id: string; state: string }> }).requests).toContainEqual(expect.objectContaining({ id, state: "pending" }));

      const approved = await runCli(sb, ["editor", "approve", id, "--project", project]);
      expect(approved.code, approved.stdout + approved.stderr).toBe(0);
      expect(approved.stdout).toContain("Approved");

      const before = hotReloads().length;
      expect((await hotReload(a)).text).not.toContain("EDITOR_IN_USE");
      expect(hotReloads().length).toBe(before + 1);
    } finally {
      await closeAll([a, b]);
    }
  });

  it("drops a session's hold when its connection ends", async () => {
    covers("daemon.editor-consent");
    const [a, b] = await open(2);
    try {
      await waitSessions(2);
      await editor(b, "hold", { reason: "mid-save" });
      expect((await hotReload(a, { subagents: "none" })).text).toContain("mid-save");
      await b.client.close();
      await waitSessions(1);
      expect((await hotReload(a)).text).not.toContain("EDITOR_IN_USE");
    } finally {
      await closeAll([a]);
    }
  });
});

describe("an external runtime and people over HTTP", () => {
  it("registers a participant, reports its work, answers for it, and holds the editor routes until it agrees", async () => {
    covers("daemon.editor-consent");
    const studio = "studio-agent-1";
    const put = await call(d, "PUT", `/v1/disruptions/participants/${studio}`, { label: "Studio agent", busy: ["editing BP_Door"] });
    expect(put.status).toBe(200);
    expect(put.body).toMatchObject({ id: studio, inflight: ["editing BP_Door"] });

    const refused = await call(d, "POST", "/v1/editor/stop", { requester: "Rebuild button" });
    expect(refused.status).toBe(409);
    expect(refused.body.request).toMatchObject({ op: "stop", state: "pending" });
    expect(refused.body.error).toContain("force");
    const id = refused.body.request.id as string;
    expect(refused.body.request.answers).toContainEqual(expect.objectContaining({ session: studio, answer: "wait_until_released" }));

    // The participant holds, then goes idle and answers go.
    expect((await call(d, "POST", `/v1/disruptions/participants/${studio}/hold`, { reason: "saving" })).status).toBe(200);
    await call(d, "PUT", `/v1/disruptions/participants/${studio}`, { busy: [] });
    expect((await call(d, "GET", "/v1/disruptions")).body.requests.find((r: { id: string }) => r.id === id).answers[0].reason).toContain("saving");
    await call(d, "POST", `/v1/disruptions/participants/${studio}/release`);
    const voted = await call(d, "POST", `/v1/disruptions/${id}/vote`, { participant: studio, vote: "go" });
    expect(voted.status).toBe(200);
    expect(voted.body.state).toBe("approved");

    // A person's request needs no subagent attestation; an external agent's does.
    const agentAsks = await call(d, "POST", "/v1/disruptions", { op: "restart", participant: studio });
    expect(agentAsks.body.state).toBe("confirm");
    const confirmed = await call(d, "POST", `/v1/disruptions/${agentAsks.body.id}/confirm`, { participant: studio, subagents: "none" });
    expect(confirmed.body.state).toBe("approved");
    expect((await call(d, "POST", `/v1/disruptions/${agentAsks.body.id}/deny`, { by: "test" })).body.state).toBe("denied");

    expect((await call(d, "POST", "/v1/editor/stop", { requestId: "r-nothere" })).status).toBe(404);
    expect((await call(d, "POST", `/v1/disruptions/${id}/vote`, { participant: studio, vote: "maybe" })).status).toBe(400);
    expect((await call(d, "DELETE", `/v1/disruptions/participants/${studio}`)).status).toBe(200);
    expect((await call(d, "GET", "/v1/health")).body.features).toContain("editor.consent");
  });
});
