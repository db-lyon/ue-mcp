/**
 * Checkpoints per editor session (#1482): whether they are on, the bridge's
 * save recording, and the checkpoint, changes, restore and clear operations
 * the editor(...) actions and the daemon routes share.
 *
 * Off unless `checkpoints: on` is in ue-mcp.yml or the daemon turned them on
 * for the session. While off, no shadow repository is created and the bridge
 * is told not to intercept saves.
 *
 * The bridge records from UPackage::PreSavePackageWithContextEvent into the
 * pending generation it was armed with. Arming happens on every connect, and
 * each checkpoint re-arms it with a fresh generation, so a checkpoint folds in
 * exactly the copies made before it.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { warn, info } from "../core/log.js";
import type { EditorSession } from "../sessions/session.js";
import {
  CheckpointError,
  DEFAULT_LARGE_FILE_BYTES,
  ShadowRepo,
  changeKind,
  type ChangeEntry,
  type CheckpointInfo,
  type CheckpointTrigger,
  type RestoreRefusal,
} from "./shadow-repo.js";
import {
  ASSET_DIFF_FORMAT,
  MAX_ASSET_DIFFS,
  assetDiffDeferred,
  assetDiffError,
  assetDiffFromBridge,
  type AssetDiff,
  type AssetThumbnail,
} from "./asset-diff.js";

export { CheckpointError } from "./shadow-repo.js";
export type { AssetDiff, AssetDiffNode, AssetDiffNodePin, AssetDiffNodeProperty, AssetDiffRecord, AssetDiffSection, AssetFact, AssetThumbnail, AssetVersion } from "./asset-diff.js";
export type { ChangeEntry, CheckpointInfo, RestoreRefusal } from "./shadow-repo.js";

/** How many Blueprint packages one list_changes diffs in the editor. */
const MAX_BLUEPRINT_DIFFS = 25;

export interface CheckpointState {
  editor: string;
  enabled: boolean;
  source: "config" | "session";
  configured: boolean;
  repository: string | null;
  checkpoints: CheckpointInfo[];
}

export interface CheckpointResult extends CheckpointInfo {
  editor: string;
  changes: ChangeEntry[];
  dirtyPackages: string[];
  editorConnected: boolean;
}

export interface ChangesResult {
  editor: string;
  from: string;
  to: string | null;
  changes: ChangeEntry[];
  /**
   * Which packages carry an asset diff. Past `limit`, the rest are `deferred`:
   * their assetDiff says so, and assetDiff(from, to, path) diffs one.
   */
  assetDiffs?: { limit: number; diffed: number; deferred: string[] };
}

export interface NativeDiffResult {
  editor: string;
  path: string;
  /** window: Unreal's diff window; editor: the asset editor on the one version of an added or deleted asset; external: the text diff tool. */
  opened: "window" | "editor" | "external";
  windowTitle?: string;
  tool?: string;
}

export interface RestoreResult {
  editor: string;
  checkpoint: string;
  before: string;
  after: string;
  restored: Array<{ path: string; action: "written" | "deleted" }>;
  refused: RestoreRefusal[];
  reloaded: string[];
  unloaded: string[];
  configsReloaded: string[];
  reopened: string[];
}

export type CheckpointEvent =
  | { type: "toggled"; data: { editor: string; enabled: boolean; source: "config" | "session" } }
  | { type: "created"; data: { editor: string; id: string; label: string; trigger: CheckpointTrigger; files: number } }
  | { type: "restored"; data: { editor: string; checkpoint: string; before: string; after: string; restored: string[]; refused: Array<{ path: string; reason: string }> } }
  | { type: "cleared"; data: { editor: string } };

const listeners = new Set<(e: CheckpointEvent) => void>();

/** Every session's checkpoint events. The daemon republishes them as checkpoint.* and checkpoints.*. */
export function subscribeCheckpointEvents(listener: (e: CheckpointEvent) => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

function emit(e: CheckpointEvent): void {
  for (const l of listeners) {
    try {
      l(e);
    } catch (err) {
      warn("checkpoints", "event listener failed", err);
    }
  }
}

type Json = Record<string, unknown>;

/** `on`, `true` and the object form's `enabled` read as on; anything else as off. */
export function configuredCheckpoints(value: unknown): { enabled: boolean; largeFileBytes: number } {
  const on = (v: unknown) => v === true || v === "on";
  if (value && typeof value === "object") {
    const o = value as { enabled?: unknown; largeFileBytes?: unknown };
    return {
      enabled: on(o.enabled),
      largeFileBytes: typeof o.largeFileBytes === "number" && o.largeFileBytes > 0 ? o.largeFileBytes : DEFAULT_LARGE_FILE_BYTES,
    };
  }
  return { enabled: on(value), largeFileBytes: DEFAULT_LARGE_FILE_BYTES };
}

export class SessionCheckpoints {
  private override: boolean | null = null;
  private repoCache: ShadowRepo | null = null;
  /** The generation the connected bridge records into, which a checkpoint must not fold in. */
  private activeGeneration: string | null = null;
  private warnedNoRecorder = false;

  constructor(private readonly session: EditorSession) {
    session.bridge.onConnectionChanged((change) => {
      if (change.connected) void this.arm().catch((e) => warn("checkpoints", `arming ${session.name} failed`, e));
    });
  }

  private get projectDir(): string {
    const dir = this.session.project.projectDir;
    if (!dir) throw new CheckpointError(400, "Checkpoints need a project; this session has none");
    return dir;
  }

  private configured(): { enabled: boolean; largeFileBytes: number } {
    return configuredCheckpoints(this.session.project.config.checkpoints);
  }

  enabled(): boolean {
    return this.override ?? this.configured().enabled;
  }

  private repo(): ShadowRepo {
    const dir = this.projectDir;
    if (!this.repoCache || this.repoCache.projectDir !== dir) {
      this.repoCache = new ShadowRepo(dir, { largeFileBytes: this.configured().largeFileBytes });
    }
    return this.repoCache;
  }

  private requireEnabled(): void {
    if (!this.enabled()) {
      throw new CheckpointError(409, "Checkpoints are off for this session. Set `checkpoints: on` in ue-mcp.yml, or turn them on through the daemon (PUT /v1/checkpoints/enabled).");
    }
  }

  async state(limit = 50): Promise<CheckpointState> {
    const configured = this.configured().enabled;
    const hasProject = !!this.session.project.projectDir;
    const repo = hasProject ? this.repo() : null;
    return {
      editor: this.session.name,
      enabled: this.enabled(),
      source: this.override === null ? "config" : "session",
      configured,
      repository: repo?.exists() ? repo.gitdir : null,
      checkpoints: repo ? await repo.list(limit) : [],
    };
  }

  /** The daemon's per-session override; null goes back to ue-mcp.yml. */
  async setEnabled(enabled: boolean | null): Promise<CheckpointState> {
    const was = this.enabled();
    this.override = enabled;
    const now = this.enabled();
    if (now !== was || enabled === null) {
      await this.arm().catch((e) => warn("checkpoints", `arming ${this.session.name} failed`, e));
      emit({ type: "toggled", data: { editor: this.session.name, enabled: now, source: this.override === null ? "config" : "session" } });
    }
    return this.state();
  }

  /**
   * Tell the connected bridge whether to record saves, and where. On means a
   * fresh generation; the previous one is left for the next checkpoint.
   */
  private async arm(): Promise<{ dirtyPackages: string[] } | null> {
    if (!this.session.bridge.isConnected || !this.session.project.projectDir) return null;
    if (!this.enabled()) {
      if (this.activeGeneration === null) return null;
      this.activeGeneration = null;
      await this.callBridge("checkpoint_arm", { enabled: false });
      return null;
    }
    const repo = this.repo();
    await repo.ensure();
    const generation = repo.newGeneration("bridge");
    const answer = await this.callBridge("checkpoint_arm", { enabled: true, pendingDir: generation, projectDir: repo.projectDir });
    if (!answer) {
      fs.rmSync(generation, { recursive: true, force: true });
      this.activeGeneration = null;
      return null;
    }
    this.activeGeneration = generation;
    const dirty = Array.isArray(answer.dirtyPackages) ? answer.dirtyPackages.map(String) : [];
    return { dirtyPackages: dirty };
  }

  /** A bridge call that reports failure as null, warning once when the plugin predates the recorder. */
  private async callBridge(method: string, params: Json): Promise<Json | null> {
    try {
      const r = (await this.session.bridge.call(method, params)) as Json | null;
      if (r && r.success === false) {
        if (method !== "checkpoint_arm") throw new CheckpointError(409, String(r.error ?? `${method} failed`));
        warn("checkpoints", `${method}: ${String(r.error ?? "failed")}`);
        return null;
      }
      return r ?? {};
    } catch (e) {
      if (e instanceof CheckpointError) throw e;
      const message = e instanceof Error ? e.message : String(e);
      if (/unknown method/i.test(message)) {
        if (!this.warnedNoRecorder) {
          this.warnedNoRecorder = true;
          warn("checkpoints", `The editor's bridge plugin predates checkpoints (${method}); package saves are not recorded until it is updated.`);
        }
        return null;
      }
      if (method === "checkpoint_arm") {
        warn("checkpoints", `${method} failed`, e);
        return null;
      }
      throw e;
    }
  }

  /** Copy a file's bytes into the history before the server writes it. A no-op while off. */
  recordBeforeWrite(file: string): void {
    if (!this.enabled() || !this.session.project.projectDir) return;
    try {
      this.repo().recordBeforeWrite(file);
    } catch (e) {
      warn("checkpoints", `could not record ${file} before writing it`, e);
    }
  }

  create(label?: string): Promise<CheckpointResult> {
    this.requireEnabled();
    return this.repo().serialize(() => this.createUnlocked(label ?? "", "client"));
  }

  private async createUnlocked(label: string, trigger: CheckpointTrigger, expected?: string[]): Promise<CheckpointResult> {
    const repo = this.repo();
    // Re-arm first, so every save before this point is in a generation this checkpoint folds in.
    const armed = await this.arm();
    const connected = armed !== null;
    const out = await repo.checkpoint({
      label,
      trigger,
      activeGeneration: connected ? this.activeGeneration : null,
      expected,
    });
    emit({ type: "created", data: { editor: this.session.name, id: out.info.id, label: out.info.label, trigger, files: out.info.files } });
    return {
      ...out.info,
      editor: this.session.name,
      changes: out.changes,
      dirtyPackages: armed?.dirtyPackages ?? [],
      editorConnected: connected,
    };
  }

  async changes(from: string, to?: string | null, opts: { blueprintDiffs?: boolean; assetDiffs?: boolean } = {}): Promise<ChangesResult> {
    if (!from) throw new CheckpointError(400, "from is required: the checkpoint id to compare against");
    const repo = this.repo();
    return repo.serialize(async () => {
      const result: ChangesResult = { editor: this.session.name, ...(await repo.changes(from, to ?? null)) };
      if (opts.assetDiffs !== false) {
        const legacy = await this.addAssetDiffs(result);
        if (legacy && opts.blueprintDiffs !== false && this.session.bridge.isConnected) await this.addBlueprintDiffs(result);
      } else if (opts.blueprintDiffs !== false && this.session.bridge.isConnected) {
        await this.addBlueprintDiffs(result);
      }
      return result;
    });
  }

  /**
   * One package's asset diff between two checkpoints (to null: the project
   * now), for a path list_changes deferred or a client asks for again.
   */
  async assetDiff(from: string, to: string | null | undefined, key: string): Promise<AssetDiff> {
    if (!from) throw new CheckpointError(400, "from is required: the checkpoint id to compare against");
    if (!key) throw new CheckpointError(400, "path is required: the project-relative path of the package");
    const repo = this.repo();
    const rel = key.replace(/\\/g, "/").toLowerCase();
    return repo.serialize(async () => {
      const result = await repo.changes(from, to ?? null);
      const change = result.changes.find((c) => c.path.toLowerCase() === rel);
      if (!change) throw new CheckpointError(404, `${key} did not change between those checkpoints`);
      if (change.kind !== "package") throw new CheckpointError(400, `${key} is not a package; its diff is the text diff list_changes gives`);
      return (await this.diffPackage(result.from, result.to, change)).diff;
    });
  }

  /** A picture asset diffs refer to, by id, as a file path. */
  thumbnail(id: string): { file: string; type: string } | null {
    if (!/^[0-9a-f]{40}$/.test(id) || !this.session.project.projectDir) return null;
    const file = path.join(this.repo().thumbsDir, `${id}.png`);
    return fs.existsSync(file) ? { file, type: "image/png" } : null;
  }

  /**
   * Open Unreal's own diff of a package between two checkpoints (to omitted:
   * the asset as loaded now) and bring the editor to the front. On an added or
   * deleted package, the asset editor opens on the one version there is.
   */
  async openNativeDiff(key: string, from: string, to?: string | null): Promise<NativeDiffResult> {
    if (!from) throw new CheckpointError(400, "from is required: the checkpoint id to compare against");
    if (!key) throw new CheckpointError(400, "path is required: the project-relative path of the package");
    if (!this.session.bridge.isConnected) throw new CheckpointError(409, "The editor is not connected, so Unreal cannot open its diff.");
    const repo = this.repo();
    const rel = key.replace(/\\/g, "/");
    return repo.serialize(async () => {
      const a = await repo.versionOf(from, rel);
      const b = to ? await repo.versionOf(to, rel) : undefined;
      if (a === undefined && (!to || b === undefined)) throw new CheckpointError(404, `Nothing recorded ${rel} by checkpoint ${from}`);
      const dir = this.diffDir("native");
      this.pruneNative(dir);
      const ext = path.extname(rel);
      const base = path.basename(rel, ext);
      const fromFile = a ? path.join(dir, `${base}-${a.slice(0, 12)}${ext}`) : null;
      const toFile = to && b ? path.join(dir, `${base}-${b.slice(0, 12)}${ext}`) : null;
      if (fromFile && a) await repo.exportVersion(a, fromFile);
      if (toFile && b) await repo.exportVersion(b, toFile);
      const labels = await this.labels(from, to ?? null);
      const current = !to && fs.existsSync(path.join(this.projectDir, rel));
      const answer = await this.callBridge("checkpoint_open_native_diff", {
        originalFile: path.join(this.projectDir, rel),
        ...(fromFile ? { fromFile } : {}),
        ...(toFile ? { toFile } : {}),
        ...(current ? { current: true } : {}),
        fromLabel: labels.from,
        toLabel: labels.to,
      });
      if (!answer) throw new CheckpointError(409, "The editor's bridge plugin predates checkpoint_open_native_diff; update it.");
      return {
        editor: this.session.name,
        path: rel,
        opened: String(answer.opened) as NativeDiffResult["opened"],
        ...(typeof answer.windowTitle === "string" ? { windowTitle: answer.windowTitle } : {}),
        ...(typeof answer.tool === "string" ? { tool: answer.tool } : {}),
      };
    });
  }

  private async labels(from: string, to: string | null): Promise<{ from: string; to: string }> {
    const list = await this.repo().list(Number.MAX_SAFE_INTEGER);
    const name = (id: string) => {
      const c = list.find((x) => x.id.startsWith(id.toLowerCase()));
      return c ? `Checkpoint ${c.id.slice(0, 7)}${c.label ? `: ${c.label}` : ""}` : `Checkpoint ${id.slice(0, 7)}`;
    };
    return { from: name(from), to: to ? name(to) : "Current" };
  }

  /** Saved/Diff maps to /Temp, which is where the editor loads a package for diffing. */
  private diffDir(sub: string): string {
    return path.join(this.projectDir, "Saved", "Diff", "UE_MCP_Checkpoints", sub);
  }

  /** Copies an open diff window loaded stay while it may be open; a day later they go. */
  private pruneNative(dir: string): void {
    try {
      const cutoff = Date.now() - 24 * 3600_000;
      for (const n of fs.readdirSync(dir)) {
        const f = path.join(dir, n);
        if (fs.statSync(f).mtimeMs < cutoff) fs.rmSync(f, { force: true });
      }
    } catch {
      // No folder yet.
    }
  }

  /**
   * Asset diffs for the first MAX_ASSET_DIFFS packages, the rest marked
   * deferred. True when the bridge predates checkpoint_diff_asset, so the
   * caller falls back to Blueprint diffs alone.
   */
  private async addAssetDiffs(result: ChangesResult): Promise<boolean> {
    const packages = result.changes.filter((c) => c.kind === "package");
    const now = packages.slice(0, MAX_ASSET_DIFFS);
    const later = packages.slice(MAX_ASSET_DIFFS);
    let legacy = false;
    for (const c of now) {
      const { diff, unknownMethod } = await this.diffPackage(result.from, result.to, c);
      c.assetDiff = diff;
      if (unknownMethod) legacy = true;
      if (diff.blueprint) c.blueprintDiff = diff.blueprint;
    }
    for (const c of later) c.assetDiff = assetDiffDeferred();
    result.assetDiffs = { limit: MAX_ASSET_DIFFS, diffed: now.length, deferred: later.map((c) => c.path) };
    return legacy;
  }

  /** One package's diff, from the cache when these two versions were diffed before. */
  private async diffPackage(from: string, to: string | null, c: ChangeEntry): Promise<{ diff: AssetDiff; unknownMethod?: boolean }> {
    if (c.large) return { diff: assetDiffError("The package is over the large-file size and kept as a plain copy, so it is not loaded to diff it.") };
    if (!this.session.bridge.isConnected) return { diff: assetDiffError("The editor is not connected, so Unreal cannot load the versions to diff them.") };
    const repo = this.repo();
    const a = c.status === "added" ? null : await repo.versionOf(from, c.path);
    const b = c.status === "deleted" ? null : await repo.versionOf(to, c.path);
    if (a === undefined || b === undefined) return { diff: assetDiffError("The shadow history has no record of one version of this package.") };

    const key = `${ASSET_DIFF_FORMAT}\n${c.path.toLowerCase()}\n${a ?? "-"}\n${b ?? "-"}`;
    const cacheFile = path.join(repo.diffsDir, `${crypto.createHash("sha1").update(key).digest("hex")}.json`);
    try {
      const cached = JSON.parse(fs.readFileSync(cacheFile, "utf-8")) as AssetDiff;
      return { diff: this.withUrls(cached) };
    } catch {
      // Not diffed yet.
    }

    const dir = this.diffDir("diff");
    const ext = path.extname(c.path);
    const base = path.basename(c.path, ext);
    const fromFile = a ? path.join(dir, `${base}-${a.slice(0, 12)}${ext}`) : null;
    const toFile = b ? path.join(dir, `${base}-${b.slice(0, 12)}${ext}`) : null;
    const thumb = (v: string) => path.join(repo.thumbsDir, `${v}.png`);
    const want = (v: string | null) => (v && !fs.existsSync(thumb(v)) ? thumb(v) : null);
    try {
      if (fromFile && a) await repo.exportVersion(a, fromFile);
      if (toFile && b) await repo.exportVersion(b, toFile);
      fs.mkdirSync(repo.thumbsDir, { recursive: true });
      const fromThumbnail = want(a);
      const toThumbnail = want(b);
      let raw: Json | null;
      try {
        raw = (await this.session.bridge.call("checkpoint_diff_asset", {
          originalFile: path.join(this.projectDir, c.path),
          ...(fromFile ? { fromFile } : {}),
          ...(toFile ? { toFile } : {}),
          ...(fromThumbnail ? { fromThumbnail } : {}),
          ...(toThumbnail ? { toThumbnail } : {}),
        })) as Json | null;
      } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        if (/unknown method/i.test(message)) {
          return { diff: assetDiffError("The editor's bridge plugin predates asset diffs (checkpoint_diff_asset); update the plugin."), unknownMethod: true };
        }
        return { diff: assetDiffError(`The editor did not answer the diff: ${message}`) };
      }
      if (!raw || raw.success === false) return { diff: assetDiffError(String(raw?.error ?? "The editor returned no diff and no reason.")) };
      const picture = (side: "before" | "after", t: Json | undefined): AssetThumbnail | undefined => {
        const version = side === "before" ? a : b;
        if (!version) return undefined;
        const meta = path.join(repo.thumbsDir, `${version}.json`);
        if (t && typeof t.file === "string") {
          const info: { source: "rendered" | "saved"; note?: string } = { source: t.source === "saved" ? "saved" : "rendered" };
          if (typeof t.note === "string" && t.note) info.note = t.note;
          fs.writeFileSync(meta, JSON.stringify(info));
          return { id: version, url: "", ...info };
        }
        if (t && typeof t.error === "string") return { error: t.error };
        try {
          const info = JSON.parse(fs.readFileSync(meta, "utf-8")) as { source: "rendered" | "saved"; note?: string };
          if (fs.existsSync(thumb(version))) return { id: version, url: "", ...info };
        } catch {
          // No picture kept for this version.
        }
        return { error: "No picture was kept for this version." };
      };
      const diff = assetDiffFromBridge(raw, c.path, picture);
      fs.mkdirSync(repo.diffsDir, { recursive: true });
      fs.writeFileSync(cacheFile, JSON.stringify(diff));
      return { diff: this.withUrls(diff) };
    } catch (e) {
      return { diff: assetDiffError(`Preparing the diff failed: ${e instanceof Error ? e.message : String(e)}`) };
    } finally {
      if (fromFile) fs.rmSync(fromFile, { force: true });
      if (toFile) fs.rmSync(toFile, { force: true });
    }
  }

  /** Picture URLs on the daemon, for this session. */
  private withUrls(d: AssetDiff): AssetDiff {
    for (const v of [d.before, d.after]) {
      if (v?.thumbnail && "id" in v.thumbnail) {
        v.thumbnail.url = `/v1/checkpoints/thumbnails/${v.thumbnail.id}.png?editor=${encodeURIComponent(this.session.name)}`;
      }
    }
    return d;
  }

  /** blueprint(diff) against the checkpoint, for modified Blueprint packages, run in the editor. */
  private async addBlueprintDiffs(result: { from: string; to: string | null; changes: ChangeEntry[] }): Promise<void> {
    const repo = this.repo();
    const candidates = result.changes
      .filter((c) => c.status === "modified" && !c.large && c.path.toLowerCase().endsWith(".uasset"))
      .slice(0, MAX_BLUEPRINT_DIFFS);
    // Saved/Diff maps to /Temp, which is where the editor loads a package for diffing.
    const tempDir = path.join(this.projectDir, "Saved", "Diff", "UE_MCP_Checkpoints");
    for (const c of candidates) {
      const base = path.basename(c.path, path.extname(c.path));
      const fromFile = path.join(tempDir, `${base}-${result.from.slice(0, 10)}.uasset`);
      const toFile = result.to ? path.join(tempDir, `${base}-${result.to.slice(0, 10)}.uasset`) : null;
      try {
        if (!(await repo.exportAt(result.from, c.path, fromFile))) continue;
        if (toFile && !(await repo.exportAt(result.to!, c.path, toFile))) continue;
        const answer = await this.callBridge("checkpoint_diff_blueprint", {
          originalFile: path.join(this.projectDir, c.path),
          fromFile,
          ...(toFile ? { toFile } : {}),
        });
        if (answer && answer.isBlueprint === true) {
          const { success: _s, isBlueprint: _b, ...diff } = answer;
          c.blueprintDiff = diff;
        }
      } catch (e) {
        warn("checkpoints", `blueprint diff of ${c.path} failed`, e);
      } finally {
        fs.rmSync(fromFile, { force: true });
        if (toFile) fs.rmSync(toFile, { force: true });
      }
    }
  }

  restore(checkpoint: string, opts: { paths?: string[]; force?: boolean } = {}): Promise<RestoreResult> {
    this.requireEnabled();
    if (!checkpoint) throw new CheckpointError(400, "checkpoint is required: the id to restore to");
    const repo = this.repo();
    return repo.serialize(async () => {
      const connected = this.session.bridge.isConnected;
      // Refuse during PIE before anything is recorded or written.
      if (connected) await this.callBridge("checkpoint_prepare_restore", { files: [], checkOnly: true });

      const needle = checkpoint.trim().toLowerCase();
      const matches = (await repo.list(Number.MAX_SAFE_INTEGER)).filter((c) => c.id.startsWith(needle));
      if (matches.length !== 1) {
        throw new CheckpointError(400, matches.length === 0 ? `No checkpoint '${checkpoint}' in this project's history` : `'${checkpoint}' matches ${matches.length} checkpoints; give more of the id`);
      }
      const target = matches[0];
      const before = await this.createUnlocked(`before restore to ${target.label}`, "restore-before");
      const { plan, refused } = await repo.planRestore(target.id, before.id, opts.paths, opts.force === true);

      const prepared = connected && plan.length > 0
        ? await this.callBridge("checkpoint_prepare_restore", { files: plan.map((p) => p.file) })
        : null;
      const restored: RestoreResult["restored"] = [];
      for (const entry of plan) {
        try {
          restored.push({ path: entry.path, action: await repo.apply(entry) });
        } catch (e) {
          refused.push({ path: entry.path, reason: "write-failed", detail: e instanceof Error ? e.message : String(e) });
        }
      }
      const finished = connected && plan.length > 0
        ? await this.callBridge("checkpoint_finish_restore", {
          files: restored.map((r) => ({ file: path.join(repo.projectDir, r.path), exists: r.action === "written", kind: changeKind(r.path) })),
          reopen: Array.isArray(prepared?.closedEditors) ? prepared!.closedEditors : [],
        })
        : null;

      const after = await this.createUnlocked(`restored to ${target.label}`, "restore-after", restored.map((r) => r.path));
      const strings = (v: unknown): string[] => (Array.isArray(v) ? v.map(String) : []);
      const result: RestoreResult = {
        editor: this.session.name,
        checkpoint: target.id,
        before: before.id,
        after: after.id,
        restored,
        refused,
        reloaded: strings(finished?.reloaded),
        unloaded: strings(finished?.unloaded),
        configsReloaded: strings(finished?.configsReloaded),
        reopened: strings(finished?.reopened),
      };
      emit({
        type: "restored",
        data: {
          editor: this.session.name,
          checkpoint: result.checkpoint,
          before: result.before,
          after: result.after,
          restored: restored.map((r) => r.path),
          refused: refused.map((r) => ({ path: r.path, reason: r.reason })),
        },
      });
      info("checkpoints", `${this.session.name}: restored ${restored.length} path(s) to ${result.checkpoint.slice(0, 10)}, refused ${refused.length}`);
      return result;
    });
  }

  /** Delete the history. Allowed while off, which is how history kept from before is removed. */
  async clear(): Promise<{ editor: string; cleared: true }> {
    const repo = this.repo();
    await repo.serialize(async () => {
      repo.clear();
      this.activeGeneration = null;
      if (this.enabled()) await this.arm().catch((e) => warn("checkpoints", "re-arming after clear failed", e));
    });
    emit({ type: "cleared", data: { editor: this.session.name } });
    return { editor: this.session.name, cleared: true };
  }
}

const bySession = new WeakMap<EditorSession, SessionCheckpoints>();

/** The session's checkpoints, created on first use; creating them creates nothing on disk. */
export function checkpointsFor(session: EditorSession): SessionCheckpoints {
  let c = bySession.get(session);
  if (!c) {
    c = new SessionCheckpoints(session);
    bySession.set(session, c);
  }
  return c;
}

/** Record a file the server is about to write, for the session a tool call runs in. */
export function recordBeforeWrite(session: EditorSession | undefined, file: string): void {
  if (session) checkpointsFor(session).recordBeforeWrite(file);
}
