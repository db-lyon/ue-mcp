/**
 * D1: an editor registered at RUNTIME gets its own dispatch surface, or it is
 * not dispatched to at all.
 *
 * `perSession` in index.ts was written exactly once, in the startup loop, and
 * project(add_editor) registers a session after that. So every per-session
 * lookup missed for the new editor and fell back to the FIRST project's load:
 *
 *   - its task registry, including that project's plugin tasks
 *   - its ue-mcp.yml, so flow(run, flowName=..., editor="B") read project A's
 *     flows and ran A's steps inside B's editor
 *   - project(get_status, editor="B") reported A's flows as B's
 *   - the dispatch union was built from A alone, so B's own `disable:` list was
 *     enforced nowhere
 *
 * The fix is a builder the server installs on the registry and add_editor
 * awaits, so an editor is never addressable before its own surface exists.
 * These cover the registry half of the contract and the tool half.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../../src/deployer.js", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../src/deployer.js")>();
  return { ...actual, attach: vi.fn(() => ({ action: "skipped" })), attachSummary: vi.fn(() => "stubbed") };
});

const { SessionRegistry } = await import("../../src/session.js");
const { projectTool } = await import("../../src/tools/project.js");

let root: string;

function makeProject(name: string): string {
  const dir = path.join(root, name);
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6" }), "utf-8");
  return uproject;
}

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-runtime-load-"));
  delete process.env.UE_MCP_PORT;
});

afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
  delete process.env.UE_MCP_PORT;
});

describe("SessionRegistry.prepare", () => {
  it("runs the server's surface builder for the session it is given", async () => {
    const registry = new SessionRegistry();
    const built: string[] = [];
    registry.prepareSession = async (session) => {
      built.push(session.name);
    };

    const session = registry.register({ projectPath: makeProject("Beta") });
    await registry.prepare(session);
    expect(built).toEqual(["Beta"]);
  });

  it("is a no-op for an embedder that installed no builder", async () => {
    const registry = new SessionRegistry();
    const session = registry.register({ projectPath: makeProject("Beta") });
    await expect(registry.prepare(session)).resolves.toBeUndefined();
  });

  it("propagates a build failure rather than swallowing it", async () => {
    const registry = new SessionRegistry();
    registry.prepareSession = async () => {
      throw new Error("plugin load blew up");
    };
    const session = registry.register({ projectPath: makeProject("Beta") });
    await expect(registry.prepare(session)).rejects.toThrow(/plugin load blew up/);
  });
});

describe("project(add_editor)", () => {
  function contextFor(registry: InstanceType<typeof SessionRegistry>) {
    const active = registry.active;
    return {
      bridge: active.guarded,
      project: active.project,
      session: active,
      sessions: registry,
      getFlows: () => [],
      getPlugins: () => [],
    };
  }

  it("builds the new editor's surface before it hands back a usable handle", async () => {
    const registry = new SessionRegistry();
    registry.register({ projectPath: makeProject("Alpha") });

    const order: string[] = [];
    let prepared: string | null = null;
    registry.prepareSession = async (session) => {
      order.push("prepare");
      // A real build awaits plugin loading and Epic enrichment.
      await new Promise((resolve) => setImmediate(resolve));
      prepared = session.name;
    };

    const result = (await projectTool.handler(contextFor(registry) as never, {
      action: "add_editor",
      projectPath: makeProject("Beta"),
    })) as Record<string, unknown>;
    order.push("returned");

    expect(result.editor).toBe("Beta");
    // Not merely started: finished, before the caller was told it can target it.
    expect(prepared).toBe("Beta");
    expect(order).toEqual(["prepare", "returned"]);
  });

  it("refuses the editor rather than letting it borrow another project's surface", async () => {
    const registry = new SessionRegistry();
    registry.register({ projectPath: makeProject("Alpha") });
    registry.prepareSession = async () => {
      throw new Error("its ue-mcp.yml names a plugin that will not load");
    };

    await expect(
      projectTool.handler(contextFor(registry) as never, {
        action: "add_editor",
        projectPath: makeProject("Beta"),
      }),
    ).rejects.toThrow(/could not build its tool surface/);
  });
});
