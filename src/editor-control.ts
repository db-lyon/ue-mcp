import * as fs from "node:fs";
import * as path from "node:path";
import { spawn } from "child_process";
import * as net from "net";
import WebSocket from "ws";
import type { ProjectContext } from "./project.js";
import { findEngineInstall } from "./deployer.js";
import { invalidatePluginFreshness } from "./plugin-freshness.js";
import {
  editorOwnsProject,
  findEditorByPid,
  findInteractiveEditors,
  readEngineState,
  readEngineSnapshot,
  readLogState,
  type EngineState,
} from "./engine-observer.js";
import { isPidAlive, lockfileIsFromThisLaunch, resolveBridgeTarget } from "./editor-target.js";
import { startProgress } from "./ui/progress.js";
import type { ProgressFn } from "./types.js";

// Process control is cross-platform: the editor binary path and the running-
// process probe differ per OS, and stopping goes through the bridge (#790).
const IS_WINDOWS = process.platform === "win32";

const NO_EDITOR_BINARY_MSG =
  "Unreal Editor executable not found. Set UE_EDITOR_PATH to the editor binary (on macOS that is inside UnrealEditor.app/Contents/MacOS/), or install the engine to a default location.";

/** Read EngineAssociation from a .uproject, or null if unreadable. */
function readEngineAssociation(projectPath: string): string | null {
  try {
    const parsed = JSON.parse(fs.readFileSync(projectPath, "utf-8"));
    return typeof parsed?.EngineAssociation === "string" ? parsed.EngineAssociation : null;
  } catch {
    return null;
  }
}

function findUEBuildTool(engineAssociation?: string | null): string | null {
  const envPath = process.env.UE_BUILD_TOOL_PATH;
  if (envPath) return envPath;

  const scriptName = IS_WINDOWS ? "Build.bat" : "Build.sh";

  // Prefer the engine the project's EngineAssociation actually points at, so a
  // 5.7 project builds with 5.7's Build tool - not whatever version happens to
  // sort first in the fallback search below. The editor launch already respects
  // the association (findEditorExecutable); without this the CLI build could
  // silently compile against a different engine than the editor runs, masking
  // API incompatibilities until the editor's own rebuild fails.
  const associatedRoot = findEngineInstall(engineAssociation ?? null);
  if (associatedRoot) {
    const associatedTool = path.join(associatedRoot, "Engine", "Build", "BatchFiles", scriptName);
    if (fs.existsSync(associatedTool)) return associatedTool;
  }

  const versions = ["5.8", "5.7", "5.6", "5.5", "5.4", "5.3"];

  const searchRoots: string[] = IS_WINDOWS
    ? [
        "C:/Program Files/Epic Games",
        "D:/Program Files/Epic Games",
        "E:/Program Files/Epic Games",
        "C:/Epic Games",
        "D:/Epic Games",
        "E:/Epic Games",
      ]
    : process.platform === "darwin"
      ? ["/Users/Shared/Epic Games"]
      : [
          path.join(process.env.HOME ?? "/home", "UnrealEngine"),
          "/opt/UnrealEngine",
        ];

  for (const basePath of searchRoots) {
    for (const version of versions) {
      const buildToolPath = path.join(basePath, `UE_${version}`, "Engine", "Build", "BatchFiles", scriptName);
      if (fs.existsSync(buildToolPath)) {
        return buildToolPath;
      }
    }
  }

  // Linux source builds: ~/UnrealEngine/Engine/Build/BatchFiles/Build.sh (no version subdir)
  if (!IS_WINDOWS && process.platform !== "darwin") {
    const home = process.env.HOME ?? "/home";
    const sourceBuild = path.join(home, "UnrealEngine", "Engine", "Build", "BatchFiles", "Build.sh");
    if (fs.existsSync(sourceBuild)) return sourceBuild;
  }

  return null;
}

/**
 * #766/#790: the editor binary lives at a different path per platform. Only the
 * Win64 path was ever checked, which is the whole reason start_editor was
 * Windows-only - engine discovery itself (findUEBuildTool) has always worked
 * cross-platform. On macOS the launchable binary is inside the .app bundle.
 */
function editorBinaryCandidates(engineRoot: string): string[] {
  const binaries = path.join(engineRoot, "Engine", "Binaries");
  if (IS_WINDOWS) {
    return [path.join(binaries, "Win64", "UnrealEditor.exe")];
  }
  if (process.platform === "darwin") {
    return [
      path.join(binaries, "Mac", "UnrealEditor.app", "Contents", "MacOS", "UnrealEditor"),
      path.join(binaries, "Mac", "UnrealEditor"),
    ];
  }
  return [path.join(binaries, "Linux", "UnrealEditor")];
}

function findEditorExecutable(project?: ProjectContext): string | null {
  const envPath = process.env.UE_EDITOR_PATH;
  if (envPath) return envPath;

  const associatedEngineRoot = findEngineInstall(project?.engineAssociation ?? null);
  if (associatedEngineRoot) {
    for (const candidate of editorBinaryCandidates(associatedEngineRoot)) {
      if (fs.existsSync(candidate)) return candidate;
    }
  }

  const buildTool = findUEBuildTool(project?.engineAssociation ?? null);
  if (!buildTool) return null;

  const engineRoot = path.resolve(buildTool, "..", "..", "..", "..");
  for (const candidate of editorBinaryCandidates(engineRoot)) {
    if (fs.existsSync(candidate)) return candidate;
  }

  return null;
}

async function isBridgeAvailable(host = process.env.UE_MCP_HOST ?? "127.0.0.1", port = 0, timeoutMs = 1000): Promise<boolean> {
  if (!port) return false;
  return new Promise((resolve) => {
    const socket = new net.Socket();
    let resolved = false;

    const timer = setTimeout(() => {
      if (!resolved) {
        resolved = true;
        socket.destroy();
        resolve(false);
      }
    }, timeoutMs);

    socket.once("connect", () => {
      if (!resolved) {
        resolved = true;
        clearTimeout(timer);
        socket.destroy();
        resolve(true);
      }
    });

    socket.once("error", () => {
      if (!resolved) {
        resolved = true;
        clearTimeout(timer);
        resolve(false);
      }
    });

    socket.connect(port, host);
  });
}

/**
 * How long startup must show no change before we suspect something is holding
 * it rather than working. Generous: shader compilation and asset registry scans
 * legitimately sit on one phase for a long time, and the check that follows
 * costs a couple of seconds.
 */
const STALLED_STARTUP_MS = 45_000;
const WINDOW_PROBE_INTERVAL_MS = 60_000;

export interface ReadyPhase {
  phase: string;
  atSeconds: number;
  detail?: string;
}

export interface ReadyResult {
  ready: boolean;
  elapsedSeconds: number;
  /** Phase transitions with the second each happened, oldest first. */
  timeline: ReadyPhase[];
  reason?: string;
  state?: EngineState;
}

/**
 * Block until the editor is genuinely usable, rendering progress to the
 * terminal while it happens.
 *
 * "The bridge socket answers" is not the same as "the editor is ready": the
 * socket comes up mid-startup, while shaders compile and the map loads. A tool
 * that returned there left the caller polling in a loop, burning tokens to
 * rediscover state the plugin already publishes four times a second. So this
 * waits for the snapshot to say `ready` and reports the whole startup as a
 * progress bar rather than handing control back early.
 */
export async function waitForEditorReadyExternal(
  projectPath: string,
  projectDir: string,
  maxWaitSeconds = 300,
  launchedAtMs?: number,
): Promise<ReadyResult> {
  return waitForEditorReady(projectPath, projectDir, maxWaitSeconds, { launchedAtMs });
}

async function waitForEditorReady(
  projectPath: string | null | undefined,
  projectDir: string | undefined,
  maxWaitSeconds: number,
  opts: { showProgress?: boolean; onProgress?: ProgressFn; launchedAtMs?: number } = {},
): Promise<ReadyResult> {
  const startTime = Date.now();
  const maxWaitMs = maxWaitSeconds * 1000;
  const timeline: ReadyPhase[] = [];
  let lastPhase = "";
  let sawSnapshot = false;
  let socketUpSince: number | null = null;
  /** Highest progress value already sent; the stream must never go backwards. */
  let lastReportedProgress = -1;
  /** When startup last visibly moved, for detecting a wait that has gone quiet. */
  let lastChangeAt = Date.now();
  let lastActivity = "";
  let lastWindowProbeAt = 0;

  const bar = opts.showProgress === false ? null : startProgress("Starting Unreal Editor");
  const elapsed = (): number => (Date.now() - startTime) / 1000;

  const finish = (result: ReadyResult): ReadyResult => {
    bar?.stop(
      result.ready
        ? `Editor ready in ${result.elapsedSeconds.toFixed(1)}s`
        : `Editor did not become ready: ${result.reason ?? "timed out"}`,
    );
    return result;
  };

  while (Date.now() - startTime < maxWaitMs) {
    const snapshot = readEngineSnapshot(projectPath);
    const logState = readLogState(projectPath);

    // Until the plugin's snapshot exists, the log is the only sensor - but the
    // log on disk at launch is the PREVIOUS session's, still ending in "editor
    // exited" or a crash from last time. Trust it only once it has been written
    // since this launch, and hand over to the snapshot as soon as there is one,
    // otherwise the timeline walks backwards through two different sessions.
    const logIsCurrent = (logState.secondsSinceWrite ?? Infinity) < elapsed() + 1;
    const snapshotIsCurrent = snapshot !== null && (snapshot.ageSeconds ?? 999) < 10;
    if (snapshotIsCurrent) sawSnapshot = true;

    // Once the snapshot has spoken, it owns the phase. A momentarily missed
    // read is not news, and falling back to the log there made the timeline
    // flip between two vocabularies mid-startup.
    const phase = snapshotIsCurrent
      ? snapshot!.phase
      : sawSnapshot
        ? lastPhase
        : logIsCurrent
          ? logState.phase
          : "launching";
    if (phase && phase !== lastPhase) {
      lastPhase = phase;
      timeline.push({
        phase,
        atSeconds: Number(elapsed().toFixed(1)),
        detail: typeof snapshot?.modulesLoaded === "number" ? `${snapshot.modulesLoaded} modules` : undefined,
      });
    }

    const label = snapshot?.slowTask?.name ?? phase ?? "launching";
    const detail =
      typeof snapshot?.modulesLoaded === "number" && snapshot.modulesLoaded > 0
        ? `${snapshot.modulesLoaded} modules · ${elapsed().toFixed(0)}s`
        : `${elapsed().toFixed(0)}s`;

    bar?.update({ fraction: snapshot?.slowTask?.fraction ?? null, message: label, detail });

    // The channel the user actually sees.
    //
    // ONE scale, and it only ever goes up. The spec requires progress to
    // increase monotonically, and clients that draw a bar from it (or drop
    // out-of-order updates) are entitled to rely on that. An earlier version
    // switched between two scales - percent-of-slow-task when the engine had
    // one, elapsed-seconds-of-timeout when it did not - which alternate
    // constantly during startup, so the value swung 68 -> 12 -> 33 and any
    // strict client discarded the stream. The reference SDK client just fires
    // its callback and hid the bug.
    //
    // Elapsed seconds against the timeout is the only quantity that is
    // monotonic for the whole wait. The engine's own percentage is far more
    // interesting, so it goes in the message, where it can jump around freely.
    if (opts.onProgress) {
      const update = nextProgressUpdate({
        elapsedSeconds: elapsed(),
        maxWaitSeconds,
        lastReportedProgress,
        label,
        detail,
        slowTaskFraction: snapshot?.slowTask?.fraction,
      });
      if (update) {
        lastReportedProgress = update.progress;
        opts.onProgress(update);
      }
    }

    // Waiting cannot fix a prompt that needs a human, or a crash. Both verdicts
    // come from the log, so they only count once the log is this session's.
    if (logIsCurrent && logState.phase === "crashed") {
      return finish({ ready: false, elapsedSeconds: elapsed(), timeline, reason: "the editor crashed during startup", state: await readEngineState(projectPath ?? null) });
    }
    if (snapshot?.modal) {
      return finish({
        ready: false,
        elapsedSeconds: elapsed(),
        timeline,
        reason: `blocked on dialog "${snapshot.modal.title}" [${(snapshot.modal.buttons ?? []).join(", ")}] - answer it with editor(respond_to_dialog)`,
        state: await readEngineState(projectPath ?? null),
      });
    }
    if (logIsCurrent && logState.blocking) {
      return finish({ ready: false, elapsedSeconds: elapsed(), timeline, reason: logState.phase, state: await readEngineState(projectPath ?? null, { probeWindows: true }) });
    }

    // A prompt raised before the bridge module loads - the "modules are missing
    // or built with a different engine version" box is the common one - is a
    // native window, invisible to the snapshot (which has no Slate access that
    // early) and silent in the log unless the engine happened to write about
    // it. Waiting out the full timeout to discover that is useless, so once
    // startup has visibly stopped moving, look at the actual windows. The probe
    // costs a couple of seconds, hence the stall gate and the rate limit.
    // Fingerprint what is MOVING, never a clock. An earlier version folded the
    // log's age into this - a value that ticks every second - so the wait always
    // looked busy and the stall check never fired once in five minutes.
    // Absolute write time is stable while the log sits still and changes the
    // moment the engine writes again.
    const logWrittenAt =
      logState.secondsSinceWrite === null ? "" : Math.round(Date.now() / 1000 - logState.secondsSinceWrite);
    const activity = `${phase}|${snapshot?.slowTask?.name ?? ""}|${snapshot?.slowTask?.fraction ?? ""}|${snapshot?.modulesLoaded ?? ""}|${logWrittenAt}`;
    if (activity !== lastActivity) {
      lastActivity = activity;
      lastChangeAt = Date.now();
    } else if (Date.now() - lastChangeAt > STALLED_STARTUP_MS && Date.now() - lastWindowProbeAt > WINDOW_PROBE_INTERVAL_MS) {
      lastWindowProbeAt = Date.now();
      const stalledState = await readEngineState(projectPath ?? null, { probeWindows: true });
      if (stalledState.dialogs.length > 0) {
        const dialog = stalledState.dialogs[0];
        const text = (dialog.text ?? []).slice(0, 4).join(" | ");
        return finish({
          ready: false,
          elapsedSeconds: elapsed(),
          timeline,
          reason: `blocked on a native dialog before the bridge loaded: "${dialog.title || dialog.className}" ${text}`.trim(),
          state: stalledState,
        });
      }
      if (!stalledState.running) {
        return finish({
          ready: false,
          elapsedSeconds: elapsed(),
          timeline,
          reason: "the editor process is gone - it exited during startup",
          state: stalledState,
        });
      }
    }

    // Ready means both: the plugin says so, and the socket actually answers.
    // #758: the port is re-read every pass rather than resolved once, because
    // the bridge binds a per-project port and only publishes it to
    // Saved/UE_MCP_Bridge/port.json once it starts, so there is nothing to
    // resolve up front. #819: only that file is consulted, and only once it has
    // been written by this launch - a lockfile left behind by a crashed session
    // can point at a port some unrelated editor has since taken, and answering
    // "ready" on the strength of that is how a wait ends up watching the wrong
    // process.
    const target = resolveBridgeTarget(projectDir);
    const socketUp =
      target.ok &&
      lockfileIsFromThisLaunch(target.writtenAtMs, opts.launchedAtMs) &&
      (await isBridgeAvailable(undefined, target.port));
    if (socketUp) {
      if (snapshot?.phase === "ready") {
        return finish({ ready: true, elapsedSeconds: elapsed(), timeline });
      }
      if (!sawSnapshot) {
        // A project on a plugin build without the status module never publishes
        // one. Give it a few seconds to appear before falling back to the old,
        // weaker signal - a single failed read must not be mistaken for that,
        // which is how a mid-startup editor got declared ready.
        if (socketUpSince === null) socketUpSince = Date.now();
        if (Date.now() - socketUpSince > 8000) {
          return finish({ ready: true, elapsedSeconds: elapsed(), timeline, reason: "bridge answered; this plugin build publishes no status snapshot" });
        }
      }
    }

    await new Promise((resolve) => setTimeout(resolve, 250));
  }

  return finish({
    ready: false,
    elapsedSeconds: elapsed(),
    timeline,
    reason: `still not ready after ${maxWaitSeconds}s`,
    state: await readEngineState(projectPath ?? null, { probeWindows: true }),
  });
}

/**
 * Decide the next progress update, or null when there is nothing new to send.
 *
 * Pure and exported so the monotonicity rule is testable without an editor.
 * The rule matters: the MCP spec requires `progress` to increase, and clients
 * that draw a bar (or drop out-of-order updates) rely on it. Elapsed seconds
 * against the timeout is the only value that holds for the whole wait; the
 * engine's own slow-task percentage swings up and down as tasks come and go,
 * so it belongs in the message where it is free to do that.
 */
export function nextProgressUpdate(input: {
  elapsedSeconds: number;
  maxWaitSeconds: number;
  lastReportedProgress: number;
  label: string;
  detail: string;
  slowTaskFraction?: number;
}): { progress: number; total: number; message: string } | null {
  const seconds = Math.min(Math.round(input.elapsedSeconds), input.maxWaitSeconds);
  if (seconds <= input.lastReportedProgress) return null;

  const percent =
    typeof input.slowTaskFraction === "number" ? ` ${Math.round(input.slowTaskFraction * 100)}%` : "";
  return {
    progress: seconds,
    total: input.maxWaitSeconds,
    message: `${input.label}${percent} (${input.detail})`,
  };
}

/** "config init 1.6s -> engine loop initialized 17.1s -> ready 20.7s" */
function describeTimeline(timeline: ReadyPhase[]): string {
  if (timeline.length === 0) return "no phases observed";
  return timeline.map((entry) => `${entry.phase} ${entry.atSeconds}s`).join(" -> ");
}

export async function startEditor(
  project: ProjectContext,
  timeoutSeconds = 300,
  onProgress?: ProgressFn,
): Promise<{ success: boolean; message: string; state?: EngineState; timeline?: ReadyPhase[]; elapsedSeconds?: number }> {
  // Every check below is about ONE editor: the one holding this project. Know
  // which project that is before looking at anything, because without it the
  // only available question is "is any editor running on this machine", and
  // refusing to launch on the strength of somebody else's editor is exactly the
  // bug this guard used to have (#819).
  if (!project.projectPath) {
    return { success: false, message: "No project loaded. Use project(action='set_project') first." };
  }
  const projectDir = path.dirname(project.projectPath);

  // Fast signal first: a bridge answering on the port THIS project published is
  // proof its editor is up, costs a millisecond, and needs no process table at
  // all. The process probe (seconds, on Windows) only runs when that fails,
  // which is also the only case where its extra detail is worth anything.
  // A lockfile whose process is gone was left by a crash, and the port it names
  // can since have been taken by something else, so an answer on it proves
  // nothing. Discarding it here costs a syscall and keeps a stale file from
  // refusing a launch forever.
  const target = resolveBridgeTarget(projectDir);
  const targetIsLive = target.ok && (target.pid === null || isPidAlive(target.pid));
  if (target.ok && targetIsLive && (await isBridgeAvailable(undefined, target.port))) {
    return {
      success: false,
      message: `Editor is already running for this project (its bridge is answering on port ${target.port}).`,
    };
  }

  const alreadyRunning = await findInteractiveEditors(project.projectPath);
  if (alreadyRunning.length > 0) {
    const state = await readEngineState(project.projectPath, { probeWindows: true });
    return {
      success: false,
      message: `Editor is already running for this project (pid ${alreadyRunning.map((p) => p.pid).join(", ")}) but its bridge is not answering yet. ${state.summary}`,
      state,
    };
  }

  const editorExe = findEditorExecutable(project);
  if (!editorExe) {
    return {
      success: false,
      message: NO_EDITOR_BINARY_MSG,
    };
  }

  try {
    // Recorded before the spawn so the wait can tell the lockfile this editor
    // publishes from one an earlier session left behind.
    const launchedAtMs = Date.now();
    const editorProcess = spawn(editorExe, [project.projectPath], {
      stdio: "ignore",
      detached: true,
    });

    editorProcess.unref();

    // Hold here until the editor is actually usable, drawing the startup as a
    // progress bar. Returning as soon as the socket answered is what left
    // callers polling get_engine_state in a loop while shaders compiled.
    const result = await waitForEditorReady(project.projectPath, projectDir, timeoutSeconds, { onProgress, launchedAtMs });

    if (!result.ready) {
      return {
        success: false,
        message: `Editor launched but did not become ready: ${result.reason}. Startup reached: ${describeTimeline(result.timeline)}.`,
        timeline: result.timeline,
        elapsedSeconds: Number(result.elapsedSeconds.toFixed(1)),
        ...(result.state ? { state: result.state } : {}),
      };
    }

    return {
      success: true,
      message: `Editor ready in ${result.elapsedSeconds.toFixed(1)}s (waited through startup: ${describeTimeline(result.timeline)}). No further status polling is needed.`,
      timeline: result.timeline,
      elapsedSeconds: Number(result.elapsedSeconds.toFixed(1)),
    };
  } catch (error) {
    return {
      success: false,
      message: `Failed to launch editor: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
}

// Ask the editor to quit ITSELF, on the game thread, via a deferred slate tick
// so the bridge can reply before the process exits. This is a clean in-process
// exit, not an OS kill.
const EDITOR_SELF_QUIT_PY = [
  "import unreal",
  "def _ue_mcp_quit(dt):",
  "    try:",
  "        unreal.SystemLibrary.quit_editor()",
  "    except Exception as e:",
  "        unreal.log_error('ue-mcp quit_editor failed: ' + str(e))",
  "unreal.register_slate_post_tick_callback(_ue_mcp_quit)",
].join("\n");

/**
 * The .uproject inside a project directory. The stop/restart paths are handed a
 * directory, but the process probe matches editors by the project file they
 * have open, so resolve one from the other.
 */
function uprojectInDir(projectDir?: string): string | null {
  if (!projectDir) return null;
  try {
    const match = fs.readdirSync(projectDir).find((f) => f.toLowerCase().endsWith(".uproject"));
    return match ? path.join(projectDir, match) : null;
  } catch {
    return null;
  }
}

/**
 * Ask the editor to quit itself via the bridge (`execute_python` -> quit_editor).
 * Returns true if the request was delivered. Never touches the OS process table.
 */
function requestEditorSelfQuit(port: number): Promise<boolean> {
  return new Promise<boolean>((resolve) => {
    let settled = false;
    const ws = new WebSocket(`ws://127.0.0.1:${port}`);
    const finish = (v: boolean) => {
      if (settled) return;
      settled = true;
      try { ws.close(); } catch { /* ignore */ }
      resolve(v);
    };
    const timer = setTimeout(() => finish(false), 8000);
    ws.on("open", () => ws.send(JSON.stringify({ id: "ue-mcp-stop", method: "execute_python", params: { code: EDITOR_SELF_QUIT_PY } })));
    ws.on("message", () => { clearTimeout(timer); finish(true); });
    ws.on("error", () => { clearTimeout(timer); finish(false); });
  });
}

/**
 * Stop the editor by asking it to quit ITSELF through the bridge. ue-mcp NEVER
 * issues an OS kill: `taskkill /IM UnrealEditor.exe` matches by image name and
 * would also close the user's other editors (e.g. their real project). `force`
 * is accepted for back-compat but there is deliberately no force-kill path.
 * Success is confirmed by the project's own bridge port going quiet, so it is
 * specific to this editor even when others are open.
 *
 * The port comes from this project's lockfile and nowhere else, and the process
 * it names is checked before the quit goes out (#819).
 */
export async function stopEditor(force = false, projectDir?: string): Promise<{ success: boolean; message: string; state?: EngineState }> {
  void force;

  const projectPath = uprojectInDir(projectDir);
  if (!projectDir || !projectPath) {
    return {
      success: false,
      message:
        "No project is loaded, so stop_editor has no editor to aim at. Use project(action='set_project') first. " +
        "A lifecycle action never falls back to whichever editor it can find.",
    };
  }

  const target = resolveBridgeTarget(projectDir);
  if (!target.ok) {
    const running = await findInteractiveEditors(projectPath);
    if (running.length === 0) {
      return { success: false, message: `Editor is not running for this project. ${target.reason}` };
    }
    const state = await readEngineState(projectPath, { probeWindows: true });
    return {
      success: false,
      message:
        `Editor is running (pid ${running.map((p) => p.pid).join(", ")}) but no bridge port is published for it, ` +
        `so it cannot be asked to quit cleanly. ${target.reason} ${state.summary} ` +
        "Close it manually - ue-mcp never force-kills processes.",
      state,
    };
  }

  // The lockfile was written by an editor that had THIS project open, but it
  // outlives a crash, and the port it names can be taken by something else
  // afterwards. Confirm the process it points at is still that editor before
  // sending anything a quit request (#819).
  if (target.pid !== null) {
    const owner = await findEditorByPid(target.pid);
    if (!owner) {
      return {
        success: false,
        message:
          `The bridge lockfile at ${target.lockfilePath} names pid ${target.pid}, which is no longer running - ` +
          "the editor exited without removing it. Nothing was asked to quit, because port " +
          `${target.port} may since have been taken by an unrelated process. Delete that file if it persists.`,
      };
    }
    if (owner.projectPath !== null && !editorOwnsProject(owner, projectPath)) {
      return {
        success: false,
        message:
          `The bridge lockfile at ${target.lockfilePath} names pid ${target.pid}, but that process now has ` +
          `${owner.projectPath} open, not this project. Nothing was asked to quit. Delete that file if it persists.`,
      };
    }
  } else if ((await findInteractiveEditors(projectPath)).length === 0) {
    // Plugin builds before the lockfile carried a pid leave nothing to identify
    // the listener with, so the process table has to answer instead.
    return {
      success: false,
      message:
        `The bridge lockfile at ${target.lockfilePath} records no pid (older plugin build) and no editor for this ` +
        `project is running, so port ${target.port} cannot be shown to belong to it. Nothing was asked to quit.`,
    };
  }

  const port = target.port;
  const bridgeUp = await isBridgeAvailable("127.0.0.1", port);
  if (!bridgeUp && (await findInteractiveEditors(projectPath)).length === 0) {
    return { success: false, message: "Editor is not running" };
  }
  if (!bridgeUp) {
    // "Unreachable" is where the user is left guessing, so say what the engine
    // is actually doing: a modal dialog waiting on an answer, a slow task at
    // 60%, or a game thread that stopped ticking are all visible from outside.
    const state = await readEngineState(projectPath, { probeWindows: true });
    return {
      success: false,
      message: `Editor is running but its bridge is unreachable, so it cannot be asked to quit cleanly. ${state.summary} Close it manually - ue-mcp never force-kills processes.`,
      state,
    };
  }

  const quitSent = await requestEditorSelfQuit(port);
  if (!quitSent) {
    return {
      success: false,
      message: "Could not deliver a quit request to the editor bridge. Close the editor manually - ue-mcp never force-kills processes.",
    };
  }

  // Confirm via the project's own bridge port closing - specific to this editor.
  for (let i = 0; i < 20; i++) {
    await new Promise((resolve) => setTimeout(resolve, 1000));
    if (!(await isBridgeAvailable("127.0.0.1", port))) {
      return { success: true, message: "Editor quit itself via the bridge" };
    }
  }
  return {
    success: false,
    message: "Asked the editor to quit but its bridge is still up after 20s. Close it manually - ue-mcp never force-kills processes.",
  };
}

export async function restartEditor(project: ProjectContext, bridge?: { connect: (timeoutMs?: number) => Promise<void> }): Promise<{ success: boolean; message: string }> {
  // Same rule as start and stop: without a loaded project there is no editor
  // this is about, and the machine-wide answer is somebody else's editor (#819).
  if (!project.projectPath) {
    return { success: false, message: "No project loaded. Use project(action='set_project') first." };
  }

  const stopResult = await stopEditor(false, project.projectDir ?? undefined);
  // Whether the stop mattered is a question about THIS project's editor: a
  // failed stop with nothing of ours left running just means it was already
  // down, and another project's editor being up says nothing either way.
  if (!stopResult.success && (await findInteractiveEditors(project.projectPath)).length > 0) {
    return { success: false, message: `Failed to stop editor: ${stopResult.message}` };
  }

  // Wait for process to fully terminate and release locks
  await new Promise((resolve) => setTimeout(resolve, 3000));

  const startResult = await startEditor(project);
  if (!startResult.success) {
    return startResult;
  }

  // Reconnect the bridge if provided
  if (bridge) {
    try {
      await bridge.connect(5000);
    } catch {
      // Bridge reconnect timer will handle it
    }
  }

  return startResult;
}

export interface BuildResult {
  success: boolean;
  message: string;
  exitCode: number | null;
}

function getPlatformString(): string {
  if (IS_WINDOWS) return "Win64";
  if (process.platform === "darwin") return "Mac";
  return "Linux";
}

export async function buildProject(
  projectPath: string,
  opts: { onOutput?: (line: string) => void } = {},
): Promise<BuildResult> {
  const resolvedPath = path.resolve(projectPath);
  const buildTool = findUEBuildTool(readEngineAssociation(resolvedPath));
  if (!buildTool) {
    return {
      success: false,
      exitCode: null,
      message:
        "Unreal Engine build tool not found. Set UE_BUILD_TOOL_PATH or install UE5.3+ to a default location.",
    };
  }

  if (!fs.existsSync(resolvedPath)) {
    return { success: false, exitCode: null, message: `Project file not found: ${resolvedPath}` };
  }

  const projectName = path.basename(resolvedPath, ".uproject");
  const target = `${projectName}Editor`;
  const platform = getPlatformString();

  // #740: the quotes around the project path are SHELL syntax, not part of the
  // value. On Windows the args are joined into a single `cmd /c` string, so
  // they are required. Off Windows the args go straight into argv with no shell
  // to strip them, so UnrealBuildTool received a path containing literal quote
  // characters and reported "Unable to find project file" for a file that was
  // plainly there - while the same command pasted into a terminal worked,
  // because the shell removed them first.
  const commonArgs = [target, platform, "Development"];
  const tailArgs = ["-WaitMutex", "-FromMsBuild"];
  const windowsArgs = [...commonArgs, `-Project="${resolvedPath}"`, ...tailArgs];
  const posixArgs = [...commonArgs, `-Project=${resolvedPath}`, ...tailArgs];

  return new Promise((resolve) => {
    let proc;
    if (IS_WINDOWS) {
      const quotedCommand = `"${buildTool}"`;
      const fullCommand = `cmd /c "${quotedCommand} ${windowsArgs.join(" ")}"`;
      proc = spawn(fullCommand, [], { shell: true, stdio: "pipe" });
    } else {
      proc = spawn(buildTool, posixArgs, { stdio: "pipe" });
    }

    const forward = (data: Buffer) => {
      const text = data.toString();
      if (opts.onOutput) opts.onOutput(text);
      else process.stdout.write(text);
    };

    if (proc.stdout) proc.stdout.on("data", forward);
    if (proc.stderr) proc.stderr.on("data", forward);

    proc.on("close", (code) => {
      // A build is the only event that can turn a "stale plugin" verdict fresh
      // ahead of the cache TTL, so drop the cached answer here rather than
      // making the next get_status report a binary that no longer exists.
      invalidatePluginFreshness();
      resolve(
        code === 0
          ? { success: true, exitCode: 0, message: "Build succeeded" }
          : { success: false, exitCode: code, message: `Build failed with exit code ${code}` },
      );
    });

    proc.on("error", (err) => {
      resolve({ success: false, exitCode: null, message: `Build error: ${err.message}` });
    });
  });
}
