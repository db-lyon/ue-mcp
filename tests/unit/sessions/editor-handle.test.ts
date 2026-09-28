/**
 * An editor handle owns what belongs to one editor: its dialog guard, its
 * load, and its Python gate and workaround memory. None of it lives in a
 * module-level map keyed by the handle.
 */
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { ProjectFixture } from "../../helpers/project-fixture.js";
import { EditorHandle, EditorSession, SessionRegistry } from "../../../src/sessions/session.js";
import { SessionLoads } from "../../../src/sessions/session-load.js";
import { ensureGuard, existingGuard, forgetGuard } from "../../../src/editor/dialog-guard.js";
import { pushWorkaround, workaroundCount } from "../../../src/dispatch/workaround-tracker.js";

let fixture: ProjectFixture;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-editor-handle-");
  delete process.env.UE_MCP_PORT;
  process.env.UE_MCP_GLOBAL_CONFIG = path.join(fixture.root, "global.yml");
});

afterEach(() => {
  fixture.cleanup();
  delete process.env.UE_MCP_GLOBAL_CONFIG;
});

describe("EditorHandle", () => {
  it("is the class the public name EditorSession refers to", () => {
    expect(EditorSession).toBe(EditorHandle);
    const sessions = new SessionRegistry();
    expect(sessions.register({ projectPath: fixture.makeProject("Alpha") })).toBeInstanceOf(EditorHandle);
  });

  it("keeps its dialog guard on itself", async () => {
    const sessions = new SessionRegistry();
    const alpha = sessions.register({ projectPath: fixture.makeProject("Alpha") });
    const beta = sessions.register({ projectPath: fixture.makeProject("Beta") });
    const guard = await ensureGuard(alpha);
    try {
      expect(alpha.dialogGuard).toBe(guard);
      expect(existingGuard(alpha)).toBe(guard);
      expect(beta.dialogGuard).toBeUndefined();
    } finally {
      forgetGuard(alpha);
    }
    expect(alpha.dialogGuard).toBeUndefined();
  });

  it("keeps each editor's workarounds on its own handle", () => {
    const sessions = new SessionRegistry();
    const alpha = sessions.register({ projectPath: fixture.makeProject("Alpha") });
    const beta = sessions.register({ projectPath: fixture.makeProject("Beta") });
    pushWorkaround({ code: "alpha()", timestamp: "t" }, { session: alpha });
    expect(alpha.workarounds.map((w) => w.code)).toEqual(["alpha()"]);
    expect(workaroundCount({ session: beta })).toBe(0);
  });

  it("carries its own load, and only the set that built it reads it", async () => {
    const sessions = new SessionRegistry();
    const alpha = sessions.register({ projectPath: fixture.makeProject("Alpha") });
    const loads = new SessionLoads(sessions, alpha, "full", "0.0.0");
    const load = await loads.ensure(alpha);
    expect(alpha.load).toBe(load);
    expect(loads.get(alpha)).toBe(load);
    expect(new SessionLoads(sessions, alpha, "full", "0.0.0").get(alpha)).toBeUndefined();
  });
});
