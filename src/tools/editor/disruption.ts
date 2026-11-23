// @session per-session: Answers and holds against the server's consent requests, under the calling session.
/**
 * editor(hold), editor(release), editor(disruption_vote),
 * editor(disruption_confirm) and editor(disruption_status): a session's side of the consent rule in
 * dispatch/disruption.ts (#2075). They change nothing in the editor or on
 * disk, only what this server lets another session do to the editor, so a
 * read-only session can use them too.
 */
import { z } from "zod";
import type { ActionSpec, ToolContext } from "../../surface/tool-types.js";
import { McpError, ErrorCode } from "../../core/errors.js";
import { currentDisruptionCaller, processCaller } from "../../dispatch/disruption-gate.js";
import {
  DISRUPTION_VOTES,
  DisruptionError,
  MAX_HOLD_TTL_SECONDS,
  SUBAGENT_ATTESTATIONS,
  describeAnswers,
  type SubagentAttestation,
  type DisruptionCaller,
  type DisruptionRegistry,
  type DisruptionVote,
} from "../../dispatch/disruption.js";

function registry(ctx: ToolContext): DisruptionRegistry {
  const r = ctx.sessions?.disruptions;
  if (!r) throw new McpError(ErrorCode.NOT_FOUND, "This server has no consent registry: it exists where calls are dispatched through the session registry.");
  return r;
}

function callerOf(ctx: ToolContext): DisruptionCaller {
  return currentDisruptionCaller(ctx.leaseHolder ?? processCaller(ctx.client?.name));
}

function text(p: Record<string, unknown>, key: string): string {
  return typeof p[key] === "string" ? (p[key] as string).trim() : "";
}

function rethrow(e: unknown): never {
  if (e instanceof DisruptionError) throw new McpError(e.kind === "not_found" ? ErrorCode.NOT_FOUND : ErrorCode.INVALID_PARAMS, e.message);
  throw e;
}

export const disruptionActions: Record<string, ActionSpec> = {
  hold: {
    kind: "handler",
    effect: "read",
    offline: "Holds live in this server process, not in the editor.",
    addressesServer: true,
    unjournaled: true,
    params: { required: ["reason"], optional: ["ttlSeconds"], notes: { ttlSeconds: `default 3600, at most ${MAX_HOLD_TTL_SECONDS}` } },
    description:
      "Hold the editor for this session: every request another session makes to stop, restart, rebuild, build under or Live Coding patch it "
      + "waits until you call editor(action=\"release\") or the hold runs out. Use it while you have unsaved work, a PIE session or a long "
      + "operation in this editor. reason is shown to the session asking. Returns the hold and its expiry.",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const reason = text(p, "reason");
      if (!reason) throw new McpError(ErrorCode.INVALID_PARAMS, "'reason' says why the editor must stay up, e.g. 'unsaved level edits in /Game/Maps/Arena'. The session asking reads it.");
      const ttl = typeof p.ttlSeconds === "number" && p.ttlSeconds > 0 ? p.ttlSeconds : undefined;
      const caller = callerOf(ctx);
      const hold = registry(ctx).hold(caller, reason, ttl);
      return { held: true, session: caller.id, reason, expiresAt: new Date(hold.expiresAt).toISOString() };
    },
  },
  release: {
    kind: "handler",
    effect: "read",
    offline: "Holds live in this server process, not in the editor.",
    addressesServer: true,
    unjournaled: true,
    params: {},
    description:
      "Release this session's hold on the editor, and turn every wait_until_released vote it gave into go, so a pending stop, restart "
      + "or rebuild can go ahead once the other sessions agree. Returns whether a hold was released and which requests it answered.",
    handler: async (ctx: ToolContext) => {
      const caller = callerOf(ctx);
      const out = registry(ctx).release(caller);
      return { session: caller.id, released: out.hadHold, votesReleased: out.votesReleased };
    },
  },
  disruption_vote: {
    kind: "handler",
    effect: "read",
    offline: "Requests live in this server process, not in the editor.",
    addressesServer: true,
    unjournaled: true,
    params: { required: ["requestId", "vote"], optional: ["reason"] },
    description:
      "Answer another session's request to stop, restart, rebuild, build under or Live Coding patch the editor, named in a notice on your "
      + "tool responses. vote: go lets it run; wait_until_released holds it until you call editor(action=\"release\") (pause your work or "
      + "subagents first); block refuses it. reason is shown to the session asking. A session cannot answer its own request. Returns the "
      + "request with every session's answer.",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const id = text(p, "requestId");
      const vote = text(p, "vote") as DisruptionVote;
      if (!id) throw new McpError(ErrorCode.INVALID_PARAMS, "'requestId' names the request, e.g. 'r-3f2a1c', as the notice gave it. editor(action='disruption_status') lists the open ones.");
      if (!DISRUPTION_VOTES.includes(vote)) throw new McpError(ErrorCode.INVALID_PARAMS, `'vote' is one of ${DISRUPTION_VOTES.join(", ")}.`);
      try {
        const view = registry(ctx).vote(callerOf(ctx), id, vote, text(p, "reason"));
        return { ...view, summary: describeAnswers(view) };
      } catch (e) {
        rethrow(e);
      }
    },
  },
  disruption_confirm: {
    kind: "handler",
    effect: "read",
    offline: "Requests live in this server process, not in the editor.",
    addressesServer: true,
    unjournaled: true,
    params: { required: ["requestId", "subagents"] },
    description:
      "On this session's own request to stop, restart, rebuild, build under or Live Coding patch the editor: say that the subagents you "
      + "started are none or paused. ue-mcp cannot see subagents that share your connection, so the request waits for this. Check each one "
      + "and have it stop using the editor first. Same as passing subagents to the action itself; use it when the request came from a flow "
      + "or another route. Calls still in flight on your connection hold the request regardless. Returns the request.",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const id = text(p, "requestId");
      const subagents = text(p, "subagents") as SubagentAttestation;
      if (!id) throw new McpError(ErrorCode.INVALID_PARAMS, "'requestId' names this session's request, as the refusal gave it.");
      if (!SUBAGENT_ATTESTATIONS.includes(subagents)) throw new McpError(ErrorCode.INVALID_PARAMS, `'subagents' is one of ${SUBAGENT_ATTESTATIONS.join(", ")}.`);
      try {
        const view = registry(ctx).attest(callerOf(ctx), id, subagents);
        return { ...view, summary: describeAnswers(view) };
      } catch (e) {
        rethrow(e);
      }
    },
  },
  disruption_status: {
    kind: "handler",
    effect: "read",
    offline: "Requests live in this server process, not in the editor.",
    addressesServer: true,
    unjournaled: true,
    params: { optional: ["requestId"] },
    description:
      "Requests to stop, restart, rebuild, build under or Live Coding patch the editor, each with its state (pending, blocked, approved, "
      + "done, denied, expired) and every other session's answer and reason, plus what each session is doing: calls in flight, its last "
      + "call, a PIE session it started, open transactions, its hold and the assets it holds leases on. With requestId, that request alone.",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const r = registry(ctx);
      const me = callerOf(ctx).id;
      const id = text(p, "requestId");
      if (id) {
        const view = r.get(id);
        if (!view) throw new McpError(ErrorCode.NOT_FOUND, `No disruption request '${id}'. editor(action='disruption_status') without requestId lists them all.`);
        return { ...view, summary: describeAnswers(view), mine: view.requester.id === me };
      }
      return { enforcing: r.enforcing, session: me, config: r.config, requests: r.list(), sessions: r.activities() };
    },
  },
};

/**
 * The attestation a disruptive action takes: the caller's own subagents,
 * which ue-mcp cannot see when they share its connection, are none or paused.
 */
export const SUBAGENTS_PARAM = z
  .enum(["none", "paused"])
  .optional()
  .describe(
    "stop_editor, restart_editor, request_editor_shutdown, build_project/build, hot_reload, live_coding_compile, a quitting execute_command; "
      + "disruption_confirm: your own subagents are none or paused. Asked for when the editor would be taken away; check them before you say so",
  );

export const disruptionSchema = {
  subagents: SUBAGENTS_PARAM,
  reason: z.string().optional().describe("hold: why the editor must stay up; disruption_vote: why you answer as you do. The session asking reads it"),
  requestId: z.string().optional().describe("disruption_vote / disruption_confirm / disruption_status: the request id, such as r-3f2a1c"),
  vote: z.enum(["go", "wait_until_released", "block"]).optional().describe("disruption_vote: go, wait_until_released or block"),
  ttlSeconds: z.number().optional().describe("hold: seconds until the hold runs out (default 3600)"),
};
