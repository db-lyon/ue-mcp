import { describe, it, expect, vi, beforeEach } from "vitest";
import * as path from "node:path";
import {
  describeProjectHolders,
  detectProjectHolders,
  contestedProject,
  rememberProjectHolders,
} from "../../src/project-holders.js";
import { editorAttribution, EDITOR_ATTRIBUTION_PREFIX } from "../../src/editor-gate.js";
import type { EditorProcess } from "../../src/engine-observer.js";

const PROJECT = path.resolve("/work/Demo/Demo.uproject");
const OTHER = path.resolve("/work/Other/Other.uproject");

function proc(pid: number, projectPath: string | null, headless = false): EditorProcess {
  return { pid, commandLine: "", projectPath, headless, responding: true, windowTitle: null };
}

beforeEach(() => {
  rememberProjectHolders(PROJECT, null);
});

describe("describeProjectHolders (#1150)", () => {
  it("is null with one editor on the project", () => {
    expect(describeProjectHolders([proc(10, PROJECT), proc(11, OTHER)], PROJECT, 10)).toBeNull();
  });

  it("lists every holder, headless included, and names the answering pid", () => {
    const holders = describeProjectHolders([proc(20, PROJECT, true), proc(10, PROJECT), proc(11, OTHER)], PROJECT, 20)!;
    expect(holders.count).toBe(2);
    expect(holders.answeringPid).toBe(20);
    expect(holders.editors).toEqual([
      { pid: 10, headless: false, answering: false },
      { pid: 20, headless: true, answering: true },
    ]);
    expect(holders.warning).toContain("pids 10, 20 (headless) (answering)");
    expect(holders.warning).toContain("connected to pid 20");
  });

  it("counts a live instance-record pid the probe could not attribute", () => {
    const holders = describeProjectHolders([proc(10, PROJECT)], PROJECT, 10, [10, 30])!;
    expect(holders.editors.map((e) => e.pid)).toEqual([10, 30]);
    expect(holders.editors[1].headless).toBeNull();
  });

  it("ignores a record pid now owned by another project's editor", () => {
    expect(describeProjectHolders([proc(10, PROJECT), proc(30, OTHER)], PROJECT, 10, [10, 30])).toBeNull();
  });

  it("says when nothing is connected", () => {
    const holders = describeProjectHolders([proc(10, PROJECT), proc(20, PROJECT)], PROJECT, null)!;
    expect(holders.answeringPid).toBeNull();
    expect(holders.warning).toContain("No editor is connected");
  });
});

describe("detectProjectHolders (#1150)", () => {
  it("skips the process probe when the records show one editor", async () => {
    const listProcesses = vi.fn();
    const result = await detectProjectHolders("/work/Demo", PROJECT, 10, {
      readRecordPids: () => [10],
      listProcesses,
    });
    expect(result).toBeNull();
    expect(listProcesses).not.toHaveBeenCalled();
    expect(contestedProject(PROJECT)).toBeNull();
  });

  it("probes when two editors published records, and remembers the answer", async () => {
    const result = await detectProjectHolders("/work/Demo", PROJECT, 20, {
      readRecordPids: () => [10, 20],
      listProcesses: async () => [proc(10, PROJECT), proc(20, PROJECT, true)],
    });
    expect(result?.count).toBe(2);
    expect(contestedProject(PROJECT)?.answeringPid).toBe(20);
  });

  it("probes when the connected pid is not the one that published a record", async () => {
    const listProcesses = vi.fn(async () => [proc(10, PROJECT), proc(20, PROJECT, true)]);
    const result = await detectProjectHolders("/work/Demo", PROJECT, 20, {
      readRecordPids: () => [10],
      listProcesses,
    });
    expect(listProcesses).toHaveBeenCalledOnce();
    expect(result?.editors.find((e) => e.answering)?.pid).toBe(20);
  });

  it("clears the remembered state once only one editor is left", async () => {
    rememberProjectHolders(PROJECT, describeProjectHolders([proc(10, PROJECT), proc(20, PROJECT)], PROJECT, 10));
    expect(contestedProject(PROJECT)).not.toBeNull();
    await detectProjectHolders("/work/Demo", PROJECT, 10, { readRecordPids: () => [10] });
    expect(contestedProject(PROJECT)).toBeNull();
  });
});

describe("response attribution carries the answering pid (#1150)", () => {
  it("stays absent at one editor on an uncontested project", () => {
    expect(editorAttribution({ name: "default", projectPath: PROJECT, pid: 10 }, 1)).toBeNull();
  });

  it("names the pid when several processes hold the project", () => {
    const line = editorAttribution({ name: "default", projectPath: PROJECT, pid: 20 }, 1, true)!;
    expect(line.startsWith(EDITOR_ATTRIBUTION_PREFIX)).toBe(true);
    expect(JSON.parse(line.slice(EDITOR_ATTRIBUTION_PREFIX.length))).toEqual({
      editor: "default",
      project: PROJECT,
      pid: 20,
    });
  });
});
