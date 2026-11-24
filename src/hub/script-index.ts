// @session per-session: Lists one project's scripts and runs through that project's editor and Saved folder.
/**
 * The project's playtest scripts and the runs ue-mcp has a record of, for the
 * hub (HubScriptIndex).
 *
 * The scripts are pie(script_list), which needs the editor. The runs are
 * pie(script_runs) when the editor's bridge has it; without it, or without an
 * editor, each results.json under Saved/MCPObservations is read instead, each
 * again only when its file changed.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { qaRunFromAnswer, qaScriptKey, type QaRun } from "./contract.js";
import type { HubHost, HubScriptIndex, ListedScript, RunTimes } from "./ports.js";
import { answerOf } from "./util.js";

/** How long the editor's script list is reused before it is asked again. */
const LIST_TTL_MS = 15_000;
/** The most runs a page asking for older ones can read. */
const MAX_LIMIT = 5000;
/** How long a bridge without pie(script_runs) is left alone before it is asked again, as after an update. */
const NO_RUNS_ACTION_MS = 5 * 60_000;

export class ScriptIndex implements HubScriptIndex {
  private list: { at: number; scripts: ListedScript[] } | null = null;
  private readonly results = new Map<string, { mtime: number; run: QaRun | null }>();
  /** started_at and ended_at of every recorded run read, which QaRun does not keep. */
  private readonly times = new Map<string, RunTimes>();
  /** When the bridge last answered that it has no pie(script_runs). */
  private noRunsAction = -Infinity;
  private readonly now: () => number;

  constructor(private readonly host: Pick<HubHost, "callAction" | "editors" | "projectDir">, now?: () => number) {
    this.now = now ?? Date.now;
  }

  private connected(): boolean {
    return this.host.editors().some((e) => e.connected);
  }

  /** The editor's script list, or an empty one without an editor or when it fails. */
  async listScripts(fresh = false): Promise<ListedScript[]> {
    if (!fresh && this.list && this.now() - this.list.at < LIST_TTL_MS) return this.list.scripts;
    if (!this.connected()) throw new Error("the editor is not connected");
    const r = await this.host.callAction("pie", "script_list", { directory: "/" });
    const b = answerOf(r);
    if (!b || !Array.isArray(b.scripts)) throw new Error("the script list could not be read");
    const scripts = (b.scripts as Array<Record<string, unknown>>)
      .filter((s) => typeof s?.path === "string")
      .map((s) => ({
        path: qaScriptKey(s.path as string),
        name: typeof s.name === "string" ? s.name : qaScriptKey(s.path as string).split("/").pop()!,
        ...(typeof s.map === "string" && s.map ? { map: s.map } : {}),
      }));
    this.list = { at: this.now(), scripts };
    return scripts;
  }

  /** The runs ue-mcp lists with pie(script_runs), newest first, or null without an editor or the action. */
  private async listedRuns(limit: number): Promise<QaRun[] | null> {
    if (this.now() - this.noRunsAction < NO_RUNS_ACTION_MS) return null;
    if (!this.connected()) return null;
    const r = await this.host.callAction("pie", "script_runs", { limit }).catch(() => null);
    const b = r ? answerOf(r) : null;
    if (!b || !Array.isArray(b.runs)) {
      // An editor built before the action answers that it does not know it.
      if (r && /unknown (action|method)|not a valid action|no handler/i.test(r.content.map((c) => c.text ?? "").join("\n"))) this.noRunsAction = this.now();
      return null;
    }
    const at = new Date(this.now()).toISOString();
    return (b.runs as unknown[]).flatMap((row) => {
      const o = row && typeof row === "object" ? (row as Record<string, unknown>) : {};
      const seenAt = typeof o.started_at === "string" && o.started_at ? o.started_at : typeof o.ended_at === "string" && o.ended_at ? o.ended_at : at;
      const run = qaRunFromAnswer(o, { seenAt });
      if (run) this.keepTimes(run.runId, o);
      return run ? [run] : [];
    });
  }

  /** The runs results.json files record, newest first, each read again only when its file changed. */
  private disk(limit: number): QaRun[] {
    const root = path.join(this.host.projectDir, "Saved", "MCPObservations");
    let files: Array<{ file: string; mtime: number }>;
    try {
      files = fs.readdirSync(root, { withFileTypes: true })
        .filter((d) => d.isDirectory())
        .map((d) => path.join(root, d.name, "results.json"))
        .flatMap((file) => {
          try {
            return [{ file, mtime: fs.statSync(file).mtimeMs }];
          } catch {
            return [];
          }
        });
    } catch {
      return [];
    }
    files.sort((a, b) => b.mtime - a.mtime);
    const runs: QaRun[] = [];
    for (const { file, mtime } of files.slice(0, limit)) {
      let hit = this.results.get(file);
      if (!hit || hit.mtime !== mtime) {
        let run: QaRun | null = null;
        try {
          const raw = JSON.parse(fs.readFileSync(file, "utf-8")) as Record<string, unknown>;
          const at = typeof raw.started_at === "string" ? raw.started_at : new Date(mtime).toISOString();
          run = qaRunFromAnswer(raw, { seenAt: at });
          if (run) this.keepTimes(run.runId, raw);
        } catch {
          // Being written: the next read takes it.
        }
        hit = { mtime, run };
        this.results.set(file, hit);
      }
      if (hit.run) runs.push(hit.run);
    }
    return runs;
  }

  private keepTimes(runId: string, o: Record<string, unknown>): void {
    const startedAt = typeof o.started_at === "string" && o.started_at ? o.started_at : undefined;
    const endedAt = typeof o.ended_at === "string" && o.ended_at ? o.ended_at : undefined;
    if (startedAt || endedAt) this.times.set(runId, { ...(startedAt ? { startedAt } : {}), ...(endedAt ? { endedAt } : {}) });
  }

  /** Every run ue-mcp has a record of, newest first, each with its wall times when ue-mcp wrote them. */
  async recorded(limit: number): Promise<Array<{ run: QaRun; times: RunTimes }>> {
    const n = Math.min(MAX_LIMIT, limit);
    const runs = (await this.listedRuns(n)) ?? this.disk(n);
    return runs.map((run) => ({ run, times: this.times.get(run.runId) ?? {} }));
  }
}
