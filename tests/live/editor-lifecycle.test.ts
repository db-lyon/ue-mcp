// @live-editor lifecycle
/**
 * The server-side lifecycle actions, against the real editor (#1223, items 4 and 5).
 *
 * Smoke only reaches bridge handlers, so stop_editor, start_editor,
 * build_project and the rebuild flow had no gate at all. #1221 shipped through
 * that hole: after a crash, the crash reporter held the project DLLs and every
 * later build_project failed with LNK1104.
 *
 * These cases stop, crash, build and start the tests/ue_mcp editor, which the
 * rest of the live suite promises never to do. So they run only when asked:
 *
 *     npm run test:live -- --lifecycle
 *
 * which runs this file alone. Every case leaves an editor running on
 * tests/ue_mcp when it ends, pass or fail.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { spawn, execFileSync, type ChildProcess } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { liveTarget, type LiveTarget } from "./harness.js";
import { LiveServer, resultJson, type LiveCallResult } from "./server.js";
import { askOnce, bridgePortCandidates } from "../../scripts/bridge-target.mjs";
import { isPidAlive } from "../../src/bridge/editor-target.js";

const ENABLED = process.env.UE_MCP_LIVE_LIFECYCLE === "1";
const WINDOWS = process.platform === "win32";

const START_TIMEOUT_S = 900;
const BUILD_TIMEOUT_MS = 1_800_000;
/** Answers the post-crash "Restore Packages" prompt so a relaunch cannot wedge on it. */
const DIALOG_POLICY = "Restore=no";

let target: LiveTarget;
let server: LiveServer;

type Json = Record<string, unknown>;

function body(result: LiveCallResult): Json {
  try {
    return resultJson<Json>(result);
  } catch {
    return { text: result.text };
  }
}

/** The editor currently answering for tests/ue_mcp: its pid and when it started, or null. */
async function runningEditor(): Promise<{ pid: number; startedAt?: string; port: number } | null> {
  const { candidates } = bridgePortCandidates({ projectDir: target.projectDir });
  for (const candidate of candidates) {
    try {
      const caps = (await askOnce(`ws://${target.host}:${candidate.port}`, "get_bridge_capabilities", {}, 5000)) as {
        pid?: number;
        startedAt?: string;
      };
      if (typeof caps?.pid === "number") return { pid: caps.pid, startedAt: caps.startedAt, port: candidate.port };
    } catch {
      // Not this port.
    }
  }
  return null;
}

/** Logs each step's duration, so a slow case says where its time went. */
function phases(name: string): (step: string) => void {
  const start = Date.now();
  let last = start;
  return (step) => {
    const now = Date.now();
    console.error(`[lifecycle] ${name}: ${step} ${((now - last) / 1000).toFixed(1)}s (total ${((now - start) / 1000).toFixed(1)}s)`);
    last = now;
  };
}

async function waitFor(what: string, check: () => Promise<boolean> | boolean, timeoutMs: number): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await check()) return;
    await new Promise((r) => setTimeout(r, 1000));
  }
  throw new Error(`timed out after ${Math.round(timeoutMs / 1000)}s waiting for ${what}`);
}

async function stopEditor(): Promise<Json> {
  // stop_editor refuses on unsaved work by design, so save first. An untitled map has no file to save to and
  // would raise the engine's save prompt on close, so a saved map is loaded over it first.
  await server.call("level", { action: "load", levelPath: "/Game/MCP_Home", discardUnsaved: true });
  await server.call("editor", { action: "save_dirty", includeMaps: true });
  return body(await server.call("editor", { action: "stop_editor" }));
}

async function startEditor(): Promise<LiveCallResult> {
  return server.call("editor", {
    action: "start_editor",
    timeout: START_TIMEOUT_S,
    dialogPolicy: DIALOG_POLICY,
    timeoutMs: START_TIMEOUT_S * 1000,
  });
}

/** Leave an editor running whatever happened above. */
async function ensureRunning(): Promise<void> {
  if (await runningEditor()) return;
  await startEditor();
}

/** Every advertised `epic_*` action, as category.action, read off tools/list. */
async function advertisedEpicActions(): Promise<string[]> {
  const tools = await server.listTools();
  return tools.flatMap((t) => {
    // The action enum is either the property's own or the first member of an anyOf beside a plain string.
    const action = (t.inputSchema as { properties?: { action?: { enum?: string[]; anyOf?: Array<{ enum?: string[] }> } } }).properties?.action;
    const actions = [...(action?.enum ?? []), ...(action?.anyOf ?? []).flatMap((m) => m.enum ?? [])];
    return actions.filter((a) => a.startsWith("epic_")).map((a) => `${t.name}.${a}`);
  }).sort();
}

/** Crash reporter pids, any project. Windows only. */
function crashReporterPids(): number[] {
  const out = execFileSync(
    "tasklist",
    ["/FO", "CSV", "/NH", "/FI", "IMAGENAME eq CrashReportClientEditor.exe"],
    { encoding: "utf8" },
  );
  const pids: number[] = [];
  for (const line of out.split(/\r?\n/)) {
    const cells = line.split('","').map((c) => c.replace(/^"|"$/g, ""));
    if (cells.length > 1 && /CrashReportClient/i.test(cells[0])) pids.push(Number(cells[1]));
  }
  return pids;
}

/** Crash reporters launched to watch this editor pid (-MONITOR=<pid>). */
function crashReportersWatching(editorPid: number): number[] {
  const out = execFileSync(
    "powershell",
    [
      "-NoProfile",
      "-Command",
      "Get-CimInstance Win32_Process -Filter \"Name='CrashReportClientEditor.exe'\" | ForEach-Object { \"$($_.ProcessId)|$($_.CommandLine)\" }",
    ],
    { encoding: "utf8" },
  );
  const pids: number[] = [];
  for (const line of out.split(/\r?\n/)) {
    const [pid, ...rest] = line.split("|");
    const watched = /-MONITOR=(\d+)/i.exec(rest.join("|"));
    if (watched && Number(watched[1]) === editorPid) pids.push(Number(pid));
  }
  return pids;
}

/** The game module's editor DLL, which every link of the project rewrites. */
function projectModuleDll(): string {
  const dir = path.join(target.projectDir, "Binaries", "Win64");
  const hit = fs.existsSync(dir)
    ? fs.readdirSync(dir).find((n) => /^UnrealEditor-ue_mcp\.dll$/i.test(n))
    : undefined;
  if (!hit) throw new Error(`no UnrealEditor-ue_mcp.dll under ${dir}; build the project once first`);
  return path.join(dir, hit);
}

/** Bump a project source file's mtime so the next build compiles and relinks the module. */
function forceRelink(): void {
  const source = path.join(target.projectDir, "Source", "ue_mcp", "ue_mcp.cpp");
  const now = new Date();
  fs.utimesSync(source, now, now);
}

async function build(): Promise<Json> {
  return body(await server.call("project", { action: "build", timeoutMs: BUILD_TIMEOUT_MS }));
}

describe.skipIf(!ENABLED)("editor lifecycle (opt-in: npm run test:live -- --lifecycle)", () => {
  beforeAll(async () => {
    target = await liveTarget();
    // The editors these cases start inherit the server's environment. Under the sandboxed profile they got
    // an empty derived data cache of their own, and a relaunch took minutes rebuilding it. The server's own
    // files stay redirected by LiveServer.
    const profile = process.env.USERPROFILE ?? process.env.HOME ?? "";
    server = await LiveServer.start({ projects: [target.uproject], env: profile ? { USERPROFILE: profile } : {} });
  }, 120_000);

  afterAll(async () => {
    if (!server) return;
    try {
      await ensureRunning();
    } finally {
      await server.close();
    }
  }, START_TIMEOUT_S * 1000 + 60_000);

  it("stop_editor closes the project's editor and its process exits", async () => {
    const before = await runningEditor();
    expect(before, "no editor is running on tests/ue_mcp").toBeTruthy();

    const stopped = await stopEditor();
    expect(stopped.success, JSON.stringify(stopped)).toBe(true);
    await waitFor(`editor pid ${before!.pid} to exit`, () => !isPidAlive(before!.pid), 180_000);
    expect(await runningEditor()).toBeNull();

    // Nothing left to stop is its own answer, not a success.
    const again = await stopEditor();
    expect(again.success).toBe(false);
    expect(again.alreadyStopped).toBe(true);
  }, 300_000);

  it("start_editor returns only once the editor answers, as a new process", async () => {
    const previous = await runningEditor();
    if (previous) await stopEditor();

    const started = await startEditor();
    const result = body(started);
    expect(started.isError, started.text).toBe(false);
    expect(result.success, JSON.stringify(result)).toBe(true);

    // No polling: a successful start_editor promises a ready bridge.
    const level = await server.call("level", { action: "get_current" });
    expect(level.isError, level.text).toBe(false);
    const now = await runningEditor();
    expect(now).toBeTruthy();
    if (previous) expect(now!.pid).not.toBe(previous.pid);

    // A second start launches nothing and says so.
    const again = body(await startEditor());
    expect(again.success).toBe(false);
    expect(again.alreadyRunning).toBe(true);
  }, START_TIMEOUT_S * 1000 * 2);

  it("advertises the same epic_* actions after the editor restarts (#1538)", async () => {
    await ensureRunning();
    // The Python toolsets register after the bridge answers, so an editor the test before this one
    // just launched has not advertised them yet; the snapshot is taken once they are in.
    let before: string[] = [];
    await waitFor("the late epic_* toolsets before the restart", async () => {
      before = await advertisedEpicActions();
      return before.some((a) => a.startsWith("level."));
    }, 420_000).catch(() => undefined);
    expect(before.filter((a) => a.startsWith("level.")).length, before.join(", ")).toBeGreaterThan(0);

    await stopEditor();
    const started = await startEditor();
    expect(started.isError, started.text).toBe(false);

    // Toolsets that register late arrive through a re-read, not on the handshake.
    let after: string[] = [];
    await waitFor("every epic_* action to come back", async () => {
      after = await advertisedEpicActions();
      return after.length === before.length;
    }, 420_000).catch(() => undefined);
    expect(after.filter((a) => !before.includes(a))).toEqual([]);
    expect(before.filter((a) => !after.includes(a))).toEqual([]);
  }, START_TIMEOUT_S * 1000 * 2 + 2 * 420_000);

  it.skipIf(!WINDOWS)(
    "stop_editor after a real crash ends the crash reporter, and the build that follows links (#1221)",
    async () => {
      const phase = phases("crash");
      const before = await runningEditor();
      expect(before, "no editor is running on tests/ue_mcp").toBeTruthy();
      const reportersBefore = new Set(crashReporterPids());

      // The engine's own crash command. The call cannot return: the editor is gone.
      await server.call("editor", { action: "execute_command", command: "debug crash" }).catch(() => undefined);
      await waitFor(`editor pid ${before!.pid} to die`, () => !isPidAlive(before!.pid), 180_000);
      phase("crash until the editor process exited");
      // The reporter is either the companion launched with the editor to watch it, or one the crash started.
      const reporterFor = (): number | undefined =>
        crashReportersWatching(before!.pid)[0] ?? crashReporterPids().find((pid) => !reportersBefore.has(pid));
      await waitFor(
        "a crash reporter for the crashed editor (is the crash reporter disabled on this machine?)",
        () => reporterFor() !== undefined,
        120_000,
      );
      const reporter = reporterFor()!;
      phase("crash reporter found");

      const stopped = await stopEditor();
      phase("stop_editor after the crash");
      expect(stopped.alreadyStopped, JSON.stringify(stopped)).toBe(true);
      const cleanup = stopped.crashReporters as { ended?: Array<{ pid: number; reason: string }>; failed?: unknown[] };
      expect(cleanup, "stop_editor reported no crash reporter cleanup").toBeTruthy();
      expect(cleanup.ended?.map((e) => e.pid)).toContain(reporter);
      expect(cleanup.failed ?? []).toEqual([]);
      await waitFor(`crash reporter ${reporter} to exit`, () => !isPidAlive(reporter), 60_000);
      phase("crash reporter exited");

      // The regression: with the reporter gone the link can write the module DLL.
      forceRelink();
      const built = await build();
      expect(built.success, String(built.message ?? JSON.stringify(built))).toBe(true);
      expect(built.lockedOutputs).toBeUndefined();
      phase("build");

      const started = body(await startEditor());
      expect(started.success, JSON.stringify(started)).toBe(true);
      phase("start_editor");
    },
    BUILD_TIMEOUT_MS + START_TIMEOUT_S * 1000 + 600_000,
  );

  it.skipIf(!WINDOWS)(
    "build_project names a locked output and the process holding it, then builds once it is released",
    async () => {
      if (await runningEditor()) {
        const stopped = await stopEditor();
        expect(stopped.success, JSON.stringify(stopped)).toBe(true);
      }
      await waitFor("the editor to be gone", async () => (await runningEditor()) === null, 180_000);

      const dll = projectModuleDll();
      // A process that holds the DLL open with no sharing, the way a crash reporter does.
      const holder: ChildProcess = spawn(
        "powershell.exe",
        [
          "-NoProfile",
          "-Command",
          `$f = [System.IO.File]::Open('${dll.replace(/'/g, "''")}', 'Open', 'Read', 'None'); Write-Output locked; Start-Sleep -Seconds 3600`,
        ],
        { stdio: ["ignore", "pipe", "ignore"] },
      );
      try {
        await new Promise<void>((resolve, reject) => {
          const timer = setTimeout(() => reject(new Error("the lock holder never took the lock")), 30_000);
          holder.stdout!.on("data", (d: Buffer) => {
            if (String(d).includes("locked")) {
              clearTimeout(timer);
              resolve();
            }
          });
          holder.on("exit", (code) => reject(new Error(`the lock holder exited early with ${code}`)));
        });

        forceRelink();
        const failed = await build();
        expect(failed.success).toBe(false);
        const locked = (failed.lockedOutputs ?? []) as Array<{ file: string; holder: string | null }>;
        expect(locked.length, String(failed.message ?? JSON.stringify(failed))).toBeGreaterThan(0);
        expect(locked.some((l) => path.basename(l.file).toLowerCase() === path.basename(dll).toLowerCase())).toBe(true);
        const named = locked.find((l) => l.holder)?.holder;
        if (named) expect(named.toLowerCase()).toContain("powershell");
        expect(String(failed.message)).toMatch(/locked|in use|being used|held open/i);
      } finally {
        holder.kill();
      }
      await waitFor("the lock holder to exit", () => holder.exitCode !== null || holder.signalCode !== null, 30_000);

      forceRelink();
      const built = await build();
      expect(built.success, String(built.message ?? JSON.stringify(built))).toBe(true);

      const started = body(await startEditor());
      expect(started.success, JSON.stringify(started)).toBe(true);
    },
    BUILD_TIMEOUT_MS * 2 + START_TIMEOUT_S * 1000 + 600_000,
  );

  it(
    "the rebuild flow stops, builds and relaunches the editor in one run",
    async () => {
      const phase = phases("rebuild flow");
      await ensureRunning();
      const before = await runningEditor();
      expect(before).toBeTruthy();

      forceRelink();
      const run = await server.call("flow", {
        action: "run",
        flowName: "live_lifecycle_rebuild",
        timeoutMs: BUILD_TIMEOUT_MS + START_TIMEOUT_S * 1000,
      });
      expect(run.isError, run.text).toBe(false);
      phase("flow run (save, stop, build, start)");

      const after = await runningEditor();
      expect(after, "no editor answers after the rebuild flow").toBeTruthy();
      expect(after!.pid).not.toBe(before!.pid);
      expect(isPidAlive(before!.pid)).toBe(false);
      const level = await server.call("level", { action: "get_current" });
      expect(level.isError, level.text).toBe(false);
    },
    BUILD_TIMEOUT_MS + START_TIMEOUT_S * 1000 + 600_000,
  );
});
