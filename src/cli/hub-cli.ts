// @session per-session: Queues one project's hub runs through its daemon, chosen with --project, and waits for them.
/**
 * `ue-mcp hub run <test|suite>`: queue a Playtest Hub test (an arrangement,
 * or <arrangement>:<region>) or every test of a suite through the project's
 * daemon, wait for the runs to end, write a JSON report and exit 0 only when
 * every run passed. For CI and scripts; the runs land in the same history
 * Studio and agents read.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { findUProject } from "../config/uproject-path.js";
import { runEnded, type RunRecord } from "../contract/hub.js";
import { daemonRequest, type DaemonResponse } from "../daemon/client.js";
import { ensureDaemon } from "../daemon/shim.js";
import { BOLD, DIM, GREEN, RED, RESET, YELLOW } from "../terminal/ansi.js";
import type { CliUsage } from "./cli-main.js";
import { selectProject } from "./editor-flag.js";

export const usage: CliUsage = {
  usage: "ue-mcp hub run <test|suite> [flags]",
  summary:
    "Run a Playtest Hub test or suite and wait for it.\n" +
    "  run <id>   A test (<arrangement> or <arrangement>:<region>) or a suite id. Exits 0 when every run passed,\n" +
    "             1 when any failed, errored or was skipped, 2 on a bad command.",
  strict: true,
  flags: [
    { name: "--headless", description: "Play on a hidden editor ue-mcp launches on a copy of the project" },
    { name: "--editor", description: "Play on the open editor" },
    { name: "--report", value: "<file>", description: "Where to write the JSON report (default hub-report.json in the working directory)" },
    { name: "--timeout", value: "<seconds>", description: "Give up waiting after this long (default 3600)" },
    { name: "--project", value: "<uproject>", description: "The project, instead of the one in the working directory" },
    { name: "--json", description: "Print the report instead of a summary" },
  ],
};

/** What `hub run` writes: every run of the batch, what was skipped and the verdict. */
export interface HubRunReport {
  target: string;
  kind: "test" | "suite";
  batch: string | null;
  ok: boolean;
  passed: number;
  failed: number;
  skipped: Array<{ test: string; reason: string }>;
  runs: RunRecord[];
  error?: string;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

function valueOf(args: string[], flag: string): string | undefined {
  const at = args.indexOf(flag);
  return at >= 0 ? args[at + 1] : undefined;
}

function answer(res: DaemonResponse): Record<string, unknown> {
  const body = (res.json && typeof res.json === "object" ? res.json : {}) as Record<string, unknown>;
  if (res.status >= 400) throw new Error(typeof body.error === "string" ? body.error : `the daemon answered ${res.status}`);
  return body;
}

/** Entry point for `ue-mcp hub run`. */
export async function run(argv: string[]): Promise<number> {
  let rest: string[];
  let project: string | undefined;
  try {
    const target = selectProject(argv, { projectFlag: true, valueFlags: new Set(["--report", "--timeout"]) });
    rest = target.rest;
    project = target.projectPath;
  } catch (e) {
    console.error(`  ${RED}${e instanceof Error ? e.message : String(e)}${RESET}`);
    return 2;
  }
  const json = rest.includes("--json");
  const reportPath = path.resolve(valueOf(rest, "--report") ?? "hub-report.json");
  const timeoutS = Number(valueOf(rest, "--timeout") ?? 3600);
  const headless = rest.includes("--headless");
  const editor = rest.includes("--editor");
  const positional = rest.filter((a, i) => !a.startsWith("--") && rest[i - 1] !== "--report" && rest[i - 1] !== "--timeout");
  const [sub, id] = positional;
  const usageError = (message: string): number => {
    console.error(`  ${RED}${message}${RESET}`);
    return 2;
  };
  if (sub !== "run") return usageError(sub ? `Unknown hub command '${sub}'. Use run.` : "Name a hub command: run.");
  if (!id) return usageError("hub run needs a test (<arrangement> or <arrangement>:<region>) or a suite id.");
  if (headless && editor) return usageError("Give --headless or --editor, not both.");
  if (!Number.isFinite(timeoutS) || timeoutS <= 0) return usageError("--timeout is a number of seconds.");
  const uproject = findUProject(project ?? process.cwd());
  if (!uproject) return usageError("No .uproject found. Run from your project directory or pass --project <uproject>.");

  const report: HubRunReport = { target: id, kind: "test", batch: null, ok: false, passed: 0, failed: 0, skipped: [], runs: [] };
  const finish = (): number => {
    report.passed = report.runs.filter((r) => r.state === "passed").length;
    report.failed = report.runs.length - report.passed;
    report.ok = !report.error && report.runs.length > 0 && report.failed === 0 && report.skipped.length === 0;
    fs.mkdirSync(path.dirname(reportPath), { recursive: true });
    fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
    if (json) console.log(JSON.stringify(report, null, 2));
    else {
      for (const r of report.runs) {
        const color = r.state === "passed" ? GREEN : r.state === "failed" ? RED : YELLOW;
        console.log(`  ${color}${r.state}${RESET}  ${BOLD}${r.title}${RESET} ${DIM}(${r.test}, ${r.runner})${RESET}${r.error ? `: ${r.error}` : ""}`);
        for (const c of r.checks) if (c.status === "failed") console.log(`    ${RED}${c.id}${RESET}: ${c.message ?? "failed"}`);
      }
      for (const s of report.skipped) console.log(`  ${YELLOW}skipped${RESET}  ${s.test}: ${s.reason}`);
      if (report.error) console.error(`  ${RED}${report.error}${RESET}`);
      console.log(`  ${report.ok ? GREEN : RED}${report.passed} passed, ${report.failed} not passed, ${report.skipped.length} skipped${RESET}  ${DIM}${reportPath}${RESET}`);
    }
    return report.ok ? 0 : 1;
  };

  try {
    const daemon = await ensureDaemon(uproject);
    const suites = answer(await daemonRequest(daemon, "hub.suites")).suites as Array<{ id: string }> | undefined;
    report.kind = suites?.some((s) => s.id === id) ? "suite" : "test";
    const queued = answer(await daemonRequest(daemon, "hub.queue.add", {
      method: "POST",
      body: { [report.kind]: id, ...(headless ? { runner: "headless" } : editor ? { runner: "editor" } : {}) },
    })) as { batch: string; queued: Array<{ id: string }>; skipped: Array<{ test: string; reason: string }> };
    report.batch = queued.batch;
    report.skipped = queued.skipped ?? [];
    const want = queued.queued?.length ?? 0;
    if (!json && want) console.log(`  ${DIM}queued ${want} run${want === 1 ? "" : "s"} (batch ${queued.batch})${RESET}`);
    const deadline = Date.now() + timeoutS * 1000;
    while (want > 0) {
      const runs = (answer(await daemonRequest(daemon, "hub.runs")).runs as RunRecord[] | undefined ?? []).filter((r) => r.batch === queued.batch);
      report.runs = runs;
      if (runs.length >= want && runs.every((r) => runEnded(r.state))) break;
      if (Date.now() > deadline) {
        report.error = `the runs did not all end within ${timeoutS}s; they go on in the daemon`;
        break;
      }
      await sleep(1000);
    }
  } catch (e) {
    report.error = e instanceof Error ? e.message : String(e);
  }
  return finish();
}
