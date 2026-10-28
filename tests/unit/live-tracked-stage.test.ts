/**
 * The release starts the live and live-base lanes together, and both stage the
 * tracked tests/ue_mcp. Each runner process stages into its own folder (#1603).
 */
import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import { describe, expect, it } from "vitest";
import { stageTrackedProject, sweepStaleTrackedStages, trackedStageDir } from "../../scripts/live-shards.mjs";

const REPO_ROOT = path.resolve(__dirname, "..", "..");
const SHARDS_MODULE = pathToFileURL(path.join(REPO_ROOT, "scripts", "live-shards.mjs")).href;

/** One runner process: stage into its own folder, as runSharded does, and report what it got. */
function stageInChild(shardRoot: string): Promise<{ code: number | null; stderr: string; dir: string }> {
  const script = `
    const { stageTrackedProject, trackedStageDir } = await import(${JSON.stringify(SHARDS_MODULE)});
    for (let i = 0; i < 2; i++) stageTrackedProject(${JSON.stringify(REPO_ROOT)}, trackedStageDir(${JSON.stringify(shardRoot)}));
    process.stdout.write(trackedStageDir(${JSON.stringify(shardRoot)}));
  `;
  return new Promise((resolve) => {
    const child = spawn(process.execPath, ["--input-type=module", "-e", script], { cwd: REPO_ROOT });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (d) => (stdout += d));
    child.stderr.on("data", (d) => (stderr += d));
    child.on("close", (code) => resolve({ code, stderr, dir: stdout }));
  });
}

describe("tracked project staging", () => {
  it("gives each runner process its own folder", () => {
    expect(trackedStageDir("/s", 101)).not.toBe(trackedStageDir("/s", 202));
    expect(path.basename(trackedStageDir("/s", 101))).toBe("_tracked-101");
  });

  it("two runners staging at the same time both get the whole tracked project", async () => {
    const shardRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-stage-"));
    try {
      const [a, b] = await Promise.all([stageInChild(shardRoot), stageInChild(shardRoot)]);
      expect(a.stderr).toBe("");
      expect(b.stderr).toBe("");
      expect([a.code, b.code]).toEqual([0, 0]);
      expect(a.dir).not.toBe(b.dir);
      for (const r of [a, b]) {
        expect(fs.existsSync(path.join(r.dir, "tests", "ue_mcp", "ue_mcp.uproject"))).toBe(true);
      }
    } finally {
      fs.rmSync(shardRoot, { recursive: true, force: true });
    }
  }, 60_000);

  it("stages the files git tracks under tests/ue_mcp", () => {
    const shardRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-stage-"));
    try {
      const project = stageTrackedProject(REPO_ROOT, trackedStageDir(shardRoot));
      expect(fs.existsSync(path.join(project, "ue_mcp.uproject"))).toBe(true);
    } finally {
      fs.rmSync(shardRoot, { recursive: true, force: true });
    }
  });

  it("sweeps folders of dead runners and the old shared folder, never a live runner's", () => {
    const shardRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-stage-"));
    try {
      for (const name of ["_tracked", "_tracked-111", "_tracked-222", "_build", "h1"]) fs.mkdirSync(path.join(shardRoot, name));
      const removed = sweepStaleTrackedStages(shardRoot, (pid: number) => pid === 222);
      expect(removed.sort()).toEqual(["_tracked", "_tracked-111"]);
      expect(fs.readdirSync(shardRoot).sort()).toEqual(["_build", "_tracked-222", "h1"]);
    } finally {
      fs.rmSync(shardRoot, { recursive: true, force: true });
    }
  });
});
