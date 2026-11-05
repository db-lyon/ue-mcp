// A crashed editor leaves CrashReportClientEditor.exe holding the project's
// module DLLs, so the next build fails at link time with LNK1104. stop_editor
// ends the reporters that belong to this project. What is pinned here is which
// ones count: never one serving a live editor, never one of another project.
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";

import {
  commandLineNamesDir,
  describeCrashReporterCleanup,
  endProjectCrashReporters,
  listProjectModuleBinaries,
  listRecordedEditorPids,
  monitoredPid,
  selectProjectCrashReporters,
  type CrashReporterDeps,
} from "../../src/editor/crash-reporter.js";
import type { CrashReporterProcess } from "../../src/editor/engine-observer.js";

const PROJECT = "C:\\Work\\Game";
const OTHER = "C:\\Work\\GameTwo";
const CRC = "C:\\UE\\Engine\\Binaries\\Win64\\CrashReportClientEditor.exe";

const reporter = (pid: number, args: string): CrashReporterProcess => ({
  pid,
  image: "CrashReportClientEditor.exe",
  commandLine: `"${CRC}" ${args}`,
});

describe("crash reporter command lines", () => {
  it("reads the watched editor pid", () => {
    expect(monitoredPid(`"${CRC}" -READ=0 -WRITE=0 -MONITOR=4242 -RequestGUID=abc`)).toBe(4242);
    expect(monitoredPid(`"${CRC}" "C:/Work/Game/Saved/Crashes/UECC-1"`)).toBeNull();
  });

  it("matches a path under the project whatever its separators and case", () => {
    expect(commandLineNamesDir(`"${CRC}" "c:/work/game/Saved/Crashes/UECC-1"`, PROJECT)).toBe(true);
    expect(commandLineNamesDir(`"${CRC}" -AbsLog=C:\\Work\\Game\\Saved\\Logs\\Game.log`, PROJECT)).toBe(true);
  });

  it("does not match a sibling project whose name starts the same way", () => {
    expect(commandLineNamesDir(`"${CRC}" "C:/Work/GameTwo/Saved/Crashes/UECC-1"`, PROJECT)).toBe(false);
  });
});

describe("which crash reporters belong to this project", () => {
  it("claims one whose command line names the project", () => {
    const matches = selectProjectCrashReporters(
      [reporter(10, `"C:/Work/Game/Saved/Crashes/UECC-1"`)],
      PROJECT,
      new Set(),
      null,
    );
    expect(matches.map((m) => m.pid)).toEqual([10]);
    expect(matches[0].reason).toContain("command line");
  });

  it("claims one holding a project binary open, naming the file", () => {
    const dll = "C:\\Work\\Game\\Plugins\\X\\Binaries\\Win64\\UnrealEditor-X.dll";
    const matches = selectProjectCrashReporters(
      [reporter(11, "-MONITOR=999")],
      PROJECT,
      new Set(),
      new Map([[11, dll]]),
    );
    expect(matches).toEqual([{ pid: 11, image: "CrashReportClientEditor.exe", reason: `it holds ${dll} open` }]);
  });

  it("leaves a reporter watching a live editor alone, even one of this project", () => {
    const matches = selectProjectCrashReporters(
      [reporter(12, `-MONITOR=500 "C:/Work/Game/Saved/Crashes/UECC-1"`)],
      PROJECT,
      new Set([500]),
      new Map([[12, "C:\\Work\\Game\\Binaries\\Win64\\UnrealEditor-Game.dll"]]),
    );
    expect(matches).toEqual([]);
  });

  it("claims one watching an editor this project recorded that has exited, with no path or binary to go on", () => {
    const matches = selectProjectCrashReporters([reporter(14, "-MONITOR=600 -RespawnedInstance")], PROJECT, new Set(), null, new Set([600]));
    expect(matches).toEqual([
      { pid: 14, image: "CrashReportClientEditor.exe", reason: "it watched editor pid 600 of this project, which has exited" },
    ]);
    // The same recorded pid still running is a live editor's companion.
    expect(selectProjectCrashReporters([reporter(14, "-MONITOR=600")], PROJECT, new Set([600]), null, new Set([600]))).toEqual([]);
    // A pid this project never recorded settles nothing by itself.
    expect(selectProjectCrashReporters([reporter(14, "-MONITOR=601")], PROJECT, new Set(), null, new Set([600]))).toEqual([]);
  });

  it("leaves another project's reporter alone", () => {
    // The holder map is built from this project's binaries only, so a reporter
    // holding another project's DLLs never appears in it.
    const matches = selectProjectCrashReporters(
      [reporter(13, `"C:/Work/GameTwo/Saved/Crashes/UECC-1"`)],
      PROJECT,
      new Set(),
      new Map(),
    );
    expect(matches).toEqual([]);
    expect(commandLineNamesDir(reporter(13, `"C:/Work/GameTwo/Saved/Crashes/UECC-1"`).commandLine, OTHER)).toBe(true);
  });
});

describe("ending this project's crash reporters", () => {
  function deps(over: Partial<CrashReporterDeps>): Partial<CrashReporterDeps> & { ended: number[] } {
    const ended: number[] = [];
    return {
      platform: "win32",
      listCrashReporters: async () => [],
      listLiveEditorPids: async () => new Set(),
      listModuleBinaries: () => [],
      findHolders: async () => new Map(),
      terminate: async (pid) => {
        ended.push(pid);
      },
      ...over,
      ended,
    };
  }

  it("ends only the matching reporter and reports it", async () => {
    const d = deps({
      listCrashReporters: async () => [
        reporter(20, `"C:/Work/Game/Saved/Crashes/UECC-1"`),
        reporter(21, `"C:/Work/GameTwo/Saved/Crashes/UECC-2"`),
        reporter(22, "-MONITOR=700"),
      ],
      listLiveEditorPids: async () => new Set([700]),
    });
    const result = await endProjectCrashReporters(PROJECT, d);
    expect(d.ended).toEqual([20]);
    expect(result.ended.map((m) => m.pid)).toEqual([20]);
    expect(result.failed).toEqual([]);
  });

  it("asks who holds the project's binaries only when the command line did not settle it", async () => {
    let asked: string[] | null = null;
    const dll = "C:\\Work\\Game\\Binaries\\Win64\\UnrealEditor-Game.dll";
    const d = deps({
      listCrashReporters: async () => [reporter(30, "-MONITOR=31")],
      listModuleBinaries: () => [dll],
      findHolders: async (files) => {
        asked = files;
        return new Map([[30, dll]]);
      },
    });
    const result = await endProjectCrashReporters(PROJECT, d);
    expect(asked).toEqual([dll]);
    expect(result.ended).toEqual([{ pid: 30, image: "CrashReportClientEditor.exe", reason: `it holds ${dll} open` }]);

    let probed = false;
    const settled = deps({
      listCrashReporters: async () => [reporter(32, `"C:/Work/Game/Saved/Crashes/UECC-3"`)],
      findHolders: async () => {
        probed = true;
        return new Map();
      },
    });
    await endProjectCrashReporters(PROJECT, settled);
    expect(probed).toBe(false);
  });

  it("ends a crashed editor's companion by the pid the project recorded, without asking who holds the binaries", async () => {
    let probed = false;
    const d = deps({
      listCrashReporters: async () => [reporter(50, "-MONITOR=51 -RespawnedInstance")],
      listProjectEditorPids: () => new Set([51]),
      findHolders: async () => {
        probed = true;
        return new Map();
      },
    });
    const result = await endProjectCrashReporters(PROJECT, d);
    expect(d.ended).toEqual([50]);
    expect(result.ended.map((m) => m.pid)).toEqual([50]);
    expect(probed).toBe(false);
  });

  it("reports a reporter it could not end instead of throwing", async () => {
    const d = deps({
      listCrashReporters: async () => [reporter(40, `"C:/Work/Game/Saved/Crashes/UECC-4"`)],
      terminate: async () => {
        throw new Error("EPERM");
      },
    });
    const result = await endProjectCrashReporters(PROJECT, d);
    expect(result.ended).toEqual([]);
    expect(result.failed).toMatchObject([{ pid: 40, error: "EPERM" }]);
    expect(describeCrashReporterCleanup(result)).toContain("Could not end");
  });

  it("reports a probe that could not run rather than claiming there was nothing", async () => {
    const d = deps({
      listCrashReporters: async () => {
        throw new Error("powershell timed out");
      },
    });
    const result = await endProjectCrashReporters(PROJECT, d);
    expect(result.probeError).toBe("powershell timed out");
    expect(d.ended).toEqual([]);
  });

  it("does nothing off Windows or without a project", async () => {
    const d = deps({
      platform: "linux",
      listCrashReporters: async () => [reporter(50, `"C:/Work/Game/Saved/Crashes/UECC-5"`)],
    });
    expect(await endProjectCrashReporters(PROJECT, d)).toEqual({ ended: [], failed: [] });
    expect(await endProjectCrashReporters(null, deps({}))).toEqual({ ended: [], failed: [] });
    expect(d.ended).toEqual([]);
  });
});

describe("the project's module binaries", () => {
  const made: string[] = [];
  afterEach(() => {
    for (const dir of made.splice(0)) fs.rmSync(dir, { recursive: true, force: true });
  });

  it("collects project and nested plugin binaries and skips everything else", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-crc-"));
    made.push(dir);
    const files = [
      "Binaries/Win64/UnrealEditor-Game.dll",
      "Binaries/Win64/UnrealEditor-Game.pdb",
      "Plugins/X/Binaries/Win64/UnrealEditor-X.dll",
      "Plugins/Group/Y/Binaries/Win64/UnrealEditor-Y.dll",
      "Plugins/X/Intermediate/Build/Win64/UnrealEditor-X.dll",
      "Plugins/X/Binaries/Win64/UnrealEditor.modules",
    ];
    for (const rel of files) {
      const full = path.join(dir, rel);
      fs.mkdirSync(path.dirname(full), { recursive: true });
      fs.writeFileSync(full, "");
    }
    const found = listProjectModuleBinaries(dir).map((f) => path.relative(dir, f).replace(/\\/g, "/")).sort();
    expect(found).toEqual([
      "Binaries/Win64/UnrealEditor-Game.dll",
      "Binaries/Win64/UnrealEditor-Game.pdb",
      "Plugins/Group/Y/Binaries/Win64/UnrealEditor-Y.dll",
      "Plugins/X/Binaries/Win64/UnrealEditor-X.dll",
    ]);
  });
});

describe("the editor pids a project recorded", () => {
  const made: string[] = [];
  afterEach(() => {
    for (const dir of made.splice(0)) fs.rmSync(dir, { recursive: true, force: true });
  });

  it("reads port.json and every instance record, whether or not the editor is still running", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-crc-"));
    made.push(dir);
    const state = path.join(dir, "Saved", "UE_MCP_Bridge");
    fs.mkdirSync(path.join(state, "instances"), { recursive: true });
    fs.writeFileSync(path.join(state, "port.json"), JSON.stringify({ port: 9877, pid: 4100 }));
    fs.writeFileSync(path.join(state, "instances", "4200.json"), JSON.stringify({ port: 9878, pid: 4200 }));
    expect([...listRecordedEditorPids(dir)].sort()).toEqual([4100, 4200]);
    expect([...listRecordedEditorPids(path.join(dir, "missing"))]).toEqual([]);
  });
});
