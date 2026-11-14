/**
 * Native Win32 dialogs the editor process shows: reading the window scan,
 * classifying what a box means, tying it to a project, and pressing it.
 *
 * The fixtures are the scan's own output, as the PowerShell watcher prints it
 * (ConvertTo-Json), including the shapes it uses for one element and none.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import {
  NativeDialogWatcher,
  classifyNativeDialog,
  cleanLabel,
  gpuStateLines,
  interpretNativeDialog,
  nativeDialogInfo,
  nativeDialogsForProject,
  parseWatcherLine,
  pressNativeDialog,
  readGpuStateLines,
  type RawNativeDialog,
} from "../../src/editor/native-dialogs.js";

const PROJECT = path.resolve(os.tmpdir(), "NativeDlg", "Vale.uproject");
const OTHER = path.resolve(os.tmpdir(), "Other", "Other.uproject");

/** The RHI's device-lost box as the scan reads it: an icon, the text, OK. */
const GPU_BOX: RawNativeDialog = {
  hwnd: "15470330",
  pid: 26728,
  processName: "UnrealEditor",
  commandLine: `"C:\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor.exe" "${PROJECT}" -log`,
  className: "#32770",
  title: "Error",
  hung: false,
  controls: [
    { className: "Static", text: "", id: 20, visible: true, enabled: true },
    { className: "Static", text: "GPU Crashed or D3D Device Removed.\r\n\r\nCheck log for GPU state information.", id: 65535, visible: true, enabled: true },
    { className: "Button", text: "OK", id: 2, visible: true, enabled: true },
  ],
};

const ASSERT_BOX: RawNativeDialog = {
  hwnd: "200",
  pid: 26728,
  commandLine: `UnrealEditor.exe ${PROJECT}`,
  className: "#32770",
  title: "Message",
  controls: [
    { className: "Static", text: "Assertion failed: Index >= 0 [File:Array.h] [Line: 771]", id: 65535 },
    { className: "Button", text: "&Abort", id: 3 },
    { className: "Button", text: "&Retry", id: 4 },
    { className: "Button", text: "&Ignore", id: 5 },
    { className: "Button", text: "Hidden", id: 9, visible: false },
  ],
};

describe("parseWatcherLine", () => {
  it("reads a list of dialogs", () => {
    const parsed = parseWatcherLine(JSON.stringify({ dialogs: [GPU_BOX, ASSERT_BOX] }));
    expect(parsed?.map((d) => d.hwnd)).toEqual(["15470330", "200"]);
  });

  it("reads the single object ConvertTo-Json writes for a one-element list, and a lone control", () => {
    const one = { ...GPU_BOX, hwnd: 15470330, controls: GPU_BOX.controls[2] };
    const parsed = parseWatcherLine(JSON.stringify({ dialogs: one }));
    expect(parsed).toHaveLength(1);
    expect(parsed![0].hwnd).toBe("15470330");
    expect(parsed![0].controls).toHaveLength(1);
  });

  it("reads null and an empty list as no dialogs", () => {
    expect(parseWatcherLine('{"dialogs":null}')).toEqual([]);
    expect(parseWatcherLine('{"dialogs":[]}')).toEqual([]);
  });

  it("ignores anything that is not a scan line", () => {
    expect(parseWatcherLine('{"ready":true}')).toBeNull();
    expect(parseWatcherLine("WARNING: something")).toBeNull();
    expect(parseWatcherLine("{not json")).toBeNull();
  });
});

describe("interpretNativeDialog", () => {
  it("reads the GPU box: text, OK and its control id, the project, and what it means", () => {
    const d = interpretNativeDialog(GPU_BOX)!;
    expect(d.title).toBe("Error");
    expect(d.text).toEqual(["GPU Crashed or D3D Device Removed.\n\nCheck log for GPU state information."]);
    expect(d.buttons).toEqual(["OK"]);
    expect(d.buttonIds).toEqual([2]);
    expect(d.projectPath).toBe(PROJECT);
    expect(d.kind).toBe("gpu-crash");
    expect(d.summary).toMatch(/GPU device was lost/);
    expect(d.summary).toMatch(/exits when OK is pressed/);
  });

  it("strips mnemonics and skips hidden buttons", () => {
    const d = interpretNativeDialog(ASSERT_BOX)!;
    expect(d.buttons).toEqual(["Abort", "Retry", "Ignore"]);
    expect(d.kind).toBe("assert");
  });

  it("is not a dialog unless the window class says so", () => {
    expect(interpretNativeDialog({ ...GPU_BOX, className: "UnrealWindow" })).toBeNull();
  });

  it("reports no project for a process whose command line could not be read", () => {
    const d = interpretNativeDialog({ ...GPU_BOX, commandLine: undefined })!;
    expect(d.projectPath).toBeNull();
  });
});

describe("classifyNativeDialog", () => {
  it.each([
    ["Error", "GPU Crashed or D3D Device Removed.", "gpu-crash"],
    ["", "DXGI_ERROR_DEVICE_HUNG", "gpu-crash"],
    ["Message", "The following modules are missing or built with a different engine version: Foo. Would you like to rebuild them now?", "missing-modules"],
    ["Message", "Assertion failed: x", "assert"],
    ["Message", "Ensure condition failed: y", "ensure"],
    ["The UE-Vale Editor has crashed", "Fatal error", "crash"],
    ["Hello", "Just saying", "message"],
  ])("%s / %s is %s", (title, text, kind) => {
    expect(classifyNativeDialog(title, [text]).kind).toBe(kind);
  });
});

describe("cleanLabel", () => {
  it("drops the mnemonic and keeps a doubled ampersand as one", () => {
    expect(cleanLabel("&Retry")).toBe("Retry");
    expect(cleanLabel("Save && E&xit")).toBe("Save & Exit");
  });
});

describe("GPU state from the log", () => {
  const LOG = [
    "[2026.11.14-10.00.00:000][  0]LogInit: Display: Starting Game.",
    "[2026.11.14-10.01.00:000][  1]LogD3D12RHI: Error: GPU crash detected: DXGI_ERROR_DEVICE_REMOVED",
    "[2026.11.14-10.01.00:001][  1]LogD3D12RHI: Error: DRED: Last tracked GPU operations:",
    "[2026.11.14-10.01.00:002][  1]LogD3D12RHI: Error: DRED: Breadcrumb 4: BasePass",
    "[2026.11.14-10.01.00:003][  1]LogRHI: Error: Active GPU breadcrumbs:",
    "[2026.11.14-10.01.00:004][  1]LogSlate: Took 0.01 seconds",
  ].join("\r\n");

  it("keeps the device loss lines and nothing else", () => {
    const lines = gpuStateLines(LOG);
    expect(lines).toHaveLength(4);
    expect(lines[0]).toMatch(/GPU crash detected/);
    expect(lines.some((l) => /LogSlate/.test(l))).toBe(false);
  });

  it("keeps each entry's continuation lines, as a 5.8 D3D12 device loss writes them", () => {
    // The shape of a real report: the loss with its reason on the next line, the active
    // breadcrumbs, an allocation that failed on the removed device, then DRED and memory.
    const REAL = [
      "[2026.11.14-16.39.25:716][466]LogWorld: UWorld::CleanupWorld for Untitled, bSessionEnded=true",
      "[2026.11.14-16.39.25:790][468]LogD3D12RHI: Error: GPU crash detected:",
      "\t- Device 0 Removed: DXGI_ERROR_DRIVER_INTERNAL_ERROR",
      "",
      "[2026.11.14-16.39.25:791][468]LogRHI: Error: Active GPU breadcrumbs:",
      "",
      "\tDevice 0, Pipeline Graphics: (In: 0x821ccd7e, Out: 0x821ccd7d)",
      "\t\t(ID: 0x821ccd7e) [     Active]\t\t\tFRDGBuilder::SubmitBufferUploads",
      "[2026.11.14-16.39.25:792][468]LogD3D12RHI: Display: D3D12 CreateReservedResource failed with params:",
      "\tResource Width: 2147483648",
      "[2026.11.14-16.39.25:792][468]LogD3D12RHI: Error: Adapter->CreateReservedResource( Desc, ... )",
      " with error DXGI_ERROR_DEVICE_REMOVED",
      "[2026.11.14-16.39.28:801][468]LogNvidiaAftermath: Warning: Timed out while waiting for Aftermath to start the GPU crash dump.",
      "[2026.11.14-16.39.28:801][468]LogD3D12RHI: Error: DRED: No breadcrumb head found.",
      "[2026.11.14-16.39.28:801][468]LogD3D12RHI: Error: \tLocal Budget:\t9285.00 MB",
      "[2026.11.14-17.03.46:333][468]LogMCPBridge: [UE-MCP] Client connected from 127.0.0.1:54043",
    ].join("\r\n");
    const lines = gpuStateLines(REAL);
    expect(lines[0]).toMatch(/GPU crash detected/);
    expect(lines[1]).toMatch(/Device 0 Removed: DXGI_ERROR_DRIVER_INTERNAL_ERROR/);
    expect(lines).toContain("\t\t(ID: 0x821ccd7e) [     Active]\t\t\tFRDGBuilder::SubmitBufferUploads");
    expect(lines).toContain(" with error DXGI_ERROR_DEVICE_REMOVED");
    expect(lines.some((l) => /Aftermath/.test(l))).toBe(true);
    expect(lines.some((l) => /Local Budget/.test(l))).toBe(true);
    expect(lines.some((l) => /LogWorld|LogMCPBridge|CreateReservedResource failed with params/.test(l))).toBe(false);
  });

  it("reads them from Saved/Logs/<Project>.log and puts them in the details", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-native-log-"));
    const uproject = path.join(dir, "Vale.uproject");
    fs.mkdirSync(path.join(dir, "Saved", "Logs"), { recursive: true });
    fs.writeFileSync(path.join(dir, "Saved", "Logs", "Vale.log"), LOG);
    try {
      expect(readGpuStateLines(uproject)).toHaveLength(4);
      const d = interpretNativeDialog({ ...GPU_BOX, hwnd: "777", commandLine: `UnrealEditor.exe "${uproject}"` })!;
      expect(nativeDialogInfo(d).details).toHaveLength(4);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it("is empty with no log", () => {
    expect(readGpuStateLines(path.join(os.tmpdir(), "nowhere-xyz", "None.uproject"))).toEqual([]);
  });
});

describe("the watcher, through a fixture", () => {
  let dir: string;
  let fixture: string;
  const saved = { fixture: process.env.UE_MCP_NATIVE_DIALOG_FIXTURE, on: process.env.UE_MCP_NATIVE_DIALOGS };

  beforeEach(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-native-"));
    fixture = path.join(dir, "windows.json");
    process.env.UE_MCP_NATIVE_DIALOG_FIXTURE = fixture;
    delete process.env.UE_MCP_NATIVE_DIALOGS;
  });

  afterEach(() => {
    if (saved.fixture === undefined) delete process.env.UE_MCP_NATIVE_DIALOG_FIXTURE;
    else process.env.UE_MCP_NATIVE_DIALOG_FIXTURE = saved.fixture;
    if (saved.on === undefined) delete process.env.UE_MCP_NATIVE_DIALOGS;
    else process.env.UE_MCP_NATIVE_DIALOGS = saved.on;
    fs.rmSync(dir, { recursive: true, force: true });
  });

  it("lists only the dialogs of an editor that has this project open", () => {
    fs.writeFileSync(fixture, JSON.stringify([GPU_BOX, { ...ASSERT_BOX, hwnd: "300", pid: 9, commandLine: `UnrealEditor.exe ${OTHER}` }]));
    const w = new NativeDialogWatcher();
    try {
      expect(nativeDialogsForProject(PROJECT, w).map((d) => d.hwnd)).toEqual(["15470330"]);
      expect(nativeDialogsForProject(OTHER, w).map((d) => d.hwnd)).toEqual(["300"]);
      expect(nativeDialogsForProject(null, w)).toEqual([]);
    } finally {
      w.stop();
    }
  });

  it("tells subscribers when the list changes, and not when it does not", async () => {
    fs.writeFileSync(fixture, "[]");
    const w = new NativeDialogWatcher();
    const seen: number[] = [];
    w.subscribe((list) => seen.push(list.length));
    try {
      fs.writeFileSync(fixture, JSON.stringify([GPU_BOX]));
      await new Promise((r) => setTimeout(r, 500));
      fs.writeFileSync(fixture, "[]");
      await new Promise((r) => setTimeout(r, 500));
      expect(seen).toEqual([1, 0]);
    } finally {
      w.stop();
    }
  });

  it("presses the named button of that window and records it", async () => {
    fs.writeFileSync(fixture, JSON.stringify([GPU_BOX]));
    const w = new NativeDialogWatcher();
    try {
      const r = await pressNativeDialog({ hwnd: "15470330", pid: 26728, button: "ok" }, PROJECT, { watcher: w });
      expect(r).toEqual({ pressed: true, closed: true, button: "OK" });
      expect(fs.readFileSync(`${fixture}.presses.jsonl`, "utf-8").trim()).toBe(JSON.stringify({ hwnd: "15470330", pid: 26728, button: "OK" }));
      expect(JSON.parse(fs.readFileSync(fixture, "utf-8"))).toEqual([]);
    } finally {
      w.stop();
    }
  });

  it("refuses a window of another project, another process, or a button it does not have", async () => {
    fs.writeFileSync(fixture, JSON.stringify([GPU_BOX]));
    const w = new NativeDialogWatcher();
    try {
      expect((await pressNativeDialog({ hwnd: "15470330", button: "OK" }, OTHER, { watcher: w })).pressed).toBe(false);
      expect((await pressNativeDialog({ hwnd: "15470330", pid: 1, button: "OK" }, PROJECT, { watcher: w })).error).toMatch(/another process/);
      expect((await pressNativeDialog({ hwnd: "15470330", button: "Cancel" }, PROJECT, { watcher: w })).error).toMatch(/no button "Cancel"/);
      expect((await pressNativeDialog({ hwnd: "1", button: "OK" }, PROJECT, { watcher: w })).pressed).toBe(false);
      expect(fs.existsSync(`${fixture}.presses.jsonl`)).toBe(false);
    } finally {
      w.stop();
    }
  });

  it("is off when UE_MCP_NATIVE_DIALOGS says so and no fixture asks for it", () => {
    process.env.UE_MCP_NATIVE_DIALOGS = "off";
    expect(NativeDialogWatcher.enabled()).toBe(true);
    delete process.env.UE_MCP_NATIVE_DIALOG_FIXTURE;
    expect(NativeDialogWatcher.enabled()).toBe(false);
    expect(nativeDialogsForProject(PROJECT, new NativeDialogWatcher())).toEqual([]);
  });
});
