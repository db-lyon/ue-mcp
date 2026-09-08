import { describe, it, expect, beforeEach, afterEach } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
// @ts-expect-error - plain ESM script, no types
import { findLiveCodingPatches, findOrphanedSources, runHygiene } from "../../scripts/pre-build-hygiene.mjs";
import { touchBuildRules } from "../../src/deployer.js";

let root: string;
beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-hygiene-"));
});
afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
});

const write = (p: string, body = "x"): string => {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, body);
  return p;
};

/**
 * Both of these were a recipe somebody was meant to follow by hand after new
 * handlers came back "Unknown method" from a build that reported success.
 */
describe("Live Coding patches are cleared before a build", () => {
  it("finds every patch artifact and leaves the real binaries alone", () => {
    const bin = path.join(root, "Binaries", "Win64");
    write(path.join(bin, "UnrealEditor-UE_MCP_Bridge.dll"));
    write(path.join(bin, "UnrealEditor-UE_MCP_Bridge.patch_1.dll"));
    write(path.join(bin, "UnrealEditor-UE_MCP_Bridge.patch_2.pdb"));
    write(path.join(bin, "UnrealEditor-UE_MCP_Bridge.patch_10.lib"));

    const found = (findLiveCodingPatches(bin) as string[]).map((f) => path.basename(f)).sort();
    expect(found).toEqual([
      "UnrealEditor-UE_MCP_Bridge.patch_1.dll",
      "UnrealEditor-UE_MCP_Bridge.patch_10.lib",
      "UnrealEditor-UE_MCP_Bridge.patch_2.pdb",
    ]);
  });

  it("deletes them, because the editor loads the newest patch over a fresh DLL", () => {
    const bin = path.join(root, "Binaries", "Win64");
    write(path.join(bin, "Real.dll"));
    write(path.join(bin, "Real.patch_3.dll"));

    runHygiene({ binariesDir: bin, sourceDir: root, deployedDir: root, log: () => {} });

    expect(fs.readdirSync(bin)).toEqual(["Real.dll"]);
  });

  it("says nothing and does nothing when there are none", () => {
    const bin = path.join(root, "Binaries", "Win64");
    write(path.join(bin, "Real.dll"));
    const said: string[] = [];
    runHygiene({ binariesDir: bin, sourceDir: root, deployedDir: root, log: (m: string) => said.push(m) });
    expect(said).toEqual([]);
  });

  it("copes with a Binaries directory that does not exist yet", () => {
    expect(findLiveCodingPatches(path.join(root, "nope"))).toEqual([]);
  });
});

describe("a deployed source the authored tree does not have is reported", () => {
  it("names the orphan, because UBT compiles whatever it finds", () => {
    const src = path.join(root, "plugin");
    const dep = path.join(root, "deployed");
    write(path.join(src, "Source", "A.cpp"));
    write(path.join(dep, "Source", "A.cpp"));
    write(path.join(dep, "Source", "OldName.cpp"));

    const orphans = findOrphanedSources(src, dep) as string[];
    expect(orphans.map((o) => path.basename(o))).toEqual(["OldName.cpp"]);
  });

  it("ignores build output, which never exists in the authored tree", () => {
    const src = path.join(root, "plugin");
    const dep = path.join(root, "deployed");
    write(path.join(src, "Source", "A.cpp"));
    write(path.join(dep, "Source", "A.cpp"));
    write(path.join(dep, "Binaries", "Win64", "x.cpp"));
    write(path.join(dep, "Intermediate", "Build", "y.h"));

    expect(findOrphanedSources(src, dep)).toEqual([]);
  });

  it("matches paths case-insensitively, as the filesystem does", () => {
    const src = path.join(root, "plugin");
    const dep = path.join(root, "deployed");
    write(path.join(src, "Source", "Handlers.cpp"));
    write(path.join(dep, "Source", "handlers.cpp"));

    expect(findOrphanedSources(src, dep)).toEqual([]);
  });

  it("tells the reader to redeploy rather than edit the deployed tree", () => {
    const src = path.join(root, "plugin");
    const dep = path.join(root, "deployed");
    write(path.join(src, "Source", "A.cpp"));
    write(path.join(dep, "Source", "Ghost.cpp"));

    const said: string[] = [];
    runHygiene({
      binariesDir: path.join(root, "none"),
      sourceDir: src,
      deployedDir: dep,
      log: (m: string) => said.push(m),
    });
    expect(said.join("\n")).toContain("deploy.mjs");
  });
});

/**
 * UnrealBuildTool caches a module's source list and rebuilds it only when the
 * module's Build.cs is newer than that cache. A brand new .cpp therefore
 * deploys, compiles into nothing, and every action in it answers "Unknown
 * method" at runtime, from a build that reported success.
 */
describe("a new source file makes UnrealBuildTool look again", () => {
  it("touches every Build.cs under the plugin", async () => {
    const plugin = path.join(root, "Plugins", "UE_MCP_Bridge");
    const rules = write(path.join(plugin, "Source", "UE_MCP_Bridge", "UE_MCP_Bridge.Build.cs"));
    const other = write(path.join(plugin, "Source", "Status", "Status.Build.cs"));
    const old = new Date(Date.now() - 60_000);
    fs.utimesSync(rules, old, old);
    fs.utimesSync(other, old, old);

    const touched = touchBuildRules(plugin, ["NewHandlers.cpp"]);

    expect(touched).toHaveLength(2);
    expect(fs.statSync(rules).mtimeMs).toBeGreaterThan(old.getTime());
    expect(fs.statSync(other).mtimeMs).toBeGreaterThan(old.getTime());
  });

  it("leaves build output alone", () => {
    const plugin = path.join(root, "Plugins", "UE_MCP_Bridge");
    write(path.join(plugin, "Source", "A.Build.cs"));
    write(path.join(plugin, "Intermediate", "Stale.Build.cs"));
    write(path.join(plugin, "Binaries", "Also.Build.cs"));

    const touched = touchBuildRules(plugin) as string[];

    expect(touched.map((t) => path.basename(t))).toEqual(["A.Build.cs"]);
  });

  it("does nothing when the plugin is not deployed yet", () => {
    expect(touchBuildRules(path.join(root, "missing"))).toEqual([]);
  });
});


/**
 * The two functions above are pure and well covered, but a check nothing calls
 * protects nothing: deleting the call from the build script used to leave
 * every test here green.
 */
describe("the build actually runs the hygiene checks", () => {
  const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
  const buildScript = fs.readFileSync(path.join(REPO, "scripts", "build.js"), "utf8");

  it("calls runHygiene", () => {
    const calls = buildScript
      .split("\n")
      .filter((l) => !l.trim().startsWith("//") && !l.trim().startsWith("import"))
      .filter((l) => l.includes("runHygiene("));
    expect(calls.length, "nothing in the build calls runHygiene").toBeGreaterThan(0);
  });

  it("calls it before the build starts, not after", () => {
    const call = buildScript.indexOf("runHygiene(");
    const start = buildScript.indexOf("Starting build...");
    expect(call).toBeGreaterThan(-1);
    expect(start).toBeGreaterThan(-1);
    expect(call, "hygiene runs after the build, which is too late").toBeLessThan(start);
  });

  it("points it at the deployed plugin and the authored tree", () => {
    expect(buildScript).toContain("binariesDir");
    expect(buildScript).toContain("sourceDir");
    expect(buildScript).toContain("deployedDir");
  });
});
