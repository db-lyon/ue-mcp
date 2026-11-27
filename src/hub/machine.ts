// @session per-session: Keeps one project's runs, queue and schedules on this machine, under the data folder it is handed.
/**
 * What stays on the machine that ran it: run history (runs.json), the queue
 * (queue.json) and schedules (schedules.json), under <dataDir>/hub. Each is
 * one JSON file written whole through a temporary file. A file that does not
 * read is set aside as <name>.bad-<time> and started fresh, with a warning.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import type { QueueItem, RunRecord, Schedule } from "./contract.js";
import { writeFileAtomic } from "./atomic-write.js";

/** The most runs kept; the oldest go first. */
export const RUNS_MAX = 5000;

interface QueueFile {
  paused: boolean;
  items: QueueItem[];
}

export class MachineState {
  private readonly dir: string;
  private runs: RunRecord[];
  private queue: QueueFile;
  private schedules: Schedule[];

  constructor(dataDir: string, private readonly log?: { warn(message: string, detail?: unknown): void }) {
    this.dir = path.join(dataDir, "hub");
    this.runs = this.load<RunRecord[]>("runs.json", [], Array.isArray);
    this.queue = this.load<QueueFile>("queue.json", { paused: false, items: [] }, (v) => !!v && Array.isArray((v as QueueFile).items));
    this.schedules = this.load<Schedule[]>("schedules.json", [], Array.isArray);
  }

  private load<T>(name: string, empty: T, ok: (v: unknown) => boolean): T {
    const file = path.join(this.dir, name);
    if (!fs.existsSync(file)) return empty;
    try {
      const v = JSON.parse(fs.readFileSync(file, "utf8")) as unknown;
      if (ok(v)) return v as T;
    } catch {
      // Set aside below.
    }
    const aside = `${file}.bad-${Date.now()}`;
    try {
      fs.renameSync(file, aside);
    } catch {
      // Left where it is; it is overwritten on the next save.
    }
    this.log?.warn(`hub: ${name} did not read and was set aside as ${path.basename(aside)}`);
    return empty;
  }

  private save(name: string, v: unknown): void {
    fs.mkdirSync(this.dir, { recursive: true });
    writeFileAtomic(path.join(this.dir, name), `${JSON.stringify(v)}\n`);
  }

  // ---- Runs ----

  /** Every run, newest first. */
  allRuns(): readonly RunRecord[] {
    return this.runs;
  }

  run(runId: string): RunRecord | undefined {
    return this.runs.find((r) => r.runId === runId);
  }

  /** Add or replace a run by id. */
  putRun(r: RunRecord): void {
    const at = this.runs.findIndex((x) => x.runId === r.runId);
    if (at >= 0) this.runs[at] = r;
    else this.runs.unshift(r);
    if (this.runs.length > RUNS_MAX) this.runs.length = RUNS_MAX;
    this.save("runs.json", this.runs);
  }

  /** A test's runs, newest first. */
  runsOf(test: string): RunRecord[] {
    return this.runs.filter((r) => r.test === test);
  }

  // ---- Queue ----

  queueItems(): readonly QueueItem[] {
    return this.queue.items;
  }

  paused(): boolean {
    return this.queue.paused;
  }

  setPaused(paused: boolean): void {
    this.queue.paused = paused;
    this.save("queue.json", this.queue);
  }

  enqueue(items: QueueItem[], next: boolean): void {
    const running = this.queue.items.filter((i) => i.state === "running");
    const queued = this.queue.items.filter((i) => i.state !== "running");
    this.queue.items = next ? [...running, ...items, ...queued] : [...running, ...queued, ...items];
    this.save("queue.json", this.queue);
  }

  updateItem(id: string, patch: Partial<QueueItem>): void {
    this.queue.items = this.queue.items.map((i) => (i.id === id ? { ...i, ...patch } : i));
    this.save("queue.json", this.queue);
  }

  removeItem(id: string): QueueItem | undefined {
    const had = this.queue.items.find((i) => i.id === id);
    if (!had) return undefined;
    this.queue.items = this.queue.items.filter((i) => i.id !== id);
    this.save("queue.json", this.queue);
    return had;
  }

  // ---- Schedules ----

  allSchedules(): readonly Schedule[] {
    return this.schedules;
  }

  putSchedule(s: Schedule): void {
    const at = this.schedules.findIndex((x) => x.id === s.id);
    if (at >= 0) this.schedules[at] = s;
    else this.schedules.push(s);
    this.save("schedules.json", this.schedules);
  }

  deleteSchedule(id: string): boolean {
    const n = this.schedules.length;
    this.schedules = this.schedules.filter((s) => s.id !== id);
    if (this.schedules.length === n) return false;
    this.save("schedules.json", this.schedules);
    return true;
  }
}
