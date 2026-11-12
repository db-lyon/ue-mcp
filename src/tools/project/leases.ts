/**
 * Asset leases from the calling session's side: claim, release, list.
 * The registry and the rules are in dispatch/leases.ts.
 */
import { z } from "zod";
import type { ActionSpec, ToolContext } from "../../core/types.js";
import { McpError, ErrorCode } from "../../core/errors.js";
import { SESSION_ID } from "../../dispatch/lock-owner.js";
import { MAX_LEASE_TTL_SECONDS, type Lease, type LeaseHolder, type LeaseRegistry } from "../../dispatch/leases.js";

function registry(ctx: ToolContext): LeaseRegistry {
  const leases = ctx.sessions?.leases;
  if (!leases) throw new McpError(ErrorCode.NOT_FOUND, "This server has no lease registry: leases exist where calls are dispatched through the session registry.");
  return leases;
}

/** Who is calling. A context with no holder is this process, the single session a stdio server has. */
function holderOf(ctx: ToolContext): LeaseHolder {
  return ctx.leaseHolder ?? { id: SESSION_ID, label: ctx.client?.name ?? "this server" };
}

function editorOf(ctx: ToolContext): string {
  return ctx.session?.name ?? "default";
}

function readPaths(raw: unknown, required: boolean): string[] | undefined {
  const list = Array.isArray(raw) ? raw : typeof raw === "string" ? [raw] : [];
  const paths = list.filter((p): p is string => typeof p === "string").map((p) => p.trim()).filter(Boolean);
  if (paths.length === 0) {
    if (required) throw new McpError(ErrorCode.INVALID_PARAMS, "'paths' names at least one asset, e.g. ['/Game/Characters/BP_Hero'], or a folder with a trailing slash.");
    return undefined;
  }
  return paths;
}

function shown(l: Lease, now: number, me: string) {
  return {
    editor: l.editor,
    path: l.path,
    holder: l.holder,
    holderLabel: l.holderLabel,
    mine: l.holder === me,
    implicit: l.implicit,
    expiresAt: new Date(l.expiresAt).toISOString(),
    secondsLeft: Math.max(0, Math.ceil((l.expiresAt - now) / 1000)),
  };
}

export const leaseActions: Record<string, ActionSpec> = {
  acquire_lease: {
    kind: "handler",
    effect: "mutate",
    description:
      "Claim assets for this session so another agent on the same editor cannot change them until the lease is released, runs out, or this session ends. "
      + "A path another session holds refuses the whole request, naming the holder and when its lease runs out. Calling again renews. "
      + "A folder path with a trailing slash covers every asset under it. Off when ue-mcp.yml sets leases.mode: off. "
      + `Params: paths (asset paths), ttlSeconds? (default from leases.ttlSeconds, 600; max ${MAX_LEASE_TTL_SECONDS})`,
    handler: async (ctx, p) => {
      const leases = registry(ctx);
      if (!leases.enforcing) {
        throw new McpError(ErrorCode.INVALID_PARAMS, "Leases are off for this server (ue-mcp.yml leases.mode: off), so a lease would not keep anyone out.");
      }
      const paths = readPaths(p.paths, true)!;
      const ttl = typeof p.ttlSeconds === "number" && p.ttlSeconds > 0 ? p.ttlSeconds : undefined;
      const holder = holderOf(ctx);
      const taken = leases.acquire(editorOf(ctx), paths, holder, ttl, false);
      const now = leases.now();
      return {
        acquired: taken.map((l) => shown(l, now, holder.id)),
        holder,
        undo: `project(action='release_lease', paths=${JSON.stringify(taken.map((l) => l.path))})`,
      };
    },
  },
  release_lease: {
    kind: "handler",
    effect: "mutate",
    description:
      "Release this session's leases: the paths named, or every lease it holds in this editor when none are. "
      + "Only the holder can release a lease; another session's runs out on its own or when that session ends. Params: paths?",
    handler: async (ctx, p) => {
      const leases = registry(ctx);
      const holder = holderOf(ctx);
      const released = leases.release(holder.id, editorOf(ctx), readPaths(p.paths, false));
      const now = leases.now();
      return { releasedCount: released.length, released: released.map((l) => shown(l, now, holder.id)) };
    },
  },
  list_leases: {
    kind: "handler",
    effect: "read",
    description:
      "Every live asset lease on this server, which session holds each and when it runs out, plus the lease mode and whether calls take leases implicitly right now. Params: none",
    handler: async (ctx) => {
      const leases = registry(ctx);
      const holder = holderOf(ctx);
      const now = leases.now();
      const list = leases.list();
      return {
        mode: leases.config.mode,
        ttlSeconds: leases.config.ttlSeconds,
        implicit: leases.enforcing && leases.implicit,
        sessions: leases.liveSessions(),
        you: holder,
        count: list.length,
        leases: list.map((l) => shown(l, now, holder.id)),
      };
    },
  },
};

export const leaseSchema: Record<string, z.ZodType> = {
  paths: z.union([z.string(), z.array(z.string())]).optional().describe("acquire_lease / release_lease: asset paths, or a folder with a trailing slash"),
  ttlSeconds: z.number().optional().describe("acquire_lease: how long the lease lasts, in seconds (default 600)"),
};
