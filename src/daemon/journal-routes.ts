// @session per-session: Resolves the editor session each request names before acting on its journal.
/**
 * The mutation journal over the daemon's HTTP API (#1481): the journal.*
 * entries of DAEMON_ROUTES. A revert restores through the session's
 * checkpoints, so its refusals carry their status the same way.
 */
import type { SessionRegistry } from "../sessions/session.js";
import { journalFor, type MutationJournal } from "../journal/journal.js";
import { StatusError } from "../core/status-error.js";
import { objectBody, ok, resolveEditorSession, type RouteHandlers } from "./router.js";

type JournalRouteName = "journal.state" | "journal.setGroup" | "journal.revert" | "journal.revertAfter";

function requireGroup(b: Record<string, unknown>): string {
  if (typeof b.group !== "string" || b.group.trim() === "") throw new StatusError(400, "group is required: the journal group");
  return b.group.trim();
}

export function journalRoutes(sessions: SessionRegistry): Pick<RouteHandlers, JournalRouteName> {
  const of = (editor: unknown): MutationJournal => journalFor(resolveEditorSession(sessions, editor));
  return {
    "journal.state": ({ query }) => {
      const group = query.get("group");
      return ok(of(query.get("editor")).state(group ? { group } : {}));
    },
    "journal.setGroup": async (r) => {
      const b = objectBody(await r.body());
      if (!("group" in b) || (b.group !== null && typeof b.group !== "string")) {
        throw new StatusError(400, "group is a string, or null to stop journaling");
      }
      return ok(of(b.editor).setGroup(b.group as string | null));
    },
    "journal.revert": async (r) => {
      const b = objectBody(await r.body());
      return ok(await of(b.editor).revert(requireGroup(b)));
    },
    "journal.revertAfter": async (r) => {
      const b = objectBody(await r.body());
      return ok(await of(b.editor).revertAfter(requireGroup(b)));
    },
  };
}
