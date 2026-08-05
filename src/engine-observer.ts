/**
 * Out-of-process observability for a running (or starting, or wedged) editor.
 *
 * Every other sensor in ue-mcp runs on the game thread: `ExecuteOnGameThread`
 * gates each bridge request, so when the game thread sits inside a modal loop,
 * a long FSlowTask, or a startup phase that predates the plugin module, the
 * bridge can only answer "Handler execution timed out". That is the whole
 * problem class behind #804 and its predecessors - the agent goes blind exactly
 * when the user needs to know what the engine is doing.
 *
 * Nothing in this file touches the bridge. It reads state the OS and the engine
 * publish regardless of what the game thread is doing:
 *
 *   - the process table (PID + command line + hung/responding state)
 *   - `Saved/Logs/<Project>.log`, written from the first millisecond of startup
 *   - native top-level windows (the pre-Slate "modules are out of date" prompt
 *     is a plain Win32 message box and is readable from outside)
 *   - `Saved/UE_MCP_Bridge/status.json`, the snapshot the plugin flushes from a
 *     writer thread that keeps running while the game thread is blocked
 */

import * as fs from "node:fs";
import * as path from "node:path";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import * as log from "./log.js";

const execFileAsync = promisify(execFile);
const IS_WINDOWS = process.platform === "win32";

/** Run a PowerShell script without quoting hazards (UTF-16LE base64). */
async function powershell(script: string, timeoutMs: number): Promise<string> {
  const encoded = Buffer.from(script, "utf16le").toString("base64");
  const { stdout } = await execFileAsync(
    "powershell.exe",
    ["-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
    { timeout: timeoutMs, maxBuffer: 4 * 1024 * 1024, windowsHide: true },
  );
  return stdout;
}

function parseJsonLoose<T>(raw: string): T[] {
  const text = raw.trim();
  if (!text) return [];
  try {
    const parsed = JSON.parse(text);
    return Array.isArray(parsed) ? (parsed as T[]) : [parsed as T];
  } catch {
    return [];
  }
}

// ---------------------------------------------------------------------------
// Process probe
// ---------------------------------------------------------------------------

export interface EditorProcess {
  pid: number;
  commandLine: string;
  /** The .uproject this process has open, absolute and normalised, if any. */
  projectPath: string | null;
  /**
   * #804: a headless shard (`-server` / `-game` / `-nullrhi` / `-unattended` /
   * a commandlet) is the same binary as an interactive editor. Matching by
   * image name alone made two running shards look like "the editor is already
   * running" while the bridge simultaneously reported nothing connected.
   */
  headless: boolean;
  /** false when the OS says the window is not pumping messages (wedged). */
  responding: boolean;
  windowTitle: string | null;
}

const HEADLESS_FLAGS = ["-server", "-game", "-nullrhi", "-unattended", "-run=", "-buildmachine"];

function extractProjectPath(commandLine: string): string | null {
  const match = commandLine.match(/"?([A-Za-z]:[\\/][^"]*?\.uproject|\/[^"\s]*?\.uproject)"?/);
  if (!match) return null;
  return path.resolve(match[1]);
}

function classify(pid: number, commandLine: string, responding: boolean, windowTitle: string | null): EditorProcess {
  const lower = commandLine.toLowerCase();
  return {
    pid,
    commandLine,
    projectPath: extractProjectPath(commandLine),
    headless: HEADLESS_FLAGS.some((flag) => lower.includes(flag)) || lower.includes("unrealeditor-cmd"),
    responding,
    windowTitle: windowTitle && windowTitle.length > 0 ? windowTitle : null,
  };
}

// One PowerShell round trip, not several: process queries on Windows cost
// seconds (interpreter start ~1.5s, CIM ~4s, and `tasklist` is no cheaper), so
// the whole probe is a single script and callers reach it only when the fast
// signal - the bridge socket itself - has already failed.
const WINDOWS_PROCESS_SCRIPT = `
$ErrorActionPreference = 'SilentlyContinue'
$procs = Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'"
$out = foreach ($p in $procs) {
  $ps = Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
  [pscustomobject]@{
    pid = $p.ProcessId
    cmd = $p.CommandLine
    responding = $(if ($ps) { [bool]$ps.Responding } else { $true })
    title = $(if ($ps) { $ps.MainWindowTitle } else { '' })
  }
}
@($out) | ConvertTo-Json -Compress -Depth 3
`;

// The process table is polled from wait loops and status calls; a short TTL
// keeps a burst of callers from paying for the same query repeatedly, while
// staying far below the timescale on which an editor starts or dies.
const PROCESS_CACHE_MS = 3000;
let processCache: { at: number; value: EditorProcess[] } | null = null;

/**
 * Every UnrealEditor process on this machine, with enough detail to tell an
 * interactive editor for one project apart from a headless shard for another.
 */
export async function listEditorProcesses(): Promise<EditorProcess[]> {
  if (processCache && Date.now() - processCache.at < PROCESS_CACHE_MS) {
    return processCache.value;
  }
  const value = await queryEditorProcesses();
  processCache = { at: Date.now(), value };
  return value;
}

async function queryEditorProcesses(): Promise<EditorProcess[]> {
  try {
    if (IS_WINDOWS) {
      const raw = await powershell(WINDOWS_PROCESS_SCRIPT, 20000);
      const rows = parseJsonLoose<{ pid: number; cmd: string; responding: boolean; title: string }>(raw);
      return rows
        .filter((r) => typeof r?.pid === "number")
        .map((r) => classify(r.pid, r.cmd ?? "", r.responding !== false, r.title ?? null));
    }

    // POSIX: `ps` gives the full argv, which carries the .uproject and the
    // headless flags. There is no cheap "responding" equivalent, so report
    // true and let the log-staleness signal stand in for a wedge.
    const { stdout } = await execFileAsync("ps", ["-eo", "pid=,args="], { timeout: 10000, maxBuffer: 4 * 1024 * 1024 });
    return stdout
      .split("\n")
      .map((line) => line.trim())
      .filter((line) => /UnrealEditor/.test(line) && !/\bgrep\b/.test(line))
      .map((line) => {
        const space = line.indexOf(" ");
        const pid = Number(line.slice(0, space));
        return classify(pid, line.slice(space + 1), true, null);
      })
      .filter((p) => Number.isFinite(p.pid));
  } catch (err) {
    log.debug("engine-observer", "process probe failed", err);
    return [];
  }
}

/**
 * The interactive editor(s) holding `projectPath` open. Headless shards are
 * excluded, and when a project path is known, editors for other projects are
 * too - launching a second project while a shard or another project's editor
 * runs is legitimate (#804).
 */
export async function findInteractiveEditors(projectPath?: string | null): Promise<EditorProcess[]> {
  const all = await listEditorProcesses();
  const interactive = all.filter((p) => !p.headless);
  if (!projectPath) return interactive;
  const target = path.resolve(projectPath).toLowerCase();
  const matched = interactive.filter((p) => p.projectPath?.toLowerCase() === target);
  // A process whose command line could not be read (permissions, race) has a
  // null projectPath. Counting it as "not ours" would let a second editor
  // launch onto the same project; counting it as ours would resurrect #804 for
  // everyone else. Treat unknown as ours only when nothing matched positively.
  if (matched.length > 0) return matched;
  return interactive.filter((p) => p.projectPath === null);
}

// ---------------------------------------------------------------------------
// Log tail
// ---------------------------------------------------------------------------

export interface StartupPhase {
  /** Short phase label derived from the newest matching marker. */
  phase: string;
  /** True when the marker means the editor is waiting on a human. */
  blocking: boolean;
  /** The log line the phase was derived from. */
  evidence: string;
}

interface Marker {
  re: RegExp;
  phase: string;
  blocking?: boolean;
  /**
   * A terminal state: once it appears, later chatter does not change it.
   * Asset registry and shader lines keep scrolling long after the editor is
   * up, so plain newest-wins would report a ready editor as "scanning asset
   * registry" forever.
   */
  sticky?: boolean;
}

/**
 * Ordered newest-line-wins. Deliberately small: the goal is a useful label plus
 * the raw evidence line, not a complete taxonomy of engine startup.
 */
const MARKERS: Marker[] = [
  { re: /=== Critical error|LogWindows: Error:|Fatal error/i, phase: "crashed", blocking: true, sticky: true },
  { re: /following modules are missing or built with a different engine version/i, phase: "waiting on rebuild prompt (modules out of date)", blocking: true, sticky: true },
  { re: /Log file closed/i, phase: "editor exited", sticky: true },
  { re: /\[UE-MCP\] Editor ready/i, phase: "ready", sticky: true },
  { re: /\[UE-MCP\] Bridge server starting/i, phase: "bridge starting" },
  { re: /LogLoad: Took .* to LoadMap/i, phase: "map loaded" },
  { re: /LogLoad: LoadMap: /i, phase: "loading map" },
  { re: /LogShaderCompilers: Display: (Compiling|Submitting)/i, phase: "compiling shaders" },
  { re: /LogAssetRegistry: (Display: )?Asset registry|FAssetRegistry.*scan/i, phase: "scanning asset registry" },
  { re: /LogDerivedDataCache/i, phase: "warming derived data cache" },
  { re: /LogEngine: Initializing Engine/i, phase: "initializing engine" },
  { re: /LogPluginManager: Mounting|LogModuleManager/i, phase: "loading modules and plugins" },
  { re: /LogInit: Display: (Base directory|Presizing)/i, phase: "process starting" },
];

export interface LogState {
  logPath: string | null;
  /** Seconds since the log file was last written. High = the process is stuck. */
  secondsSinceWrite: number | null;
  phase: string;
  blocking: boolean;
  lastLine: string | null;
  tail: string[];
  errors: string[];
  warnings: string[];
}

function projectLogPath(projectPath: string): string {
  const dir = path.dirname(projectPath);
  const name = path.basename(projectPath, ".uproject");
  return path.join(dir, "Saved", "Logs", `${name}.log`);
}

/**
 * Read the last `bytes` of a file without loading the whole thing. `partial`
 * says whether the read started mid-file, which is the only case where the
 * first line is a fragment worth discarding - dropping it unconditionally
 * throws away the first real line of any log shorter than the window.
 */
function readTailBytes(file: string, bytes: number): { text: string; partial: boolean } {
  const size = fs.statSync(file).size;
  const start = Math.max(0, size - bytes);
  const length = size - start;
  if (length <= 0) return { text: "", partial: false };
  const buf = Buffer.alloc(length);
  const fd = fs.openSync(file, "r");
  try {
    fs.readSync(fd, buf, 0, length, start);
  } finally {
    fs.closeSync(fd);
  }
  return { text: buf.toString("utf-8"), partial: start > 0 };
}

/**
 * What the engine's own log says right now. This is the only sensor that works
 * before the plugin module exists, which is where the "editor launched but the
 * bridge never came up" reports have always come from.
 */
export function readLogState(projectPath: string | null | undefined, tailLines = 25): LogState {
  const empty: LogState = {
    logPath: null,
    secondsSinceWrite: null,
    phase: "unknown",
    blocking: false,
    lastLine: null,
    tail: [],
    errors: [],
    warnings: [],
  };
  if (!projectPath) return empty;

  const logPath = projectLogPath(projectPath);
  let stat: fs.Stats;
  try {
    stat = fs.statSync(logPath);
  } catch {
    return { ...empty, logPath };
  }

  let tail: { text: string; partial: boolean };
  try {
    tail = readTailBytes(logPath, 128 * 1024);
  } catch (err) {
    log.debug("engine-observer", `could not read ${logPath}`, err);
    return { ...empty, logPath };
  }

  const lines = tail.text.split(/\r?\n/).filter((l) => l.trim().length > 0);
  if (tail.partial && lines.length > 1) lines.shift();

  let phase = "unknown";
  let blocking = false;

  // Sticky markers win wherever they appear, in declaration order: a crash or a
  // prompt waiting on a human does not stop being true because a background
  // system logged something afterwards.
  const sticky = MARKERS.filter((m) => m.sticky).find((m) => lines.some((l) => m.re.test(l)));
  if (sticky) {
    phase = sticky.phase;
    blocking = sticky.blocking === true;
  } else {
    outer: for (let i = lines.length - 1; i >= 0; i--) {
      for (const marker of MARKERS) {
        if (marker.re.test(lines[i])) {
          phase = marker.phase;
          blocking = marker.blocking === true;
          break outer;
        }
      }
    }
  }

  const errors = lines.filter((l) => /: Error: |=== Critical error/i.test(l)).slice(-10);
  const warnings = lines.filter((l) => /: Warning: /i.test(l)).slice(-10);

  return {
    logPath,
    secondsSinceWrite: Math.max(0, (Date.now() - stat.mtimeMs) / 1000),
    phase,
    blocking,
    lastLine: lines.length > 0 ? lines[lines.length - 1] : null,
    tail: lines.slice(-tailLines),
    errors,
    warnings,
  };
}

// ---------------------------------------------------------------------------
// Native window probe (Windows)
// ---------------------------------------------------------------------------

export interface NativeWindow {
  handle: string;
  className: string;
  title: string;
  /** Static text children, which is where a message box keeps its message. */
  text: string[];
  hung: boolean;
}

/**
 * The prompts that fire before Slate exists - "The following modules are
 * missing or built with a different engine version. Would you like to rebuild
 * them now?", the assert dialog, the crash reporter - are plain Win32 message
 * boxes owned by the editor process. Their text lives in real child controls,
 * so it can be read from outside even when the process is not pumping. This is
 * the only way to see a dialog that appears before the plugin loads.
 */
const WINDOWS_DIALOG_SCRIPT = `
$ErrorActionPreference = 'SilentlyContinue'
$sig = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class UeMcpWin {
  public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
  public static string Text(IntPtr h) {
    StringBuilder sb = new StringBuilder(1024);
    GetWindowTextW(h, sb, sb.Capacity);
    return sb.ToString();
  }
  public static string Cls(IntPtr h) {
    StringBuilder sb = new StringBuilder(256);
    GetClassNameW(h, sb, sb.Capacity);
    return sb.ToString();
  }
}
'@
Add-Type -TypeDefinition $sig -Language CSharp | Out-Null
$targetPids = @(__PIDS__)
$results = New-Object System.Collections.ArrayList
$top = [UeMcpWin+EnumProc]{
  param($h, $l)
  $procId = 0
  [void][UeMcpWin]::GetWindowThreadProcessId($h, [ref]$procId)
  if ($targetPids -contains [int]$procId -and [UeMcpWin]::IsWindowVisible($h)) {
    $children = New-Object System.Collections.ArrayList
    $child = [UeMcpWin+EnumProc]{
      param($c, $cl)
      $t = [UeMcpWin]::Text($c)
      if ($t -and $t.Trim().Length -gt 0) { [void]$children.Add($t) }
      return $true
    }
    [void][UeMcpWin]::EnumChildWindows($h, $child, [IntPtr]::Zero)
    [void]$results.Add([pscustomobject]@{
      handle = $h.ToString()
      className = [UeMcpWin]::Cls($h)
      title = [UeMcpWin]::Text($h)
      text = @($children)
      hung = [bool][UeMcpWin]::IsHungAppWindow($h)
    })
  }
  return $true
}
[void][UeMcpWin]::EnumWindows($top, [IntPtr]::Zero)
@($results) | ConvertTo-Json -Compress -Depth 4
`;

/**
 * Visible top-level windows belonging to the given PIDs, with their child text.
 * Windows only; returns [] elsewhere. Compiling the interop type costs about a
 * second, so call this on demand (a stalled wait, an explicit state query),
 * never on a hot path.
 */
export async function readNativeWindows(pids: number[]): Promise<NativeWindow[]> {
  if (!IS_WINDOWS || pids.length === 0) return [];
  try {
    const script = WINDOWS_DIALOG_SCRIPT.replace("__PIDS__", pids.map((p) => String(Math.trunc(p))).join(","));
    const raw = await powershell(script, 25000);
    return parseJsonLoose<NativeWindow>(raw).filter((w) => w && typeof w.title === "string");
  } catch (err) {
    log.debug("engine-observer", "native window probe failed", err);
    return [];
  }
}

/** Windows that look like a prompt waiting on a human. */
export function dialogLikeWindows(windows: NativeWindow[]): NativeWindow[] {
  return windows.filter((w) => {
    if (w.className === "#32770") return true; // native dialog box class
    const haystack = [w.title, ...(w.text ?? [])].join(" ").toLowerCase();
    return /would you like to|do you want to|rebuild|missing or built with|crash|assertion/.test(haystack);
  });
}

// ---------------------------------------------------------------------------
// In-process snapshot (written by the plugin's status writer thread)
// ---------------------------------------------------------------------------

export interface EngineSnapshot {
  writtenAt?: string;
  ageSeconds?: number;
  phase?: string;
  /**
   * Null until the engine loop starts ticking. Startup has no tick loop to
   * stall, so a number there would be a false alarm on every cold launch.
   */
  gameThreadStalledSeconds?: number | null;
  gameThreadTicking?: boolean;
  modulesLoaded?: number;
  slowTask?: { name: string; fraction: number; stack?: Array<{ name: string; fraction: number }> } | null;
  modal?: { title: string; message: string; buttons: string[] } | null;
  compiling?: { shaders: number; assets: number };
  handler?: { method: string; elapsedSeconds: number } | null;
  [key: string]: unknown;
}

/**
 * The snapshot the plugin flushes to disk from a dedicated writer thread. It
 * keeps updating while the game thread is inside a modal loop or a slow task,
 * which is precisely when a bridge request cannot be answered.
 */
export function readEngineSnapshot(projectPath: string | null | undefined): EngineSnapshot | null {
  if (!projectPath) return null;
  const file = path.join(path.dirname(projectPath), "Saved", "UE_MCP_Bridge", "status.json");

  // The writer publishes atomically (temp file + move) four times a second, so
  // a read can land in the instant between unlink and rename. One retry turns
  // that transient miss back into a hit; without it, callers see a null and
  // conclude the plugin has no status module at all.
  for (let attempt = 0; attempt < 2; attempt++) {
    try {
      const stat = fs.statSync(file);
      const parsed = JSON.parse(fs.readFileSync(file, "utf-8")) as EngineSnapshot;
      return { ...parsed, ageSeconds: Math.max(0, (Date.now() - stat.mtimeMs) / 1000) };
    } catch {
      // fall through to the retry
    }
  }
  return null;
}

// ---------------------------------------------------------------------------
// Merged view
// ---------------------------------------------------------------------------

export interface EngineState {
  running: boolean;
  processes: EditorProcess[];
  log: LogState;
  snapshot: EngineSnapshot | null;
  dialogs: NativeWindow[];
  /** One line an agent can act on without reading any of the above. */
  summary: string;
  /** True when something is waiting on a human answer. */
  blocked: boolean;
}

function summarise(state: Omit<EngineState, "summary" | "blocked">): { summary: string; blocked: boolean } {
  const ours = state.processes;
  if (ours.length === 0) {
    return { summary: `No editor process for this project. Log phase: ${state.log.phase}.`, blocked: false };
  }

  if (state.dialogs.length > 0) {
    const d = state.dialogs[0];
    const body = (d.text ?? []).slice(0, 4).join(" | ");
    return { summary: `Editor is waiting on a native dialog: "${d.title || d.className}" ${body}`.trim(), blocked: true };
  }

  const snap = state.snapshot;
  if (snap && (snap.ageSeconds ?? 999) < 15) {
    if (snap.modal) {
      return { summary: `Editor is blocked on modal "${snap.modal.title}": ${snap.modal.message} [${(snap.modal.buttons ?? []).join(", ")}]`, blocked: true };
    }
    if (snap.slowTask) {
      const pct = Math.round((snap.slowTask.fraction ?? 0) * 100);
      return { summary: `Editor is busy: ${snap.slowTask.name} (${pct}%)`, blocked: false };
    }
    if (snap.gameThreadTicking === false) {
      // Still starting: the engine loop has not begun, so module count and the
      // log phase are the honest progress signals, not a stall time.
      const modules = typeof snap.modulesLoaded === "number" ? `, ${snap.modulesLoaded} modules loaded` : "";
      return { summary: `Editor is still starting (${snap.phase ?? state.log.phase}${modules}); the engine loop has not begun ticking.`, blocked: false };
    }
    if ((snap.gameThreadStalledSeconds ?? 0) > 10) {
      return { summary: `Game thread has not ticked for ${Math.round(snap.gameThreadStalledSeconds!)}s. Phase: ${snap.phase ?? state.log.phase}.`, blocked: false };
    }
  }

  if (state.processes.some((p) => !p.responding)) {
    return { summary: `Editor process is not responding to the window manager. Log phase: ${state.log.phase}, log last written ${Math.round(state.log.secondsSinceWrite ?? 0)}s ago.`, blocked: false };
  }

  if (state.log.blocking) {
    return { summary: `Editor is stopped at: ${state.log.phase}. ${state.log.lastLine ?? ""}`.trim(), blocked: true };
  }

  return {
    summary: `Editor is up (${state.log.phase}); log last written ${Math.round(state.log.secondsSinceWrite ?? 0)}s ago.`,
    blocked: false,
  };
}

/**
 * Everything that can be known about the editor without asking the game thread.
 * `probeWindows` costs about a second on Windows, so it defaults off and should
 * be turned on when something already looks wrong.
 */
export async function readEngineState(
  projectPath: string | null | undefined,
  opts: { probeWindows?: boolean } = {},
): Promise<EngineState> {
  const processes = await findInteractiveEditors(projectPath);
  const logState = readLogState(projectPath);
  const snapshot = readEngineSnapshot(projectPath);

  let dialogs: NativeWindow[] = [];
  if (opts.probeWindows && processes.length > 0) {
    dialogs = dialogLikeWindows(await readNativeWindows(processes.map((p) => p.pid)));
  }

  const base = { running: processes.length > 0, processes, log: logState, snapshot, dialogs };
  return { ...base, ...summarise(base) };
}
