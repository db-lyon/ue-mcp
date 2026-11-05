/**
 * ue-mcp.http turns on the flow HTTP surface whichever way the server runs.
 * Daemon mode is the default, so the daemon serves it too, and stops it when
 * it stops.
 */
import * as net from "node:net";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { McpServer, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";

const TOKEN = "integration-http-token";
let sb: Sandbox;
let port: number;

function freePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const s = net.createServer();
    s.once("error", reject);
    s.listen(0, "127.0.0.1", () => {
      const p = (s.address() as net.AddressInfo).port;
      s.close(() => resolve(p));
    });
  });
}

beforeAll(async () => {
  sb = new Sandbox("ue-mcp-http-daemon-");
  port = await freePort();
});
afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("the flow HTTP surface in daemon mode", () => {
  it("is served by the daemon behind its token, and goes when the daemon stops", async () => {
    covers("flow.http");
    const uproject = sb.makeProject("HttpDaemon", {
      config: [
        "ue-mcp:",
        "  version: 1",
        "  http:",
        "    enabled: true",
        `    port: ${port}`,
        "    host: 127.0.0.1",
        "flows:",
        "  info:",
        "    description: Reads the project.",
        "    steps:",
        '      "1":',
        "        task: project.get_info",
        "",
      ].join("\n"),
    });
    const server = await McpServer.start(sb, { projects: [uproject], mode: "daemon", env: { UE_MCP_HTTP_TOKEN: TOKEN } });
    try {
      // The shim relays; the daemon behind it owns the port.
      const health = await waitFor(async () => {
        const r = await fetch(`http://127.0.0.1:${port}/health`, { headers: { authorization: `Bearer ${TOKEN}` } });
        return r.ok ? ((await r.json()) as { ok: boolean; port: number }) : null;
      }, 30_000, "the flow HTTP server");
      expect(health).toMatchObject({ ok: true, port });

      const refused = await fetch(`http://127.0.0.1:${port}/flows`);
      expect(refused.status).toBe(401);

      const flows = (await (await fetch(`http://127.0.0.1:${port}/flows`, { headers: { authorization: `Bearer ${TOKEN}` } })).json()) as {
        flows: Array<{ name: string }>;
      };
      expect(flows.flows.map((f) => f.name)).toContain("info");
    } finally {
      await server.close();
    }
    await waitFor(async () => {
      try {
        await fetch(`http://127.0.0.1:${port}/health`);
        return false;
      } catch {
        return true;
      }
    }, 15_000, "the flow HTTP server to stop with the daemon");
  });
});
