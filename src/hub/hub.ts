// @session per-session: One project's hub: its files, this machine's runs and queue, and the runners that play them.
/**
 * The Playtest Hub's service (#2146): the one owner of the hub's data,
 * validation, runs and runners. Agents (the hub category) and Studio (the
 * daemon's /v1/hub routes) call the same methods.
 *
 * Clips, arrangements and suites are the project's text files (store.ts).
 * Runs, the queue and schedules are this machine's (machine.ts). Health is
 * worked out from the runs on every read, never stored.
 *
 * A queue item plays one test on one runner: the open editor, under the
 * hub's lease, only while nothing else holds it (lease.ts); or a headless
 * editor a client supplies. The runner gets the arrangement whole with the
 * clips it places (PlayRequest) and is polled until it ends.
 */
import * as crypto from "node:crypto";
import {
  HUB_CHANGED,
  HUB_ID,
  HUB_LEASE,
  PERFORMER_CAPABILITIES,
  SCHEDULE_TIME,
  arrangementLength,
  arrangementOf,
  audibleTracks,
  clipOf,
  flakiness,
  instanceFindings,
  parseTestId,
  passRate,
  passRateHistory,
  performerLabel,
  performerOf,
  rank,
  runEnded,
  slugOf,
  stateFromChecks,
  suiteHealth,
  suiteLoop,
  suiteOf,
  suiteTests,
  testChecks,
  testHealth,
  testIdOf,
  testsOf,
  type Arrangement,
  type ArrangementSummary,
  type ArrangementView,
  type CheckResult,
  type Clip,
  type ClipSummary,
  type CurrentProvenance,
  type EditorLease as LeaseView,
  type HubChangedWhat,
  type HubOverview,
  type OccupancyReason,
  type Performer,
  type PerformerCandidate,
  type PerformerKind,
  type PerformersView,
  type PlayRequest,
  type QueueItem,
  type QueueState,
  type RunRecord,
  type RunSource,
  type RunnerKind,
  type Schedule,
  type Suite,
  type SuiteState,
  type TestDetail,
  type TestState,
} from "./contract.js";
import type { HubHost, HubRunner } from "./ports.js";
import { EditorLease, occupancyOf, type OccupancySignals } from "./lease.js";
import { HubFiles } from "./store.js";
import { MachineState } from "./machine.js";
import { EditorRunner } from "./runner.js";
import { workspaceNow } from "./provenance.js";
import { FindingError, answerOf, bad, conflict, failureOf, isObject, notFound } from "./util.js";

export interface HubOptions {
  host: HubHost;
  /** What the daemon knows about the editor, beyond whether it is connected. */
  editor: Omit<OccupancySignals, "connected">;
  enabled?: () => boolean;
  headless?: () => HubRunner | undefined;
  /** The open editor's runner; ue-mcp's own over pie(arrangement_*) by default. */
  editorRunner?: HubRunner;
  workspace?: () => Promise<CurrentProvenance>;
  now?: () => Date;
  pollMs?: number;
  /** Start the timers; tests drive tick() themselves. */
  timers?: boolean;
}

/** How often a live run is read and the queue looked at. */
export const HUB_TICK_MS = 500;
/** The most actors GET /hub/performers reads from the level. */
const PERFORMER_ACTOR_LIMIT = 200;
/** Actors a level keeps for itself, never offered as performers. */
const LEVEL_INFRASTRUCTURE = /WorldSettings|Light|Volume|Navigation|NavMesh|Sky|Fog|PostProcess|Brush|Info$|HUD|GameMode|GameState|PlayerState|Manager|Camera/;

interface Live {
  item: QueueItem;
  runner: HubRunner;
  polling: boolean;
}

const id8 = (): string => crypto.randomBytes(4).toString("hex");

export class Hub {
  readonly files: HubFiles;
  readonly machine: MachineState;
  private readonly lease: EditorLease;
  private readonly editorRunner: HubRunner;
  private readonly live = new Map<string, Live>();
  private timer: NodeJS.Timeout | null = null;
  private lastScheduleMinute = "";
  private recording: { performer: Performer; kind: PerformerKind; clip?: string } | null = null;
  private ticking = false;
  private pumping: Promise<void> | null = null;

  constructor(private readonly o: HubOptions) {
    this.files = new HubFiles(o.host.projectDir);
    this.machine = new MachineState(o.host.dataDir, o.host.log);
    this.lease = new EditorLease(undefined, (lease) => o.host.events.publish(HUB_LEASE, { lease }));
    this.editorRunner = o.editorRunner ?? new EditorRunner(o.host);
    // A run that was live when the daemon last stopped never reported its end.
    for (const item of this.machine.queueItems()) {
      if (item.state !== "running") continue;
      const r = item.runId ? this.machine.run(item.runId) : undefined;
      if (r && !runEnded(r.state)) this.machine.putRun({ ...r, state: "stopped", endedAt: this.now(), error: "the daemon stopped while it ran" });
      this.machine.removeItem(item.id);
    }
    if (o.timers !== false) {
      this.timer = setInterval(() => void this.tick(), o.pollMs ?? HUB_TICK_MS);
      this.timer.unref?.();
    }
  }

  dispose(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  private now(): string {
    return (this.o.now?.() ?? new Date()).toISOString();
  }

  private changed(what: HubChangedWhat[], ids: { id?: string; runId?: string } = {}): void {
    this.o.host.events.publish(HUB_CHANGED, { what, ...ids });
  }

  private connected(): boolean {
    return this.o.host.editors().some((e) => e.connected);
  }

  // ---------- Clips ----------

  private clipMap(): Map<string, Clip> {
    return this.files.list("clip").items;
  }

  clips(): ClipSummary[] {
    const arrangements = [...this.files.list("arrangement").items.values()];
    return [...this.clipMap().values()].map((c) => ({
      id: c.id,
      title: c.title,
      performer: c.performer,
      rate: c.rate,
      length: c.length,
      lanes: c.input.length + c.lanes.length,
      usedBy: arrangements.filter((a) => a.tracks.some((t) => t.instances.some((i) => i.clip === c.id))).map((a) => a.id),
    }));
  }

  clip(id: string): Clip {
    const got = this.files.get("clip", id);
    if (!got) throw notFound(`no clip ${id}`);
    if (!got.value) throw new FindingError(got.findings);
    return got.value;
  }

  private newId(kind: "clip" | "arrangement" | "suite", body: unknown): string {
    const o = isObject(body) ? body : {};
    if (typeof o.id === "string") {
      if (!HUB_ID.test(o.id)) throw bad("id is lowercase letters, digits and dashes, at most 64");
      if (this.files.exists(kind, o.id)) throw conflict(`a ${kind} ${o.id} already exists; PUT it to change it`);
      return o.id;
    }
    return this.files.freeId(kind, slugOf(typeof o.title === "string" ? o.title : kind));
  }

  createClip(body: unknown): Clip {
    return this.putClip(this.newId("clip", body), body);
  }

  putClip(id: string, body: unknown): Clip {
    if (!HUB_ID.test(id)) throw bad("a clip id is lowercase letters, digits and dashes");
    const { clip, findings } = clipOf(body, id);
    if (findings.some((f) => f.blocks)) throw new FindingError(findings);
    this.files.write("clip", clip);
    this.changed(["clips"], { id });
    return clip;
  }

  deleteClip(id: string): void {
    if (!this.files.exists("clip", id)) throw notFound(`no clip ${id}`);
    const users = [...this.files.list("arrangement").items.values()].filter((a) => a.tracks.some((t) => t.instances.some((i) => i.clip === id)));
    if (users.length) throw conflict(`clip ${id} is placed in ${users.map((a) => a.id).join(", ")}; remove its instances there first`);
    this.files.delete("clip", id);
    this.changed(["clips"], { id });
  }

  // ---------- Arrangements ----------

  private summary(a: Arrangement, clips: Map<string, Clip>): ArrangementSummary {
    return {
      id: a.id,
      title: a.title,
      level: a.level,
      tracks: a.tracks.length,
      checks: a.checks.length,
      regions: a.regions.map((r) => ({ id: r.id, name: r.name })),
      length: arrangementLength(a, (c) => clips.get(c)),
      findings: instanceFindings(a, (c) => clips.get(c), [...clips.keys()]),
    };
  }

  arrangements(): ArrangementSummary[] {
    const clips = this.clipMap();
    return [...this.files.list("arrangement").items.values()].map((a) => this.summary(a, clips));
  }

  private arrangementValue(id: string): Arrangement {
    const got = this.files.get("arrangement", id);
    if (!got) throw notFound(`no arrangement ${id}`);
    if (!got.value) throw new FindingError(got.findings);
    return got.value;
  }

  arrangement(id: string): ArrangementView {
    const a = this.arrangementValue(id);
    const all = this.clipMap();
    const clips: Record<string, Clip> = {};
    for (const t of a.tracks) for (const i of t.instances) if (all.has(i.clip)) clips[i.clip] = all.get(i.clip)!;
    const states = this.testStates();
    return {
      arrangement: a,
      length: arrangementLength(a, (c) => all.get(c)),
      findings: instanceFindings(a, (c) => all.get(c), [...all.keys()]),
      clips,
      tests: testsOf(a).map((t) => states.get(testIdOf(t))!).filter(Boolean),
    };
  }

  createArrangement(body: unknown): ArrangementView {
    return this.putArrangement(this.newId("arrangement", body), body);
  }

  putArrangement(id: string, body: unknown): ArrangementView {
    if (!HUB_ID.test(id)) throw bad("an arrangement id is lowercase letters, digits and dashes");
    const all = this.clipMap();
    const { arrangement, findings } = arrangementOf(body, id, (c) => all.get(c));
    const known = [...all.keys()];
    for (const f of findings) if (f.message.includes(": no clip ") && !f.candidates) f.candidates = rank(f.message.slice(f.message.lastIndexOf(" ") + 1), known);
    if (findings.some((f) => f.blocks)) throw new FindingError(findings);
    this.files.write("arrangement", arrangement);
    this.changed(["arrangements"], { id });
    return this.arrangement(id);
  }

  deleteArrangement(id: string): void {
    if (!this.files.exists("arrangement", id)) throw notFound(`no arrangement ${id}`);
    const users = [...this.files.list("suite").items.values()].filter((s) => s.items.some((it) => "test" in it && parseTestId(it.test)?.arrangement === id));
    if (users.length) throw conflict(`arrangement ${id} is tested in suite ${users.map((s) => s.id).join(", ")}; take its tests out of them first`);
    if (this.machine.queueItems().some((i) => parseTestId(i.test)?.arrangement === id)) throw conflict(`arrangement ${id} is in the queue; remove it there or let it run first`);
    this.files.delete("arrangement", id);
    this.changed(["arrangements"], { id });
  }

  // ---------- Tests ----------

  /** A test's arrangement and region, or why there is none. */
  private resolveTest(id: string): { a: Arrangement; region?: string; title: string } | { missing: string } {
    const ref = parseTestId(id);
    if (!ref) return { missing: `${id} is not a test id: <arrangement> or <arrangement>:<region>` };
    const got = this.files.get("arrangement", ref.arrangement);
    if (!got) return { missing: `no arrangement ${ref.arrangement}` };
    if (!got.value) return { missing: `arrangement ${ref.arrangement} does not read: ${got.findings.map((f) => f.message).join("; ")}` };
    const a = got.value;
    if (!ref.region) return { a, title: a.title };
    const r = a.regions.find((x) => x.id === ref.region);
    if (!r) return { missing: `arrangement ${a.id} has no region ${ref.region}` };
    return { a, region: r.id, title: `${a.title} / ${r.name}` };
  }

  private testState(id: string, o: { quarantined?: boolean; marked?: boolean } = {}): TestState {
    const ref = parseTestId(id) ?? { arrangement: id };
    const t = this.resolveTest(id);
    const runs = this.machine.runsOf(id);
    const fl = flakiness(runs);
    const last = runs[0];
    return {
      id,
      arrangement: ref.arrangement,
      ...(ref.region ? { region: ref.region } : {}),
      title: "missing" in t ? id : t.title,
      health: testHealth(runs, { missing: "missing" in t }),
      checks: "missing" in t ? 0 : testChecks(t.a, t.region).length,
      passRate: passRate(runs),
      flaky: { detected: fl.flaky, marked: o.marked === true, checks: fl.checks },
      quarantined: o.quarantined === true,
      ...(last ? { last: { runId: last.runId, state: last.state, startedAt: last.startedAt, endedAt: last.endedAt } } : {}),
    };
  }

  /** Every test of every arrangement, and every test a suite names that is missing, quarantined or marked flaky where any suite says so. */
  private testStates(): Map<string, TestState> {
    const suites = [...this.files.list("suite").items.values()];
    const quarantined = new Set(suites.flatMap((s) => s.quarantined));
    const marked = new Set(suites.flatMap((s) => s.flaky));
    const ids = new Set<string>();
    for (const a of this.files.list("arrangement").items.values()) for (const t of testsOf(a)) ids.add(testIdOf(t));
    for (const s of suites) for (const it of s.items) if ("test" in it) ids.add(it.test);
    return new Map([...ids].map((id) => [id, this.testState(id, { quarantined: quarantined.has(id), marked: marked.has(id) })]));
  }

  tests(): TestState[] {
    return [...this.testStates().values()];
  }

  test(id: string, limit = 20): TestDetail {
    const t = this.resolveTest(id);
    if ("missing" in t) throw notFound(t.missing);
    return { test: this.testStates().get(id) ?? this.testState(id), checks: testChecks(t.a, t.region), runs: this.machine.runsOf(id).slice(0, Math.min(Math.max(1, limit), 200)) };
  }

  // ---------- Suites ----------

  private suiteState(s: Suite, suites: Map<string, Suite>): SuiteState {
    const walk = suiteTests(suites, s.id);
    const scope = new Set<string>([s.id]);
    const visit = (id: string) => {
      for (const it of suites.get(id)?.items ?? []) if ("suite" in it && !scope.has(it.suite)) (scope.add(it.suite), visit(it.suite));
    };
    visit(s.id);
    const quarantined = new Set([...scope].flatMap((id) => suites.get(id)?.quarantined ?? []));
    const marked = new Set([...scope].flatMap((id) => suites.get(id)?.flaky ?? []));
    const tests = walk.tests.map((id) => this.testState(id, { quarantined: quarantined.has(id), marked: marked.has(id) }));
    const counted = new Set(tests.filter((t) => !t.quarantined).map((t) => t.id));
    const results = tests.filter((t) => !t.quarantined && t.passRate !== null);
    return {
      id: s.id,
      title: s.title,
      ...(s.description ? { description: s.description } : {}),
      items: s.items,
      health: suiteHealth(tests),
      passRate: results.length ? Math.round((results.reduce((n, t) => n + t.passRate!, 0) / results.length) * 1000) / 1000 : null,
      history: passRateHistory(counted, this.machine.allRuns()),
      tests,
      quarantined: s.quarantined,
      flaky: s.flaky,
      missing: [...tests.filter((t) => t.health === "missing").map((t) => t.id), ...walk.missingSuites],
    };
  }

  suites(): SuiteState[] {
    const suites = this.files.list("suite").items;
    return [...suites.values()].map((s) => this.suiteState(s, suites));
  }

  private suiteValue(id: string): Suite {
    const got = this.files.get("suite", id);
    if (!got) throw notFound(`no suite ${id}`);
    if (!got.value) throw new FindingError(got.findings);
    return got.value;
  }

  suite(id: string): SuiteState {
    const s = this.suiteValue(id);
    return this.suiteState(s, this.files.list("suite").items);
  }

  createSuite(body: unknown): SuiteState {
    return this.putSuite(this.newId("suite", body), body);
  }

  putSuite(id: string, body: unknown): SuiteState {
    if (!HUB_ID.test(id)) throw bad("a suite id is lowercase letters, digits and dashes");
    const { suite, findings } = suiteOf(body, id);
    const suites = new Map(this.files.list("suite").items);
    const testIds = [...this.testStates().keys()].filter((t) => !("missing" in this.resolveTest(t)));
    for (const [i, it] of suite.items.entries()) {
      if ("suite" in it && !suites.has(it.suite)) findings.push({ path: `items[${i}]`, message: `no suite ${it.suite}`, blocks: true, candidates: rank(it.suite, [...suites.keys()]) });
      if ("test" in it && "missing" in this.resolveTest(it.test)) findings.push({ path: `items[${i}]`, message: (this.resolveTest(it.test) as { missing: string }).missing, blocks: true, candidates: rank(it.test, testIds) });
    }
    suites.set(id, suite);
    const loop = suiteLoop(suites, id);
    if (loop) findings.push({ path: "items", message: `suites would contain each other: ${loop.join(" > ")}`, blocks: true });
    if (findings.some((f) => f.blocks)) throw new FindingError(findings);
    this.files.write("suite", suite);
    this.changed(["suites"], { id });
    return this.suiteState(suite, suites);
  }

  deleteSuite(id: string): void {
    if (!this.files.exists("suite", id)) throw notFound(`no suite ${id}`);
    const parents = [...this.files.list("suite").items.values()].filter((s) => s.items.some((it) => "suite" in it && it.suite === id));
    if (parents.length) throw conflict(`suite ${id} is inside ${parents.map((s) => s.id).join(", ")}; take it out of them first`);
    if (this.machine.allSchedules().some((s) => s.suite === id)) throw conflict(`a schedule runs suite ${id}; delete the schedule or point it elsewhere first`);
    this.files.delete("suite", id);
    this.changed(["suites"], { id });
  }

  /** Quarantine a test in a suite, or let it count again. */
  quarantine(id: string, body: unknown): SuiteState {
    return this.toggle(id, body, "quarantined");
  }

  /** Mark a test flaky in a suite, or clear the mark. */
  markFlaky(id: string, body: unknown): SuiteState {
    return this.toggle(id, body, "flaky");
  }

  private toggle(id: string, body: unknown, list: "quarantined" | "flaky"): SuiteState {
    const o = isObject(body) ? body : {};
    if (typeof o.test !== "string" || !parseTestId(o.test)) throw bad("test is a test id: <arrangement> or <arrangement>:<region>");
    const on = o.on !== false;
    const s = this.suiteValue(id);
    const suites = this.files.list("suite").items;
    if (on && !suiteTests(suites, id).tests.includes(o.test)) throw conflict(`suite ${id} does not hold ${o.test}; add it to the suite first`);
    const set = new Set(s[list]);
    if (on) set.add(o.test);
    else set.delete(o.test);
    const next: Suite = { ...s, [list]: [...set].sort() };
    this.files.write("suite", next);
    this.changed(["suites"], { id });
    return this.suiteState(next, new Map(suites).set(id, next));
  }

  // ---------- Runs ----------

  runs(q: { test?: unknown; suite?: unknown; limit?: unknown; before?: unknown } = {}): { runs: RunRecord[]; more: boolean } {
    let list: readonly RunRecord[] = this.machine.allRuns();
    if (typeof q.test === "string" && q.test) list = list.filter((r) => r.test === q.test);
    if (typeof q.suite === "string" && q.suite) {
      const tests = new Set(suiteTests(this.files.list("suite").items, q.suite).tests);
      list = list.filter((r) => tests.has(r.test));
    }
    if (typeof q.before === "string" && q.before) list = list.filter((r) => r.startedAt < (q.before as string));
    const limit = Math.min(Math.max(Number(q.limit) || 50, 1), 500);
    return { runs: list.slice(0, limit), more: list.length > limit };
  }

  run(runId: string): RunRecord {
    const r = this.machine.run(runId);
    if (!r) throw notFound(`no run ${runId}`);
    return r;
  }

  /** A manual check's verdict, from a person or an agent. */
  async verdict(runId: string, body: unknown, by: "person" | "agent"): Promise<RunRecord> {
    const o = isObject(body) ? body : {};
    const r = this.run(runId);
    const live = this.live.get(runId);
    if (!live || runEnded(r.state)) throw conflict(`run ${runId} is not playing; a verdict goes to a run that is waiting for one`);
    if (typeof o.check !== "string") throw bad("check is the id of the manual check");
    if (o.verdict !== "pass" && o.verdict !== "fail") throw bad("verdict is pass or fail");
    const t = this.resolveTest(r.test);
    const check = "missing" in t ? undefined : testChecks(t.a, t.region).find((c) => c.id === o.check);
    if (!check || check.mode !== "manual") throw bad(`${o.check} is not a manual check of ${r.test}`);
    if (check.personOnly && by === "agent") throw conflict(`${check.title} takes a person's verdict only; ask the person to give it in Studio`);
    const note = typeof o.note === "string" ? o.note.slice(0, 2000) : undefined;
    await live.runner.verdict(runId, check.id, o.verdict, note);
    const checks = r.checks.map((c) => (c.id === check.id ? { ...c, status: o.verdict === "pass" ? ("passed" as const) : ("failed" as const), verdict: { by, ...(note ? { note } : {}), at: this.now() } } : c));
    const next = { ...r, checks };
    this.machine.putRun(next);
    this.changed(["runs"], { runId });
    return next;
  }

  /** Stop a run the hub is playing. */
  async stopRun(runId: string, reason = "stopped by a person"): Promise<RunRecord> {
    const live = this.live.get(runId);
    if (!live) throw conflict(`run ${runId} is not playing`);
    await live.runner.stop(runId, reason).catch((e: unknown) => this.o.host.log.warn(`hub: run ${runId} did not stop cleanly`, e));
    return this.finish(runId, { state: "stopped", error: reason });
  }

  // ---------- Queue ----------

  private occupancy(scheduled: boolean): OccupancyReason[] {
    return occupancyOf({ connected: () => this.connected(), ...this.o.editor }, this.lease.held(), { scheduled });
  }

  queueView(): QueueState {
    const items = this.machine.queueItems();
    const nextEditor = items.find((i) => i.state === "queued" && i.runner === "editor");
    const editorBusy = [...this.live.values()].some((l) => l.runner.kind === "editor");
    return {
      paused: this.machine.paused(),
      waiting: nextEditor && !editorBusy ? this.occupancy(nextEditor.source === "schedule") : [],
      running: items.filter((i) => i.state === "running"),
      queued: items.filter((i) => i.state === "queued"),
    };
  }

  private runnerFor(kind: RunnerKind): HubRunner {
    if (kind === "editor") return this.editorRunner;
    const h = this.o.headless?.();
    if (!h) throw conflict("no headless runner is set up on this machine; queue it with runner editor to play it on the open editor");
    return h;
  }

  /** Queue one test, or every test of a suite. Answers what was queued and what was left out and why. */
  enqueue(body: unknown, source: RunSource): { batch: string; queued: QueueItem[]; skipped: Array<{ test: string; reason: string }>; queue: QueueState } {
    const o = isObject(body) ? body : {};
    const runner: RunnerKind = o.runner === "headless" ? "headless" : "editor";
    if (o.runner !== undefined && o.runner !== "editor" && o.runner !== "headless") throw bad("runner is editor or headless");
    this.runnerFor(runner);
    const hasTest = typeof o.test === "string" && !!o.test;
    const hasSuite = typeof o.suite === "string" && !!o.suite;
    if (hasTest === hasSuite) throw bad("give exactly one of test (a test id) and suite (a suite id)");
    const batch = `b_${Date.now().toString(36)}_${id8()}`;
    const skipped: Array<{ test: string; reason: string }> = [];
    let ids: string[];
    if (hasSuite) {
      this.suiteValue(o.suite as string);
      ids = suiteTests(this.files.list("suite").items, o.suite as string).tests;
    } else ids = [o.test as string];
    const queued: QueueItem[] = [];
    for (const id of ids) {
      const t = this.resolveTest(id);
      if ("missing" in t) {
        if (!hasSuite) throw notFound(t.missing);
        skipped.push({ test: id, reason: t.missing });
        continue;
      }
      if (!testChecks(t.a, t.region).length) {
        const reason = `${t.title} has no checks: add one to the arrangement's Checks track${t.region ? " inside the region" : ""}`;
        if (!hasSuite) throw conflict(reason);
        skipped.push({ test: id, reason });
        continue;
      }
      const findings = instanceFindings(t.a, (c) => this.files.get("clip", c)?.value ?? undefined).filter((f) => f.blocks);
      if (findings.length) {
        const reason = `${t.a.title} cannot play: ${findings.map((f) => f.message).join("; ")}`;
        if (!hasSuite) throw conflict(reason);
        skipped.push({ test: id, reason });
        continue;
      }
      queued.push({ id: `q_${id8()}`, test: id, title: t.title, runner, source, batch, ...(hasSuite ? { suite: o.suite as string } : {}), queuedAt: this.now(), state: "queued" });
    }
    if (queued.length) {
      this.machine.enqueue(queued, o.next === true);
      if (source !== "schedule" && this.machine.paused()) this.machine.setPaused(false);
      this.changed(["queue"]);
      void this.pump();
    }
    return { batch, queued, skipped, queue: this.queueView() };
  }

  removeQueued(id: string): QueueState {
    const item = this.machine.queueItems().find((i) => i.id === id);
    if (!item) throw notFound(`no queue item ${id}`);
    if (item.state === "running") throw conflict(`${item.title} is playing; stop its run ${item.runId} instead`);
    this.machine.removeItem(id);
    this.changed(["queue"]);
    return this.queueView();
  }

  setPaused(body: unknown): QueueState {
    const o = isObject(body) ? body : {};
    if (typeof o.paused !== "boolean") throw bad("paused is true or false");
    this.machine.setPaused(o.paused);
    this.changed(["queue"]);
    if (!o.paused) void this.pump();
    return this.queueView();
  }

  leaseView(): { lease: LeaseView | null } {
    return { lease: this.lease.get() };
  }

  /** Give the editor back: stop the hub's run on it and pause the queue. */
  async release(): Promise<QueueState> {
    this.machine.setPaused(true);
    for (const [runId, l] of this.live) if (l.runner.kind === "editor") await this.stopRun(runId, "a person released the editor").catch(() => undefined);
    this.lease.release();
    this.changed(["queue"]);
    return this.queueView();
  }

  /** The daemon is stopping: stop every run the hub plays. */
  async shutdown(): Promise<void> {
    this.dispose();
    for (const runId of [...this.live.keys()]) await this.stopRun(runId, "the daemon stopped").catch(() => undefined);
  }

  /** Read the live runs, start what the queue may start, run due schedules. */
  async tick(): Promise<void> {
    if (this.ticking) return;
    this.ticking = true;
    try {
      for (const runId of [...this.live.keys()]) await this.poll(runId);
      await this.pump();
      this.runSchedules();
    } finally {
      this.ticking = false;
    }
  }

  /** Start what the queue may start; one pass at a time, so an item never starts twice. */
  private pump(): Promise<void> {
    return (this.pumping ??= this.pumpOnce().finally(() => {
      this.pumping = null;
    }));
  }

  private async pumpOnce(): Promise<void> {
    if (this.o.enabled && !this.o.enabled()) return;
    if (this.machine.paused()) return;
    const busy = new Set([...this.live.values()].map((l) => l.runner.kind));
    for (const kind of ["editor", "headless"] as const) {
      if (busy.has(kind)) continue;
      const item = this.machine.queueItems().find((i) => i.state === "queued" && i.runner === kind);
      if (!item) continue;
      if (kind === "editor" && this.occupancy(item.source === "schedule").length) continue;
      let runner: HubRunner;
      try {
        runner = this.runnerFor(kind);
      } catch {
        continue;
      }
      if (runner.unavailable()) continue;
      busy.add(kind);
      await this.start(item, runner);
    }
  }

  private async start(item: QueueItem, runner: HubRunner): Promise<void> {
    const runId = `run_${Date.now().toString(36)}_${id8()}`;
    const t = this.resolveTest(item.test);
    const ws = await (this.o.workspace?.() ?? workspaceNow(this.o.host.projectDir)).catch((): CurrentProvenance => ({ vcs: null, revision: null, dirty: null }));
    const record: RunRecord = {
      runId,
      test: item.test,
      title: item.title,
      runner: runner.kind,
      source: item.source,
      batch: item.batch,
      ...(item.suite ? { suite: item.suite } : {}),
      startedAt: this.now(),
      endedAt: null,
      state: "starting",
      checks: "missing" in t ? [] : testChecks(t.a, t.region).map((c) => ({ id: c.id, status: "pending" as const })),
      revision: ws.revision,
      dirty: ws.dirty,
    };
    this.machine.putRun(record);
    this.machine.updateItem(item.id, { state: "running", runId });
    this.live.set(runId, { item: { ...item, state: "running", runId }, runner, polling: false });
    if ("missing" in t) {
      this.finish(runId, { state: "error", error: t.missing });
      return;
    }
    if (runner.kind === "editor") this.lease.take({ purpose: `Run ${item.title}`, item: item.id, runId });
    this.changed(["queue", "runs"], { runId });
    const audible = audibleTracks(t.a.tracks);
    const clips: Record<string, Clip> = {};
    for (const tr of audible) for (const i of tr.instances) {
      const c = this.files.get("clip", i.clip)?.value;
      if (c) clips[c.id] = c;
    }
    const request: PlayRequest = { runId, arrangement: { ...t.a, tracks: audible }, clips, ...(t.region ? { region: t.region } : {}) };
    try {
      const started = await runner.play(request);
      if (started.pieStarted && runner.kind === "editor") this.lease.update({ pie: true });
      const r = this.machine.run(runId);
      if (r && r.state === "starting") this.machine.putRun({ ...r, state: "running" });
    } catch (e) {
      this.finish(runId, { state: "error", error: e instanceof Error ? e.message : String(e) });
    }
  }

  private async poll(runId: string): Promise<void> {
    const live = this.live.get(runId);
    if (!live || live.polling) return;
    live.polling = true;
    try {
      const s = await live.runner.status(runId);
      const r = this.machine.run(runId);
      if (!r) return;
      const had = new Map(r.checks.map((c) => [c.id, c]));
      const checks: CheckResult[] = r.checks.map((c) => {
        const got = s.checks.find((x) => x.id === c.id);
        return got ? { ...got, ...(had.get(c.id)?.verdict ? { verdict: had.get(c.id)!.verdict } : {}) } : c;
      });
      if (s.pieStarted && live.runner.kind === "editor") this.lease.update({ pie: true });
      if (s.state === "finished") return void this.finish(runId, { checks, state: stateFromChecks(checks), frame: s.frame });
      if (s.state === "stopped" || s.state === "error") return void this.finish(runId, { checks, state: s.state, frame: s.frame, error: s.error ?? (s.state === "stopped" ? "the editor stopped the run" : "the run failed on the editor") });
      const state = s.state === "awaiting_verdict" ? "awaiting_verdict" : s.state === "starting" ? "starting" : "running";
      const next = { ...r, checks, state, ...(s.frame !== undefined ? { frame: s.frame } : {}) } as RunRecord;
      if (JSON.stringify(next) !== JSON.stringify(r)) {
        this.machine.putRun(next);
        this.changed(["runs"], { runId });
      }
    } catch (e) {
      this.finish(runId, { state: "error", error: `the run could not be read: ${e instanceof Error ? e.message : String(e)}` });
    } finally {
      live.polling = false;
    }
  }

  private finish(runId: string, end: Partial<Pick<RunRecord, "checks" | "state" | "frame" | "error">>): RunRecord {
    const live = this.live.get(runId);
    this.live.delete(runId);
    const r = this.machine.run(runId)!;
    const checks = (end.checks ?? r.checks).map((c) => (c.status === "passed" || c.status === "failed" ? c : { ...c, status: "skipped" as const }));
    const next: RunRecord = { ...r, ...end, checks, endedAt: this.now() };
    if (end.state === "passed" || end.state === "failed") next.state = stateFromChecks(checks);
    this.machine.putRun(next);
    if (live) this.machine.removeItem(live.item.id);
    if (live?.runner.kind === "editor") this.lease.release();
    this.changed(["runs", "queue"], { runId });
    void this.pump();
    return next;
  }

  // ---------- Schedules ----------

  schedules(): readonly Schedule[] {
    return this.machine.allSchedules();
  }

  private scheduleOf(body: unknown, had?: Schedule): Schedule {
    const o = isObject(body) ? body : {};
    const s: Schedule = had ? { ...had } : { id: `s_${id8()}`, title: "", runner: "editor", days: [1, 2, 3, 4, 5], time: "03:00", enabled: true, lastRunAt: null };
    if (o.title !== undefined) s.title = String(o.title).trim().slice(0, 200);
    if (o.suite !== undefined || o.test !== undefined) {
      delete s.suite;
      delete s.test;
      if (typeof o.suite === "string" && o.suite) s.suite = o.suite;
      if (typeof o.test === "string" && o.test) s.test = o.test;
    }
    if (o.runner !== undefined) s.runner = o.runner === "headless" ? "headless" : "editor";
    if (o.days !== undefined) {
      if (!Array.isArray(o.days) || !o.days.every((d) => Number.isInteger(d) && d >= 0 && d <= 6) || !o.days.length) throw bad("days is a list of weekdays, 0 Sunday to 6 Saturday");
      s.days = [...new Set(o.days as number[])].sort();
    }
    if (o.time !== undefined) {
      if (typeof o.time !== "string" || !SCHEDULE_TIME.test(o.time)) throw bad("time is HH:MM, local");
      s.time = o.time;
    }
    if (o.enabled !== undefined) s.enabled = o.enabled === true;
    if (!!s.suite === !!s.test) throw bad("a schedule runs exactly one of suite and test");
    if (s.suite && !this.files.exists("suite", s.suite)) throw notFound(`no suite ${s.suite}`);
    if (s.test && "missing" in this.resolveTest(s.test)) throw notFound((this.resolveTest(s.test) as { missing: string }).missing);
    if (!s.title) s.title = s.suite ?? s.test!;
    return s;
  }

  createSchedule(body: unknown): Schedule {
    const s = this.scheduleOf(body);
    this.machine.putSchedule(s);
    this.changed(["schedules"], { id: s.id });
    return s;
  }

  updateSchedule(id: string, body: unknown): Schedule {
    const had = this.machine.allSchedules().find((s) => s.id === id);
    if (!had) throw notFound(`no schedule ${id}`);
    const s = this.scheduleOf(body, had);
    this.machine.putSchedule(s);
    this.changed(["schedules"], { id });
    return s;
  }

  deleteSchedule(id: string): void {
    if (!this.machine.deleteSchedule(id)) throw notFound(`no schedule ${id}`);
    this.changed(["schedules"], { id });
  }

  private runSchedules(): void {
    const now = this.o.now?.() ?? new Date();
    const minute = `${now.getFullYear()}-${now.getMonth()}-${now.getDate()} ${now.getHours()}:${now.getMinutes()}`;
    if (minute === this.lastScheduleMinute) return;
    this.lastScheduleMinute = minute;
    const hhmm = `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
    for (const s of this.machine.allSchedules()) {
      if (!s.enabled || s.time !== hhmm || !s.days.includes(now.getDay())) continue;
      try {
        this.enqueue({ ...(s.suite ? { suite: s.suite } : { test: s.test }), runner: s.runner }, "schedule");
      } catch (e) {
        this.o.host.log.warn(`hub: schedule ${s.title} could not queue its runs`, e);
      }
      this.machine.putSchedule({ ...s, lastRunAt: now.toISOString() });
      this.changed(["schedules"], { id: s.id });
    }
  }

  // ---------- Performers and recording ----------

  /** What a track can drive: the editor's player and the first remote clients, then the actors of the open level, pawns first. */
  async performers(arrangement?: string): Promise<PerformersView> {
    const level = arrangement ? this.arrangementValue(arrangement).level : null;
    const players: PerformerCandidate[] = [0, 1, 2, 3].map((client) => {
      const performer: Performer = { kind: "player", client };
      return { performer, label: performerLabel(performer), capabilities: [...PERFORMER_CAPABILITIES.player] };
    });
    let thrown: string | null = null;
    const r = await this.o.host.callAction("level", "get_outliner", { limit: PERFORMER_ACTOR_LIMIT }).catch((e: unknown) => {
      thrown = e instanceof Error ? e.message : String(e);
      return null;
    });
    const error = r ? failureOf(r) : thrown;
    const body = r ? answerOf(r) : null;
    if (error || !body) return { level, performers: players, reason: `the level's actors could not be read: ${error ?? "no editor answered"}` };
    const actors: PerformerCandidate[] = [];
    for (const row of Array.isArray(body.actors) ? (body.actors as Array<Record<string, unknown>>) : []) {
      const cls = typeof row.class === "string" ? row.class : "";
      const name = typeof row.label === "string" && row.label ? row.label : typeof row.name === "string" ? row.name : "";
      if (!name || LEVEL_INFRASTRUCTURE.test(cls)) continue;
      const comps = Array.isArray(row.components) ? (row.components as Array<{ class?: unknown }>).map((c) => String(c.class ?? "")) : [];
      const pawn = /Character|Pawn/.test(cls) || comps.some((c) => /(Character|Pawn)Movement|FloatingPawnMovement/.test(c));
      actors.push({ performer: { kind: "actor", actor: name }, label: name, className: cls, pawn, capabilities: [...PERFORMER_CAPABILITIES.actor] });
    }
    actors.sort((a, b) => Number(b.pawn) - Number(a.pawn) || a.label.localeCompare(b.label));
    const open = typeof body.worldName === "string" ? body.worldName : null;
    const elsewhere = level && open && !level.endsWith(`/${open}`) ? `the editor has ${open} open, not the arrangement's level ${level}; these are ${open}'s actors` : undefined;
    return { level, performers: [...players, ...actors], ...(elsewhere ? { reason: elsewhere } : actors.length === 0 ? { reason: "the open level has no actors to drive" } : {}) };
  }

  /**
   * Start recording a performance into a clip: pie(clip_record_start) with the
   * performer, and for a punch-in the clip and the frames to replace.
   */
  async recordStart(body: unknown): Promise<{ recording: true }> {
    const o = isObject(body) ? body : {};
    if (this.recording) throw conflict("a recording is already under way; stop it first");
    const performer = performerOf(o.performer);
    if (!performer) throw bad('performer is {kind: "player", client} or {kind: "actor", actor}');
    const args: Record<string, unknown> = { performer, kind: performer.kind };
    if (typeof o.clip === "string") {
      const clip = this.clip(o.clip);
      if (clip.performer !== performer.kind) throw bad(`clip ${clip.id} is for a ${clip.performer}, and the performer is ${performerLabel(performer)}`);
      const from = typeof o.from === "number" ? o.from : 0;
      const to = typeof o.to === "number" ? o.to : clip.length;
      if (from < 0 || to <= from) throw bad("a punch-in records from `from` to a later `to`, in the clip's frames");
      args.punch = { clip, from, to };
      args.rate = clip.rate;
    } else if (typeof o.rate === "number") args.rate = o.rate;
    const r = await this.o.host.callAction("pie", "clip_record_start", args);
    const failed = failureOf(r);
    if (failed) throw conflict(`the recording did not start: ${failed}`);
    this.recording = { performer, kind: performer.kind, ...(typeof o.clip === "string" ? { clip: o.clip } : {}) };
    return { recording: true };
  }

  /** Stop recording and save the take: into the punched clip, or as a new clip (id or title from the body). */
  async recordStop(body: unknown): Promise<Clip> {
    const o = isObject(body) ? body : {};
    const rec = this.recording;
    if (!rec) throw conflict("nothing is recording; start a recording first");
    this.recording = null;
    const r = await this.o.host.callAction("pie", "clip_record_stop", {});
    const failed = failureOf(r);
    const answer = answerOf(r);
    if (failed || !answer || !isObject(answer.clip)) throw conflict(`the take could not be read: ${failed ?? "the editor answered no clip"}`);
    const id = rec.clip ?? this.newId("clip", { ...(typeof o.id === "string" ? { id: o.id } : {}), title: typeof o.title === "string" ? o.title : "Take" });
    const had = rec.clip ? this.clip(rec.clip) : null;
    const title = typeof o.title === "string" && o.title.trim() ? o.title : had?.title ?? "Take";
    return this.putClip(id, { ...answer.clip, performer: rec.kind, title, ...(had?.description ? { description: had.description } : {}) });
  }

  // ---------- Overview ----------

  overview(): HubOverview {
    const clips = this.files.list("clip");
    const arrangements = this.files.list("arrangement");
    const suites = this.files.list("suite");
    return {
      clips: this.clips(),
      arrangements: this.arrangements(),
      suites: this.suites(),
      tests: this.tests(),
      queue: this.queueView(),
      lease: this.lease.get(),
      recent: this.machine.allRuns().slice(0, 20),
      schedules: [...this.machine.allSchedules()],
      problems: [...clips.problems, ...arrangements.problems, ...suites.problems],
    };
  }
}
