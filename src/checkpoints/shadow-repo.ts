/**
 * The shadow history behind checkpoints (#1482): a git repository at
 * <project>/Saved/UE_MCP_Bridge/shadow.git, driven by isomorphic-git, that
 * records only the paths something wrote through ue-mcp.
 *
 * Recording is lazy. Before the first write of a path after a checkpoint, the
 * writer copies the path's current bytes (or notes that it did not exist) into
 * a pending generation under shadow-pending/. The bridge does this from
 * UPackage::PreSavePackageWithContextEvent on the game thread, and the server
 * does it before its own file writes. A checkpoint folds the pending copies in.
 *
 * Every checkpoint that recorded a path for the first time is preceded by a
 * baseline commit holding that path's state at the previous checkpoint, so
 * "the path at checkpoint k" is the checkpoint's own tree when it tracked the
 * path, else the first later baseline that does. Each commit's message carries
 * the tracked paths that did not exist, which a tree cannot hold.
 *
 * Files above the size cap are stored as a plain copy in shadow-large/, named
 * by SHA-256, and the tree holds a pointer blob to it.
 *
 * Only gitdir is passed to isomorphic-git, never dir, so nothing here can read
 * or write the project's own .git.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import * as crypto from "node:crypto";
import git from "isomorphic-git";
import { unifiedDiff } from "./text-diff.js";

export const DEFAULT_LARGE_FILE_BYTES = 1024 * 1024 * 1024;
const BRANCH = "refs/heads/checkpoints";
const META_PREFIX = "ue-mcp-checkpoint: ";
const LARGE_MAGIC = "ue-mcp shadow large file v1\n";
const AUTHOR = { name: "ue-mcp", email: "checkpoints@ue-mcp.invalid", timezoneOffset: 0 };
const MAX_TEXT_DIFF_BYTES = 512 * 1024;
const FILE_MODE = "100644";

/** Top-level folders never recorded: build output, caches and the editor's own state. */
const EXCLUDED_ROOTS = ["binaries", "intermediate", "deriveddatacache", "saved", ".git", ".vs"];
const EXCLUDE_FILE = [
  "# Written by ue-mcp. The shadow history records only paths ue-mcp wrote, and never these.",
  "/Binaries/",
  "/Intermediate/",
  "/DerivedDataCache/",
  "/Saved/",
  "/Plugins/**/Binaries/",
  "/Plugins/**/Intermediate/",
  "",
].join("\n");

const TEXT_EXTENSIONS = new Set([
  ".ini", ".h", ".hpp", ".cpp", ".c", ".inl", ".cs", ".uproject", ".uplugin", ".json", ".yml", ".yaml",
  ".txt", ".md", ".py", ".usf", ".ush", ".xml", ".csv", ".js", ".ts", ".toml",
]);

export type CheckpointTrigger = "client" | "restore-before" | "restore-after";
export type ChangeStatus = "added" | "modified" | "deleted";
export type ChangeKind = "package" | "config" | "source" | "other";

export interface CheckpointInfo {
  id: string;
  label: string;
  createdAt: string;
  trigger: CheckpointTrigger;
  files: number;
}

export interface ChangeEntry {
  path: string;
  status: ChangeStatus;
  kind: ChangeKind;
  untracked?: true;
  large?: true;
  diff?: string;
  blueprintDiff?: unknown;
  /** A package's diff from the editor, built on Unreal's own diff code: see asset-diff.ts. */
  assetDiff?: import("./asset-diff.js").AssetDiff;
}

export interface RestoreRefusal {
  path: string;
  reason: "edited-outside" | "not-recorded" | "write-failed";
  detail?: string;
}

/** One file restore will put back, before it is written. */
export interface RestorePlanEntry {
  path: string;
  /** Absolute path in the project. */
  file: string;
  /** Bytes to write, or null to delete. */
  target: State;
}

interface Meta {
  v: 1;
  kind: "checkpoint" | "baseline";
  label: string;
  createdAt: string;
  trigger?: CheckpointTrigger;
  /** Tracked paths that did not exist at this commit. */
  absent: string[];
  /** Paths changed outside a recorded write since the previous checkpoint. */
  external?: string[];
  /** Paths this checkpoint changed against the previous one. */
  changed?: string[];
}

interface Commit {
  oid: string;
  tree: string;
  parent: string | null;
  meta: Meta;
}

/** A path's content: a blob oid, or null when the path did not exist. */
type State = string | null;

interface PendingRecord {
  path: string;
  existed: boolean;
  /** Absolute path of the pre-write copy, when the path existed. */
  copy: string | null;
}

interface StatEntry {
  size: number;
  mtimeMs: number;
  oid: string;
  /** When the file was hashed. An entry hashed within RACY_MS of its mtime is not trusted (git's racy-clean rule). */
  checkedAt: number;
}

const RACY_MS = 3000;

export interface ShadowRepoOptions {
  largeFileBytes?: number;
}

export interface CheckpointOutcome {
  info: CheckpointInfo;
  changes: ChangeEntry[];
}

/** Project-relative, forward slashes, no leading ./ */
export function relativeKey(projectDir: string, file: string): string | null {
  const abs = path.resolve(projectDir, file);
  const rel = path.relative(path.resolve(projectDir), abs);
  if (!rel || rel.startsWith("..") || path.isAbsolute(rel)) return null;
  return rel.split(path.sep).join("/");
}

/** Whether a project-relative path is one the shadow history never records. */
export function isExcludedPath(key: string): boolean {
  const parts = key.toLowerCase().split("/");
  if (EXCLUDED_ROOTS.includes(parts[0])) return true;
  // Plugin build output: Plugins/<...>/Binaries|Intermediate.
  if (parts[0] === "plugins" && parts.some((p, i) => i > 0 && (p === "binaries" || p === "intermediate"))) return true;
  return false;
}

export function changeKind(key: string): ChangeKind {
  const lower = key.toLowerCase();
  if (lower.endsWith(".uasset") || lower.endsWith(".umap")) return "package";
  if (lower.endsWith(".ini")) return "config";
  if (/\.(h|hpp|cpp|c|inl|cs)$/.test(lower) || lower.startsWith("source/") || lower.endsWith(".uproject") || lower.endsWith(".uplugin")) return "source";
  return "other";
}

const fold = (key: string): string => (process.platform === "win32" ? key.toLowerCase() : key);

export class ShadowRepo {
  readonly root: string;
  readonly gitdir: string;
  readonly largeDir: string;
  /** Pictures of package versions, one PNG per blob id, rendered by the editor. */
  readonly thumbsDir: string;
  /** Asset diffs already computed, per path and pair of versions. */
  readonly diffsDir: string;
  readonly pendingRoot: string;
  readonly largeFileBytes: number;
  private readonly commits = new Map<string, Commit>();
  private readonly trees = new Map<string, Map<string, string>>();
  private readonly tracked = new Map<string, Map<string, { key: string; state: State }>>();
  private tsGeneration: string | null = null;
  private generationSeq = 0;
  private queue: Promise<unknown> = Promise.resolve();

  constructor(readonly projectDir: string, opts: ShadowRepoOptions = {}) {
    this.root = path.join(projectDir, "Saved", "UE_MCP_Bridge");
    this.gitdir = path.join(this.root, "shadow.git");
    this.largeDir = path.join(this.root, "shadow-large");
    this.pendingRoot = path.join(this.root, "shadow-pending");
    this.thumbsDir = path.join(this.root, "shadow-thumbs");
    this.diffsDir = path.join(this.root, "shadow-diffs");
    this.largeFileBytes = opts.largeFileBytes && opts.largeFileBytes > 0 ? opts.largeFileBytes : DEFAULT_LARGE_FILE_BYTES;
  }

  /** Run one operation at a time: a checkpoint and a restore must never interleave. */
  serialize<T>(fn: () => Promise<T>): Promise<T> {
    const run = this.queue.then(fn, fn);
    this.queue = run.catch(() => {});
    return run;
  }

  exists(): boolean {
    return fs.existsSync(path.join(this.gitdir, "HEAD"));
  }

  /** Create the repository if it is not there yet. */
  async ensure(): Promise<void> {
    if (this.exists()) return;
    fs.mkdirSync(this.root, { recursive: true });
    await git.init({ fs, gitdir: this.gitdir, bare: true, defaultBranch: "checkpoints" });
    // The project root is the work tree, so `git --git-dir=<shadow.git> status` reads as expected.
    await git.setConfig({ fs, gitdir: this.gitdir, path: "core.bare", value: false });
    await git.setConfig({ fs, gitdir: this.gitdir, path: "core.worktree", value: this.projectDir.split(path.sep).join("/") });
    await git.setConfig({ fs, gitdir: this.gitdir, path: "core.autocrlf", value: "false" });
    fs.mkdirSync(path.join(this.gitdir, "info"), { recursive: true });
    fs.writeFileSync(path.join(this.gitdir, "info", "exclude"), EXCLUDE_FILE);
  }

  /** Delete the history, the plain copies and everything pending. */
  clear(): void {
    for (const dir of [this.gitdir, this.largeDir, this.pendingRoot, this.thumbsDir, this.diffsDir]) fs.rmSync(dir, { recursive: true, force: true });
    this.commits.clear();
    this.trees.clear();
    this.tracked.clear();
    this.tsGeneration = null;
  }

  // ── Pending generations ─────────────────────────────────────────

  /** A fresh, empty pending generation directory, for the bridge or the server to record into. */
  newGeneration(owner: "bridge" | "server"): string {
    const name = `${String(Date.now()).padStart(15, "0")}-${String(++this.generationSeq).padStart(6, "0")}-${owner}`;
    const dir = path.join(this.pendingRoot, name);
    fs.mkdirSync(dir, { recursive: true });
    return dir;
  }

  /**
   * Record a path's current bytes before the server writes it. Synchronous, so
   * the copy is on disk before the caller's write starts.
   */
  recordBeforeWrite(file: string): void {
    const key = relativeKey(this.projectDir, file);
    if (!key || isExcludedPath(key)) return;
    if (!this.tsGeneration || !fs.existsSync(this.tsGeneration)) this.tsGeneration = this.newGeneration("server");
    appendPendingRecord(this.tsGeneration, key, path.resolve(this.projectDir, file));
  }

  private pendingGenerations(): string[] {
    if (!fs.existsSync(this.pendingRoot)) return [];
    return fs.readdirSync(this.pendingRoot, { withFileTypes: true })
      .filter((e) => e.isDirectory())
      .map((e) => path.join(this.pendingRoot, e.name))
      .sort();
  }

  /** Every pending record, oldest first, from every generation except `skip`. */
  private readPending(skip: Set<string>): { records: PendingRecord[]; dirs: string[] } {
    const records: PendingRecord[] = [];
    const dirs: string[] = [];
    for (const dir of this.pendingGenerations()) {
      if (skip.has(path.resolve(dir))) continue;
      dirs.push(dir);
      const manifest = path.join(dir, "manifest.jsonl");
      if (!fs.existsSync(manifest)) continue;
      for (const line of fs.readFileSync(manifest, "utf-8").split(/\r?\n/)) {
        if (!line.trim()) continue;
        let r: { path?: unknown; existed?: unknown; copy?: unknown };
        try {
          r = JSON.parse(line);
        } catch {
          continue; // A line cut short by a crash.
        }
        if (typeof r.path !== "string") continue;
        const key = relativeKey(this.projectDir, r.path);
        if (!key || isExcludedPath(key)) continue;
        const copy = r.existed === true && typeof r.copy === "string" ? path.resolve(dir, r.copy) : null;
        records.push({ path: key, existed: r.existed === true && copy !== null && fs.existsSync(copy), copy });
      }
    }
    return { records, dirs };
  }

  // ── Objects ─────────────────────────────────────────────────────

  /** Store a file's bytes and return the blob oid: the content, or a pointer to a plain copy. */
  private async storeFile(file: string, moveFrom?: string): Promise<string> {
    const size = fs.statSync(file).size;
    if (size <= this.largeFileBytes) {
      return git.writeBlob({ fs, gitdir: this.gitdir, blob: fs.readFileSync(file) });
    }
    const sha = await sha256File(file);
    fs.mkdirSync(this.largeDir, { recursive: true });
    const stored = path.join(this.largeDir, sha);
    if (!fs.existsSync(stored)) {
      const tmp = `${stored}.tmp-${process.pid}`;
      if (moveFrom) {
        try {
          fs.renameSync(moveFrom, tmp);
        } catch {
          fs.copyFileSync(file, tmp);
        }
      } else {
        fs.copyFileSync(file, tmp);
      }
      fs.renameSync(tmp, stored);
    }
    const pointer = `${LARGE_MAGIC}sha256 ${sha}\nsize ${size}\n`;
    return git.writeBlob({ fs, gitdir: this.gitdir, blob: Buffer.from(pointer, "utf-8") });
  }

  /** A blob's content, or the plain copy it points to (as a path, for large files). */
  private async readState(oid: string): Promise<{ bytes: Buffer } | { largeFile: string; size: number }> {
    const { blob } = await git.readBlob({ fs, gitdir: this.gitdir, oid });
    const buf = Buffer.from(blob);
    if (buf.length < 256 && buf.subarray(0, LARGE_MAGIC.length).toString("utf-8") === LARGE_MAGIC) {
      const m = /sha256 ([0-9a-f]{64})\nsize (\d+)\n/.exec(buf.toString("utf-8"));
      if (m) {
        const stored = path.join(this.largeDir, m[1]);
        if (!fs.existsSync(stored)) throw new Error(`The plain copy ${stored} this checkpoint points to is missing`);
        return { largeFile: stored, size: Number(m[2]) };
      }
    }
    return { bytes: buf };
  }

  /** The current disk state of a path, with a stat cache so an unchanged file is not reread. */
  private async diskState(key: string, cache: Record<string, StatEntry>): Promise<State> {
    const file = path.join(this.projectDir, key);
    let st: fs.Stats;
    try {
      st = fs.statSync(file);
    } catch {
      return null;
    }
    if (!st.isFile()) return null;
    const hit = cache[fold(key)];
    if (hit && hit.size === st.size && hit.mtimeMs === st.mtimeMs && hit.checkedAt - hit.mtimeMs > RACY_MS) return hit.oid;
    const oid = await this.storeFile(file);
    cache[fold(key)] = { size: st.size, mtimeMs: st.mtimeMs, oid, checkedAt: Date.now() };
    return oid;
  }

  private statCachePath(): string {
    return path.join(this.gitdir, "ue-mcp-stat.json");
  }

  private readStatCache(): Record<string, StatEntry> {
    try {
      return JSON.parse(fs.readFileSync(this.statCachePath(), "utf-8")) as Record<string, StatEntry>;
    } catch {
      return {};
    }
  }

  private writeStatCache(cache: Record<string, StatEntry>): void {
    fs.writeFileSync(this.statCachePath(), JSON.stringify(cache));
  }

  // ── Commits and trees ───────────────────────────────────────────

  private async head(): Promise<string | null> {
    try {
      return await git.resolveRef({ fs, gitdir: this.gitdir, ref: BRANCH });
    } catch {
      return null;
    }
  }

  private async readCommit(oid: string): Promise<Commit> {
    const cached = this.commits.get(oid);
    if (cached) return cached;
    const { commit } = await git.readCommit({ fs, gitdir: this.gitdir, oid });
    const line = commit.message.split("\n").find((l) => l.startsWith(META_PREFIX));
    if (!line) throw new Error(`Commit ${oid} in the shadow history carries no checkpoint record`);
    const meta = JSON.parse(line.slice(META_PREFIX.length)) as Meta;
    const parsed: Commit = { oid, tree: commit.tree, parent: commit.parent[0] ?? null, meta };
    this.commits.set(oid, parsed);
    return parsed;
  }

  /** Every commit, oldest first. */
  private async chain(): Promise<Commit[]> {
    const out: Commit[] = [];
    let at = await this.head();
    while (at) {
      const c = await this.readCommit(at);
      out.push(c);
      at = c.parent;
    }
    return out.reverse();
  }

  /** A tree as path -> blob oid, folded keys mapped to the real-case path held alongside. */
  private async flatTree(oid: string): Promise<Map<string, string>> {
    const cached = this.trees.get(oid);
    if (cached) return cached;
    const out = new Map<string, string>();
    const walk = async (treeOid: string, prefix: string): Promise<void> => {
      const { tree } = await git.readTree({ fs, gitdir: this.gitdir, oid: treeOid });
      for (const e of tree) {
        const p = prefix ? `${prefix}/${e.path}` : e.path;
        if (e.type === "tree") await walk(e.oid, p);
        else if (e.type === "blob") out.set(p, e.oid);
      }
    };
    await walk(oid, "");
    this.trees.set(oid, out);
    return out;
  }

  private async writeFlatTree(files: Map<string, string>): Promise<string> {
    interface Dir { files: Map<string, string>; dirs: Map<string, Dir> }
    const root: Dir = { files: new Map(), dirs: new Map() };
    for (const [p, oid] of files) {
      const parts = p.split("/");
      let d = root;
      for (const part of parts.slice(0, -1)) {
        let next = d.dirs.get(part);
        if (!next) {
          next = { files: new Map(), dirs: new Map() };
          d.dirs.set(part, next);
        }
        d = next;
      }
      d.files.set(parts[parts.length - 1], oid);
    }
    const write = async (d: Dir): Promise<string> => {
      const entries: Array<{ mode: string; path: string; oid: string; type: "blob" | "tree" }> = [];
      for (const [name, sub] of d.dirs) entries.push({ mode: "040000", path: name, oid: await write(sub), type: "tree" });
      for (const [name, oid] of d.files) entries.push({ mode: FILE_MODE, path: name, oid, type: "blob" });
      return git.writeTree({ fs, gitdir: this.gitdir, tree: entries });
    };
    return write(root);
  }

  private async commit(tree: string, parent: string | null, meta: Meta): Promise<string> {
    const timestamp = Math.floor(Date.parse(meta.createdAt) / 1000);
    const message = `${meta.label || meta.kind}\n\n${META_PREFIX}${JSON.stringify(meta)}\n`;
    const oid = await git.writeCommit({
      fs,
      gitdir: this.gitdir,
      commit: {
        message,
        tree,
        parent: parent ? [parent] : [],
        author: { ...AUTHOR, timestamp },
        committer: { ...AUTHOR, timestamp },
      },
    });
    await git.writeRef({ fs, gitdir: this.gitdir, ref: BRANCH, value: oid, force: true });
    return oid;
  }

  /** Tracked state of every path at one commit: tree entries plus recorded absences. */
  private async trackedAt(c: Commit): Promise<Map<string, { key: string; state: State }>> {
    const cached = this.tracked.get(c.oid);
    if (cached) return cached;
    const out = new Map<string, { key: string; state: State }>();
    for (const [key, oid] of await this.flatTree(c.tree)) out.set(fold(key), { key, state: oid });
    for (const key of c.meta.absent) out.set(fold(key), { key, state: null });
    this.tracked.set(c.oid, out);
    return out;
  }

  // ── Checkpoint ──────────────────────────────────────────────────

  /**
   * Fold every pending generation except `activeGeneration` into the history
   * and commit the current state of every tracked path.
   *
   * `expected` names paths the caller itself just wrote (a restore), which are
   * not reported as written outside the bridge.
   */
  async checkpoint(opts: {
    label: string;
    trigger: CheckpointTrigger;
    activeGeneration?: string | null;
    expected?: Iterable<string>;
  }): Promise<CheckpointOutcome> {
    await this.ensure();
    const createdAt = new Date().toISOString();
    // Seal the server's own generation: writes from here on start a new one.
    this.tsGeneration = null;
    const skip = new Set<string>();
    if (opts.activeGeneration) skip.add(path.resolve(opts.activeGeneration));
    const { records, dirs } = this.readPending(skip);
    const expected = new Set([...(opts.expected ?? [])].map(fold));

    const headOid = await this.head();
    const head = headOid ? await this.readCommit(headOid) : null;
    const tracked = new Map(head ? await this.trackedAt(head) : []);
    const external = new Set<string>();

    // Baseline: the state at the previous checkpoint of every path recorded for the first time.
    const baseline = new Map<string, { key: string; state: State }>();
    const recorded = new Set<string>();
    for (const r of records) {
      const f = fold(r.path);
      if (recorded.has(f)) continue;
      recorded.add(f);
      const before = r.existed && r.copy ? await this.storeFile(r.copy, r.copy) : null;
      const known = tracked.get(f);
      if (known) {
        // Tracked already: a first write that found other bytes than the checkpoint holds was preceded by an outside edit.
        if (known.state !== before) external.add(known.key);
        continue;
      }
      baseline.set(f, { key: r.path, state: before });
    }

    let parent = headOid;
    if (baseline.size > 0) {
      const files = new Map<string, string>();
      const absent: string[] = [];
      for (const { key, state } of [...tracked.values(), ...baseline.values()]) {
        if (state === null) absent.push(key);
        else files.set(key, state);
      }
      const tree = await this.writeFlatTree(files);
      parent = await this.commit(tree, parent, { v: 1, kind: "baseline", label: "baseline", createdAt, absent: absent.sort() });
      for (const [f, entry] of baseline) tracked.set(f, entry);
    }

    // The checkpoint itself: the disk state of every tracked path.
    const cache = this.readStatCache();
    const files = new Map<string, string>();
    const absent: string[] = [];
    const changes: ChangeEntry[] = [];
    for (const [f, { key, state: previous }] of tracked) {
      const now = await this.diskState(key, cache);
      if (now === null) absent.push(key);
      else files.set(key, now);
      if (now === previous) continue;
      // Changed with no recorded write since the last checkpoint: written outside the bridge.
      if (!recorded.has(f) && !expected.has(f)) external.add(key);
      changes.push(await this.describe(key, previous, now, external.has(key), false));
    }
    const tree = await this.writeFlatTree(files);
    const label = opts.label || `checkpoint ${createdAt}`;
    const oid = await this.commit(tree, parent, {
      v: 1,
      kind: "checkpoint",
      label,
      createdAt,
      trigger: opts.trigger,
      absent: absent.sort(),
      external: [...external].sort(),
      changed: changes.map((c) => c.path).sort(),
    });
    this.writeStatCache(cache);
    for (const dir of dirs) fs.rmSync(dir, { recursive: true, force: true });
    changes.sort((a, b) => a.path.localeCompare(b.path));
    return { info: { id: oid, label, createdAt, trigger: opts.trigger, files: changes.length }, changes };
  }

  // ── Reading ─────────────────────────────────────────────────────

  async list(limit = 50): Promise<CheckpointInfo[]> {
    if (!this.exists()) return [];
    const chain = await this.chain();
    return chain
      .filter((c) => c.meta.kind === "checkpoint")
      .reverse()
      .slice(0, limit)
      .map((c) => this.info(c));
  }

  private info(c: Commit): CheckpointInfo {
    return {
      id: c.oid,
      label: c.meta.label,
      createdAt: c.meta.createdAt,
      trigger: c.meta.trigger ?? "client",
      files: c.meta.changed?.length ?? 0,
    };
  }

  /** Resolve a checkpoint id or unique prefix to its index in the chain. */
  private async resolve(chain: Commit[], id: string): Promise<number> {
    const needle = id.trim().toLowerCase();
    if (!/^[0-9a-f]{4,40}$/.test(needle)) throw new CheckpointError(400, `'${id}' is not a checkpoint id`);
    const hits = chain.map((c, i) => ({ c, i })).filter(({ c }) => c.meta.kind === "checkpoint" && c.oid.startsWith(needle));
    if (hits.length === 0) throw new CheckpointError(400, `No checkpoint '${id}' in this project's history`);
    if (hits.length > 1) throw new CheckpointError(400, `'${id}' matches ${hits.length} checkpoints; give more of the id`);
    return hits[0].i;
  }

  /** A path's state at chain[i], from its tree, its absences, or the first later baseline; undefined when never recorded. */
  private async stateAt(chain: Commit[], i: number, f: string, pending: Map<string, PendingRecord> | null): Promise<{ key: string; state: State } | undefined> {
    for (let j = i; j < chain.length; j++) {
      const hit = (await this.trackedAt(chain[j])).get(f);
      if (hit) return hit;
    }
    const r = pending?.get(f);
    if (r) return { key: r.path, state: r.existed && r.copy ? await this.storeFile(r.copy) : null };
    return undefined;
  }

  /**
   * Every file that differs between checkpoint `from` and checkpoint `to`, or
   * the project as it is now when `to` is omitted, with text diffs.
   */
  async changes(from: string, to?: string | null): Promise<{ from: string; to: string | null; changes: ChangeEntry[] }> {
    if (!this.exists()) throw new CheckpointError(400, "This project has no checkpoint history yet");
    const chain = await this.chain();
    const i = await this.resolve(chain, from);
    const k = to ? await this.resolve(chain, to) : null;
    const pending = k === null ? this.firstPending() : null;

    // Every path tracked anywhere from `from` on, plus what is pending now.
    const keys = new Map<string, string>();
    const last = k === null ? chain.length - 1 : Math.max(i, k);
    for (let j = Math.min(i, k ?? i); j <= last; j++) {
      for (const [f, { key }] of await this.trackedAt(chain[j])) keys.set(f, key);
    }
    if (pending) for (const [f, r] of pending) keys.set(f, r.path);

    const cache = this.readStatCache();
    const externalBetween = new Set<string>();
    const lo = Math.min(i, k ?? chain.length - 1);
    const hi = Math.max(i, k ?? chain.length - 1);
    for (let j = lo + 1; j <= hi; j++) for (const key of chain[j].meta.external ?? []) externalBetween.add(fold(key));
    const latest = chain.length - 1;

    const out: ChangeEntry[] = [];
    for (const [f, key] of keys) {
      const a = await this.stateAt(chain, i, f, pending);
      if (!a) continue;
      let b: State;
      let untracked = externalBetween.has(f);
      if (k === null) {
        b = await this.diskState(key, cache);
        const recordedNow = await this.stateAt(chain, latest, f, pending);
        if (!pending?.has(f) && recordedNow && recordedNow.state !== b) untracked = true;
      } else {
        const at = await this.stateAt(chain, k, f, null);
        if (!at) continue;
        b = at.state;
      }
      if (a.state === b) continue;
      out.push(await this.describe(a.key, a.state, b, untracked, true));
    }
    this.writeStatCache(cache);
    out.sort((x, y) => x.path.localeCompare(y.path));
    return { from: chain[i].oid, to: k === null ? null : chain[k].oid, changes: out };
  }

  /** The first pending record per path, across every generation. */
  private firstPending(): Map<string, PendingRecord> {
    const out = new Map<string, PendingRecord>();
    for (const r of this.readPending(new Set()).records) if (!out.has(fold(r.path))) out.set(fold(r.path), r);
    return out;
  }

  private async describe(key: string, before: State, after: State, untracked: boolean, withDiff: boolean): Promise<ChangeEntry> {
    const status: ChangeStatus = before === null ? "added" : after === null ? "deleted" : "modified";
    const entry: ChangeEntry = { path: key, status, kind: changeKind(key) };
    if (untracked) entry.untracked = true;
    const a = before ? await this.readState(before) : null;
    const b = after ? await this.readState(after) : null;
    if ((a && "largeFile" in a) || (b && "largeFile" in b)) entry.large = true;
    if (withDiff && !entry.large && TEXT_EXTENSIONS.has(path.extname(key).toLowerCase())) {
      const ab = a && "bytes" in a ? a.bytes : Buffer.alloc(0);
      const bb = b && "bytes" in b ? b.bytes : Buffer.alloc(0);
      if (ab.length <= MAX_TEXT_DIFF_BYTES && bb.length <= MAX_TEXT_DIFF_BYTES && !isBinary(ab) && !isBinary(bb)) {
        const diff = unifiedDiff(ab.toString("utf-8"), bb.toString("utf-8"), key);
        if (diff) entry.diff = diff;
      }
    }
    return entry;
  }

  /**
   * The version of a path at checkpoint `id`, or on disk now when `id` is null:
   * its blob id, null when it did not exist, undefined when nothing recorded it.
   * Reading the disk stores that version, so exportVersion can write it out.
   */
  async versionOf(id: string | null, key: string): Promise<string | null | undefined> {
    if (id === null) {
      const cache = this.readStatCache();
      const state = await this.diskState(key, cache);
      this.writeStatCache(cache);
      return state;
    }
    const chain = await this.chain();
    const i = await this.resolve(chain, id);
    const at = await this.stateAt(chain, i, fold(key), null);
    return at ? at.state : undefined;
  }

  /** The bytes of one version, from versionOf, written to `dest`. */
  async exportVersion(version: string, dest: string): Promise<void> {
    await this.writeState(version, dest);
  }

  /** The bytes of a path at a checkpoint, written to `dest`, for a diff the editor runs. False when it did not exist. */
  async exportAt(id: string, key: string, dest: string): Promise<boolean> {
    const chain = await this.chain();
    const i = await this.resolve(chain, id);
    const at = await this.stateAt(chain, i, fold(key), null);
    if (!at || at.state === null) return false;
    await this.writeState(at.state, dest);
    return true;
  }

  private async writeState(oid: string, dest: string): Promise<void> {
    const s = await this.readState(oid);
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    if ("largeFile" in s) fs.copyFileSync(s.largeFile, dest);
    else fs.writeFileSync(dest, s.bytes);
  }

  // ── Restore ─────────────────────────────────────────────────────

  /**
   * What restoring `paths` (default every path that differs) to checkpoint
   * `id` would write, measured against checkpoint `current`, which the caller
   * has just taken. A path edited outside the bridge since `id` is refused
   * unless `force`.
   */
  async planRestore(id: string, current: string, paths: string[] | undefined, force: boolean): Promise<{ plan: RestorePlanEntry[]; refused: RestoreRefusal[] }> {
    const chain = await this.chain();
    const i = await this.resolve(chain, id);
    const c = await this.resolve(chain, current);
    const external = new Set<string>();
    for (let j = i + 1; j <= c; j++) for (const key of chain[j].meta.external ?? []) external.add(fold(key));

    let wanted: string[];
    if (paths && paths.length > 0) {
      wanted = paths.map((p) => relativeKey(this.projectDir, p) ?? p.replace(/\\/g, "/"));
    } else {
      const keys = new Map<string, string>();
      for (let j = Math.min(i, c); j <= Math.max(i, c); j++) for (const [f, { key }] of await this.trackedAt(chain[j])) keys.set(f, key);
      wanted = [];
      for (const [f, key] of keys) {
        const a = await this.stateAt(chain, i, f, null);
        const b = await this.stateAt(chain, c, f, null);
        if (a && b && a.state !== b.state) wanted.push(key);
      }
    }

    const plan: RestorePlanEntry[] = [];
    const refused: RestoreRefusal[] = [];
    for (const key of wanted) {
      const f = fold(key);
      const target = await this.stateAt(chain, i, f, null);
      if (!target) {
        refused.push({ path: key, reason: "not-recorded", detail: "Nothing wrote this path through ue-mcp by that checkpoint, so its state there is unknown." });
        continue;
      }
      const now = await this.stateAt(chain, c, f, null);
      if (now && now.state === target.state) continue;
      if (external.has(f) && !force) {
        refused.push({ path: target.key, reason: "edited-outside", detail: "Changed outside the agent after that checkpoint. Pass force to overwrite it." });
        continue;
      }
      plan.push({ path: target.key, file: path.join(this.projectDir, target.key), target: target.state });
    }
    plan.sort((a, b) => a.path.localeCompare(b.path));
    return { plan, refused };
  }

  /** Write one planned entry to disk. */
  async apply(entry: RestorePlanEntry): Promise<"written" | "deleted"> {
    if (entry.target === null) {
      fs.rmSync(entry.file, { force: true });
      return "deleted";
    }
    await this.writeState(entry.target, entry.file);
    return "written";
  }
}

export class CheckpointError extends Error {
  constructor(readonly status: number, message: string) {
    super(message);
  }
}

/** Append one record to a pending generation: copy the file's bytes first, or note that it does not exist. */
export function appendPendingRecord(dir: string, key: string, file: string): void {
  fs.mkdirSync(dir, { recursive: true });
  const manifest = path.join(dir, "manifest.jsonl");
  let existed = false;
  let copy: string | null = null;
  try {
    existed = fs.statSync(file).isFile();
  } catch {
    existed = false;
  }
  if (existed) {
    const n = fs.existsSync(manifest) ? fs.readFileSync(manifest, "utf-8").split("\n").filter(Boolean).length : 0;
    copy = `${n}.bin`;
    fs.copyFileSync(file, path.join(dir, copy));
  }
  fs.appendFileSync(manifest, `${JSON.stringify({ path: key, existed, ...(copy ? { copy } : {}) })}\n`);
}

function isBinary(buf: Buffer): boolean {
  return buf.subarray(0, 8000).includes(0);
}

function sha256File(file: string): Promise<string> {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash("sha256");
    fs.createReadStream(file)
      .on("data", (d) => hash.update(d))
      .on("error", reject)
      .on("end", () => resolve(hash.digest("hex")));
  });
}
