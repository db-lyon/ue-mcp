/**
 * How a stdio server picks daemon or in process, spawned: from ue-mcp.yml,
 * from the variable, and the fallback when a daemon cannot start. Split from
 * daemon-cli.test.ts so the two run in parallel.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { McpServer, Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, readDiscovery } from "./daemon-fakes.js";

let sb: Sandbox;
beforeAll(() => {
  sb = new Sandbox();
});
afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("server mode", () => {
  const config = (mode: string) => `ue-mcp:\n  version: 1\n  server:\n    mode: ${mode}\n`;

  it("takes daemon or in-process from ue-mcp.yml when no variable names one", async () => {
    covers("server.mode.config");
    const viaDaemon = sb.makeProject("ModeDaemon", { config: config("daemon") });
    const inProcess = sb.makeProject("ModeInProcess", { config: config("in-process") });
    const env = { UE_MCP_SERVER_MODE: undefined, UE_MCP_LOG_LEVEL: "info" };
    const [a, b] = await Promise.all([
      McpServer.start(sb, { projects: [viaDaemon], env }),
      McpServer.start(sb, { projects: [inProcess], env }),
    ]);
    try {
      expect(await a.toolNames()).toEqual(["flow", "tools"]);
      expect(await b.toolNames()).toEqual(["flow", "tools"]);
      const d = readDiscovery(sb, viaDaemon)!;
      expect(d).not.toBeNull();
      expect((await call(d, "GET", "/v1/health")).body.clients).toBe(1);
      expect(readDiscovery(sb, inProcess)).toBeNull();
      expect(b.log).toContain("serving in process (server.mode in ue-mcp.yml)");

      // The variable wins over the file.
      const overridden = await McpServer.start(sb, { projects: [sb.makeProject("ModeOverride", { config: config("daemon") })], mode: "in-process" });
      try {
        expect(await overridden.toolNames()).toEqual(["flow", "tools"]);
        expect(readDiscovery(sb, path.join(sb.root, "ModeOverride", "ModeOverride.uproject"))).toBeNull();
      } finally {
        await overridden.client.close();
      }
    } finally {
      await a.client.close();
      await b.client.close();
    }
  });

  it("serves in process, promptly, when the daemon cannot start", async () => {
    covers("server.mode.fallback");
    const project = sb.makeProject("NoDaemon");
    // A daemon loads extensions from here and dies on a file; the in-process server never reads it.
    const blocker = sb.path("extensions-is-a-file");
    fs.writeFileSync(blocker, "not a directory");
    const t0 = Date.now();
    const server = await McpServer.start(sb, { projects: [project], mode: "daemon", env: { UE_MCP_EXTENSIONS_DIR: blocker, UE_MCP_LOG_LEVEL: "warn" } });
    try {
      const r = await server.call("tools", { action: "call", category: "project", method: "get_status" });
      expect(r.isError, r.text).toBe(false);
      expect(Date.now() - t0).toBeLessThan(30_000);
      expect(server.log).toMatch(/exited before it started/);
      expect(server.log).toContain("daemon unavailable; serving in process");
      expect(readDiscovery(sb, project)).toBeNull();
    } finally {
      await server.client.close();
    }
  });

  it("serves in process when the discovery directory cannot be created", async () => {
    covers("server.mode.fallback");
    const project = sb.makeProject("NoDaemonDir");
    const file = sb.path("daemons-is-a-file");
    fs.writeFileSync(file, "not a directory");
    const server = await McpServer.start(sb, { projects: [project], mode: "daemon", env: { UE_MCP_DAEMON_DIR: path.join(file, "daemons"), UE_MCP_LOG_LEVEL: "warn" } });
    try {
      expect(await server.toolNames()).toEqual(["flow", "tools"]);
      const r = await server.call("tools", { action: "call", category: "project", method: "get_status" });
      expect(r.isError, r.text).toBe(false);
      expect(server.log).toContain("daemon unavailable; serving in process");
    } finally {
      await server.client.close();
    }
  });
});
