/**
 * Hook session resolution (#817, plan 6.3).
 *
 * The behaviour that must not change: a hook that resolves to no ue-mcp
 * project stays silent. What changed is which directory it asks about.
 */
import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { hookProjectDir, feedbackDisabledForDir } from "../../src/hook-session.js";

let root: string;

function project(name: string, yml?: string): string {
  const dir = path.join(root, name);
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  fs.writeFileSync(path.join(dir, `${name}.uproject`), "{}", "utf-8");
  if (yml !== undefined) fs.writeFileSync(path.join(dir, "ue-mcp.yml"), yml, "utf-8");
  return dir;
}

beforeEach(() => {
  root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-hook-")));
});

afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
});

describe("hookProjectDir", () => {
  it("prefers a project path the call itself carried", async () => {
    const alpha = project("Alpha");
    const resolved = await hookProjectDir(
      { tool_input: { action: "set_project", projectPath: path.join(alpha, "Alpha.uproject") } },
      root,
    );
    expect(resolved.dir).toBe(alpha);
    expect(resolved.source).toBe("payload project path");
  });

  it("resolves an editor the call addressed by name", async () => {
    const beta = project("Beta");
    const resolved = await hookProjectDir(
      { tool_input: { action: "execute_python", editor: "Beta" } },
      root,
      async () => path.join(beta, "Beta.uproject"),
    );
    expect(resolved.dir).toBe(beta);
    expect(resolved.source).toContain("Beta");
  });

  it("falls through to cwd when the named editor is unknown", async () => {
    const resolved = await hookProjectDir(
      { tool_input: { editor: "nosuch" }, cwd: root },
      root,
      async () => {
        throw new Error("not a registered editor");
      },
    );
    expect(resolved.dir).toBe(root);
    expect(resolved.source).toBe("payload cwd");
  });

  it("prefers the client's cwd over this process's", async () => {
    const gamma = project("Gamma");
    const resolved = await hookProjectDir({ cwd: gamma }, root);
    expect(resolved.dir).toBe(gamma);
  });

  it("ends at the process cwd when the payload names nothing usable", async () => {
    const resolved = await hookProjectDir({ cwd: path.join(root, "gone") }, root);
    expect(resolved).toEqual({ dir: root, source: "process cwd" });
  });
});

describe("feedbackDisabledForDir", () => {
  it("stays silent where no ue-mcp project is found", async () => {
    const bare = path.join(root, "unrelated-repo");
    fs.mkdirSync(bare, { recursive: true });
    expect(await feedbackDisabledForDir(bare)).toBe(true);
  });

  it("stays silent with no directory at all", async () => {
    expect(await feedbackDisabledForDir(null)).toBe(true);
  });

  it("fires inside a ue-mcp project", async () => {
    const dir = project("Delta", "ue-mcp:\n  version: 1\n");
    expect(await feedbackDisabledForDir(dir)).toBe(false);
  });

  it("honours a project that disabled feedback", async () => {
    const dir = project("Epsilon", "ue-mcp:\n  disable:\n    - feedback\n");
    expect(await feedbackDisabledForDir(dir)).toBe(true);
  });

  it("judges each project by its own config", async () => {
    // The point of resolving from the payload: two registered editors can
    // disagree about feedback, and the hook has to read the right one.
    const on = project("On", "ue-mcp:\n  version: 1\n");
    const off = project("Off", "ue-mcp:\n  disable:\n    - feedback\n");
    expect(await feedbackDisabledForDir(on)).toBe(false);
    expect(await feedbackDisabledForDir(off)).toBe(true);
  });

  it("stays silent on a malformed config", async () => {
    const dir = project("Bad", "ue-mcp:\n  disable: [unclosed\n");
    expect(await feedbackDisabledForDir(dir)).toBe(true);
  });
});
