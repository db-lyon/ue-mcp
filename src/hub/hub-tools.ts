// @session independent: Pure functions that turn the hub category's inputs into calls on a hub handed to them.
/**
 * The hub category's actions over a Hub: the same methods the daemon's
 * /v1/hub routes call, so an agent and Studio change the same files and read
 * the same health. src/tools/hub.ts declares them as the `hub` MCP category.
 *
 * Each returns plain data, or throws with the reason (and, for a body that
 * does not read, every finding with its candidates).
 */
import type { Hub } from "./hub.js";
import { FindingError, bad } from "./util.js";

export const HUB_ACTIONS = [
  "overview",
  "list_clips",
  "read_clip",
  "write_clip",
  "delete_clip",
  "record_start",
  "record_stop",
  "read_recording",
  "list_arrangements",
  "read_arrangement",
  "write_arrangement",
  "delete_arrangement",
  "list_performers",
  "list_tests",
  "read_test",
  "list_suites",
  "read_suite",
  "write_suite",
  "delete_suite",
  "quarantine",
  "mark_flaky",
  "run",
  "read_queue",
  "list_runs",
  "read_run",
  "verdict",
  "stop_run",
  "play",
  "read_play",
  "stop_play",
] as const;
export type HubAction = (typeof HUB_ACTIONS)[number];

const str = (p: Record<string, unknown>, k: string): string => {
  const v = p[k];
  if (typeof v !== "string" || !v) throw bad(`${k} is required`);
  return v;
};

/** A body for write_*: `body` as an object or JSON text, with `id` left to the hub. */
function bodyOf(p: Record<string, unknown>): Record<string, unknown> {
  let b = p.body;
  if (typeof b === "string") {
    try {
      b = JSON.parse(b) as unknown;
    } catch {
      throw bad("body is the file's JSON, as an object or as text");
    }
  }
  if (!b || typeof b !== "object" || Array.isArray(b)) throw bad("body is the file's JSON object");
  return b as Record<string, unknown>;
}

const ACTIONS: { [A in HubAction]: (hub: Hub, p: Record<string, unknown>) => unknown } = {
  overview: (hub) => hub.overview(),
  list_clips: (hub) => ({ clips: hub.clips() }),
  read_clip: (hub, p) => ({ clip: hub.clip(str(p, "id")) }),
  write_clip: (hub, p) => ({ clip: typeof p.id === "string" && p.id ? hub.putClip(p.id, bodyOf(p)) : hub.createClip(bodyOf(p)) }),
  delete_clip: (hub, p) => (hub.deleteClip(str(p, "id")), { deleted: p.id }),
  record_start: (hub, p) => hub.recordStart(p),
  record_stop: async (hub, p) => ({ clip: await hub.recordStop(p) }),
  read_recording: (hub) => hub.readRecording(),
  list_arrangements: (hub) => ({ arrangements: hub.arrangements() }),
  read_arrangement: (hub, p) => hub.arrangement(str(p, "id")),
  write_arrangement: (hub, p) => (typeof p.id === "string" && p.id ? hub.putArrangement(p.id, bodyOf(p)) : hub.createArrangement(bodyOf(p))),
  delete_arrangement: (hub, p) => (hub.deleteArrangement(str(p, "id")), { deleted: p.id }),
  list_performers: (hub) => hub.performers(),
  list_tests: (hub) => ({ tests: hub.tests() }),
  read_test: (hub, p) => hub.test(str(p, "test"), typeof p.limit === "number" ? p.limit : undefined),
  list_suites: (hub) => ({ suites: hub.suites() }),
  read_suite: (hub, p) => hub.suite(str(p, "id")),
  write_suite: (hub, p) => (typeof p.id === "string" && p.id ? hub.putSuite(p.id, bodyOf(p)) : hub.createSuite(bodyOf(p))),
  delete_suite: (hub, p) => (hub.deleteSuite(str(p, "id")), { deleted: p.id }),
  quarantine: (hub, p) => hub.quarantine(str(p, "suite"), { test: p.test, on: p.on }),
  mark_flaky: (hub, p) => hub.markFlaky(str(p, "suite"), { test: p.test, on: p.on }),
  run: (hub, p) => hub.enqueue({ test: p.test, suite: p.suite, runner: p.runner, next: p.next }, "agent"),
  read_queue: (hub) => hub.queueView(),
  list_runs: (hub, p) => hub.runs({ test: p.test, suite: p.suite, limit: p.limit }),
  read_run: (hub, p) => hub.run(str(p, "run_id")),
  verdict: (hub, p) => hub.verdict(str(p, "run_id"), { check: p.check, verdict: p.verdict, note: p.note }, "agent"),
  stop_run: (hub, p) => hub.stopRun(str(p, "run_id"), "stopped by an agent"),
  play: (hub, p) => hub.play({ test: p.test, from: p.from, loop: p.loop }),
  read_play: (hub) => hub.readPlay(),
  stop_play: (hub) => hub.stopPlay(),
};

export async function runHubAction(hub: Hub, action: string, p: Record<string, unknown>): Promise<unknown> {
  const run = ACTIONS[action as HubAction];
  if (!run) throw bad(`unknown hub action ${action}`);
  try {
    return await run(hub, p);
  } catch (e) {
    if (e instanceof FindingError) {
      const lines = e.findings.filter((f) => f.blocks).map((f) => `- ${f.path ? `${f.path}: ` : ""}${f.message}${f.candidates?.length ? ` (did you mean ${f.candidates.join(", ")}?)` : ""}`);
      throw new Error(`The body does not read:\n${lines.join("\n")}`);
    }
    throw e;
  }
}
