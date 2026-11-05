/**
 * The mutation journal over the daemon's HTTP API (#1481).
 *
 *   GET  /v1/journal              ?editor&group   state, groups, and a group's entries
 *   PUT  /v1/journal/group        { editor?, group: string | null }
 *   POST /v1/journal/revert       { editor?, group }   revert one group
 *   POST /v1/journal/revert-after { editor?, group }   revert every later group
 *
 * `editor` names a session as /v1/health lists it; omitted is the active one.
 */
import type { SessionRegistry } from "../sessions/session.js";
import { JournalError, journalFor, type MutationJournal } from "../journal/journal.js";
import { CheckpointError } from "../checkpoints/service.js";

export interface JournalRouteResult {
  status: number;
  body: unknown;
}

type Json = Record<string, unknown>;

function resolve(sessions: SessionRegistry, editor: unknown): MutationJournal {
  if (editor === undefined || editor === null || editor === "") return journalFor(sessions.active);
  if (typeof editor !== "string") throw new JournalError(400, "editor is a session name");
  const session = sessions.find(editor);
  if (!session) throw new JournalError(404, `No editor session '${editor}'. /v1/health lists them.`);
  return journalFor(session);
}

function requireGroup(b: Json): string {
  if (typeof b.group !== "string" || b.group.trim() === "") throw new JournalError(400, "group is required: the journal group");
  return b.group.trim();
}

/** The journal routes, or null for a path that is not one. */
export async function journalRoute(
  sessions: SessionRegistry,
  method: string,
  sub: string,
  query: URLSearchParams,
  body: unknown,
): Promise<JournalRouteResult | null> {
  const b = (body && typeof body === "object" && !Array.isArray(body) ? body : {}) as Json;
  try {
    if (sub === "" && method === "GET") {
      const group = query.get("group");
      return ok(resolve(sessions, query.get("editor")).state(group ? { group } : {}));
    }
    if (sub === "/group" && method === "PUT") {
      if (!("group" in b) || (b.group !== null && typeof b.group !== "string")) {
        throw new JournalError(400, "group is a string, or null to stop journaling");
      }
      return ok(resolve(sessions, b.editor).setGroup(b.group as string | null));
    }
    if (sub === "/revert" && method === "POST") {
      return ok(await resolve(sessions, b.editor).revert(requireGroup(b)));
    }
    if (sub === "/revert-after" && method === "POST") {
      return ok(await resolve(sessions, b.editor).revertAfter(requireGroup(b)));
    }
    return null;
  } catch (e) {
    if (e instanceof JournalError || e instanceof CheckpointError) return { status: e.status, body: { error: e.message } };
    throw e;
  }
}

const ok = (body: unknown): JournalRouteResult => ({ status: 200, body });
