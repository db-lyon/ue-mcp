/**
 * Checkpoints over the daemon's HTTP API (#1482), for a client that brackets
 * each chat turn and lets the user keep or revert its files one by one.
 *
 *   GET    /v1/checkpoints           ?editor&limit   state and history, newest first
 *   PUT    /v1/checkpoints/enabled   { editor?, enabled: true | false | null }
 *   POST   /v1/checkpoints           { editor?, label? }
 *   GET    /v1/checkpoints/changes   ?editor&from&to&assetDiffs&blueprintDiffs
 *   GET    /v1/checkpoints/asset-diff ?editor&from&to&path      one package's asset diff
 *   GET    /v1/checkpoints/thumbnails/<id>.png ?editor           a picture an asset diff names
 *   POST   /v1/checkpoints/native-diff { editor?, path, from, to? }   Unreal's own diff, in the editor
 *   POST   /v1/checkpoints/restore   { editor?, checkpoint, paths?, force? }
 *   DELETE /v1/checkpoints           ?editor
 *
 * `editor` names a session as /v1/health lists it; omitted is the active one.
 * The enabled override lives in the daemon for its lifetime and is never
 * written to a file.
 */
import type { SessionRegistry } from "../sessions/session.js";
import { CheckpointError, checkpointsFor, type SessionCheckpoints } from "../checkpoints/service.js";

export interface RouteResult {
  status: number;
  body: unknown;
  /** A file to send as the body instead, with its content type. */
  file?: { path: string; type: string };
}

type Json = Record<string, unknown>;

class RouteError extends Error {
  constructor(readonly status: number, message: string) {
    super(message);
  }
}

function resolveSession(sessions: SessionRegistry, editor: unknown): SessionCheckpoints {
  if (editor === undefined || editor === null || editor === "") return checkpointsFor(sessions.active);
  if (typeof editor !== "string") throw new RouteError(400, "editor is a session name");
  const session = sessions.find(editor);
  if (!session) throw new RouteError(404, `No editor session '${editor}'. /v1/health lists them.`);
  return checkpointsFor(session);
}

/** The checkpoint routes, or null for a path that is not one. */
export async function checkpointRoute(
  sessions: SessionRegistry,
  method: string,
  sub: string,
  query: URLSearchParams,
  body: unknown,
): Promise<RouteResult | null> {
  const b = (body && typeof body === "object" && !Array.isArray(body) ? body : {}) as Json;
  try {
    if (sub === "" && method === "GET") {
      const limit = Number(query.get("limit") ?? 50);
      return ok(await resolveSession(sessions, query.get("editor")).state(Number.isFinite(limit) && limit > 0 ? limit : 50));
    }
    if (sub === "/enabled" && method === "PUT") {
      if (!("enabled" in b) || (b.enabled !== null && typeof b.enabled !== "boolean")) {
        throw new RouteError(400, "enabled is true, false, or null to follow ue-mcp.yml again");
      }
      return ok(await resolveSession(sessions, b.editor).setEnabled(b.enabled as boolean | null));
    }
    if (sub === "" && method === "POST") {
      if (b.label !== undefined && typeof b.label !== "string") throw new RouteError(400, "label is a string");
      return ok(await resolveSession(sessions, b.editor).create(b.label as string | undefined));
    }
    if (sub === "/changes" && method === "GET") {
      const from = query.get("from");
      if (!from) throw new RouteError(400, "from is required: the checkpoint id to compare against");
      return ok(await resolveSession(sessions, query.get("editor")).changes(from, query.get("to"), {
        blueprintDiffs: query.get("blueprintDiffs") !== "false",
        assetDiffs: query.get("assetDiffs") !== "false",
      }));
    }
    if (sub === "/asset-diff" && method === "GET") {
      const from = query.get("from");
      const file = query.get("path");
      if (!from) throw new RouteError(400, "from is required: the checkpoint id to compare against");
      if (!file) throw new RouteError(400, "path is required: the project-relative path of the package");
      return ok(await resolveSession(sessions, query.get("editor")).assetDiff(from, query.get("to"), file));
    }
    const thumb = /^\/thumbnails\/([0-9a-f]{40})\.png$/.exec(sub);
    if (thumb && method === "GET") {
      const found = resolveSession(sessions, query.get("editor")).thumbnail(thumb[1]);
      if (!found) throw new RouteError(404, "No such picture");
      return { status: 200, body: null, file: { path: found.file, type: found.type } };
    }
    if (sub === "/native-diff" && method === "POST") {
      if (typeof b.path !== "string" || !b.path) throw new RouteError(400, "path is required: the project-relative path of the package");
      if (typeof b.from !== "string" || !b.from) throw new RouteError(400, "from is required: the checkpoint id to compare against");
      if (b.to !== undefined && b.to !== null && typeof b.to !== "string") throw new RouteError(400, "to is a checkpoint id");
      return ok(await resolveSession(sessions, b.editor).openNativeDiff(b.path, b.from, (b.to as string | null | undefined) ?? null));
    }
    if (sub === "/restore" && method === "POST") {
      if (typeof b.checkpoint !== "string" || !b.checkpoint) throw new RouteError(400, "checkpoint is required: the id to restore to");
      if (b.paths !== undefined && (!Array.isArray(b.paths) || b.paths.some((p) => typeof p !== "string"))) {
        throw new RouteError(400, "paths is a list of project-relative paths");
      }
      if (b.force !== undefined && typeof b.force !== "boolean") throw new RouteError(400, "force is a boolean");
      return ok(await resolveSession(sessions, b.editor).restore(b.checkpoint, {
        paths: b.paths as string[] | undefined,
        force: b.force === true,
      }));
    }
    if (sub === "" && method === "DELETE") {
      return ok(await resolveSession(sessions, query.get("editor")).clear());
    }
    return null;
  } catch (e) {
    if (e instanceof RouteError || e instanceof CheckpointError) return { status: e.status, body: { error: e.message } };
    throw e;
  }
}

const ok = (body: unknown): RouteResult => ({ status: 200, body });
