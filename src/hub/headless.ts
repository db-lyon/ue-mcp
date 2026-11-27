// @session per-session: Owns the one hidden editor it launches on its own copy of the project it is handed.
/**
 * The Playtest Hub's headless runner (#2146). A run plays on a hidden editor
 * ue-mcp launches itself on its own copy of the project, never on the
 * person's editor:
 *
 * - the copy is <dataDir>/hub/headless/<project>, mirrored from the project
 *   before each launch (only what changed is copied) without Saved,
 *   Intermediate or caches (src/editor/editor-copy.ts, shared with the
 *   sharded live run);
 * - the editor starts only when it fits in memory beside the headroom the
 *   rest of the machine keeps, with -nullrhi -unattended and no window;
 * - before anything is sent, the editor is asked which project it has open,
 *   and a run is refused unless it is the copy;
 * - it stays up between runs and closes when the hub shuts down or after it
 *   has idled a while. Only the process this runner started is ever stopped.
 *
 * It plays an arrangement the same way the open editor does: the pie actions
 * arrangement_play, _status, _stop and _verdict, over its own bridge.
 */
import { spawn, type ChildProcess } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { ActionResult } from "../contract/daemon.js";
import type { PlayRequest, PlayStatus } from "./contract.js";
import type { HubRunner } from "./ports.js";
import { EditorRunner } from "./runner.js";
import { KIND_FLAGS, awaitEditorStartup, editorFits, EDITOR_PEAK_MB, headroomMB, mirror, pidAlive, projectCopyFilter } from "../editor/editor-copy.js";

/** The editor flags every hidden editor takes besides its kind's: no source-code accessor, no background throttling. */
const HIDDEN_EDITOR_ARGS = [
  "-ini:EditorSettings:[/Script/SourceCodeAccess.SourceCodeAccessSettings]:PreferredAccessor=NullSourceCodeAccessor",
  "-ini:EditorSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False",
];

/** Asks the editor which project it has open; the answer line starts with MCP_PROJECT_DIR:. */
const PROJECT_IDENTITY_PYTHON = 'import unreal\nprint("MCP_PROJECT_DIR:" + unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))';

/** How long the hidden editor may idle before it is closed. */
export const HEADLESS_IDLE_MS = 10 * 60_000;
/** How long a launch may take to report ready. */
export const HEADLESS_READY_TIMEOUT_S = 600;

/** The bridge the runner talks to its editor over. */
export interface HeadlessBridge {
  connect(timeoutMs?: number): Promise<void>;
  call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<unknown>;
  disconnect(): void;
}

export interface HeadlessRunnerOptions {
  /** The project's own folder and .uproject. */
  projectDir: string;
  projectPath: string;
  /** Where the copy lives: <dataDir>/hub/headless. */
  workDir: string;
  /** The editor binary for the project, or null when none is installed. */
  editorExe: () => string | null;
  /** Free and total memory in MB; the OS's by default. */
  memory?: () => { freeMB: number; totalMB: number };
  log: { warn(message: string, detail?: unknown): void };
  idleMs?: number;
  readyTimeoutS?: number;
  /** Seams for tests. */
  spawn?: (exe: string, args: string[]) => ChildProcess;
  waitReady?: (uproject: string, projectDir: string, timeoutS: number, opts: { launchedAtMs: number; signal: AbortSignal }) => Promise<{ ready: boolean; reason?: string }>;
  bridge?: (port: number) => HeadlessBridge;
  /** Close the editor on the copy; stopEditor addressed to the copy's project by default. */
  stop?: (copyDir: string, pid: number) => Promise<void>;
}

const normalize = (p: string): string => path.resolve(p).replace(/[\\/]+$/, "").toLowerCase();

/** The OS's memory, in MB. */
export const osMemory = (): { freeMB: number; totalMB: number } => ({ freeMB: Math.round(os.freemem() / 1048576), totalMB: Math.round(os.totalmem() / 1048576) });

export class HeadlessRunner implements HubRunner {
  readonly kind = "headless" as const;
  private child: ChildProcess | null = null;
  private bridge: HeadlessBridge | null = null;
  private starting: Promise<void> | null = null;
  private idle: NodeJS.Timeout | null = null;
  private inner: EditorRunner;

  constructor(private readonly o: HeadlessRunnerOptions) {
    this.inner = new EditorRunner({ callAction: (category, action, args) => this.callAction(category, action, args) });
  }

  /** The copy the editor opens. */
  get copyDir(): string {
    return path.join(this.o.workDir, path.basename(this.o.projectDir));
  }

  private running(): boolean {
    return !!this.child && pidAlive(this.child.pid);
  }

  /** Why a run cannot go headless now, or null. An editor already up always takes it. */
  unavailable(): string | null {
    if (this.running()) return null;
    if (!this.o.editorExe()) return "no Unreal Editor is installed for this project, so there is nothing to launch hidden";
    const { freeMB, totalMB } = (this.o.memory ?? osMemory)();
    if (!editorFits("headless", freeMB, totalMB)) {
      return `a hidden editor needs about ${Math.round(EDITOR_PEAK_MB.headless! / 1024 * 10) / 10} GB and ${Math.round(freeMB / 1024 * 10) / 10} GB is free, with ${Math.round(headroomMB(totalMB) / 1024 * 10) / 10} GB kept for the rest of the machine`;
    }
    return null;
  }

  private async callAction(category: string, action: string, args?: Record<string, unknown>): Promise<ActionResult> {
    await this.ensure();
    this.touch();
    try {
      const v = await this.bridge!.call(`${category}_${action}`, args ?? {}, 60_000);
      return { isError: false, content: [{ type: "text", text: JSON.stringify(v ?? {}) }] };
    } catch (e) {
      return { isError: true, content: [{ type: "text", text: e instanceof Error ? e.message : String(e) }] };
    }
  }

  private touch(): void {
    if (this.idle) clearTimeout(this.idle);
    this.idle = setTimeout(() => this.close("it idled"), this.o.idleMs ?? HEADLESS_IDLE_MS);
    this.idle.unref?.();
  }

  /** Launch the hidden editor on the copy if it is not up, and prove it has the copy open. */
  private ensure(): Promise<void> {
    if (this.running() && this.bridge) return Promise.resolve();
    return (this.starting ??= this.launch().finally(() => {
      this.starting = null;
    }));
  }

  private async launch(): Promise<void> {
    const why = this.unavailable();
    if (why) throw new Error(`the run cannot go headless: ${why}`);
    const exe = this.o.editorExe()!;
    const copy = this.copyDir;
    fs.mkdirSync(copy, { recursive: true });
    fs.rmSync(path.join(copy, "Saved"), { recursive: true, force: true });
    mirror(this.o.projectDir, copy, projectCopyFilter(this.o.projectDir));
    const uproject = path.join(copy, path.basename(this.o.projectPath));
    const args = [uproject, ...KIND_FLAGS.headless!, ...HIDDEN_EDITOR_ARGS];
    const launchedAtMs = Date.now();
    const child = this.o.spawn
      ? this.o.spawn(exe, args)
      : spawn(exe, args, { stdio: "ignore", detached: true, windowsHide: true, env: { ...process.env, UE_MCP_DIALOG_POLICY: "Restore=no" } });
    child.unref?.();
    this.child = child;
    child.once("exit", () => {
      if (this.child === child) this.dropEditor();
    });
    try {
      const waitReady = this.o.waitReady ?? (await import("../editor/editor-control.js")).waitForEditorReady;
      const timeoutS = this.o.readyTimeoutS ?? HEADLESS_READY_TIMEOUT_S;
      await awaitEditorStartup({ child, wait: (signal) => waitReady(uproject, copy, timeoutS, { launchedAtMs, signal }), readyTimeoutS: timeoutS, projectDir: copy, launchedAtMs });
      const lock = JSON.parse(fs.readFileSync(path.join(copy, "Saved", "UE_MCP_Bridge", "port.json"), "utf8")) as { port?: unknown };
      if (typeof lock.port !== "number") throw new Error("the hidden editor reported ready but published no port");
      const bridge = this.o.bridge ? this.o.bridge(lock.port) : new (await import("../bridge/bridge.js")).EditorBridge("127.0.0.1", lock.port);
      await bridge.connect(15_000);
      const answer = JSON.stringify(await bridge.call("execute_python", { code: PROJECT_IDENTITY_PYTHON }, 60_000));
      const reported = /MCP_PROJECT_DIR:([^"\\]+(?:\\\\[^"\\]+)*)/.exec(answer)?.[1]?.replace(/\\\\/g, "\\").trim();
      if (!reported || normalize(reported) !== normalize(copy)) {
        bridge.disconnect();
        throw new Error(`the editor on port ${lock.port} has ${reported ?? "an unknown project"} open, not the hub's copy at ${copy}; nothing was sent`);
      }
      this.bridge = bridge;
      this.touch();
    } catch (e) {
      this.close("it did not start");
      throw e;
    }
  }

  private dropEditor(): void {
    this.bridge?.disconnect();
    this.bridge = null;
    this.child = null;
    if (this.idle) clearTimeout(this.idle);
    this.idle = null;
  }

  /** Ask the hidden editor this runner started to close, addressed to the copy's project. Nothing else is ever stopped. */
  close(reason: string): void {
    const child = this.child;
    this.dropEditor();
    if (!child?.pid || !pidAlive(child.pid)) return;
    const stop = this.o.stop ?? (async (dir: string) => {
      const r = await (await import("../editor/editor-control.js")).stopEditor(dir, { discardTransient: true });
      if (!r.success && !r.alreadyStopped) throw new Error(r.message);
    });
    void stop(this.copyDir, child.pid).catch((e: unknown) => this.o.log.warn(`hub: the hidden editor (pid ${child.pid}) did not close after ${reason}`, e));
  }

  play(request: PlayRequest): Promise<{ pieStarted?: boolean }> {
    return this.inner.play(request);
  }

  status(runId: string): Promise<PlayStatus> {
    if (!this.running()) return Promise.resolve({ runId, state: "error", checks: [], error: "the hidden editor exited during the run" });
    return this.inner.status(runId);
  }

  stop(runId: string, reason: string): Promise<void> {
    return this.running() ? this.inner.stop(runId, reason) : Promise.resolve();
  }

  verdict(): Promise<void> {
    return Promise.reject(new Error("a headless run takes no verdicts: a test with manual checks plays on the open editor"));
  }
}

/** The project's .uproject in its folder, or null. */
export function uprojectIn(projectDir: string): string | null {
  try {
    const name = fs.readdirSync(projectDir).find((n) => n.toLowerCase().endsWith(".uproject"));
    return name ? path.join(projectDir, name) : null;
  } catch {
    return null;
  }
}
