/**
 * editor(journal_group), list_journal, revert_group and revert_after_group
 * (#1481): the MCP face of src/journal/journal.ts, with the same payloads as
 * the daemon's /v1/journal routes.
 */
import { z } from "zod";
import type { ActionSpec, ToolContext } from "../core/types.js";
import { journalFor } from "../journal/journal.js";

function journalOf(ctx: ToolContext) {
  if (!ctx.session) throw new Error("The journal needs an editor session; this call has none");
  return journalFor(ctx.session);
}

const groupOf = (p: Record<string, unknown>): string => (typeof p.journalGroup === "string" ? p.journalGroup.trim() : "");

export const journalActions: Record<string, ActionSpec> = {
  journal_group: {
    kind: "handler",
    effect: "mutate",
    description: "Name the group every later call that may change state is journaled into, until another group is named; omit journalGroup to stop journaling. Use one group per chat turn. Each journaled call records its rollback descriptor when its handler returned one, and with checkpoints on (checkpoints: on in ue-mcp.yml) a checkpoint before and after it, so the files it wrote can be put back. The journal is Saved/UE_MCP_Bridge/journal.jsonl in the project and survives restarts; the current group does not, so name it again after one. Returns the journal state. Params: journalGroup?",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const group = groupOf(p);
      return journalOf(ctx).setGroup(group === "" ? null : group);
    },
  },
  list_journal: {
    kind: "handler",
    effect: "read",
    description: "The journal: the current group, whether checkpoints are on, and every group with its entry, revertible and reverted counts in the order they were first recorded. With journalGroup, also that group's entries: task, effect, the rollback descriptor or checkpoint snapshot that restores it, and for one that cannot be restored, the reason. Params: journalGroup?",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const group = groupOf(p);
      return journalOf(ctx).state(group ? { group } : {});
    },
  },
  revert_group: {
    kind: "handler",
    effect: "mutate",
    description: "Revert every call journaled into journalGroup that is not reverted yet, newest first: a rollback descriptor is replayed through the bridge (falling back to the files when it fails and a snapshot exists), otherwise the call's files are restored to the checkpoint taken before it and reloaded in the editor. Returns one result per entry: reverted, failed with the error, or not-revertible with the reason, so nothing is skipped silently. Works after an editor or server restart. Params: journalGroup",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => journalOf(ctx).revert(groupOf(p)),
  },
  revert_after_group: {
    kind: "handler",
    effect: "mutate",
    description: "Revert every call of every group first journaled after journalGroup, newest first, the same way revert_group does; journalGroup itself is kept. Params: journalGroup",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => journalOf(ctx).revertAfter(groupOf(p)),
  },
};

export const journalSchema = {
  journalGroup: z.string().optional().describe("journal_group: the group later calls are journaled into (omit to stop); list_journal: the group whose entries to list; revert_group, revert_after_group: the group to revert, or to revert everything after"),
};
