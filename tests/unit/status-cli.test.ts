/**
 * `ue-mcp status --json` is what the UI and installer read to decide what is
 * missing, so it must describe an install without changing it.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { apply, plan } from "../../src/cli/init-core.js";
import { collectStatus } from "../../src/cli/status.js";
import { packageVersion } from "../../src/core/package-root.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;
let savedEnv: NodeJS.ProcessEnv;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-status-");
  savedEnv = { ...process.env };
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
  process.env.UE_MCP_USER_STATE = path.join(fixture.root, "state.json");
});

afterEach(() => {
  for (const k of Object.keys(process.env)) if (!(k in savedEnv)) delete process.env[k];
  Object.assign(process.env, savedEnv);
  fixture.cleanup();
});

describe("status", () => {
  it("reports a bare project as not installed and writes nothing", () => {
    const uproject = fixture.makeProject("Bare");
    const before = fs.readdirSync(path.dirname(uproject));
    const s = collectStatus(uproject, () => false);
    expect(s.plugin.deployed).toBe(false);
    expect(s.plugin.installKind).toBeNull();
    expect(s.config.exists).toBe(false);
    expect(s.editor.running).toBe(false);
    expect(s.problems.map((p) => p.code)).toContain("bridge_not_deployed");
    expect(fs.readdirSync(path.dirname(uproject))).toEqual(before);
  });

  it("reports what init wrote: kind, marker and the pinned client", async () => {
    const uproject = fixture.makeProject("Done");
    const hooks = { platform: "linux" as const, env: {}, exists: () => true, run: () => "clang" };
    await apply(plan({ project: uproject, clients: ["claude-code"], pin: true, install: "source" }, hooks));

    const s = collectStatus(uproject, () => false);
    expect(s.plugin).toMatchObject({ deployed: true, installKind: "source", dirSource: "default" });
    expect(s.plugin.marker?.kind).toBe("source");
    expect(s.config.exists).toBe(true);
    expect(s.clients.find((c) => c.id === "claude-code")).toMatchObject({ configured: true, pinned: packageVersion() });
  });
});
