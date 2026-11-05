// @live-editor render
/**
 * Editor diagnostics and settings, each read back: the output log, crash
 * folders, screenshots, Python module purges, stat commands, scalability,
 * console variables, function libraries, editor tabs and PIE settings.
 * Refs #1223.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, closeEditorTabWindows, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const CRASH_FOLDER = "BCov_Editor_FakeCrash";
const MARKER = `BCovEditorMarker_${process.pid}_${Date.now()}`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

let bridge: EditorBridge;
let scratch = "";
let crashesDir = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function cvar(name: string): Promise<string> {
  const read = await call("get_cvars", { name });
  return String((read.cvars as Json[])[0].value);
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "bcov-editor-"));
}, 60_000);

afterAll(async () => {
  try {
    if (crashesDir) fs.rmSync(path.join(crashesDir, CRASH_FOLDER), { recursive: true, force: true });
    if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
  } finally {
    if (bridge) {
      await closeEditorTabWindows(bridge);
      disconnectBridge();
    }
  }
}, 60_000);

describe("the output log", () => {
  it("get_output_log and search_log find a line a Python script just logged", async () => {
    await call("execute_python", { code: `import unreal\nunreal.log("${MARKER}")` });

    const lines = await call("get_output_log", { filter: MARKER });
    const rows = lines.lines as Array<{ message: string; category: string }>;
    expect(rows.length).toBeGreaterThan(0);
    expect(rows[0].message).toContain(MARKER);
    expect(rows[0].category.toLowerCase()).toContain("python");

    const found = await call("search_log", { query: MARKER });
    expect(Number(found.matchCount)).toBeGreaterThan(0);
    expect(JSON.stringify(found)).toContain(MARKER);

    const none = await call("search_log", { query: `${MARKER}_never_logged` });
    expect(none.matchCount).toBe(0);
  }, 60_000);
});

describe("Python modules", () => {
  it("purge_python_modules drops a module from sys.modules", async () => {
    const mod = `bcov_editor_probe_${process.pid}`;
    await call("execute_python", { code: `import sys, types\nsys.modules["${mod}"] = types.ModuleType("${mod}")` });
    const purged = await call("purge_python_modules", { prefix: mod });
    expect(purged.purged as string[]).toContain(mod);
    expect(purged.count).toBe(1);

    const after = await call("execute_python", { code: `import sys\nbcov_present = "${mod}" in sys.modules`, resultVariable: "bcov_present" });
    expect(String(after.result)).toBe("False");
  }, 60_000);
});

describe("crash folders", () => {
  it("list_crashes, get_crash_info and check_for_crashes read a crash folder written under Saved/Crashes", async () => {
    const first = await call("list_crashes", {});
    crashesDir = String(first.crashesDir);
    expect(crashesDir).toMatch(/Crashes$/);

    const folder = path.join(crashesDir, CRASH_FOLDER);
    fs.mkdirSync(folder, { recursive: true });
    fs.writeFileSync(path.join(folder, "BCovCrash.log"), `${MARKER} fatal error\n`);

    const listed = await call("list_crashes", { limit: 500 });
    const entry = (listed.crashes as Array<{ folder: string; files: string[] }>).find((c) => c.folder === CRASH_FOLDER);
    expect(entry?.files).toContain("BCovCrash.log");

    const info = await call("get_crash_info", { crashFolder: CRASH_FOLDER });
    const file = (info.files as Record<string, { content?: string; size?: number }>)["BCovCrash.log"];
    expect(file.content).toContain(MARKER);

    const recent = await call("check_for_crashes", {});
    expect((recent.recentCrashes as Array<{ folder: string }>).map((c) => c.folder)).toContain(CRASH_FOLDER);

    fs.rmSync(folder, { recursive: true, force: true });
    const missing = await call("get_crash_info", { crashFolder: CRASH_FOLDER });
    expect(missing.available).toBe(false);
  }, 60_000);
});

describe("screenshots", () => {
  it("capture_screenshot with target=window writes a PNG to the given path", async () => {
    const file = path.join(scratch, "bcov-editor-window.png");
    const shot = await call("capture_screenshot", { filename: file, target: "window" });
    expect(shot.target).toBe("window");
    expect(Number(shot.width)).toBeGreaterThan(0);
    expect(fs.existsSync(String(shot.filename))).toBe(true);
    expect(fs.statSync(String(shot.filename)).size).toBeGreaterThan(100);
  }, 60_000);
});

describe("performance readouts", () => {
  it("get_editor_performance_stats and get_frame_timing report a live process and an advancing frame", async () => {
    const stats = await call("get_editor_performance_stats", {});
    expect(Number((stats.memory as Json).usedPhysical)).toBeGreaterThan(0);

    const a = await call("get_frame_timing", {});
    let b = await call("get_frame_timing", {});
    for (let i = 0; i < 100 && Number(b.frame) <= Number(a.frame); i++) {
      await sleep(50);
      b = await call("get_frame_timing", {});
    }
    expect(Number(b.frame)).toBeGreaterThan(Number(a.frame));
    expect(Number((a.timings as Json).frameMs)).toBeGreaterThan(0);
    expect(typeof a.bound).toBe("string");
  }, 60_000);

  it("run_stat_command turns a stat overlay on the level viewport on, and stat none clears it", async () => {
    // stat startfile writes nothing on newer engines (Insights replaced the stats file), so the overlay is the effect.
    await call("run_stat_command", { command: "stat none" });
    const on = await call("run_stat_command", { command: "stat fps" });
    expect(on.statsTurnedOn as string[]).toContain("fps");
    expect(on.enabledStats as string[]).toContain("fps");
    const off = await call("run_stat_command", { command: "stat none" });
    expect(off.statsTurnedOff as string[]).toContain("fps");
    expect(off.enabledStats as string[]).toHaveLength(0);
  }, 60_000);
});

describe("scalability and console variables", () => {
  it("set_scalability writes every sg.* group, as get_cvars reads them", async () => {
    const before = await cvar("sg.ShadowQuality");
    const low = await call("set_scalability", { level: "Low" });
    try {
      expect(low.qualityLevel).toBe(0);
      expect(await cvar("sg.ShadowQuality")).toBe("0");
      expect(await cvar("sg.TextureQuality")).toBe("0");
    } finally {
      const previous = low.previousLevel as string | undefined;
      if (previous) await call("set_scalability", { level: previous });
      else await call("set_cvars", { cvars: { "sg.ShadowQuality": before } });
    }
    if (low.previousLevel) expect(await cvar("sg.ShadowQuality")).toBe(before);
  }, 60_000);

  it("set_cvars writes a console variable get_cvars reads back, and its rollback restores it", async () => {
    const before = await cvar("t.MaxFPS");
    const set = await call("set_cvars", { cvars: [{ name: "t.MaxFPS", value: "77" }] });
    try {
      expect((set.applied as Json[])[0].oldValue).toBe(before);
      expect(Number(await cvar("t.MaxFPS"))).toBe(77);
    } finally {
      const rollback = set.rollback as { method: string; payload: Json };
      await call(rollback.method, rollback.payload);
    }
    expect(await cvar("t.MaxFPS")).toBe(before);
  }, 60_000);
});

describe("function libraries", () => {
  it("list_function_libraries lists KismetMathLibrary with a function invoke_static_function can call", async () => {
    const listed = await call("list_function_libraries", { pattern: "KismetMathLibrary" });
    const lib = (listed.libraries as Array<{ name: string; functions: Array<{ name: string }> }>).find((l) => l.name === "KismetMathLibrary");
    expect(lib).toBeDefined();
    const names = lib!.functions.map((f) => f.name);
    expect(names).toContain("Abs");

    const abs = await call("invoke_static_function", { className: "KismetMathLibrary", functionName: "Abs", args: { A: -3.5 } });
    expect(Number((abs.returnValues as Json).ReturnValue)).toBe(3.5);
  }, 60_000);
});

describe("editor tabs", () => {
  it("open_tab opens a registered tab that a second call finds already open", async () => {
    const first = await call("open_tab", { tabId: "OutputLog" });
    expect(first.opened).toBe(true);
    const again = await call("open_tab", { tabId: "OutputLog" });
    expect(again.alreadyOpen).toBe(true);
    expect(again.changed).toBe(false);
  }, 60_000);

  it("open_settings opens the Project Settings viewer that a second call finds already open", async () => {
    const opened = await call("open_settings", { container: "Project", category: "Engine", section: "Physics" });
    expect(opened.category).toBe("Engine");
    expect(opened.section).toBe("Physics");
    const again = await call("open_settings", { container: "Project" });
    expect(again.alreadyOpen).toBe(true);
    expect(again.changed).toBe(false);
  }, 60_000);
});

describe("build status and handler reload", () => {
  it("get_build_status reports idle and reload_handlers leaves the registry answering", async () => {
    expect((await call("get_build_status", {})).status).toBe("idle");
    const reloaded = await call("reload_handlers", {});
    expect(reloaded.unchanged).toBe(true);
    expect((await call("get_build_status", {})).status).toBe("idle");
  }, 60_000);
});

describe("PIE settings", () => {
  it("configure_pie writes the client count get_pie_config reads, and its rollback restores it", async () => {
    const before = await call("get_pie_config", {});
    const wanted = Number(before.numClients) === 2 ? 3 : 2;
    const set = await call("configure_pie", { numClients: wanted });
    try {
      expect(set.previousNumClients).toBe(before.numClients);
      expect((await call("get_pie_config", {})).numClients).toBe(wanted);
    } finally {
      const rollback = set.rollback as { method: string; payload: Json };
      await call(rollback.method, rollback.payload);
    }
    const after = await call("get_pie_config", {});
    expect(after.numClients).toBe(before.numClients);
    expect(after.netMode).toBe(before.netMode);
  }, 60_000);
});
