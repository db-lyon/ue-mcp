/**
 * The upgrade notice, seeded through the version cache so nothing is
 * fetched: a cache checked moments ago naming a newer release is answered
 * from disk. Each client's first tool result carries the notice once, in
 * process and through the daemon, where one process serves every client.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { McpServer, PACKAGE_VERSION, Sandbox } from "./harness.js";

const NOTICE = "UE_MCP_UPGRADE_AVAILABLE";
/** Lets the check run, which the sandbox turns off for every other test. */
const CHECKING = { UE_MCP_DISABLE_UPDATE_CHECK: undefined };

let sb: Sandbox;
let project: string;

beforeAll(() => {
  sb = new Sandbox();
  project = sb.makeProject("Game");
  const cache = path.join(sb.home, ".ue-mcp", "version-check.json");
  fs.mkdirSync(path.dirname(cache), { recursive: true });
  fs.writeFileSync(cache, JSON.stringify({ checkedAt: Date.now(), latest: "99.0.0" }));
});

afterAll(() => sb.cleanup());

const info = (s: McpServer) => s.call("tools", { action: "call", category: "project", method: "get_info" });

/** The first tool result after startup, once the check has had a moment to read its cache. */
async function firstResult(s: McpServer) {
  await new Promise((r) => setTimeout(r, 300));
  return info(s);
}

function expectNotice(text: string): void {
  expect(text).toContain(NOTICE);
  expect(text).toContain(`installed: ${PACKAGE_VERSION}`);
  expect(text).toContain("latest:    99.0.0");
}

describe("upgrade notice", () => {
  it("leads the first tool result once, in process", async () => {
    covers("version-check.notice");
    const server = await McpServer.start(sb, { projects: [project], mode: "in-process", env: CHECKING });
    try {
      const first = await firstResult(server);
      expect(first.blocks[0]).toMatch(new RegExp(`^${NOTICE}`));
      expectNotice(first.text);
      expect(JSON.parse(first.blocks[1]).projectName).toBe("Game");
      expect((await info(server)).text).not.toContain(NOTICE);
      expect((await server.call("flow", { action: "list" })).text).not.toContain(NOTICE);
    } finally {
      await server.close();
    }
  }, 60_000);

  it("reaches every client of the daemon once each", async () => {
    covers("version-check.notice");
    const a = await McpServer.start(sb, { projects: [project], mode: "daemon", env: CHECKING });
    let b: McpServer | undefined;
    try {
      const first = await firstResult(a);
      expectNotice(first.text);
      expect((await info(a)).text).not.toContain(NOTICE);

      b = await McpServer.start(sb, { projects: [project], mode: "daemon", env: CHECKING });
      const second = await info(b);
      expectNotice(second.text);
      expect((await info(b)).text).not.toContain(NOTICE);
      expect((await info(a)).text).not.toContain(NOTICE);
    } finally {
      await b?.close();
      await a.close();
    }
  }, 90_000);

  it("says nothing when the cached latest is not newer", async () => {
    covers("version-check.notice");
    const cache = path.join(sb.home, ".ue-mcp", "version-check.json");
    fs.writeFileSync(cache, JSON.stringify({ checkedAt: Date.now(), latest: "0.0.1" }));
    const server = await McpServer.start(sb, { projects: [project], mode: "in-process", env: CHECKING });
    try {
      expect((await firstResult(server)).text).not.toContain(NOTICE);
    } finally {
      await server.close();
    }
  }, 60_000);
});
