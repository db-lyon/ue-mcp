// @session per-session: Reads and briefly keeps one project's clips (timeline clips and step-list playtests) for the hub's set.
/**
 * The clips the set refers to (#2113): timeline clips read with pie(clip_read)
 * and listed with pie(clip_list), step-list playtests from the hub's script
 * index. Layout, validation and composition are synchronous, so the hub loads
 * what an answer needs first and reads facts from here after.
 *
 * A clip read stands for a few seconds only: an edit in the piano roll changes
 * every instance of the clip, and the next read must see it.
 */
import {
  clipFacts,
  targetKey,
  type ClipEntry,
  type ClipFacts,
  type ClipJson,
  type InstanceKind,
  type RunRecord,
} from "./contract.js";
import type { HubHost, HubScriptIndex } from "./ports.js";
import { answerOf, failureOf } from "./util.js";

/** How long a clip read or a list stands. */
const FRESH_MS = 3000;

interface Read { at: number; clip: ClipJson | null; name: string }

export class ClipLibrary {
  private readonly clips = new Map<string, Read>();
  private clipList: { at: number; list: Array<{ path: string; name: string; map: string | null }> | null } | null = null;
  private scriptList: { at: number; list: Array<{ path: string; name: string; map: string | null }> | null } | null = null;

  constructor(
    private readonly o: {
      host: Pick<HubHost, "callAction">;
      index: HubScriptIndex;
      /** A script's newest runs, newest first. */
      runsOf: (target: string) => RunRecord[];
      connected: () => boolean;
      now?: () => number;
    },
  ) {}

  private now(): number {
    return this.o.now?.() ?? Date.now();
  }

  /** Read every clip named here that is not fresh, and the lists. Offline, what was read stands. */
  async load(refs: ReadonlyArray<{ kind: InstanceKind; target: string }>, lists = true): Promise<void> {
    if (!this.o.connected()) return;
    const jobs: Array<Promise<unknown>> = [];
    if (lists) jobs.push(this.lists());
    const seen = new Set<string>();
    for (const r of refs) {
      if (r.kind !== "clip") continue;
      const t = targetKey(r.target);
      if (seen.has(t)) continue;
      seen.add(t);
      const had = this.clips.get(t);
      if (had && this.now() - had.at < FRESH_MS) continue;
      jobs.push(this.readClip(t));
    }
    await Promise.all(jobs);
  }

  private async readClip(target: string): Promise<void> {
    const r = await this.o.host.callAction("pie", "clip_read", { path: target }).catch(() => null);
    const body = r ? answerOf(r) : null;
    const clip = body && !failureOf(r!) && body.clip && typeof body.clip === "object" ? (body.clip as ClipJson) : null;
    this.clips.set(target, { at: this.now(), clip, name: target.slice(target.lastIndexOf("/") + 1) });
  }

  private async lists(): Promise<void> {
    const fresh = (x: { at: number } | null) => !!x && this.now() - x.at < FRESH_MS;
    const jobs: Array<Promise<void>> = [];
    if (!fresh(this.clipList)) {
      jobs.push((async () => {
        const r = await this.o.host.callAction("pie", "clip_list", { directory: "/Game" }).catch(() => null);
        const body = r ? answerOf(r) : null;
        const list = body && Array.isArray(body.clips)
          ? (body.clips as Array<Record<string, unknown>>).filter((c) => typeof c.path === "string").map((c) => ({ path: targetKey(String(c.path)), name: typeof c.name === "string" ? c.name : leaf(String(c.path)), map: typeof c.map === "string" && c.map ? c.map : null }))
          : null;
        this.clipList = { at: this.now(), list };
      })());
    }
    if (!fresh(this.scriptList)) {
      jobs.push((async () => {
        const list = await this.o.index.listScripts().catch(() => null);
        this.scriptList = { at: this.now(), list: list ? list.map((s) => ({ path: s.path, name: s.name, map: s.map ?? null })) : null };
      })());
    }
    await Promise.all(jobs);
  }

  /** A clip's JSON as last read, or undefined. */
  clip(target: string): ClipJson | undefined {
    return this.clips.get(targetKey(target))?.clip ?? undefined;
  }

  /** Facts for layout and validation; undefined when the clip is not known to exist. */
  facts(target: string, kind: InstanceKind): ClipFacts | undefined {
    const t = targetKey(target);
    if (kind === "clip") {
      const c = this.clip(t);
      const listed = this.clipList?.list?.find((x) => x.path === t);
      return c ? clipFacts(t, listed?.name ?? leaf(t), c) : undefined;
    }
    const listed = this.scriptList?.list;
    const entry = listed?.find((s) => s.path === t);
    const runs = this.o.runsOf(t);
    if (listed && !entry) return undefined;
    if (!listed && runs.length === 0) return undefined;
    const ms = runs.find((r) => r.durationMs && r.durationMs > 0)?.durationMs ?? null;
    return { kind: "script", target: t, name: entry?.name ?? runs[0]?.name ?? leaf(t), length: ms ? Math.max(1, Math.round((ms / 1000) * 60)) : null, rate: 60, map: entry?.map ?? runs[0]?.map ?? null };
  }

  /** Every clip of a kind on this checkout, or null when the list could not be read. */
  library(kind: InstanceKind): string[] | null {
    const l = kind === "clip" ? this.clipList?.list : this.scriptList?.list;
    return l ? l.map((x) => x.path) : null;
  }

  /** The browser's list: both kinds, filtered by a search over names and paths. */
  async entries(q: string, kind?: InstanceKind, lastRun?: (kind: InstanceKind, target: string) => ClipEntry["lastRun"]): Promise<ClipEntry[]> {
    await this.load([], true);
    const words = q.toLowerCase().split(/\s+/).filter(Boolean);
    const match = (s: string) => words.every((w) => s.toLowerCase().includes(w));
    const out: ClipEntry[] = [];
    for (const k of ["clip", "script"] as const) {
      if (kind && kind !== k) continue;
      for (const x of (k === "clip" ? this.clipList?.list : this.scriptList?.list) ?? []) {
        if (!match(`${x.name} ${x.path}`)) continue;
        const f = this.facts(x.path, k);
        out.push({ kind: k, target: x.path, name: x.name, map: x.map, length: f?.length ?? null, rate: f?.rate ?? 60, lastRun: lastRun?.(k, x.path) ?? null });
      }
    }
    return out.sort((a, b) => a.name.localeCompare(b.name));
  }

  /** Forget a clip's read, after it was changed here. */
  forget(target: string): void {
    this.clips.delete(targetKey(target));
    this.clipList = null;
    this.scriptList = null;
  }
}

const leaf = (p: string): string => p.slice(p.lastIndexOf("/") + 1);
