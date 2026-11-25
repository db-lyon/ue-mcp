import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { clipListFollowUp, clipPackage, clipRuns, readClipRuns } from "../../src/pie/clip-runs.js";
import type { ToolContext } from "../../src/surface/tool-types.js";

let dir: string;
const ctx = () => ({ project: { projectDir: dir } }) as unknown as ToolContext;

function run(id: string, results: Record<string, unknown> | string) {
  const d = path.join(dir, "Saved", "MCPObservations", id);
  fs.mkdirSync(d, { recursive: true });
  fs.writeFileSync(path.join(d, "clip_results.json"), typeof results === "string" ? results : JSON.stringify(results));
}

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "clip-runs-"));
  run("clip_a1", {
    run_id: "clip_a1", clip: "/Game/C/CL_Door.CL_Door", clip_name: "CL_Door", mode: "play", state: "finished", passed: false,
    started_at: "2026-11-20T10:00:00Z", ended_at: "2026-11-20T10:00:05Z", summary: { total: 2, failed: 1 },
    checks: [{ id: "c1", status: "failed", started_frame: 0, ended_frame: 60, evidence: { samples: 60 } }],
    regions: [{ id: "r1", name: "Walk", start: 0, end: 120, status: "failed", checks: ["c1"], drift: { max_position_cm: 2 } }],
  });
  run("clip_a2", { run_id: "clip_a2", clip: "/Game/C/CL_Door.CL_Door", mode: "record", state: "finished", passed: true, ended_at: "2026-11-21T09:00:00Z" });
  run("clip_b1", { run_id: "clip_b1", clip: "/Game/C/CL_Jump.CL_Jump", state: "stopped", ended_at: "2026-11-22T09:00:00Z" });
  run("broken", "{not json");
  // A script run's folder has no clip_results.json and is skipped.
  fs.mkdirSync(path.join(dir, "Saved", "MCPObservations", "script_x"), { recursive: true });
});

afterEach(() => fs.rmSync(dir, { recursive: true, force: true }));

describe("clip run history", () => {
  it("lists every clip run newest first, without per-frame evidence", () => {
    const { runs, total } = readClipRuns(dir);
    expect(total).toBe(3);
    expect(runs.map((r) => r.run_id)).toEqual(["clip_b1", "clip_a2", "clip_a1"]);
    const a1 = runs[2];
    expect(a1).toMatchObject({ clip_name: "CL_Door", passed: false, summary: { failed: 1 } });
    expect(a1.checks).toEqual([{ id: "c1", status: "failed", started_frame: 0, ended_frame: 60 }]);
    expect(a1.regions).toEqual([{ id: "r1", name: "Walk", start: 0, end: 120, status: "failed", drift: { max_position_cm: 2 } }]);
  });

  it("filters by clip however its path is spelled, and caps at limit", () => {
    expect(clipPackage("/Game/C/CL_Door")).toBe(clipPackage("/Game/C/CL_Door.CL_Door"));
    const answer = clipRuns(ctx(), { path: "/Game/C/CL_Door", limit: 1 });
    expect(answer).toMatchObject({ success: true, count: 1, total: 2 });
    expect((answer.runs as Array<{ run_id: string }>).map((r) => r.run_id)).toEqual(["clip_a2"]);
  });

  it("answers an empty history for a project with no runs", () => {
    expect(readClipRuns(path.join(dir, "nothing"))).toEqual({ runs: [], total: 0 });
  });

  it("adds each clip's newest run to clip_list", async () => {
    const answered = {
      success: true, count: 3, clips: [
        { path: "/Game/C/CL_Door", object_path: "/Game/C/CL_Door.CL_Door", name: "CL_Door" },
        { path: "/Game/C/CL_Jump", object_path: "/Game/C/CL_Jump.CL_Jump", name: "CL_Jump" },
        { path: "/Game/C/CL_New", object_path: "/Game/C/CL_New.CL_New", name: "CL_New" },
      ],
    };
    const out = (await clipListFollowUp(ctx(), {}, answered)) as { clips: Array<Record<string, unknown>> };
    expect(out.clips[0].last_run).toMatchObject({ run_id: "clip_a2", passed: true, mode: "record" });
    expect(out.clips[0].last_run).not.toHaveProperty("checks");
    expect(out.clips[1].last_run).toMatchObject({ run_id: "clip_b1", state: "stopped" });
    expect(out.clips[2]).not.toHaveProperty("last_run");
  });
});
