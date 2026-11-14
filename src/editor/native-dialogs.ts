/**
 * Native Win32 dialogs the editor process shows, seen and answered from outside it.
 *
 * Some prompts never reach Slate: the RHI's "GPU Crashed or D3D Device
 * Removed" box, an assert or ensure box, "modules are missing, rebuild them
 * now?" before the plugin loads. Each is a plain `#32770` window owned by the
 * editor process, raised with the game thread (or the thread that blocks it)
 * inside MessageBox, so the bridge cannot answer while one is up and its own
 * list_dialogs never sees it.
 *
 * This file reads them from outside: one long-lived PowerShell process
 * compiles a small interop type once and scans the editor processes' top-level
 * windows about once a second, reporting each native dialog with its title,
 * its text and its buttons (read from the dialog's own child controls). A
 * press is aimed at one window by handle, re-verified against the process id
 * and window class, and sent as WM_COMMAND with the named button's control id
 * to that window, never as focus or keystrokes.
 *
 * UE_MCP_NATIVE_DIALOGS=off turns it off. UE_MCP_NATIVE_DIALOG_FIXTURE names a
 * JSON file that stands in for the scan, on any platform, for tests.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { spawn, execFile, type ChildProcess } from "node:child_process";
import { promisify } from "node:util";
import * as log from "../core/log.js";
import { readEnv } from "../core/env.js";
import { extractProjectPath, sameProjectFile } from "./engine-observer.js";

const execFileAsync = promisify(execFile);

/** One child control of a dialog, as the scan read it. */
export interface NativeControl {
  className: string;
  text: string;
  /** GetDlgCtrlID: what WM_COMMAND names a button by. */
  id: number;
  visible?: boolean;
  enabled?: boolean;
}

/** A top-level dialog window, as the scan reports it before any interpretation. */
export interface RawNativeDialog {
  /** The window handle, as a decimal string. */
  hwnd: string;
  pid: number;
  processName?: string;
  commandLine?: string;
  className: string;
  title: string;
  hung?: boolean;
  controls: NativeControl[];
}

export type NativeDialogKind = "gpu-crash" | "assert" | "ensure" | "missing-modules" | "crash" | "message";

/** A native dialog, read and classified. */
export interface NativeDialog {
  hwnd: string;
  pid: number;
  processName: string | null;
  /** The .uproject the owning process has open, from its command line. */
  projectPath: string | null;
  title: string;
  /** The dialog's message, from its static text controls, in window order. */
  text: string[];
  /** Button labels in window order, mnemonic ampersands removed. */
  buttons: string[];
  /** Each button's control id, the same order as `buttons`. */
  buttonIds: number[];
  hung: boolean;
  kind: NativeDialogKind;
  /** One plain sentence on what this dialog means and what answering it does. */
  summary: string;
}

/** What a dialog event and a refusal carry about a native dialog. */
export interface NativeDialogInfo {
  hwnd: string;
  pid: number;
  kind: NativeDialogKind;
  summary: string;
  /** Lines from the editor log that explain it, such as the GPU state after a device loss. */
  details?: string[];
}

/** "&Retry" -> "Retry", "Save && Exit" -> "Save & Exit". */
export function cleanLabel(label: string): string {
  return label.replace(/&(&?)/g, (_m, keep: string) => keep).trim();
}

const GPU_RE = /GPU Crashed|D3D Device Removed|DXGI_ERROR_DEVICE_(?:REMOVED|HUNG|RESET)|device (?:was )?(?:removed|lost)|GPU (?:has )?(?:crashed|hung)/i;

/** What kind of prompt this is, and one sentence a person can act on. */
export function classifyNativeDialog(title: string, text: string[]): { kind: NativeDialogKind; summary: string } {
  const all = [title, ...text].join("\n");
  if (GPU_RE.test(all)) {
    return {
      kind: "gpu-crash",
      summary:
        "The editor's GPU device was lost (the GPU crashed or the D3D device was removed). Unreal cannot recover "
        + "from this: the editor exits when OK is pressed, and it has to be started again.",
    };
  }
  if (/missing or built with a different engine version|modules are missing|rebuild them now/i.test(all)) {
    return {
      kind: "missing-modules",
      summary: "The editor is asking whether to rebuild modules that are missing or out of date before it can open the project.",
    };
  }
  if (/Assertion failed/i.test(all)) {
    return { kind: "assert", summary: "An assertion failed inside the editor. It is stopped on this box, and usually exits once it is answered." };
  }
  if (/Ensure condition failed|ensure\(/i.test(all)) {
    return { kind: "ensure", summary: "An ensure failed inside the editor. It is stopped on this box until it is answered." };
  }
  if (/fatal error|crash/i.test(all)) {
    return { kind: "crash", summary: "The editor reported a fatal error and is stopped on this box. It usually exits once it is answered." };
  }
  return { kind: "message", summary: "A Windows message box from the editor is waiting for an answer, and the editor is stopped until it gets one." };
}

/** A scanned window, read: text, buttons, project and kind. Null for anything that is not a dialog with content. */
export function interpretNativeDialog(raw: RawNativeDialog): NativeDialog | null {
  if (!raw || typeof raw.hwnd !== "string" || typeof raw.pid !== "number") return null;
  if (raw.className !== "#32770") return null;
  const controls = Array.isArray(raw.controls) ? raw.controls : [];
  const buttons: string[] = [];
  const buttonIds: number[] = [];
  const text: string[] = [];
  for (const c of controls) {
    if (!c || typeof c.className !== "string") continue;
    if (c.visible === false) continue;
    const value = typeof c.text === "string" ? c.text : "";
    if (/^button$/i.test(c.className)) {
      const label = cleanLabel(value);
      if (label !== "") {
        buttons.push(label);
        buttonIds.push(typeof c.id === "number" ? c.id : 0);
      }
    } else if (/^static$/i.test(c.className) || /^edit$/i.test(c.className) || /^richedit/i.test(c.className)) {
      const t = value.replace(/\r\n/g, "\n").trim();
      if (t !== "") text.push(t);
    }
  }
  const title = typeof raw.title === "string" ? raw.title : "";
  if (title === "" && text.length === 0 && buttons.length === 0) return null;
  const { kind, summary } = classifyNativeDialog(title, text);
  return {
    hwnd: raw.hwnd,
    pid: raw.pid,
    processName: typeof raw.processName === "string" ? raw.processName : null,
    projectPath: typeof raw.commandLine === "string" ? extractProjectPath(raw.commandLine) : null,
    title,
    text,
    buttons,
    buttonIds,
    hung: raw.hung === true,
    kind,
    summary,
  };
}

/** One line of watcher output: `{"dialogs":[...]}`. Null for anything else, so noise on stdout is ignored. */
export function parseWatcherLine(line: string): RawNativeDialog[] | null {
  const trimmed = line.trim();
  if (!trimmed.startsWith("{")) return null;
  try {
    const parsed = JSON.parse(trimmed) as { dialogs?: unknown };
    if (!parsed || !("dialogs" in parsed)) return null;
    const d = parsed.dialogs;
    // ConvertTo-Json writes a one-element array as the element and an empty one as null.
    if (d === null || d === undefined) return [];
    const list = Array.isArray(d) ? d : [d];
    return list.map((x) => {
      const r = x as RawNativeDialog & { controls?: unknown };
      const controls = r.controls === null || r.controls === undefined ? [] : Array.isArray(r.controls) ? r.controls : [r.controls];
      return { ...r, hwnd: String(r.hwnd), controls: controls as NativeControl[] };
    });
  } catch {
    return null;
  }
}

/** Where a log entry begins: `[2026.11.14-16.39.25:790][468]`. Lines without it continue the entry above. */
const LOG_ENTRY = /^\[\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3}\]/;
/** The entry the RHI writes when it finds the device gone. */
const GPU_LOSS = /GPU crash detected|GPU hang detected|Device \d* ?Removed|DXGI_ERROR_DEVICE_(?:REMOVED|HUNG|RESET)|VK_ERROR_DEVICE_LOST/i;
/** The entries that describe the GPU's state after it: the RHI's errors, DRED, Aftermath, breadcrumbs. */
const GPU_STATE = /LogD3D1[12]RHI: Error|LogRHI: Error|LogVulkanRHI: Error|LogNvidiaAftermath|LogAMD\w*|DRED|Breadcrumb|PageFault|GPU crash|GPU hang/i;

/**
 * The GPU's state at a device loss, from the editor log: the RHI's report of
 * the loss and the entries after it (the removal reason, active breadcrumbs,
 * DRED, Aftermath, video memory), each with its continuation lines. Starts at
 * the last loss in the text; empty when there is none.
 */
export function gpuStateLines(logText: string, limit = 60): string[] {
  const entries: string[][] = [];
  for (const line of logText.split(/\r?\n/)) {
    if (LOG_ENTRY.test(line) || entries.length === 0) entries.push([line]);
    else entries[entries.length - 1].push(line);
  }
  // The RHI's own report opens the section; a later call that failed on the
  // removed device also names the loss, so it is the fallback, not the start.
  const lastWhere = (re: RegExp): number => {
    for (let i = entries.length - 1; i >= 0; i--) if (re.test(entries[i].join("\n"))) return i;
    return -1;
  };
  const reported = lastWhere(/GPU crash detected|GPU hang detected/i);
  const start = reported >= 0 ? reported : lastWhere(GPU_LOSS);
  if (start < 0) return [];
  const out: string[] = [];
  for (const entry of entries.slice(start)) {
    if (!GPU_STATE.test(entry[0]) && !GPU_LOSS.test(entry.join("\n"))) continue;
    for (const line of entry) if (line.trim() !== "") out.push(line.trimEnd());
    if (out.length >= limit) break;
  }
  return out.slice(0, limit);
}

/** The GPU state lines from the end of the project's log, or [] when there are none. */
export function readGpuStateLines(projectPath: string | null): string[] {
  if (!projectPath) return [];
  const file = path.join(path.dirname(projectPath), "Saved", "Logs", `${path.basename(projectPath, path.extname(projectPath))}.log`);
  try {
    const size = fs.statSync(file).size;
    const start = Math.max(0, size - 256 * 1024);
    const buf = Buffer.alloc(size - start);
    const fd = fs.openSync(file, "r");
    try {
      fs.readSync(fd, buf, 0, buf.length, start);
    } finally {
      fs.closeSync(fd);
    }
    return gpuStateLines(buf.toString("utf-8"));
  } catch {
    return [];
  }
}

// ---------------------------------------------------------------------------
// The scan
// ---------------------------------------------------------------------------

const INTEROP = `
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class UeMcpCtl { public string className { get; set; } public string text { get; set; } public int id { get; set; } public bool visible { get; set; } public bool enabled { get; set; } }
public class UeMcpDlg { public string hwnd { get; set; } public int pid { get; set; } public string processName { get; set; } public string commandLine { get; set; } public string className { get; set; } public string title { get; set; } public bool hung { get; set; } public List<UeMcpCtl> controls { get; set; } }
public static class UeMcpNative {
  public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int InternalGetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsHungAppWindow(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public static string Text(IntPtr h) { StringBuilder sb = new StringBuilder(4096); InternalGetWindowText(h, sb, sb.Capacity); return sb.ToString(); }
  public static string Cls(IntPtr h) { StringBuilder sb = new StringBuilder(256); GetClassNameW(h, sb, sb.Capacity); return sb.ToString(); }
  static List<UeMcpCtl> Controls(IntPtr dlg) {
    List<UeMcpCtl> list = new List<UeMcpCtl>();
    EnumChildWindows(dlg, delegate(IntPtr c, IntPtr l) {
      UeMcpCtl x = new UeMcpCtl();
      x.className = Cls(c); x.text = Text(c); x.id = GetDlgCtrlID(c); x.visible = IsWindowVisible(c); x.enabled = IsWindowEnabled(c);
      list.Add(x);
      return true;
    }, IntPtr.Zero);
    return list;
  }
  public static List<UeMcpDlg> Scan(Dictionary<int, string> pids) {
    List<UeMcpDlg> found = new List<UeMcpDlg>();
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (!pids.ContainsKey((int)pid) || !IsWindowVisible(h)) return true;
      string cls = Cls(h);
      if (cls != "#32770") return true;
      UeMcpDlg d = new UeMcpDlg();
      d.hwnd = h.ToInt64().ToString(); d.pid = (int)pid; d.processName = pids[(int)pid]; d.className = cls; d.title = Text(h); d.hung = IsHungAppWindow(h); d.controls = Controls(h);
      found.Add(d);
      return true;
    }, IntPtr.Zero);
    return found;
  }
  // 0 pressed and closed, 1 pressed and still open, 2 no such window, 3 another process, 4 not a dialog, 5 no such button, 6 the button is disabled
  public static int Press(long hwnd, int pid, string label, int waitMs) {
    IntPtr h = new IntPtr(hwnd);
    if (!IsWindow(h)) return 2;
    uint owner; GetWindowThreadProcessId(h, out owner);
    if ((int)owner != pid) return 3;
    if (Cls(h) != "#32770") return 4;
    IntPtr target = IntPtr.Zero; int id = 0; bool enabled = false;
    List<IntPtr> kids = new List<IntPtr>();
    EnumChildWindows(h, delegate(IntPtr c, IntPtr l) { kids.Add(c); return true; }, IntPtr.Zero);
    foreach (IntPtr c in kids) {
      if (!string.Equals(Cls(c), "Button", StringComparison.OrdinalIgnoreCase) || !IsWindowVisible(c)) continue;
      string t = Text(c).Replace("&&", "\\u0001").Replace("&", "").Replace("\\u0001", "&").Trim();
      if (string.Equals(t, label, StringComparison.OrdinalIgnoreCase)) { target = c; id = GetDlgCtrlID(c); enabled = IsWindowEnabled(c); break; }
    }
    if (target == IntPtr.Zero) return 5;
    if (!enabled) return 6;
    // WM_COMMAND, BN_CLICKED in the high word, to the dialog: what the dialog procedure ends on.
    PostMessageW(h, 0x0111, new IntPtr(id & 0xFFFF), target);
    int waited = 0;
    while (waited < waitMs) {
      System.Threading.Thread.Sleep(100); waited += 100;
      if (!IsWindow(h) || !IsWindowVisible(h)) return 0;
    }
    return 1;
  }
}
`;

/** The processes whose windows are scanned: the editor binaries and the crash reporter. */
export const EDITOR_PROCESS_NAMES: readonly string[] = ["UnrealEditor", "UnrealEditor-Cmd", "UE4Editor", "UE4Editor-Cmd", "UE5Editor", "CrashReportClient", "CrashReportClientEditor"];

/** The long-lived scan: one line of JSON when the dialogs change, and every 10 s regardless. Exits with its parent. */
function watchScript(parentPid: number, intervalMs: number, names: readonly string[]): string {
  const NAMES = names.filter((n) => /^[A-Za-z0-9_.-]+$/.test(n));
  return `
$ErrorActionPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -TypeDefinition @'
${INTEROP}
'@ -Language CSharp | Out-Null
$names = @(${NAMES.map((n) => `'${n.toLowerCase()}'`).join(",")})
$cmd = @{}
$last = ''
$beat = [DateTime]::UtcNow
[Console]::Out.WriteLine('{"ready":true}')
[Console]::Out.Flush()
while ($true) {
  try { [void][System.Diagnostics.Process]::GetProcessById(${Math.trunc(parentPid)}) } catch { exit 0 }
  $pids = New-Object 'System.Collections.Generic.Dictionary[int,string]'
  foreach ($p in [System.Diagnostics.Process]::GetProcesses()) {
    if ($names -contains $p.ProcessName.ToLowerInvariant()) { $pids[$p.Id] = $p.ProcessName }
  }
  $found = @()
  if ($pids.Count -gt 0) { $found = @([UeMcpNative]::Scan($pids)) }
  foreach ($d in $found) {
    if (-not $cmd.ContainsKey($d.pid)) {
      $cmd[$d.pid] = (Get-CimInstance Win32_Process -Filter ("ProcessId=" + $d.pid)).CommandLine
    }
    $d.commandLine = $cmd[$d.pid]
  }
  $json = ConvertTo-Json -InputObject @{ dialogs = @($found) } -Compress -Depth 6
  $now = [DateTime]::UtcNow
  if ($json -ne $last -or ($now - $beat).TotalSeconds -ge 10) {
    [Console]::Out.WriteLine($json)
    [Console]::Out.Flush()
    $last = $json
    $beat = $now
  }
  Start-Sleep -Milliseconds ${Math.trunc(intervalMs)}
}
`;
}

function pressScript(hwnd: string, pid: number, label: string, waitMs: number): string {
  // The label goes in as base64 so no character in it can end the string.
  const b64 = Buffer.from(label, "utf-8").toString("base64");
  return `
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
${INTEROP}
'@ -Language CSharp | Out-Null
$label = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String('${b64}'))
[Console]::Out.WriteLine([UeMcpNative]::Press([long]${BigInt(hwnd).toString()}, ${Math.trunc(pid)}, $label, ${Math.trunc(waitMs)}))
`;
}

function encoded(script: string): string {
  return Buffer.from(script, "utf16le").toString("base64");
}

type Listener = (dialogs: NativeDialog[]) => void;

/**
 * Every native dialog any editor process shows, kept current by one scan.
 * Started on first use; it never holds the process open.
 */
export class NativeDialogWatcher {
  private dialogs: NativeDialog[] = [];
  private readonly listeners = new Set<Listener>();
  private child: ChildProcess | null = null;
  private fixtureTimer: NodeJS.Timeout | null = null;
  private started = false;
  private restarts = 0;
  private buffer = "";
  private signature = "[]";

  private readonly intervalMs: number;
  private readonly processNames: readonly string[];

  constructor(opts: { intervalMs?: number; processNames?: readonly string[] } = {}) {
    this.intervalMs = opts.intervalMs ?? 750;
    this.processNames = opts.processNames ?? EDITOR_PROCESS_NAMES;
  }

  /** Whether this machine can scan at all, or a fixture stands in. */
  static enabled(): boolean {
    // A fixture is a test asking for exactly this, so it wins over off.
    if (readEnv("nativeDialogFixture")) return true;
    const off = (readEnv("nativeDialogs") ?? "").toLowerCase();
    if (off === "off" || off === "0" || off === "false") return false;
    return process.platform === "win32";
  }

  /** The dialogs as last scanned. Starts the scan on first call. */
  list(): NativeDialog[] {
    this.ensureStarted();
    return this.dialogs;
  }

  /** Called with the list whenever it changes. Returns the unsubscribe. */
  subscribe(listener: Listener): () => void {
    this.ensureStarted();
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  ensureStarted(): void {
    if (this.started || !NativeDialogWatcher.enabled()) return;
    this.started = true;
    const fixture = readEnv("nativeDialogFixture");
    if (fixture) this.watchFixture(fixture);
    else this.spawnScan();
  }

  stop(): void {
    this.started = false;
    if (this.fixtureTimer) clearInterval(this.fixtureTimer);
    this.fixtureTimer = null;
    const child = this.child;
    this.child = null;
    try {
      child?.kill();
    } catch {
      // Already gone.
    }
  }

  /** Drop one window a press saw close, ahead of the next scan. */
  forget(hwnd: string): void {
    const raw = this.dialogs.filter((d) => d.hwnd !== hwnd);
    if (raw.length === this.dialogs.length) return;
    this.dialogs = raw;
    this.signature = JSON.stringify(raw);
    for (const l of [...this.listeners]) {
      try {
        l(raw);
      } catch (e) {
        log.debug("native-dialogs", "listener threw", e);
      }
    }
  }

  /** Replace the list, for the scan and for tests. */
  update(raw: RawNativeDialog[]): void {
    const next = raw.map(interpretNativeDialog).filter((d): d is NativeDialog => d !== null);
    const signature = JSON.stringify(next);
    if (signature === this.signature) return;
    this.signature = signature;
    this.dialogs = next;
    for (const l of [...this.listeners]) {
      try {
        l(next);
      } catch (e) {
        log.debug("native-dialogs", "listener threw", e);
      }
    }
  }

  private watchFixture(file: string): void {
    const read = () => {
      try {
        const text = fs.readFileSync(file, "utf-8");
        const parsed = parseWatcherLine(JSON.stringify({ dialogs: JSON.parse(text) }));
        if (parsed) this.update(parsed);
      } catch {
        this.update([]);
      }
    };
    read();
    this.fixtureTimer = setInterval(read, 200);
    this.fixtureTimer.unref?.();
  }

  private spawnScan(): void {
    let child: ChildProcess;
    try {
      child = spawn(
        "powershell.exe",
        ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-EncodedCommand", encoded(watchScript(process.pid, this.intervalMs, this.processNames))],
        { windowsHide: true, stdio: ["ignore", "pipe", "ignore"] },
      );
    } catch (e) {
      log.debug("native-dialogs", "could not start the window scan", e);
      return;
    }
    this.child = child;
    child.unref();
    (child.stdout as unknown as { unref?: () => void })?.unref?.();
    child.stdout?.setEncoding("utf-8");
    child.stdout?.on("data", (chunk: string) => {
      this.buffer += chunk;
      let nl: number;
      while ((nl = this.buffer.indexOf("\n")) >= 0) {
        const line = this.buffer.slice(0, nl);
        this.buffer = this.buffer.slice(nl + 1);
        const parsed = parseWatcherLine(line);
        if (parsed) {
          this.restarts = 0;
          this.update(parsed);
        }
      }
    });
    child.on("error", (e) => log.debug("native-dialogs", "window scan failed", e));
    child.on("exit", () => {
      if (this.child !== child) return;
      this.child = null;
      this.buffer = "";
      this.update([]);
      if (!this.started || this.restarts >= 5) return;
      this.restarts++;
      const t = setTimeout(() => {
        if (this.started && !this.child) this.spawnScan();
      }, 2000 * this.restarts);
      t.unref?.();
    });
  }
}

let shared: NativeDialogWatcher | null = null;

/** The one watcher this process runs. */
export function nativeDialogWatcher(): NativeDialogWatcher {
  if (!shared) {
    shared = new NativeDialogWatcher();
    process.once("exit", () => shared?.stop());
  }
  return shared;
}

/** Test seam: drop the shared watcher. */
export function resetNativeDialogWatcher(): void {
  shared?.stop();
  shared = null;
}

/**
 * The native dialogs shown by an editor that has this project open.
 *
 * The .uproject on the owning process's command line is what ties a dialog to
 * a project, the same rule every lifecycle action follows. A process whose
 * command line could not be read is never counted as this project's.
 */
export function nativeDialogsForProject(projectPath: string | null | undefined, watcher = nativeDialogWatcher()): NativeDialog[] {
  if (!projectPath || !NativeDialogWatcher.enabled()) return [];
  return watcher.list().filter((d) => d.projectPath !== null && sameProjectFile(d.projectPath, projectPath));
}

const gpuDetails = new Map<string, { at: number; lines: string[] }>();

/** What an event and a refusal carry, with the GPU state from the log for a device loss. */
export function nativeDialogInfo(d: NativeDialog): NativeDialogInfo {
  const info: NativeDialogInfo = { hwnd: d.hwnd, pid: d.pid, kind: d.kind, summary: d.summary };
  if (d.kind === "gpu-crash") {
    const cached = gpuDetails.get(d.hwnd);
    // Read again for a few seconds: the RHI may still be writing its report as the box goes up.
    const lines = cached && Date.now() - cached.at > 5000 ? cached.lines : readGpuStateLines(d.projectPath);
    if (!cached || Date.now() - cached.at <= 5000) gpuDetails.set(d.hwnd, { at: cached?.at ?? Date.now(), lines });
    if (lines.length > 0) info.details = lines;
  }
  return info;
}

export interface NativePressResult {
  /** The press went to the window. */
  pressed: boolean;
  /** The window was gone within the wait after the press. */
  closed: boolean;
  button?: string;
  error?: string;
}

const PRESS_CODES: Record<number, NativePressResult> = {
  0: { pressed: true, closed: true },
  1: { pressed: true, closed: false },
  2: { pressed: false, closed: true, error: "That dialog is no longer open." },
  3: { pressed: false, closed: false, error: "That window now belongs to another process, so nothing was pressed." },
  4: { pressed: false, closed: false, error: "That window is not a dialog, so nothing was pressed." },
  5: { pressed: false, closed: false, error: "The dialog has no such button." },
  6: { pressed: false, closed: false, error: "That button is disabled." },
};

/**
 * Press one named button on one native dialog of this project's editor.
 *
 * The dialog has to be one the scan currently lists for this project, by
 * handle and process id, and offer the button; the press script checks the
 * handle, the owning process and the window class again at the moment it
 * sends, so a handle reused by another window is never pressed.
 */
export async function pressNativeDialog(
  target: { hwnd: string; pid?: number; button: string },
  projectPath: string | null | undefined,
  opts: { watcher?: NativeDialogWatcher; waitMs?: number } = {},
): Promise<NativePressResult> {
  const watcher = opts.watcher ?? nativeDialogWatcher();
  const listed = nativeDialogsForProject(projectPath, watcher).find((d) => d.hwnd === String(target.hwnd));
  if (!listed) return { pressed: false, closed: false, error: "No native dialog with that handle is open in this project's editor." };
  if (target.pid !== undefined && target.pid !== listed.pid) {
    return { pressed: false, closed: false, error: "That dialog belongs to another process than the one named, so nothing was pressed." };
  }
  const wanted = cleanLabel(target.button);
  const button = listed.buttons.find((b) => b.toLowerCase() === wanted.toLowerCase());
  if (!button) {
    return { pressed: false, closed: false, error: `The dialog has no button "${target.button}". Its buttons are ${listed.buttons.map((b) => `"${b}"`).join(", ") || "none Windows can read"}.` };
  }
  const fixture = readEnv("nativeDialogFixture");
  if (fixture) {
    const r = pressFixture(fixture, listed, button);
    if (r.closed) watcher.forget(listed.hwnd);
    return { ...r, button };
  }
  if (process.platform !== "win32") return { pressed: false, closed: false, error: "Native dialogs can only be pressed on Windows." };
  try {
    const { stdout } = await execFileAsync(
      "powershell.exe",
      ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-EncodedCommand", encoded(pressScript(listed.hwnd, listed.pid, button, opts.waitMs ?? 5000))],
      { timeout: 30_000, windowsHide: true },
    );
    const code = Number(stdout.trim().split(/\r?\n/).pop());
    const result = PRESS_CODES[code] ?? { pressed: false, closed: false, error: `The press script answered ${JSON.stringify(stdout.trim())}.` };
    if (result.closed) watcher.forget(listed.hwnd);
    return { ...result, button };
  } catch (e) {
    return { pressed: false, closed: false, button, error: `The press could not be sent: ${e instanceof Error ? e.message : String(e)}` };
  }
}

/** The fixture's press: the dialog leaves the file and the press is recorded beside it. */
function pressFixture(file: string, d: NativeDialog, button: string): NativePressResult {
  try {
    const list = JSON.parse(fs.readFileSync(file, "utf-8")) as RawNativeDialog[];
    fs.writeFileSync(file, JSON.stringify(list.filter((x) => String(x.hwnd) !== d.hwnd)));
    fs.appendFileSync(`${file}.presses.jsonl`, JSON.stringify({ hwnd: d.hwnd, pid: d.pid, button }) + "\n");
    return { pressed: true, closed: true };
  } catch (e) {
    return { pressed: false, closed: false, error: e instanceof Error ? e.message : String(e) };
  }
}
