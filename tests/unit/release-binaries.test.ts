import { describe, it, expect } from "vitest";
import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  ENGINES,
  REQUIRED_ENGINES,
  manifestEngines,
  missingBinaries,
  missingMessage,
  requiredBinaries,
  requiredEngines,
} from "../../scripts/release-binaries.mjs";

const script = path.resolve(__dirname, "../../scripts/release-binaries.mjs");
const run = (version: string, assets: string[], extra: string[] = []) =>
  spawnSync(process.execPath, [script, "--version", version, ...extra], { input: assets.join("\n"), encoding: "utf-8" });

function manifestFile(version: string, engines: string[]): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "rb-"));
  const file = path.join(dir, `UE_MCP_Bridge-${version}-manifest.json`);
  const assets = engines.map((e) => ({ engine: e, platform: "Win64", file: `UE_MCP_Bridge-${version}-UE${e}-Win64.zip` }));
  fs.writeFileSync(file, JSON.stringify({ version, assets }));
  return file;
}

describe("release binaries gate", () => {
  it("requires a Win64 bridge for each promised engine and the manifest", () => {
    expect(REQUIRED_ENGINES).toEqual(["5.7", "5.8"]);
    expect(requiredBinaries("1.4.0-beta.3")).toEqual([
      "UE_MCP_Bridge-1.4.0-beta.3-UE5.7-Win64.zip",
      "UE_MCP_Bridge-1.4.0-beta.3-UE5.8-Win64.zip",
      "UE_MCP_Bridge-1.4.0-beta.3-manifest.json",
    ]);
  });

  it("lists every engine once, and requires 5.9 only once it is built", () => {
    const versions = ENGINES.map((e) => e.version);
    expect(new Set(versions).size).toBe(versions.length);
    for (const e of ENGINES) expect(["always", "when-built"]).toContain(e.required);
    expect(requiredEngines()).toEqual(["5.7", "5.8"]);
    expect(requiredEngines(["5.8"])).toEqual(["5.7", "5.8"]);
    expect(requiredEngines(["5.7", "5.8", "5.9"])).toEqual(["5.7", "5.8", "5.9"]);
    // An engine the table does not name is never required.
    expect(requiredEngines(["5.6"])).toEqual(["5.7", "5.8"]);
  });

  it("reads the build set off a manifest", () => {
    const m = JSON.parse(fs.readFileSync(manifestFile("1.5.0", ["5.7", "5.9"]), "utf8"));
    expect(manifestEngines(m)).toEqual(["5.7", "5.9"]);
    expect(manifestEngines(null)).toEqual([]);
  });

  it("names what a release is missing, and nothing when it has all of it", () => {
    expect(missingBinaries("1.4.0-beta.2", ["ue-mcp.tar.gz"])).toEqual(requiredBinaries("1.4.0-beta.2"));
    expect(missingBinaries("1.4.0-beta.3", [...requiredBinaries("1.4.0-beta.3"), "ue-mcp.tar.gz"])).toEqual([]);
    // Another version's binaries do not count.
    expect(missingBinaries("1.4.0-beta.3", requiredBinaries("1.4.0-beta.1"))).toHaveLength(3);
  });

  it("says how to fix it", () => {
    const m = missingMessage("1.4.0-beta.2", ["UE_MCP_Bridge-1.4.0-beta.2-UE5.8-Win64.zip"]);
    expect(m).toContain("UE_MCP_Bridge-1.4.0-beta.2-UE5.8-Win64.zip");
    expect(m).toContain("npm run binaries:build");
    expect(m).toContain("npm run binaries:upload");
  });

  it("fails the publish step without the binaries and passes with them", () => {
    const bad = run("1.4.0-beta.2", ["ue-mcp.tar.gz"]);
    expect(bad.status).toBe(1);
    expect(bad.stderr).toContain("UE_MCP_Bridge-1.4.0-beta.2-manifest.json");
    const good = run("1.4.0-beta.3", [...requiredBinaries("1.4.0-beta.3"), ""]);
    expect(good.status).toBe(0);
  });

  it("requires the 5.9 zip at publish when the manifest says 5.9 was built", () => {
    const today = requiredBinaries("1.5.0");
    // Today's release: no 5.9 in the build set, so none is required.
    expect(run("1.5.0", today, ["--manifest", manifestFile("1.5.0", ["5.7", "5.8"])]).status).toBe(0);
    // A missing manifest file does not invent a requirement.
    expect(run("1.5.0", today, ["--manifest", path.join(os.tmpdir(), "absent-manifest.json")]).status).toBe(0);
    const built = manifestFile("1.5.0", ["5.7", "5.8", "5.9"]);
    const bad = run("1.5.0", today, ["--manifest", built]);
    expect(bad.status).toBe(1);
    expect(bad.stderr).toContain("UE_MCP_Bridge-1.5.0-UE5.9-Win64.zip");
    const good = run("1.5.0", [...today, "UE_MCP_Bridge-1.5.0-UE5.9-Win64.zip"], ["--manifest", built]);
    expect(good.status).toBe(0);
  });
});
