// @session per-session: One project's set (tracks, scenes, slots, timelines) over its goal store, clips and runs.
/**
 * The set the Session and Arrangement views edit (#2113), over the goal store:
 * tracks are goals, scenes are milestones. It answers the set with layout and
 * findings, applies set ops (making a clip unique first, when asked), and
 * composes what a track, scene, slot or arrangement run plays.
 */
import {
  ARRANGEMENT_RATE,
  applyArrangementOps,
  arrangementFrom,
  applySetOps,
  arrangementEnd,
  arrangementFromText,
  arrangementLayout,
  arrangementOf,
  arrangementOpsOf,
  arrangementToText,
  audibleTracks,
  blockingFindings,
  composeArrangement,
  setOpsOf,
  setToText,
  targetKey,
  validateArrangement,
  type Arrangement,
  type ArrangementComposition,
  type ArrangementFinding,
  type ArrangementRunKind,
  type ArrangementView,
  type ClipEntry,
  type ComposeTrack,
  type GoalStatus,
  type InstanceKind,
  type PlanScope,
  type SetModel,
  type SetOp,
  type SetView,
} from "./contract.js";
import type { Goals } from "./goals.js";
import type { ClipLibrary } from "./clip-library.js";
import type { HubHost } from "./ports.js";
import { answerOf, bad, failureOf, isObject, notFound } from "./util.js";

/** An answer that carries a finding: 400 with { error, finding, findings }. */
export class FindingError extends Error {
  readonly status = 400;
  constructor(readonly finding: ArrangementFinding, readonly findings: ArrangementFinding[] = [finding]) {
    super(finding.message);
  }
}

export interface SetRunPlan {
  kind: ArrangementRunKind;
  goalId: string;
  title: string;
  composition: ArrangementComposition;
  tracks: Array<{ goalId: string; title: string; player: number }>;
  /** The frame the timelines play from (at 60 per second); the composition counts from there. */
  from?: number;
}

export class SetService {
  constructor(
    private readonly o: {
      goals: Goals;
      library: ClipLibrary;
      host: Pick<HubHost, "callAction">;
      /** Every goal's status, by id. */
      statuses: () => Promise<Map<string, GoalStatus>>;
      lastRun: (kind: InstanceKind, target: string) => ClipEntry["lastRun"];
      /** Why a track or scene cannot be deleted now (a run made for it is queued or running), or null. */
      deleteBlocked?: (id: string) => string | null;
    },
  ) {}

  private opts() {
    const lib = this.o.library;
    return { factsOf: (t: string, k: InstanceKind) => lib.facts(t, k), library: (k: InstanceKind) => lib.library(k), newId: (title: string) => this.o.goals.newId(title) };
  }

  private refsOf(m: SetModel): Array<{ kind: InstanceKind; target: string }> {
    return [...m.tracks.flatMap((t) => t.arrangement.instances), ...m.scenes.flatMap((s) => s.slots)].map((x) => ({ kind: x.kind, target: x.target }));
  }

  /** GET /hub/set. */
  async view(): Promise<SetView> {
    const m = this.o.goals.setModel();
    await this.o.library.load(this.refsOf(m));
    return this.viewOf(m, await this.statuses(false));
  }

  /** Every goal's status as last read. An edit takes it without reading again, since no op changes a status; a new track is not started. */
  private lastStatuses: Map<string, GoalStatus> | null = null;
  private statusRead: Promise<Map<string, GoalStatus>> | null = null;

  /** Statuses read now, or with `stale` the last reading, read again behind the answer. */
  private async statuses(stale: boolean): Promise<Map<string, GoalStatus>> {
    const read = (this.statusRead ??= this.o.statuses().then((s) => (this.lastStatuses = s), (e) => {
      if (this.lastStatuses) return this.lastStatuses;
      throw e;
    }).finally(() => {
      this.statusRead = null;
    }));
    if (stale && this.lastStatuses) {
      read.catch(() => {});
      return this.lastStatuses;
    }
    return read;
  }

  private viewOf(m: SetModel, statuses: Map<string, GoalStatus>): SetView {
    const o = this.opts();
    const findings: ArrangementFinding[] = [];
    for (const t of m.tracks) findings.push(...validateArrangement(t.arrangement, o).map((f) => ({ ...f, id: `${t.id}/${f.id}` })));
    for (const s of m.scenes) findings.push(...this.compose("scene", s.id, m).composition.findings.map((f) => ({ ...f, id: `${s.id}/${f.id}` })));
    return {
      tracks: m.tracks.map((t) => ({ ...t, layout: arrangementLayout(t.arrangement, o.factsOf), status: statuses.get(t.id) ?? "not_started" })),
      scenes: m.scenes.map((s) => ({ ...s, slots: s.slots.map((sl) => ({ ...sl, length: o.factsOf(sl.target, sl.kind)?.length ?? null })), status: statuses.get(s.id) ?? "not_started" })),
      text: setToText(m),
      findings,
    };
  }

  /** POST /hub/set/edit: ops applied in order, all or none. */
  async edit(body: unknown): Promise<SetView & { changed: string[] }> {
    if (!isObject(body)) throw bad("body must be { ops }");
    const parsed = setOpsOf(body.ops);
    if (parsed.findings.length) throw new FindingError(parsed.findings[0], parsed.findings);
    const m = this.o.goals.setModel();
    const targetsOf = (list: readonly SetOp[]) => list.flatMap((op): Array<{ kind: InstanceKind; target: string }> => {
      const x = op as SetOp & { edit?: { kind?: unknown; target?: unknown }; kind?: unknown; target?: unknown; track?: unknown; id?: unknown };
      // A retarget keeps its instance's kind unless it names one.
      const own = op.op === "instance.retarget" ? m.tracks.find((t) => t.id === x.track || t.title.toLowerCase() === String(x.track).toLowerCase())?.arrangement.instances.find((i) => i.id === x.id)?.kind : undefined;
      const k = x.edit?.kind ?? x.kind ?? own;
      const t = x.edit?.target ?? x.target;
      return (k === "clip" || k === "script") && typeof t === "string" ? [{ kind: k, target: t }] : [];
    });
    // Clips read before stand as they are: a move or a trim waits on no editor round trip, only a clip never read does.
    await this.o.library.load([...this.refsOf(m), ...targetsOf(parsed.ops)], true, true);
    const ops = await this.uniques(parsed.ops, m);
    // The copies make unique just made are read like any clip an op names, and taken as clips even before the editor lists them.
    const copies = targetsOf(ops.filter((op) => !parsed.ops.includes(op)));
    if (copies.length) await this.o.library.load(copies);
    const r = applySetOps(m, ops, { ...this.opts(), made: new Set(copies.map((c) => targetKey(c.target))) });
    if (!r.ok) throw new FindingError(r.finding);
    // Deleting through an op is held to what DELETE /hub/goals/:id refuses.
    const kept = new Set([...r.set.tracks, ...r.set.scenes].map((x) => x.id));
    for (const x of [...m.tracks, ...m.scenes]) {
      const why = kept.has(x.id) ? null : this.o.deleteBlocked?.(x.id);
      if (why) throw new FindingError({ id: x.id, field: "delete", message: why, blocks: true });
    }
    this.o.goals.applySet(r.set);
    return { ...this.viewOf(this.o.goals.setModel(), await this.statuses(true)), changed: r.changed };
  }

  /**
   * Make unique: copy the clip asset beside the original (asset duplicate) and
   * point the slot or instance at the copy, as slot.set and instance.retarget.
   */
  private async uniques(ops: readonly SetOp[], m: SetModel): Promise<SetOp[]> {
    const out: SetOp[] = [];
    for (const op of ops) {
      if (op.op !== "slot.unique" && op.op !== "instance.unique") {
        out.push(op);
        continue;
      }
      const track = m.tracks.find((t) => t.id === op.track || t.title.toLowerCase() === String(op.track).toLowerCase());
      if (!track) throw new FindingError({ id: String(op.track), field: "track", message: `no track ${String(op.track)}`, blocks: true });
      let source: { kind: InstanceKind; target: string } | undefined;
      if (op.op === "slot.unique") {
        const scene = m.scenes.find((s) => s.id === op.scene || s.title.toLowerCase() === String(op.scene).toLowerCase());
        source = scene?.slots.find((s) => s.track === track.id);
        if (!scene || !source) throw new FindingError({ id: `${String(op.scene)}/${track.id}`, field: "slot", message: `no clip in ${String(op.scene)} on ${track.title}`, blocks: true });
      } else {
        source = track.arrangement.instances.find((i) => i.id === op.id);
        if (!source) throw new FindingError({ id: `${track.id}/${op.id}`, field: "id", message: `${track.title} has no instance ${op.id}`, blocks: true });
      }
      const copy = await this.duplicateAsset(source.target);
      if (op.op === "slot.unique") out.push({ op: "slot.set", scene: op.scene, track: track.id, kind: source.kind, target: copy });
      else out.push({ op: "instance.retarget", track: track.id, id: op.id, target: copy });
    }
    return out;
  }

  /** A copy of an asset beside it, named <name>_<n>; returns its path. */
  private async duplicateAsset(target: string): Promise<string> {
    const base = targetKey(target);
    const dir = base.slice(0, base.lastIndexOf("/"));
    const name = base.slice(base.lastIndexOf("/") + 1);
    for (let n = 2; n < 100; n++) {
      const dest = `${dir}/${name}_${n}`;
      const r = await this.o.host.callAction("asset", "duplicate", { sourcePath: base, destinationPath: dest, onConflict: "error" });
      const error = failureOf(r);
      if (!error) {
        this.o.library.forget(dest);
        const body = answerOf(r);
        return typeof body?.path === "string" ? targetKey(body.path) : dest;
      }
      if (!/exist/i.test(error)) throw new FindingError({ id: base, field: "target", message: `the clip could not be copied: ${error}`, blocks: true });
    }
    throw new FindingError({ id: base, field: "target", message: "no free name for the copy", blocks: true });
  }

  // ── One track's timeline ─────────────────────────────────────────

  private track(id: string) {
    const t = this.o.goals.setModel().tracks.find((x) => x.id === id);
    if (!t) throw notFound(`no track ${id}`);
    return t;
  }

  private arrangementView(a: Arrangement): ArrangementView {
    const o = this.opts();
    return { arrangement: a, text: arrangementToText(a), layout: arrangementLayout(a, o.factsOf), findings: validateArrangement(a, o) };
  }

  async arrangement(id: string): Promise<ArrangementView> {
    const t = this.track(id);
    await this.o.library.load(t.arrangement.instances);
    return this.arrangementView(t.arrangement);
  }

  async putArrangement(id: string, body: unknown): Promise<ArrangementView> {
    this.track(id);
    if (!isObject(body)) throw bad("body must be { arrangement } or { text }");
    const read = typeof body.text === "string" ? arrangementFromText(body.text) : arrangementOf(body.arrangement);
    await this.o.library.load(read.arrangement.instances);
    const findings = [...read.findings, ...validateArrangement(read.arrangement, this.opts())];
    const stop = blockingFindings(findings)[0];
    if (stop) throw new FindingError(stop, findings);
    this.o.goals.setArrangement(id, read.arrangement);
    return this.arrangementView(read.arrangement);
  }

  async editArrangement(id: string, body: unknown): Promise<ArrangementView & { changed: string[] }> {
    const t = this.track(id);
    if (!isObject(body)) throw bad("body must be { ops }");
    const parsed = arrangementOpsOf(body.ops);
    if (parsed.findings.length) throw new FindingError(parsed.findings[0], parsed.findings);
    const added = parsed.ops.flatMap((op) => (op.op === "add" && (op.kind === "clip" || op.kind === "script") && typeof op.target === "string" ? [{ kind: op.kind, target: op.target }] : []));
    await this.o.library.load([...t.arrangement.instances, ...added], true, true);
    const r = applyArrangementOps(t.arrangement, parsed.ops, this.opts());
    if (!r.ok) throw new FindingError(r.finding, r.findings);
    this.o.goals.setArrangement(id, r.arrangement);
    return { ...this.arrangementView(r.arrangement), changed: r.changed };
  }

  /** Where a track's timeline ends, with the clips read so far. */
  end(a: Arrangement): number {
    return arrangementEnd(arrangementLayout(a, (t, k) => this.o.library.facts(t, k)));
  }

  /** GET /hub/clips. */
  clips(q: { q?: unknown; kind?: unknown }): Promise<ClipEntry[]> {
    const kind = q.kind === "clip" || q.kind === "script" ? q.kind : undefined;
    return this.o.library.entries(typeof q.q === "string" ? q.q : "", kind, this.o.lastRun);
  }

  // ── Runs ─────────────────────────────────────────────────────────

  /** What a scope plays, from the set as it is now. Throws 404 for a track or scene that is gone. */
  compose(kind: ArrangementRunKind, goalId: string, m: SetModel = this.o.goals.setModel(), from = 0): SetRunPlan {
    const o = this.opts();
    // Timelines play from `from`; a slot always plays its clip from the start.
    const start = kind === "slot" ? 0 : from;
    const asTrack = (t: SetModel["tracks"][number], arrangement: Arrangement): ComposeTrack => ({ goalId: t.id, title: t.title, arrangement: arrangementFrom(arrangement, start, o.factsOf), player: t.player });
    let tracks: ComposeTrack[];
    let title: string;
    if (kind === "track") {
      const t = m.tracks.find((x) => x.id === goalId);
      if (!t) throw notFound(`no track ${goalId}`);
      tracks = [asTrack(t, t.arrangement)];
      title = t.title;
    } else if (kind === "arrangement") {
      tracks = audibleTracks(m.tracks).map((t) => asTrack(t, t.arrangement));
      title = "Arrangement";
    } else if (kind === "scene") {
      // Phase 1: a scene is a composition of tracks, each playing its timeline.
      const s = m.scenes.find((x) => x.id === goalId);
      if (!s) throw notFound(`no scene ${goalId}`);
      const mine = s.tracks.map((id) => m.tracks.find((t) => t.id === id)).filter((t): t is SetModel["tracks"][number] => !!t);
      tracks = audibleTracks(mine).map((t) => asTrack(t, t.arrangement));
      title = s.title;
    } else {
      const [sceneId, trackId] = kind === "slot" ? goalId.split("/") : [goalId, undefined];
      const s = m.scenes.find((x) => x.id === sceneId);
      if (!s) throw notFound(`no scene ${sceneId}`);
      const playing = kind === "slot" ? m.tracks.filter((t) => t.id === trackId) : audibleTracks(m.tracks);
      if (kind === "slot" && playing.length === 0) throw notFound(`no track ${trackId}`);
      tracks = playing.flatMap((t) => {
        const slot = s.slots.find((x) => x.track === t.id);
        return slot ? [asTrack(t, { rate: t.arrangement.rate, instances: [{ id: s.id.slice(0, 30), kind: slot.kind, target: slot.target, start: 0, ...(slot.name ? { name: slot.name } : {}) }] })] : [];
      });
      title = kind === "slot" ? `${s.title}: ${playing[0].title}` : s.title;
    }
    const composition = composeArrangement(tracks, { clip: (t) => this.o.library.clip(t), facts: o.factsOf, scene: tracks.length > 1 || kind !== "track" });
    if (tracks.length === 0) composition.findings.push({ id: goalId, field: "tracks", message: kind === "arrangement" ? "no track plays: every track is muted, or none is soloed that has clips" : "nothing in it plays", blocks: true });
    else if (start > 0 && composition.segments.length === 0) composition.findings.push({ id: goalId, field: "from", message: `nothing plays from ${(start / ARRANGEMENT_RATE).toFixed(2).replace(/\.?0+$/, "")} s: every clip ends before it`, blocks: true });
    return { kind, goalId, title, composition, tracks: tracks.map((t) => ({ goalId: t.goalId, title: t.title ?? t.goalId, player: t.player })), ...(start > 0 ? { from: start } : {}) };
  }

  /** Compose a plan item's run with the clips it uses read fresh. */
  async plan(kind: ArrangementRunKind, goalId: string, from = 0): Promise<SetRunPlan> {
    const m = this.o.goals.setModel();
    await this.o.library.load(this.refsOf(m), true);
    return this.compose(kind, goalId, m, from);
  }
}

/** The run a plan scope stands for, as a plan item's check, or null for a scope of playtests. */
export function setItemOf(scope: PlanScope, goals: Goals): { kind: ArrangementRunKind; target: string } | null {
  switch (scope.kind) {
    case "track":
    case "scene":
      return { kind: scope.kind, target: scope.goalId };
    case "slot":
      return { kind: "slot", target: `${scope.sceneId}/${scope.trackId}` };
    case "arrangement":
      return { kind: "arrangement", target: "arrangement" };
    case "goal": {
      const g = goals.get(scope.goalId);
      if (g?.kind === "goal" && g.arrangement && g.arrangement.instances.some((i) => i.kind === "clip")) return { kind: "track", target: g.id };
      if (g?.kind === "milestone" && g.slots?.length) return { kind: "scene", target: g.id };
      return null;
    }
    default:
      return null;
  }
}
