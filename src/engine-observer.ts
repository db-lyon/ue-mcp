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

/**
 * Where a path argument can begin on a command line: the start of the string,
 * or straight after a separator that cannot itself be part of a path (a space,
 * a quote, or the `=` of `-Project=`). Matching the root marker rather than the
 * whole token is what lets an unquoted path containing spaces still be found.
 */
const PATH_ROOT_RE = /(?:^|[\s"'=])((?:[A-Za-z]:[\\/])|\/)/g;

/**
 * The .uproject argument on a process command line, absolute and resolved.
 *
 * #967/#970/#965: this used to be one lazy regex over the whole command line,
 * `[A-Za-z]:[\\/][^"]*?\.uproject`. `[^"]` matches spaces, so on the unquoted
 * command line Windows reports for a spawned editor
 *
 *     C:\...\UnrealEditor.exe C:\work\Demo\Demo.uproject
 *
 * the match started at the drive letter of the EXECUTABLE and ran all the way
 * to the end, and every process came back with a "project path" that was the
 * exe and the project concatenated. Nothing ever equalled that, so
 * editorOwnsProject was false for the very editor that had the project open:
 * stop_editor refused to stop a healthy editor while printing that same
 * concatenation back as its evidence, and get_engine_state reported no process
 * for a project whose editor was answering on the bridge.
 *
 * So: find where the .uproject argument ENDS (a token boundary, not just any
 * occurrence of the extension), then walk back to the last place a path could
 * have begun. That handles the quoted form, the unquoted form, `-Project=` with
 * and without quotes, and POSIX roots, without assuming a path has no spaces.
 */
export function extractProjectPath(commandLine: string): string | null {
  const lower = commandLine.toLowerCase();
  const EXT = ".uproject";

  let end = -1;
  for (let at = lower.indexOf(EXT); at >= 0; at = lower.indexOf(EXT, at + EXT.length)) {
    const after = commandLine[at + EXT.length];
    if (after === undefined || /[\s"']/.test(after)) {
      end = at + EXT.length;
      break;
    }
  }
  if (end < 0) return null;

  const head = commandLine.slice(0, end);
  let start = -1;
  PATH_ROOT_RE.lastIndex = 0;
  for (let m = PATH_ROOT_RE.exec(head); m !== null; m = PATH_ROOT_RE.exec(head)) {
    // m[0] may include the delimiter that proved this is a token boundary;
    // the path itself starts where the captured root does.
    start = m.index + m[0].length - m[1].length;
  }
  if (start < 0) return null;

  const raw = commandLine.slice(start, end).replace(/^["']|["']$/g, "").trim();
  if (raw.length === 0) return null;

  // A candidate that still swallowed the executable is the exact failure this
  // function exists to prevent, so never return one. It can only happen when
  // the project argument itself is relative, and there the last whitespace
  // delimited token is the honest answer.
  if (/\.exe(\s|$)/i.test(raw)) {
    const token = raw.split(/\s+/).pop();
    return token && token.toLowerCase().endsWith(EXT) ? path.resolve(token) : null;
  }
  return path.resolve(raw);
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
let processCache: { at: number; value: EditorProcess[]; failed: boolean } | null = null;

interface ProcessProbe {
  processes: EditorProcess[];
  /**
   * #965: the probe itself did not run. PowerShell timing out, a policy that
   * blocks CIM, or `ps` being unavailable all produce an empty list that means
   * "I could not look", not "there is no editor". The two must never be
   * reported as the same thing.
   */
  failed: boolean;
}

/**
 * Every UnrealEditor process on this machine, with enough detail to tell an
 * interactive editor for one project apart from a headless shard for another.
 */
export async function listEditorProcesses(): Promise<EditorProcess[]> {
  return (await probeEditorProcesses()).processes;
}

async function probeEditorProcesses(): Promise<ProcessProbe> {
  if (processCache && Date.now() - processCache.at < PROCESS_CACHE_MS) {
    return { processes: processCache.value, failed: processCache.failed };
  }
  const probe = await queryEditorProcesses();
  processCache = { at: Date.now(), value: probe.processes, failed: probe.failed };
  return probe;
}

async function queryEditorProcesses(): Promise<ProcessProbe> {
  try {
    if (IS_WINDOWS) {
      const raw = await powershell(WINDOWS_PROCESS_SCRIPT, 20000);
      const rows = parseJsonLoose<{ pid: number; cmd: string; responding: boolean; title: string }>(raw);
      return {
        processes: rows
          .filter((r) => typeof r?.pid === "number")
          .map((r) => classify(r.pid, r.cmd ?? "", r.responding !== false, r.title ?? null)),
        failed: false,
      };
    }

    // POSIX: `ps` gives the full argv, which carries the .uproject and the
    // headless flags. There is no cheap "responding" equivalent, so report
    // true and let the log-staleness signal stand in for a wedge.
    const { stdout } = await execFileAsync("ps", ["-eo", "pid=,args="], { timeout: 10000, maxBuffer: 4 * 1024 * 1024 });
    return {
      processes: stdout
        .split("\n")
        .map((line) => line.trim())
        .filter((line) => /UnrealEditor/.test(line) && !/\bgrep\b/.test(line))
        .map((line) => {
          const space = line.indexOf(" ");
          const pid = Number(line.slice(0, space));
          return classify(pid, line.slice(space + 1), true, null);
        })
        .filter((p) => Number.isFinite(p.pid)),
      failed: false,
    };
  } catch (err) {
    log.debug("engine-observer", "process probe failed", err);
    return { processes: [], failed: true };
  }
}

/** Two .uproject paths that name the same file, whatever their spelling. */
export function sameProjectFile(a: string, b: string): boolean {
  const norm = (v: string): string => path.resolve(v).replace(/\\/g, "/").toLowerCase();
  return norm(a) === norm(b);
}

/**
 * Does this process have exactly `projectPath` open?
 *
 * The `.uproject` on the command line is the only thing that ties a process to
 * a project, so it is the only thing this looks at. A process whose command
 * line could not be read is never a positive match: every lifecycle action that
 * can stop or attach to an editor is scoped through this, and "might be ours"
 * is the wrong answer to give any of them (#819).
 */
export function editorOwnsProject(proc: EditorProcess, projectPath: string): boolean {
  return proc.projectPath !== null && sameProjectFile(proc.projectPath, projectPath);
}

/**
 * Narrow a process list to the editors for one project. Pure, so the matching
 * rule can be tested without a process table.
 *
 * Editors for other projects are always excluded. Headless shards are excluded
 * unless `includeHeadless` is set: launching beside a shard is legitimate
 * (#804), but stopping has to see every editor holding the project (#1150).
 */
export function selectEditorsForProject(
  processes: EditorProcess[],
  projectPath?: string | null,
  opts: { includeHeadless?: boolean } = {},
): EditorProcess[] {
  const interactive = opts.includeHeadless ? processes : processes.filter((p) => !p.headless);
  if (!projectPath) return interactive;
  const matched = interactive.filter((p) => editorOwnsProject(p, projectPath));
  // A process whose command line could not be read (permissions, race) has a
  // null projectPath. Counting it as "not ours" would let a second editor
  // launch onto the same project; counting it as ours would resurrect #804 for
  // everyone else. Treat unknown as ours only when nothing matched positively.
  if (matched.length > 0) return matched;
  return interactive.filter((p) => p.projectPath === null);
}

/** The interactive editor(s) holding `projectPath` open, from the live process table. */
export async function findInteractiveEditors(projectPath?: string | null): Promise<EditorProcess[]> {
  return selectEditorsForProject(await listEditorProcesses(), projectPath);
}

/**
 * Every editor holding `projectPath` open, headless ones included. Stop and
 * ownership checks use this: a -nullrhi editor still holds the project (#1150).
 */
export async function findProjectEditors(projectPath?: string | null): Promise<EditorProcess[]> {
  return selectEditorsForProject(await listEditorProcesses(), projectPath, { includeHeadless: true });
}

/** The running editor with this PID, or null when it is gone. */
export async function findEditorByPid(pid: number): Promise<EditorProcess | null> {
  const all = await listEditorProcesses();
  return all.find((p) => p.pid === pid) ?? null;
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
/**
 * The engine root the project was last actually opened with, from its own log.
 *
 * Unreal writes `LogInit: Base Directory: <engine>/Engine/Binaries/<Platform>/`
 * in the first few lines of every run, which is the only durable record of
 * which engine tree launched an editor. It survives the editor exiting, so it
 * answers "which engine is this project on" while the editor is stopped, which
 * is exactly when a full rebuild has to be resolved (#959, #974).
 *
 * Returns null when the log is absent, unreadable, or has no such line.
 */
export function readEngineRootFromLog(projectPath: string | null | undefined): string | null {
  if (!projectPath) return null;
  const file = projectLogPath(projectPath);
  let head: string;
  try {
    const size = fs.statSync(file).size;
    const length = Math.min(size, 64 * 1024);
    if (length <= 0) return null;
    const buf = Buffer.alloc(length);
    const fd = fs.openSync(file, "r");
    try {
      fs.readSync(fd, buf, 0, length, 0);
    } finally {
      fs.closeSync(fd);
    }
    head = buf.toString("utf-8");
  } catch {
    return null;
  }

  const match = head.match(/Log\w*:\s*(?:Base|Engine) Directory:\s*(.+)/i);
  if (!match) return null;
  const directory = match[1].trim().replace(/[\\/]+$/, "");
  if (!directory) return null;

  // Base Directory points inside Engine/Binaries/<Platform>; Engine Directory
  // points at the Engine folder itself. Both sit under the root we want.
  let current = path.resolve(directory);
  while (current && current !== path.dirname(current)) {
    if (path.basename(current).toLowerCase() === "engine") return path.dirname(current);
    current = path.dirname(current);
  }
  return null;
}

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

/**
 * A prompt the editor is showing, as the snapshot records it.
 *
 * `blocksGameThread` is the fact that decides whether anything is actually
 * stuck. Only a window on Slate's modal stack parks the game thread; a prompt
 * raised without AddModalWindow is on screen and holds nothing (#1118).
 * Absent on plugin builds before that, where the two were one field.
 */
export interface EngineSnapshotModal {
  title: string;
  message: string;
  buttons: string[];
  blocksGameThread?: boolean;
}

/**
 * Is the engine parked behind this prompt?
 *
 * An older plugin does not say, and there the conservative answer is the only
 * safe one: it is the behaviour every caller had before the field existed, and
 * calling a real modal harmless would send a quit at an unanswered question.
 */
export function modalBlocksGameThread(modal: EngineSnapshotModal | null | undefined): boolean {
  if (!modal) return false;
  return modal.blocksGameThread !== false;
}

export interface EngineSnapshot {
  writtenAt?: string;
  ageSeconds?: number;
  /** Which process this snapshot describes. Absent on plugin builds before #990. */
  pid?: number;
  phase?: string;
  /**
   * Null until the engine loop starts ticking. Startup has no tick loop to
   * stall, so a number there would be a false alarm on every cold launch.
   */
  gameThreadStalledSeconds?: number | null;
  gameThreadTicking?: boolean;
  modulesLoaded?: number;
  slowTask?: { name: string; fraction: number; stack?: Array<{ name: string; fraction: number }> } | null;
  modal?: EngineSnapshotModal | null;
  compiling?: { shaders: number; assets: number };
  handler?: { method: string; elapsedSeconds: number } | null;
  [key: string]: unknown;
}

/**
 * The snapshot the plugin flushes to disk from a dedicated writer thread. It
 * keeps updating while the game thread is inside a modal loop or a slow task,
 * which is precisely when a bridge request cannot be answered.
 */
function readSnapshotFile(file: string): EngineSnapshot | null {
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

/**
 * `status.<pid>.json` names, newest last-written first.
 *
 * #990: one project directory can hold the status of several processes, and a
 * single shared `status.json` describes only whichever of them wrote last. The
 * per-process files are the ones that describe each editor honestly. A file
 * whose process has died stops being updated, so newest-wins picks a live
 * editor over a crashed one without a process-table query.
 */
function instanceStatusFiles(dir: string): string[] {
  try {
    return fs
      .readdirSync(dir)
      .filter((name) => /^status\.\d+\.json$/i.test(name))
      .map((name) => {
        const file = path.join(dir, name);
        let mtimeMs = 0;
        try {
          mtimeMs = fs.statSync(file).mtimeMs;
        } catch {
          // Removed between readdir and stat; sorts last and reads as null.
        }
        return { file, mtimeMs };
      })
      .sort((a, b) => b.mtimeMs - a.mtimeMs)
      .map((entry) => entry.file);
  } catch {
    return [];
  }
}

export function readEngineSnapshot(projectPath: string | null | undefined): EngineSnapshot | null {
  if (!projectPath) return null;
  const dir = path.join(path.dirname(projectPath), "Saved", "UE_MCP_Bridge");

  // Per-process first, shared second. The shared path is still written by
  // current plugin builds and is the only thing an older one writes, so it
  // stays the fallback rather than being dropped.
  for (const file of instanceStatusFiles(dir)) {
    const snapshot = readSnapshotFile(file);
    if (snapshot) return snapshot;
  }
  return readSnapshotFile(path.join(dir, "status.json"));
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
  /**
   * #965: the out-of-process probe could not run. Reported on its own rather
   * than folded into an empty `processes`, because "I could not look" and
   * "there is no editor" are different answers and only one of them is a
   * reason to conclude the editor is down.
   */
  processProbeFailed: boolean;
  /** What `running` is asserted on. A bridge reply outranks the process table. */
  runningEvidence: "bridge-snapshot" | "process-table" | "none";
  /** Where `snapshot` came from. */
  snapshotSource: "bridge" | "status.json" | "none";
  /** One line an agent can act on without reading any of the above. */
  summary: string;
  /** True when something is waiting on a human answer. */
  blocked: boolean;
}

function summarise(state: Omit<EngineState, "summary" | "blocked">): { summary: string; blocked: boolean } {
  const ours = state.processes;
  if (ours.length === 0 && !state.running) {
    return {
      summary: state.processProbeFailed
        ? `The process probe failed, so whether an editor is running for this project is unknown. Log phase: ${state.log.phase}.`
        : `No editor process for this project. Log phase: ${state.log.phase}.`,
      blocked: false,
    };
  }

  // #965: an editor answered this request over the bridge while the process
  // table came back empty, and the report said "No editor process for this
  // project" in the same breath as it returned that editor's own snapshot.
  // Whatever the process table managed to see, an editor that replied exists.
  if (ours.length === 0) {
    const modal = state.snapshot?.modal;
    if (modal) {
      const blocked = modalBlocksGameThread(modal);
      const verb = blocked ? "is blocked on modal" : "is showing a non-blocking prompt";
      return {
        summary: `Editor answered over the bridge and ${verb} "${modal.title}": ${modal.message} [${(modal.buttons ?? []).join(", ")}]`,
        blocked,
      };
    }
    return {
      summary:
        `Editor answered over the bridge (phase: ${state.snapshot?.phase ?? state.log.phase}), but the process probe ` +
        `${state.processProbeFailed ? "failed" : "did not find it"}, so no pid or command line is available.`,
      blocked: false,
    };
  }

  if (state.dialogs.length > 0) {
    const d = state.dialogs[0];
    const body = (d.text ?? []).slice(0, 4).join(" | ");
    return { summary: `Editor is waiting on a native dialog: "${d.title || d.className}" ${body}`.trim(), blocked: true };
  }

  const snap = state.snapshot;
  if (snap && (snap.ageSeconds ?? 999) < 15) {
    if (snap.modal && modalBlocksGameThread(snap.modal)) {
      return { summary: `Editor is blocked on modal "${snap.modal.title}": ${snap.modal.message} [${(snap.modal.buttons ?? []).join(", ")}]`, blocked: true };
    }
    if (snap.modal) {
      // On screen, holding nothing: the game thread ticks behind it and every
      // call still runs. Reported, because an unanswered question is worth
      // seeing, but never as a reason work cannot proceed (#1118).
      return {
        summary: `Editor is showing a non-blocking prompt "${snap.modal.title}": ${snap.modal.message} [${(snap.modal.buttons ?? []).join(", ")}]`,
        blocked: false,
      };
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
  const probe = await probeEditorProcesses();
  // Every interactive editor holding this .uproject open, not just one: two
  // editors on one project is a legitimate state and hiding the second is how
  // a stop lands on the wrong one (#965).
  const processes = selectEditorsForProject(probe.processes, projectPath);
  const logState = readLogState(projectPath);
  const snapshot = readEngineSnapshot(projectPath);

  let dialogs: NativeWindow[] = [];
  if (opts.probeWindows && processes.length > 0) {
    dialogs = dialogLikeWindows(await readNativeWindows(processes.map((p) => p.pid)));
  }

  const base: Omit<EngineState, "summary" | "blocked"> = {
    running: processes.length > 0,
    processes,
    log: logState,
    snapshot,
    dialogs,
    processProbeFailed: probe.failed,
    runningEvidence: processes.length > 0 ? "process-table" : "none",
    snapshotSource: snapshot ? "status.json" : "none",
  };
  return { ...base, ...summarise(base) };
}

/**
 * Fold in a snapshot the editor served over its own bridge.
 *
 * #965: one report contained `"running": false, "processes": []` and, in the
 * same object, a live snapshot with an uptime and a ticking game thread, served
 * BY the editor over the bridge. An editor that answers a request is running;
 * a process table that came back empty is a failed measurement, not a fact
 * about the world. So a bridge reply sets `running` and the process probe's
 * silence is reported as its own field.
 */
export function withBridgeSnapshot(state: EngineState, snapshot: EngineSnapshot): EngineState {
  const base: Omit<EngineState, "summary" | "blocked"> = {
    ...state,
    // It was served in answer to this call, so its age is zero. Leaving it
    // unset made the summary's freshness gate treat it as 999 seconds old and
    // skip the modal and slow-task lines it was fetched to provide.
    snapshot: { ...snapshot, ageSeconds: 0 },
    snapshotSource: "bridge",
    running: true,
    runningEvidence: "bridge-snapshot",
  };
  return { ...base, ...summarise(base) };
}
