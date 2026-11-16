// @session per-session: Resolves the editor session each request names before acting on its checkpoints.
/**
 * Checkpoints over the daemon's HTTP API (#1482), for a client that brackets
 * each chat turn and lets the user keep or revert its files one by one. The
 * routes, with their queries and bodies, are the checkpoints.* entries of
 * DAEMON_ROUTES. The enabled override lives in the daemon for its lifetime and
 * is never written to a file.
 */
import type { SessionRegistry } from "../sessions/session.js";
import { checkpointsFor, type SessionCheckpoints } from "../checkpoints/service.js";
import { StatusError } from "../core/status-error.js";
import { objectBody, ok, resolveEditorSession, type RouteHandlers } from "./router.js";

type CheckpointRouteName =
  | "checkpoints.state"
  | "checkpoints.setEnabled"
  | "checkpoints.create"
  | "checkpoints.changes"
  | "checkpoints.assetDiff"
  | "checkpoints.thumbnail"
  | "checkpoints.nativeDiff"
  | "checkpoints.restore"
  | "checkpoints.clear";

const FROM_REQUIRED = "from is required: the checkpoint id to compare against";
const PATH_REQUIRED = "path is required: the project-relative path of the package";

export function checkpointRoutes(sessions: SessionRegistry): Pick<RouteHandlers, CheckpointRouteName> {
  const of = (editor: unknown): SessionCheckpoints => checkpointsFor(resolveEditorSession(sessions, editor));
  return {
    "checkpoints.state": async ({ query }) => {
      const limit = Number(query.get("limit") ?? 50);
      return ok(await of(query.get("editor")).state(Number.isFinite(limit) && limit > 0 ? limit : 50));
    },
    "checkpoints.setEnabled": async (r) => {
      const b = objectBody(await r.body());
      if (!("enabled" in b) || (b.enabled !== null && typeof b.enabled !== "boolean")) {
        throw new StatusError(400, "enabled is true, false, or null to follow ue-mcp.yml again");
      }
      return ok(await of(b.editor).setEnabled(b.enabled as boolean | null));
    },
    "checkpoints.create": async (r) => {
      const b = objectBody(await r.body());
      if (b.label !== undefined && typeof b.label !== "string") throw new StatusError(400, "label is a string");
      return ok(await of(b.editor).create(b.label as string | undefined));
    },
    "checkpoints.changes": async ({ query }) => {
      const from = query.get("from");
      if (!from) throw new StatusError(400, FROM_REQUIRED);
      return ok(await of(query.get("editor")).changes(from, query.get("to"), {
        blueprintDiffs: query.get("blueprintDiffs") !== "false",
        assetDiffs: query.get("assetDiffs") !== "false",
      }));
    },
    "checkpoints.assetDiff": async ({ query }) => {
      const from = query.get("from");
      const file = query.get("path");
      if (!from) throw new StatusError(400, FROM_REQUIRED);
      if (!file) throw new StatusError(400, PATH_REQUIRED);
      return ok(await of(query.get("editor")).assetDiff(from, query.get("to"), file));
    },
    "checkpoints.thumbnail": ({ params, query }) => {
      // A picture is named by the blob id of the version it shows, so it never changes.
      const id = /^([0-9a-f]{40})\.png$/.exec(params.file)?.[1];
      if (!id) return { status: 404 };
      const found = of(query.get("editor")).thumbnail(id);
      if (!found) throw new StatusError(404, "No such picture");
      return {
        status: 200,
        headers: { "content-type": found.type, "cache-control": "private, max-age=31536000, immutable", "x-content-type-options": "nosniff" },
        file: found.file,
      };
    },
    "checkpoints.nativeDiff": async (r) => {
      const b = objectBody(await r.body());
      if (typeof b.path !== "string" || !b.path) throw new StatusError(400, PATH_REQUIRED);
      if (typeof b.from !== "string" || !b.from) throw new StatusError(400, FROM_REQUIRED);
      if (b.to !== undefined && b.to !== null && typeof b.to !== "string") throw new StatusError(400, "to is a checkpoint id");
      return ok(await of(b.editor).openNativeDiff(b.path, b.from, (b.to as string | null | undefined) ?? null));
    },
    "checkpoints.restore": async (r) => {
      const b = objectBody(await r.body());
      if (typeof b.checkpoint !== "string" || !b.checkpoint) throw new StatusError(400, "checkpoint is required: the id to restore to");
      if (b.paths !== undefined && (!Array.isArray(b.paths) || b.paths.some((p) => typeof p !== "string"))) {
        throw new StatusError(400, "paths is a list of project-relative paths");
      }
      if (b.force !== undefined && typeof b.force !== "boolean") throw new StatusError(400, "force is a boolean");
      return ok(await of(b.editor).restore(b.checkpoint, {
        paths: b.paths as string[] | undefined,
        force: b.force === true,
      }));
    },
    "checkpoints.clear": async ({ query }) => ok(await of(query.get("editor")).clear()),
  };
}
