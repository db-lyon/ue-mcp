/**
 * The sharded live run's planning (#1436): every live file declares a kind,
 * shards balance by measured time, and the gates read the merged timing.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, expect, it } from "vitest";
import {
  balance, buildOutputFilter, editorFits, headlessShardCount, liveFiles, mirror, overBudget, planShards, readKind,
  splitAtDeath, timingFromReports, undeployedSources,
} from "../../scripts/live-shards.mjs";
import { liveShardProjectDir, LIVE_SHARD_ROOT } from "../../scripts/bridge-target.mjs";

const REPO_ROOT = path.resolve(__dirname, "..", "..");

describe("live test kinds", () => {
  it("every file under tests/live declares a known kind", () => {
    const files = liveFiles(REPO_ROOT);
    expect(files.length).toBeGreaterThan(100);
    expect(files.find((f) => f.file.endsWith("editor-lifecycle.test.ts"))?.kind).toBe("lifecycle");
  });

  it("reads the kind from the file header only", () => {
    expect(readKind("// @live-editor render\n/** doc */")).toBe("render");
    expect(readKind("/** doc */\nconst x = 1;")).toBeNull();
  });
});

describe("shard planning", () => {
  it("puts the longest files first onto the lightest shard", () => {
    const w = new Map([["a", 100], ["b", 60], ["c", 50], ["d", 40]]);
    const shards = balance(["a", "b", "c", "d"], w, 2);
    expect(shards.map((s) => s.weightMs).sort()).toEqual([110, 140]);
  });

  it("gives each kind its own shards and never plans the lifecycle file", () => {
    const entries = [
      { file: "h1", kind: "headless" }, { file: "h2", kind: "headless" }, { file: "h3", kind: "headless" },
      { file: "r1", kind: "render" }, { file: "i1", kind: "interactive" }, { file: "n1", kind: "none" },
      { file: "l1", kind: "lifecycle" },
    ];
    const plan = planShards(entries, { files: {} }, { headless: 2, render: 1, interactive: 1 });
    expect(plan.map((s) => s.name)).toEqual(["h1", "h2", "r1", "i1"]);
    expect(plan.flatMap((s) => s.files)).not.toContain("l1");
    expect(plan.filter((s) => s.kind === "headless").flatMap((s) => s.files).sort()).toEqual(["h1", "h2", "h3", "n1"]);
  });
});

describe("timing and budgets", () => {
  const report = {
    testResults: [{
      name: path.join(REPO_ROOT, "tests", "live", "x.test.ts"),
      startTime: 1000, endTime: 6000,
      assertionResults: [
        { fullName: "suite fast", duration: 200, status: "passed" },
        { fullName: "suite slow", duration: 45_000, status: "passed" },
      ],
    }],
  };

  it("keys tests by file and full name", () => {
    const t = timingFromReports([report], REPO_ROOT);
    expect(t.files["tests/live/x.test.ts"].durationMs).toBe(5000);
    expect(t.tests["tests/live/x.test.ts > suite slow"]).toBe(45_000);
  });

  it("takes a file's time from the file reporter, which counts hooks and setup", () => {
    const t = timingFromReports([report], REPO_ROOT, { "tests/live/x.test.ts": 61_000 });
    expect(t.files["tests/live/x.test.ts"].durationMs).toBe(61_000);
  });

  it("flags a test over the default budget unless it has its own", () => {
    const t = timingFromReports([report], REPO_ROOT);
    expect(overBudget(t.tests, { defaultTestMs: 30_000, tests: {} }).map((o) => o.test)).toEqual(["tests/live/x.test.ts > suite slow"]);
    expect(overBudget(t.tests, { defaultTestMs: 30_000, tests: { "tests/live/x.test.ts > suite slow": 60_000 } })).toEqual([]);
  });
});

describe("shard copies", () => {
  it("mirrors a tree, keeps the named roots, prunes what the source dropped, and copies only changes", () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-shard-"));
    const src = path.join(root, "src");
    const dst = path.join(root, "dst");
    for (const f of ["ue_mcp.uproject", "Content/A.uasset", "Plugins/P/Intermediate/y.obj", "Plugins/P/Binaries/p.dll"]) {
      fs.mkdirSync(path.dirname(path.join(src, f)), { recursive: true });
      fs.writeFileSync(path.join(src, f), f);
    }
    expect(mirror(path.join(src, "Plugins"), path.join(dst, "Plugins"), buildOutputFilter(path.join(src, "Plugins")))).toBe(1);
    expect(fs.existsSync(path.join(dst, "Plugins", "P", "Intermediate"))).toBe(false);
    fs.mkdirSync(path.join(dst, "Content"), { recursive: true });
    fs.writeFileSync(path.join(dst, "Content", "Stale.uasset"), "x");
    const tracked = path.join(root, "tracked");
    fs.mkdirSync(path.join(tracked, "Content"), { recursive: true });
    fs.writeFileSync(path.join(tracked, "Content", "A.uasset"), "a");
    expect(mirror(tracked, dst, undefined, new Set(["Plugins"]))).toBe(1);
    expect(fs.existsSync(path.join(dst, "Content", "Stale.uasset"))).toBe(false);
    expect(fs.existsSync(path.join(dst, "Plugins", "P", "Binaries", "p.dll"))).toBe(true);
    expect(mirror(tracked, dst, undefined, new Set(["Plugins"]))).toBe(0);
    fs.rmSync(root, { recursive: true, force: true });
  });

  it("accepts a shard directory only inside the shard root", () => {
    expect(liveShardProjectDir({})).toBeNull();
    expect(() => liveShardProjectDir({ UE_MCP_LIVE_SHARD_DIR: path.join(REPO_ROOT, "tests", "ue_mcp") })).toThrow(/under/);
    expect(() => liveShardProjectDir({ UE_MCP_LIVE_SHARD_DIR: path.join(LIVE_SHARD_ROOT, "h9", "ue_mcp") })).toThrow(/under/);
  });
});

describe("memory decides the editor count", () => {
  it("fits headless shards into what is free after the headroom and one short editor's turn", () => {
    // 32 GB, 28 GB free: 28672 - 4096 headroom - 5600 for the largest short editor = 18976, five of 3600.
    expect(headlessShardCount({ freeMB: 28672, totalMB: 32768, otherKinds: ["render", "scm", "base"] })).toBe(5);
  });

  it("still runs one headless shard on a busy machine, and never more than the cap", () => {
    expect(headlessShardCount({ freeMB: 5000, totalMB: 32768, otherKinds: ["render"] })).toBe(1);
    expect(headlessShardCount({ freeMB: 500_000, totalMB: 524_288, max: 8 })).toBe(8);
  });

  it("holds an editor back until it fits beside the headroom", () => {
    expect(editorFits("headless", 8000, 32768)).toBe(true);
    expect(editorFits("render", 8000, 32768)).toBe(false);
  });

  it("gives the base shard only the files that need it", () => {
    const entries = [
      { file: "b", kind: "base" }, { file: "h1", kind: "headless" }, { file: "h2", kind: "headless" }, { file: "h3", kind: "headless" },
    ];
    const plan = planShards(entries, { files: {} }, { headless: 2 });
    expect(plan.map((s) => s.name)).toEqual(["h1", "h2", "b1"]);
    expect(plan.find((s) => s.name === "b1")?.files).toEqual(["b"]);
  });
});

describe("an editor that dies mid-run", () => {
  it("names the file it died in and hands back the files it never reached", () => {
    const results = new Map<string, unknown>([
      ["a", { startTime: 0, endTime: 10, status: "passed" }],
      ["b", { startTime: 10, endTime: 30, status: "failed", message: "expected 1 to be 2" }],
      ["c", { startTime: 0, endTime: 0, status: "failed", message: "Error: Could not reach the UE MCP bridge for the smoke test project." }],
      ["e", { startTime: 31, endTime: 32, status: "passed" }],
    ]);
    const { culprit, unrun } = splitAtDeath(["a", "b", "c", "d", "e"], results, 20);
    expect(culprit).toBe("b");
    expect(unrun).toEqual(["c", "d", "e"]);
  });
});

describe("the deployed bridge check", () => {
  it("names the source files whose deployed copy is missing or differs, and nothing else", () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "undeployed-"));
    try {
      const src = path.join(root, "src");
      const dst = path.join(root, "dst");
      for (const [rel, text] of [["A.cpp", "a"], ["Sub/B.h", "b"], ["Sub/C.h", "c"]]) {
        fs.mkdirSync(path.dirname(path.join(src, rel)), { recursive: true });
        fs.writeFileSync(path.join(src, rel), text);
      }
      fs.mkdirSync(path.join(dst, "Sub"), { recursive: true });
      fs.writeFileSync(path.join(dst, "A.cpp"), "a");
      fs.writeFileSync(path.join(dst, "Sub", "B.h"), "changed");
      fs.writeFileSync(path.join(dst, "Extra.cpp"), "only deployed");
      expect(undeployedSources(src, dst).sort()).toEqual(["Sub/B.h", "Sub/C.h"]);
      fs.writeFileSync(path.join(dst, "Sub", "B.h"), "b");
      fs.writeFileSync(path.join(dst, "Sub", "C.h"), "c");
      expect(undeployedSources(src, dst)).toEqual([]);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });
});
