/**
 * Asset leases: several agents sharing one editor without editing the same
 * asset at once.
 *
 * A lease is an exclusive claim on one asset path (or a folder, written with a
 * trailing slash) in one editor, held by one MCP session until it is released,
 * runs out, or the session ends. Every server process holds its own registry,
 * so the sharing point is the daemon: each client connected to it is a
 * session, and they all dispatch through the one registry here.
 *
 * Dispatch reads it before any action whose declared effect is `mutate` or
 * `unknown` and whose parameters name asset paths. A path another session
 * holds refuses the call, naming the holder and when the lease runs out.
 *
 * Modes (`ue-mcp.leases.mode` in ue-mcp.yml):
 *   auto  (default) leases taken with project(acquire_lease) are honoured, and
 *         while more than one session is connected a mutating call also takes
 *         a lease on the paths it names, for its own session, so a second
 *         agent is kept off an asset the first one is working on. With one
 *         session nothing is taken implicitly: there is nobody to coordinate
 *         with, and a solo agent pays nothing.
 *   on    as auto, with implicit leases even for a single session.
 *   off   nothing is checked; acquire_lease is refused.
 *
 * Separate from asset locking (locking.ts), which wraps each mutating call in
 * a lock held by the editor for that one call. A lease spans calls: an agent
 * claims a blueprint, makes twenty edits to it, and releases it.
 */
import { McpError, ErrorCode } from "../core/errors.js";

export type LeaseMode = "auto" | "on" | "off";
export const LEASE_MODES: readonly LeaseMode[] = ["auto", "on", "off"];

export interface LeaseConfig {
  mode: LeaseMode;
  /** How long a lease lasts when the call does not say, in seconds. */
  ttlSeconds: number;
}

export const DEFAULT_LEASE_TTL_SECONDS = 600;
/** The longest a lease may be taken for. A crashed holder frees its paths within this. */
export const MAX_LEASE_TTL_SECONDS = 24 * 60 * 60;

export function resolveLeaseConfig(cfg?: { mode?: string; ttlSeconds?: number }): LeaseConfig {
  const mode = LEASE_MODES.includes(cfg?.mode as LeaseMode) ? (cfg!.mode as LeaseMode) : "auto";
  const ttl = typeof cfg?.ttlSeconds === "number" && cfg.ttlSeconds > 0
    ? Math.min(cfg.ttlSeconds, MAX_LEASE_TTL_SECONDS)
    : DEFAULT_LEASE_TTL_SECONDS;
  return { mode, ttlSeconds: ttl };
}

/** Who holds a lease: the MCP session's id, and a name a person can read. */
export interface LeaseHolder {
  id: string;
  label: string;
}

export interface Lease {
  /** The editor session the asset lives in. */
  editor: string;
  /** The path as the holder wrote it, normalised to a package path. */
  path: string;
  holder: string;
  holderLabel: string;
  acquiredAt: number;
  expiresAt: number;
  /** Taken by a mutating call rather than by acquire_lease. */
  implicit: boolean;
}

/**
 * A content path reduced to its package: `/Game/A/B.B` and `/Game/A/B.B:Sub`
 * are `/Game/A/B`. A trailing slash is kept, since it is what makes a folder
 * lease. Null for anything that is not a mounted content path.
 */
export function normalizeLeasePath(raw: unknown): string | null {
  if (typeof raw !== "string") return null;
  let p = raw.trim().replace(/\\/g, "/");
  if (!/^\/[A-Za-z0-9_]/.test(p) || p.startsWith("//")) return null;
  if (p.endsWith("/*")) p = p.slice(0, -1);
  const folder = p.endsWith("/");
  if (!folder) {
    const colon = p.indexOf(":");
    if (colon >= 0) p = p.slice(0, colon);
    const slash = p.lastIndexOf("/");
    const dot = p.indexOf(".", slash);
    if (dot > slash) p = p.slice(0, dot);
  }
  return p.length > 1 ? p : null;
}

/** Whether two normalised paths name overlapping content: equal, or one a folder holding the other. */
function overlaps(a: string, b: string): boolean {
  const x = a.toLowerCase();
  const y = b.toLowerCase();
  if (x === y) return true;
  if (x.endsWith("/") && (y.startsWith(x) || `${y}/` === x)) return true;
  if (y.endsWith("/") && (x.startsWith(y) || `${x}/` === y)) return true;
  return false;
}

/** The refusal for a call that would touch a path another session holds. */
export function leaseConflictMessage(lease: Lease, path: string, now: number): string {
  const left = Math.max(0, Math.ceil((lease.expiresAt - now) / 1000));
  const what = lease.path.toLowerCase() === path.toLowerCase() ? `'${path}'` : `'${path}' (under the lease on '${lease.path}')`;
  return `Asset ${what} is leased by session '${lease.holderLabel}' (${lease.holder}) `
    + `until ${new Date(lease.expiresAt).toISOString()}, about ${left}s from now`
    + `${lease.implicit ? ", taken when that session last changed it" : ""}. `
    + "Wait for it to run out, ask that session to release it with project(action='release_lease'), or work on another asset. "
    + "project(action='list_leases') shows every lease.";
}

export class LeaseRegistry {
  private readonly leases = new Map<string, Lease>();
  config: LeaseConfig = resolveLeaseConfig();
  /** How many MCP sessions share this registry. The daemon sets it; a stdio server has one. */
  liveSessions: () => number = () => 1;
  /** The clock, replaceable in tests. */
  now: () => number = () => Date.now();

  private key(editor: string, path: string): string {
    return `${editor}\u0000${path.toLowerCase()}`;
  }

  private prune(): void {
    const t = this.now();
    for (const [k, l] of this.leases) if (l.expiresAt <= t) this.leases.delete(k);
  }

  /** Whether dispatch checks leases at all. */
  get enforcing(): boolean {
    return this.config.mode !== "off";
  }

  /** Whether a mutating call takes a lease on what it names. */
  get implicit(): boolean {
    return this.config.mode === "on" || (this.config.mode === "auto" && this.liveSessions() > 1);
  }

  /** The first live lease another holder has over any of these paths, with the path it covers. */
  conflict(editor: string, paths: readonly string[], holder: string): { lease: Lease; path: string } | null {
    this.prune();
    for (const path of paths) {
      for (const lease of this.leases.values()) {
        if (lease.editor !== editor || lease.holder === holder) continue;
        if (overlaps(lease.path, path)) return { lease, path };
      }
    }
    return null;
  }

  /**
   * Take or renew leases on every path, all or none: a path another session
   * holds refuses the whole request with ASSET_LEASED. A path this holder
   * already has is renewed; an explicit request turns an implicit lease into
   * an explicit one.
   */
  acquire(editor: string, rawPaths: readonly string[], holder: LeaseHolder, ttlSeconds?: number, implicit = false): Lease[] {
    const paths = rawPaths.map((p) => {
      const n = normalizeLeasePath(p);
      if (!n) throw new McpError(ErrorCode.INVALID_PARAMS, `'${p}' is not a content path. A lease names an asset such as /Game/Characters/BP_Hero, or a folder with a trailing slash.`);
      return n;
    });
    const hit = this.conflict(editor, paths, holder.id);
    if (hit) throw new McpError(ErrorCode.ASSET_LEASED, leaseConflictMessage(hit.lease, hit.path, this.now()));
    const ttl = Math.min(Math.max(1, ttlSeconds ?? this.config.ttlSeconds), MAX_LEASE_TTL_SECONDS);
    const t = this.now();
    const out: Lease[] = [];
    for (const path of [...new Set(paths)]) {
      const k = this.key(editor, path);
      const existing = this.leases.get(k);
      const lease: Lease = {
        editor,
        path,
        holder: holder.id,
        holderLabel: holder.label,
        acquiredAt: existing?.holder === holder.id ? existing.acquiredAt : t,
        // An implicit renewal never shortens a lease the holder asked for.
        expiresAt: Math.max(t + ttl * 1000, implicit && existing?.holder === holder.id ? existing.expiresAt : 0),
        implicit: implicit && (existing?.holder !== holder.id || existing.implicit),
      };
      this.leases.set(k, lease);
      out.push(lease);
    }
    return out;
  }

  /** Release this holder's leases on these paths, or every one it holds in the editor when none are named. */
  release(holder: string, editor?: string, rawPaths?: readonly string[]): Lease[] {
    this.prune();
    const wanted = rawPaths?.map((p) => normalizeLeasePath(p)?.toLowerCase()).filter((p): p is string => !!p);
    const out: Lease[] = [];
    for (const [k, l] of this.leases) {
      if (l.holder !== holder) continue;
      if (editor !== undefined && l.editor !== editor) continue;
      if (wanted && !wanted.includes(l.path.toLowerCase())) continue;
      this.leases.delete(k);
      out.push(l);
    }
    return out;
  }

  /** Drop every lease a session holds, when it disconnects. */
  releaseHolder(holder: string): number {
    return this.release(holder).length;
  }

  /** Live leases, optionally in one editor. */
  list(editor?: string): Lease[] {
    this.prune();
    return [...this.leases.values()]
      .filter((l) => editor === undefined || l.editor === editor)
      .sort((a, b) => a.path.localeCompare(b.path));
  }
}
