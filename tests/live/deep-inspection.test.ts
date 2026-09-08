/**
 * The three deep-inspection actions, against a real editor (#1004/#1006/#995).
 *
 * These three exist because reading state that the editor plainly has was
 * forcing callers through `execute_python`, and each one's correctness is a
 * claim about the engine rather than about this code:
 *
 *   - the material usage flags are whatever `EMaterialUsage` reflects on the
 *     engine in hand, so a hand-written list is wrong the moment Epic adds an
 *     enumerator, and the property behind each one is resolved rather than
 *     assembled;
 *   - a console variable's value and the priority it was set at are read out
 *     of the running process, not out of a config file;
 *   - and `entryPoint` has to load a file WITHOUT firing its `__main__` guard,
 *     which is the whole point and the one thing a unit test cannot observe.
 *
 * The smoke sweep proves each handler answers. This proves what it answers.
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

// The flags are read off a copy inside the test project rather than off the
// engine material it is duplicated from. set_usage writes, and a write to
// engine content dirties a package outside the disposable project - which
// then blocks a clean editor shutdown even though the file on disk is
// read-only and never actually changed.
const ENGINE_MATERIAL = "/Engine/EngineMaterials/DefaultMaterial";
const TEST_MATERIAL = `${TEST_PREFIX}/M_UsageProbe`;

type UsageRow = { usage?: string; propertyName?: string; enabled?: boolean };
type CVarRow = { name?: string; value?: string; defaultValue?: string; isDefault?: boolean; setBy?: string; type?: string };

let bridge: EditorBridge;
let scriptDir = "";
let scriptPath = "";

/**
 * Shaped like the Tools/ script #995 describes: several independent stages
 * plus a main() that chains them behind the usual guard. `noisy` exists to
 * give the log bounds something to bound.
 */
const PROBE_SCRIPT = [
  "import unreal",
  "",
  "",
  "def check_cpp():",
  '    return "CHECK_CPP_RAN"',
  "",
  "",
  "def add(a, b, suffix=\"\"):",
  '    return "{}{}{}".format(a, b, suffix)',
  "",
  "",
  "def noisy():",
  "    for i in range(200):",
  '        unreal.log("NOISE_LINE {}".format(i))',
  '    return "NOISY_DONE"',
  "",
  "",
  "def main():",
  '    unreal.log("MAIN_GUARD_FIRED")',
  "    return check_cpp()",
  "",
  "",
  'if __name__ == "__main__":',
  "    main()",
  "",
].join("\n");

beforeAll(async () => {
  bridge = await getBridge();
  scriptDir = mkdtempSync(join(tmpdir(), "ue-mcp-entrypoint-"));
  scriptPath = join(scriptDir, "entry_probe.py");
  writeFileSync(scriptPath, PROBE_SCRIPT, "utf8");

  await callBridge(bridge, "delete_asset", { assetPath: TEST_MATERIAL, force: true });
  const copy = await callBridge(bridge, "duplicate_asset", {
    sourcePath: ENGINE_MATERIAL,
    destinationPath: TEST_MATERIAL,
  });
  expect(copy.ok, copy.error).toBe(true);
});

afterAll(async () => {
  if (scriptDir) rmSync(scriptDir, { recursive: true, force: true });
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: TEST_MATERIAL, force: true });
    disconnectBridge();
  }
});

describe("material usage flags come from the engine (#1004)", () => {
  let usages: UsageRow[] = [];

  beforeAll(async () => {
    const read = await callBridge(bridge, "get_material_usage", { assetPath: TEST_MATERIAL });
    expect(read.ok, read.error).toBe(true);
    usages = (resultArray(read.result, "usages") ?? []) as UsageRow[];
  });

  it("reports more flags than the hand-written list could name", () => {
    // The table this replaced had nineteen entries and could not be extended
    // without naming enumerators that do not exist on older engines.
    expect(usages.length).toBeGreaterThan(19);
  });

  it("includes the enumerators the old table could not reach", () => {
    const names = usages.map((u) => u.usage);
    expect(names).toContain("VolumetricCloud");
    expect(names).toContain("Nanite");
  });

  it("names a real bUsedWith property for every flag", () => {
    // Resolved against UMaterial rather than assembled, which is also the
    // guard that keeps an enumerator with no property away from the engine
    // accessors, whose unhandled arm is UE_LOG(Fatal).
    expect(usages.length).toBeGreaterThan(0);
    for (const row of usages) {
      expect(row.propertyName, `${row.usage} has no property`).toMatch(/^bUsedWith/);
    }
  });

  it("gets the irregular plural right", () => {
    // MATUSAGE_SplineMesh is backed by bUsedWithSplineMeshes. Assembling the
    // name would have produced bUsedWithSplineMesh and found nothing.
    const spline = usages.find((u) => u.usage === "SplineMesh");
    expect(spline?.propertyName).toBe("bUsedWithSplineMeshes");
  });

  it("accepts a usage name set_usage can also take", async () => {
    const applied = await callBridge(bridge, "set_material_usage", {
      assetPath: TEST_MATERIAL,
      usage: "VolumetricCloud",
    });
    // Either it applied or it was already set; what must NOT happen is the
    // name coming back as unrecognised, which is what #1004 reported.
    expect(applied.ok, applied.error).toBe(true);
    const unknown = (applied.result as Record<string, unknown>)?.unknown;
    expect(unknown ?? []).toEqual([]);
  });
});

describe("console variables are read from the running editor (#1006)", () => {
  it("reports the value, its default and the priority it was set at", async () => {
    const read = await callBridge(bridge, "get_cvars", { name: "r.VolumetricCloud" });
    expect(read.ok, read.error).toBe(true);
    const rows = (resultArray(read.result, "cvars") ?? []) as CVarRow[];
    expect(rows).toHaveLength(1);
    expect(rows[0].name).toBe("r.VolumetricCloud");
    expect(typeof rows[0].value).toBe("string");
    expect(typeof rows[0].defaultValue).toBe("string");
    expect(typeof rows[0].isDefault).toBe("boolean");
    // The field that separates "sitting at its default" from "driven there by
    // a scalability group", which read identically before.
    expect(rows[0].setBy).toBeTruthy();
  });

  it("puts a name this build does not have under notFound rather than failing", async () => {
    const read = await callBridge(bridge, "get_cvars", { name: "r.ThisDoesNotExist.AtAll" });
    expect(read.ok, read.error).toBe(true);
    expect(resultArray(read.result, "notFound")).toEqual(["r.ThisDoesNotExist.AtAll"]);
    expect(resultArray(read.result, "cvars")).toEqual([]);
  });

  it("caps a pattern search and says that it did", async () => {
    const read = await callBridge(bridge, "get_cvars", { pattern: "VolumetricCloud", limit: 5 });
    expect(read.ok, read.error).toBe(true);
    const rows = (resultArray(read.result, "cvars") ?? []) as CVarRow[];
    const result = read.result as Record<string, unknown>;
    expect(rows.length).toBeLessThanOrEqual(5);
    // A truncated answer must never read as a complete one.
    expect(typeof result.matchedCount).toBe("number");
    expect(result.truncated).toBe((result.matchedCount as number) > rows.length);
  });
});

describe("run_python_file calls one function in a file (#995)", () => {
  it("calls the named function and returns its value", async () => {
    const run = await callBridge(bridge, "run_python_file", {
      filePath: scriptPath,
      entryPoint: "check_cpp",
    });
    expect(run.ok, run.error).toBe(true);
    const result = run.result as Record<string, unknown>;
    expect(String(result.result)).toContain("CHECK_CPP_RAN");
  });

  it("does not fire the file's __main__ guard", async () => {
    // The whole reason entryPoint exists: running the file top to bottom runs
    // main(), which is never what a caller asking for one stage wants.
    const run = await callBridge(bridge, "run_python_file", {
      filePath: scriptPath,
      entryPoint: "check_cpp",
    });
    expect(run.ok, run.error).toBe(true);
    expect(String((run.result as Record<string, unknown>).output)).not.toContain("MAIN_GUARD_FIRED");
  });

  it("passes args and kwargs to the call", async () => {
    const run = await callBridge(bridge, "run_python_file", {
      filePath: scriptPath,
      entryPoint: "add",
      args: ["2", "3"],
      kwargs: { suffix: "!" },
    });
    expect(run.ok, run.error).toBe(true);
    expect(String((run.result as Record<string, unknown>).result)).toContain("23!");
  });

  it("fails loudly when the named function is not there", async () => {
    const run = await callBridge(bridge, "run_python_file", {
      filePath: scriptPath,
      entryPoint: "not_a_function_here",
    });
    const failed = !run.ok || (run.result as Record<string, unknown>)?.success === false;
    expect(failed).toBe(true);
  });

  it("suppresses the log on captureLog=false but still reports its size", async () => {
    const run = await callBridge(bridge, "run_python_file", {
      filePath: scriptPath,
      entryPoint: "noisy",
      captureLog: false,
    });
    expect(run.ok, run.error).toBe(true);
    const result = run.result as Record<string, unknown>;
    expect(result.logSuppressed).toBe(true);
    expect(String(result.output)).not.toContain("NOISE_LINE");
    // The counts stay, so a caller can tell a quiet run from a suppressed one.
    expect(result.logChars as number).toBeGreaterThan(100);
  });

  it("keeps the tail under maxLogChars and marks the truncation", async () => {
    const run = await callBridge(bridge, "run_python_file", {
      filePath: scriptPath,
      entryPoint: "noisy",
      maxLogChars: 200,
    });
    expect(run.ok, run.error).toBe(true);
    const result = run.result as Record<string, unknown>;
    expect(result.logTruncated).toBe(true);
    // Whole entries are dropped from the front, so the kept text is bounded by
    // the cap plus at most the one entry that straddles it.
    expect(String(result.output).length).toBeLessThan(400);
  });
});
