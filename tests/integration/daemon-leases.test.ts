/**
 * Asset leases through the daemon: two MCP clients on one editor, one holding
 * an asset, the other refused a change to it with the holder named, and the
 * lease gone the moment the holder's session ends.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
let project: string;

beforeAll(async () => {
  sb = new Sandbox();
  project = sb.makeProject("Leases", { engine: "5.9", content: true });
  bridge = await FakeBridge.start({ projectDir: path.dirname(project) });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

const addVariable = (server: McpServer, name: string) =>
  server.call("tools", { action: "call", category: "blueprint", method: "add_variable", args: { blueprintPath: "/Game/BP_Shared.BP_Shared", name, type: "float" } });
const leases = async () => (await call(d, "GET", "/v1/leases")).body as { count: number; leases: Array<{ path: string; holder: string; implicit: boolean }> };
const sessions = async () => (await call(d, "GET", "/v1/sessions")).body.sessions as Array<{ id: string }>;

describe("asset leases between two sessions on one daemon", () => {
  it("refuses the other session's change, naming the holder, until the holder's session ends", async () => {
    covers("daemon.leases");
    const one = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    const two = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    let oneOpen = true;
    try {
      await waitFor(async () => (await sessions()).length === 2, 10_000, "both sessions to be listed");
      const got = await one.call("tools", { action: "call", category: "project", method: "acquire_lease", args: { paths: ["/Game/BP_Shared"], ttlSeconds: 300 } });
      expect(got.isError, got.text).toBe(false);
      const holder = (JSON.parse(got.text) as { holder: { id: string } }).holder.id;
      expect((await sessions()).map((s) => s.id)).toContain(holder);

      const listed = await leases();
      expect(listed.count).toBe(1);
      expect(listed.leases[0]).toMatchObject({ path: "/Game/BP_Shared", holder, implicit: false });

      const before = bridge.calls.length;
      const refused = await addVariable(two, "Refused");
      expect(refused.isError).toBe(true);
      expect(refused.text).toMatch(/^Error \[ASSET_LEASED\]: Asset '\/Game\/BP_Shared' is leased by session '[^']+' \(/);
      expect(refused.text).toContain(holder);
      // Refused before it reached the editor.
      expect(bridge.calls.slice(before).some((c) => c.method === "add_variable")).toBe(false);

      // The holder's own change goes through.
      expect((await addVariable(one, "Mine")).text).not.toContain("ASSET_LEASED");

      await one.client.close();
      oneOpen = false;
      await waitFor(async () => (await leases()).count === 0, 10_000, "the holder's lease to go with its session");
      expect((await addVariable(two, "Now")).text).not.toContain("ASSET_LEASED");
    } finally {
      if (oneOpen) await one.client.close();
      await two.client.close();
    }
  });

  it("has a change take a lease for its session while two sessions share the editor", async () => {
    covers("daemon.leases");
    const one = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    const two = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    try {
      await waitFor(async () => (await sessions()).length === 2, 10_000, "both sessions to be listed");
      expect((await addVariable(one, "First")).text).not.toContain("ASSET_LEASED");
      const listed = await leases();
      expect(listed.leases).toEqual([expect.objectContaining({ path: "/Game/BP_Shared", implicit: true })]);
      const refused = await addVariable(two, "Second");
      expect(refused.text).toContain("taken when that session last changed it");
    } finally {
      await one.client.close();
      await two.client.close();
    }
    await waitFor(async () => (await leases()).count === 0, 10_000, "leases to go with the sessions");
  });
});
