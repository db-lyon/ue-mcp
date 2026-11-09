/**
 * `ue-mcp daemon <run|start|stop|status|open> [project] [--json] [--browser]`
 *
 * run     the daemon in the foreground (what start launches)
 * start   a detached daemon for the project, if none is running
 * stop    ask the project's daemon to exit
 * status  the project's daemon: discovery record and health
 * open    the UI URL (with --browser, open it)
 */
import * as path from "node:path";
import { findUProject } from "../config/uproject-path.js";
import { readAnsweringDiscovery } from "../daemon/discovery.js";
import { runDaemon } from "../daemon/daemon.js";
import { ensureDaemon } from "../daemon/shim.js";
import { waitForExit } from "../daemon/supersede-watch.js";

/** How long `daemon stop` waits for the process to exit. */
const STOP_WAIT_MS = 30_000;
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { fail, ok } from "./ui/ansi.js";
import type { CliUsage } from "./cli-main.js";
import { openUrl } from "../core/open-url.js";

const USAGE = "usage: ue-mcp daemon <run|start|stop|status|open> [project] [--json] [--browser]";

export const usage: CliUsage = {
  usage: "ue-mcp daemon <run|start|stop|status|open> [project] [flags]",
  summary: "Run, start, stop or inspect a project's daemon.",
  strict: true,
  flags: [
    { name: "--json", description: "Print one JSON document" },
    { name: "--browser", description: "open: use the system browser" },
    { name: "--editor", value: "<name-or-path>", description: "Target one of the editors this server drives" },
  ],
};

export async function run(argv: string[]): Promise<number | void> {
  const json = argv.includes("--json");
  const out = (v: unknown): void => {
    process.stdout.write(JSON.stringify(v, null, 2) + "\n");
  };
  const browser = argv.includes("--browser");
  const [sub, ...rest] = argv.filter((a) => a !== "--json" && a !== "--browser");
  let target: { projectPath?: string; rest: string[] };
  try {
    target = takeEditorTarget(rest);
  } catch (e) {
    fail(e instanceof EditorFlagError ? e.message : String(e));
    return 2;
  }
  const positional = target.rest.filter((a) => !a.startsWith("-"));
  const projects = target.projectPath ? [target.projectPath, ...positional] : positional;
  if (projects.length === 0) {
    const found = findUProject(process.cwd());
    if (found) projects.push(found);
  }
  if (!sub || !["run", "start", "stop", "status", "open"].includes(sub)) {
    fail(USAGE);
    return 2;
  }
  if (projects.length === 0) {
    fail("No .uproject found. Run from your project directory or pass the path.");
    return 2;
  }
  const projectDir = path.dirname(path.resolve(projects[0]));

  if (sub === "run") return runDaemon(projects);

  if (sub === "start") {
    const d = await ensureDaemon(projects[0]);
    if (json) out({ ok: true, pid: d.pid, port: d.port, version: d.version });
    else ok(`daemon ${d.version} running (pid ${d.pid}, port ${d.port})`);
    return 0;
  }

  if (sub === "open") {
    const live = await ensureDaemon(projects[0]);
    const url = `http://127.0.0.1:${live.port}/ui/?token=${live.token}`;
    if (json) out({ ok: true, url });
    else console.log(url);
    if (browser) openUrl(url);
    return 0;
  }

  // A record whose pid was reused by another process is no daemon to stop or report (#1460).
  const d = await readAnsweringDiscovery(projectDir);
  if (!d) {
    if (json) out({ ok: true, running: false });
    else ok("no daemon running for this project");
    return 0;
  }
  const auth = { authorization: `Bearer ${d.token}` };

  if (sub === "stop") {
    await fetch(`http://127.0.0.1:${d.port}/v1/shutdown`, { method: "POST", headers: auth }).catch(() => undefined);
    // Return once the process is gone, so a start right after never runs beside it.
    if (!(await waitForExit(d.pid, STOP_WAIT_MS))) {
      const error = `daemon pid ${d.pid} did not exit within ${STOP_WAIT_MS / 1000}s`;
      if (json) out({ ok: false, pid: d.pid, error });
      else fail(error);
      return 1;
    }
    if (json) out({ ok: true, stopped: d.pid });
    else ok(`stopped daemon pid ${d.pid}`);
    return 0;
  }

  try {
    const res = await fetch(`http://127.0.0.1:${d.port}/v1/health`, { headers: auth });
    const health = await res.json();
    if (json) out({ ok: true, running: true, discovery: { pid: d.pid, port: d.port, version: d.version, startedAt: d.startedAt }, health });
    else {
      const h = health as { editors: Array<{ name: string; connected: boolean; lastDisconnect: { cause: string } | null }>; clients: number };
      ok(`daemon ${d.version} pid ${d.pid} port ${d.port}, ${h.clients} client(s)`);
      for (const e of h.editors) ok(`${e.name}: ${e.connected ? "connected" : `not connected${e.lastDisconnect ? ` (${e.lastDisconnect.cause})` : ""}`}`);
    }
    return 0;
  } catch (e) {
    if (json) out({ ok: false, running: true, error: e instanceof Error ? e.message : String(e) });
    else fail(`daemon pid ${d.pid} did not answer: ${e instanceof Error ? e.message : e}`);
    return 1;
  }
}
