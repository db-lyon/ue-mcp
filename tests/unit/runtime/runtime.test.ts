/**
 * The runtime owns the server-wide settings. Explicit ones win; anything left
 * out comes from the primary project's `ue-mcp:` block, where every existing
 * config keeps them.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { ProjectFixture } from "../../helpers/project-fixture.js";
import { UeMcpRuntime, openProject } from "../../../src/runtime/runtime.js";
import { resolveContextStrategy } from "../../../src/surface/context/lean-context.js";
import { resolveLockingConfig } from "../../../src/dispatch/locking.js";

const resolvers = { strategy: resolveContextStrategy, locking: resolveLockingConfig };

let fixture: ProjectFixture;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-runtime-");
  vi.stubEnv("UE_MCP_GLOBAL_CONFIG", path.join(fixture.root, "global.yml"));
  vi.stubEnv("UE_MCP_CONTEXT_STRATEGY", undefined);
  vi.stubEnv("UE_MCP_PORT", undefined);
});

afterEach(() => {
  vi.unstubAllEnvs();
  fixture.cleanup();
});

describe("UeMcpRuntime settings", () => {
  it("falls back to the primary project's block", () => {
    const settings = UeMcpRuntime.resolveSettings(
      { context: { strategy: "lean" }, http: { enabled: true, port: 7800 }, locking: { enabled: true, ttlSeconds: 60 } },
      undefined,
      resolvers,
    );
    expect(settings).toEqual({
      contextStrategy: "lean",
      http: { enabled: true, port: 7800, host: undefined },
      locking: { enabled: true, ttlSeconds: 60 },
    });
  });

  it("lets explicit settings win over the project's", () => {
    const settings = UeMcpRuntime.resolveSettings(
      { context: { strategy: "lean" }, locking: { enabled: true } },
      { context: { strategy: "full" }, locking: { enabled: false } },
      resolvers,
    );
    expect(settings.contextStrategy).toBe("full");
    expect(settings.locking.enabled).toBe(false);
    expect(settings.http.enabled).toBe(false);
  });

  it("reads them from the first project a server starts with", async () => {
    const uproject = fixture.makeProject("Alpha");
    fs.writeFileSync(
      path.join(path.dirname(uproject), "ue-mcp.yml"),
      "ue-mcp:\n  version: 1\n  context: { strategy: full }\n  locking: { enabled: true, ttlSeconds: 42 }\n",
    );
    const runtime = await UeMcpRuntime.start({ projectArgs: [uproject], packageVersion: "0.0.0" });
    try {
      expect(runtime.settings.contextStrategy).toBe("full");
      expect(runtime.settings.locking).toEqual({ enabled: true, ttlSeconds: 42 });
      expect(runtime.baseContext().openAssetLocks).toBeTypeOf("function");
      expect(runtime.loads.get(runtime.primary)?.registry).toBeDefined();
    } finally {
      for (const s of runtime.sessions.list()) s.dialogGuard?.stopWatching();
    }
  });
});

describe("openProject", () => {
  it("loads a project with its config snapshot", () => {
    const project = openProject(fixture.makeProject("Beta"));
    expect(project.projectName).toBe("Beta");
    expect(project.projectConfig).not.toBeNull();
  });
});
