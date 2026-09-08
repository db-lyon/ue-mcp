import { describe, it, expect, beforeEach, afterEach } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { deployPluginTree } from "../../src/deployer.js";

let root: string;
let source: string;
let target: string;

const write = (p: string, body = "x"): string => {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, body);
  return p;
};

/** Push a file's timestamp back, so a touch is detectable. */
const age = (p: string, seconds = 60): number => {
  const when = new Date(Date.now() - seconds * 1000);
  fs.utimesSync(p, when, when);
  return fs.statSync(p).mtimeMs;
};

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-rescan-"));
  source = path.join(root, "plugin", "ue_mcp_bridge");
  target = path.join(root, "project", "Plugins", "UE_MCP_Bridge");
  write(path.join(source, "UE_MCP_Bridge.uplugin"), "{}");
  write(path.join(source, "Source", "UE_MCP_Bridge", "UE_MCP_Bridge.Build.cs"), "// rules");
  write(path.join(source, "Source", "UE_MCP_Bridge", "Private", "Existing.cpp"), "// a");
});

afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
});

const rules = (): string =>
  path.join(target, "Source", "UE_MCP_Bridge", "UE_MCP_Bridge.Build.cs");

/**
 * UnrealBuildTool caches a module's source list and rebuilds it only when the
 * module's Build.cs is newer than that cache. A source file it has never seen
 * therefore deploys, compiles into nothing, and every action in it answers
 * "Unknown method" at runtime, from a build that reported success.
 *
 * These drive the real deploy against real directories, so the assertion is
 * about what deploying does rather than about a helper called in isolation.
 */
describe("deploying a new source file makes UnrealBuildTool look again", () => {
  it("touches Build.cs when a source appears that the deployed tree lacked", () => {
    deployPluginTree(source, target);
    const before = age(rules());

    write(path.join(source, "Source", "UE_MCP_Bridge", "Private", "NewHandlers.cpp"), "// new");
    deployPluginTree(source, target);

    expect(fs.existsSync(path.join(target, "Source", "UE_MCP_Bridge", "Private", "NewHandlers.cpp"))).toBe(true);
    expect(
      fs.statSync(rules()).mtimeMs,
      "Build.cs was not touched, so the new file would compile into nothing",
    ).toBeGreaterThan(before);
  });

  it("touches it for a new header too, since a header changes the module's inputs", () => {
    deployPluginTree(source, target);
    const before = age(rules());

    write(path.join(source, "Source", "UE_MCP_Bridge", "Private", "NewHandlers.h"), "// new");
    deployPluginTree(source, target);

    expect(fs.statSync(rules()).mtimeMs).toBeGreaterThan(before);
  });

  it("leaves it alone when nothing new arrived", () => {
    deployPluginTree(source, target);
    const before = age(rules());

    // Same tree, deployed again.
    deployPluginTree(source, target);

    expect(
      fs.statSync(rules()).mtimeMs,
      "an unchanged deploy invalidated the module's cache for no reason",
    ).toBe(before);
  });

  it("leaves it alone when an existing source merely changed", () => {
    deployPluginTree(source, target);
    const before = age(rules());

    fs.writeFileSync(path.join(source, "Source", "UE_MCP_Bridge", "Private", "Existing.cpp"), "// edited");
    deployPluginTree(source, target);

    expect(fs.readFileSync(path.join(target, "Source", "UE_MCP_Bridge", "Private", "Existing.cpp"), "utf8"))
      .toBe("// edited");
    expect(fs.statSync(rules()).mtimeMs).toBe(before);
  });

  it("copies the tree on a first deploy", () => {
    expect(deployPluginTree(source, target)).toBe(true);
    expect(fs.existsSync(path.join(target, "UE_MCP_Bridge.uplugin"))).toBe(true);
    expect(fs.existsSync(path.join(target, "Source", "UE_MCP_Bridge", "Private", "Existing.cpp"))).toBe(true);
  });

  it("prunes a deployed source the authored tree no longer has", () => {
    deployPluginTree(source, target);
    const ghost = path.join(target, "Source", "UE_MCP_Bridge", "Private", "Ghost.cpp");
    write(ghost, "// left behind by a rename");

    deployPluginTree(source, target);

    expect(fs.existsSync(ghost), "a stale source survived and would be compiled").toBe(false);
  });

  it("says nothing happened when the authored tree is missing", () => {
    expect(deployPluginTree(path.join(root, "nowhere"), target)).toBe(false);
  });
});
