// @session per-session: Every category tool is cloned per session; the hub it reaches is the project's one, found from the session's project.
/**
 * hub: the Playtest Hub as an MCP category. Tracks and scenes with their
 * status, clips and their health, links to tracker items, and plans that
 * run clips under the editor lease. The same data and rules as the
 * daemon's /v1/hub routes (docs/hub.md); status is worked out from the runs
 * on this computer and never stored. Wire names (goal, milestone, check,
 * playtests) are unchanged.
 */
import { z } from "zod";
import { GOAL_PRIORITIES, TRACKER_LINK_RELS } from "../contract/hub.js";
import { runHubAction } from "../hub/hub-tools.js";
import { hubOf } from "../hub/hub-client.js";
import type { ActionSpec, ParamDecl, ToolContext, ToolDef } from "../surface/tool-types.js";
import type { ParamChoice } from "../surface/handler-spec.js";
import { categoryTool } from "../surface/category-tool.js";

const RELS = TRACKER_LINK_RELS.join(", ");

function hubFor(ctx: ToolContext) {
  const hub = hubOf(ctx.project.projectDir ?? "");
  if (!hub) throw new Error("The Playtest Hub runs in the project's daemon. This server runs in process, without one: start the daemon (ue-mcp daemon start) and connect through it.");
  return hub;
}

const FILES = "Reads and writes the hub's files under the project's data folder, and works out status from the runs recorded there.";

/** One action: it runs the hub function of the same name and hands back its data. */
function action(name: string, effect: "read" | "mutate", description: string, params: ParamDecl, editor?: string): ActionSpec {
  return {
    kind: "handler",
    effect,
    ...(editor ? { editorBound: editor } : { offline: FILES }),
    params,
    description,
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => runHubAction(hubFor(ctx), name, p),
  };
}

const oneTarget: ParamChoice = { mode: "exactlyOne", branches: [["goal_id"], ["check_id"], ["run_id"]] };
const GOAL_FIELDS = ["kind", "description", "owner", "priority", "due", "acceptance", "checks", "requires", "links", "archived"] as const;

export const hubTool: ToolDef = categoryTool(
  "hub",
  "The Playtest Hub: tracks (what the game must do) and scenes (tracks that require other tracks), the clips (PIE test scripts) that prove them, their runs as evidence, and plans that run clips under the editor lease. The actions and fields keep their original names: a track is a goal, a scene is a goal of kind milestone, and a clip is a check or playtest. Tracks are kept in one store per project outside the game repo, and reference clips by asset path. Status (failing, ready_to_sign_off, at_risk, running, in_progress, not_started, met) and a clip's health are worked out on every read from the runs recorded on this computer, never stored. Needs the project's daemon.",
  {
    list_goals: action("list_goals", "read", "List the hub's tracks and scenes with their status, progress, owner, priority, due date, the tracks each requires and is part of, and links. unused: no scene or track requires it. Archived tracks are left out unless include_archived is true.", { optional: ["include_archived"] }),
    read_goal: action("read_goal", "read", "Read one track or scene: its fields, its status, and each clip with its health, latest run and links. A clip missing on this checkout shows health missing; that is no failure of the track.", { required: ["id"] }),
    read_check: action("read_check", "read", "Read one clip: its health, the tracks that list it, its links, and its latest runs with their ids and links.", { required: ["check_id"], optional: ["limit"], notes: { limit: "runs to return, 10 by default, at most 50" } }),
    create_goal: action("create_goal", "mutate", "Create a track, or a scene with kind milestone, in the project's store. Its id is made from the title and a short random ending unless id is given. Only title is required.", { required: ["title"], optional: ["id", ...GOAL_FIELDS] }),
    update_goal: action("update_goal", "mutate", "Change a track's or scene's fields. Only the fields given change; checks, requires and links, when given, replace the lists. To add or remove one link, use add_link or remove_link.", { required: ["id"], optional: ["title", ...GOAL_FIELDS] }),
    add_link: action("add_link", "mutate", `Link a track, a clip or a run to an item in a tracker or anywhere else: an issue, a ticket, a card, a document. Give exactly one of goal_id, check_id and run_id. Any URL with a scheme works except javascript:, data: and file:; the hub stores it and never contacts it. A link to a URL already there is replaced. rel says how the item relates, free text, usually one of ${RELS}.`, { required: ["url"], optional: ["title", "rel", "goal_id", "check_id", "run_id"], choices: [oneTarget] }),
    remove_link: action("remove_link", "mutate", "Remove a link, by its URL, from a track, a clip or a run. Give exactly one of goal_id, check_id and run_id.", { required: ["url"], optional: ["goal_id", "check_id", "run_id"], choices: [oneTarget] }),
    require_goal: action("require_goal", "mutate", "Make one track require another: goal_id (often a scene) is met only when requires is. Added at the end of goal_id's list; a track it requires already keeps its place and takes the required given. A track can be required by any number of tracks, so the same track can sit in several scenes. A loop is refused.", { required: ["goal_id", "requires"], optional: ["required"] }),
    unrequire_goal: action("unrequire_goal", "mutate", "Stop one track requiring another. Only goal_id changes; the other track stays.", { required: ["goal_id", "requires"] }),
    run_plan: action("run_plan", "mutate", "Queue a plan that runs a track's clips (and those of the tracks it requires), or the clips named, one at a time. The plan runs on the editor under the hub's lease: it starts only while nothing else holds the editor (Play In Editor in use, a dialog, another client's calls, a build), starts and ends only the Play In Editor session its own run started, and a manual step holds the run for a person's verdict. Returns the queued plan at once; follow it with read_queue. Refused when a client has set agent plans to Manual.", {
      optional: ["goal_id", "playtests", "attended", "replay"],
      choices: [{ mode: "exactlyOne", branches: [["goal_id"], ["playtests"]] }],
      notes: { attended: "a person is watching, so manual steps run as well; false (default) skips clips that need a person", replay: "replay each clip's last recording instead of playing it fresh" },
    }, "Queues a plan the hub runs on the editor under its lease; the call itself returns at once with the queued plan."),
    read_queue: action("read_queue", "read", "The plan queue: whether it is paused, why the next item waits (waiting), the plan running now, those queued, and the last few finished, each with its items' state and run id.", {}),
    read_set: action("read_set", "read", "Read one arrangement's set as text: each track (a goal) with its color, performer (player=N, or performer=actor:<name> for a track that drives an actor), mute and solo and the clip instances on its timeline (kind, id, asset path, start frame, in, out, length), then each scene (a milestone) with the tracks it plays. Also each track's and scene's status, every finding with candidates, the arrangement read (with the level its runs open) and every arrangement of the project. A project holds any number of arrangements, each with its own tracks and scenes; clips are shared by all of them. arrangement names one by id or exact name, the first one by default.", { optional: ["arrangement"] }),
    edit_set: action("edit_set", "mutate", "Edit the set with ops, one per line, all or none. Tracks: track add \"Name\" [color=C] [player=N | performer=player:N|actor:<name>] | track delete|rename|color|move|mute|solo|player <track> ... | track performer <track> player:N|actor:<name>. A track drives one performer: the editor's player (player:0), a remote client (player:N) or an actor in the level by label, name or path (list_performers gives the candidates). A player takes input, action and value lanes, an actor move, action and value lanes, and a run refuses a clip with a lane its track's performer does not take. Scenes: scene add|insert \"Name\" [at N] | scene delete|rename|color|duplicate|move <scene> ... | scene track <scene> add|remove <track>. Instances: add <track> <clip|script> <asset path> [at <frame>] | duplicate|delete|unique <track>/<id> | copy <track>/<id> to <track> [at <frame>] | move <track>/<id> to <frame> | by <frames> | to <track> [at <frame>] | trim <track>/<id> in=N out=N | length=N | rename <track>/<id> \"Name\" | color <track>/<id> <color> | retarget <track>/<id> [clip|script] <asset path>. Tracks and scenes are named by id or exact name. unique copies the clip asset and points the instance at the copy; retarget points it at another clip, keeping its place, name, color and trim. Frames are at the track's rate (60). A name or clip that matches nothing fails with candidates. The ops edit one arrangement: arrangement, by id or exact name, the first one by default; a track or scene added joins it.", { required: ["ops"], optional: ["arrangement"] }),
    list_performers: action("list_performers", "read", "List what a track in the arrangement can drive, in the text form edit_set's track performer takes: the editor's player and the first remote clients, then the actors of the level the editor has open, pawns first, each with its class and the lanes it takes. Without an editor only the players are listed, with the reason.", { optional: ["arrangement"] }),
    run: action("run", "mutate", "Queue one run of a track's timeline (track <track>), a scene's tracks together (scene <scene>), or every timeline of the arrangement (arrangement), in one PIE session under the hub's lease. Tracks and scenes are looked up in the arrangement named by arrangement (id or exact name, the first one by default), and a run of an arrangement with a level opens that map first. Append from <seconds>s or from <frame> to play the timelines from that point, as from the Arrangement's playhead. Each track drives its own performer, a player or an actor; muted tracks are left out and, when any track is soloed, only soloed ones play. Returns the plan and where it went: when it starts at once, the call answers once it has started, with the runId read_run takes; otherwise follow it with read_queue.", { required: ["scope"], optional: ["attended", "arrangement"] }, "Queues a run the hub plays on the editor under its lease; when it starts at once, the call waits for that start."),
    read_run: action("read_run", "read", "Read a run's result: for a track, scene or arrangement run, each instance at its place (track/id, status, start and end, checks passed), each track's status, the arrangement it was made in, and why it stopped. arrangement, when given, must be the one the run was made in.", { required: ["run_id"], optional: ["arrangement"] }),
  },
  {
    include_archived: z.boolean().optional().describe("list_goals: also list archived tracks"),
    id: z.string().optional().describe("read_goal, update_goal: the track's or scene's id; create_goal: an id to use (lowercase letters, digits and dashes)"),
    limit: z.number().int().optional().describe("read_check: how many runs to return, 10 by default, at most 50"),
    check_id: z.string().optional().describe("read_check, add_link, remove_link: a clip, as script:<asset path> (as read_goal gives it)"),
    goal_id: z.string().optional().describe("add_link, remove_link: a track to link; require_goal, unrequire_goal: the track or scene that requires; run_plan: the track whose clips to run"),
    run_id: z.string().optional().describe("add_link, remove_link: a run to link (from read_check); read_run: the run to read (from read_queue)"),
    title: z.string().optional().describe("create_goal, update_goal: what the game must do, in a sentence; add_link: what the link is shown as, such as the issue's title"),
    kind: z.enum(["goal", "milestone"]).optional().describe("create_goal, update_goal: goal (default) for a track, or milestone for a scene: shown as a heading, usually requiring other tracks"),
    description: z.string().optional().describe("create_goal, update_goal: what done means, in Markdown"),
    owner: z.string().nullable().optional().describe("create_goal, update_goal: who owns the track, as the team names people; null clears it"),
    priority: z.enum(GOAL_PRIORITIES as [string, ...string[]]).nullable().optional().describe("create_goal, update_goal: p0 (most urgent) to p3; null clears it"),
    due: z.string().nullable().optional().describe("create_goal, update_goal: YYYY-MM-DD, or null"),
    acceptance: z.object({
      stableRuns: z.number().int().optional().describe("how many passes in a row each required clip needs, 1 to 10"),
      signOff: z.boolean().optional().describe("a person signs the track off before it is met"),
    }).optional().describe("create_goal, update_goal: when the track is met"),
    checks: z.array(z.object({
      kind: z.literal("script"),
      target: z.string().describe("the script's asset path without the object name"),
      required: z.boolean().optional(),
    })).optional().describe("create_goal, update_goal: the clips that prove the track, in order; given, it replaces the list"),
    requires: z.union([
      z.string(),
      z.array(z.object({ goal: z.string(), required: z.boolean().optional() })),
    ]).optional().describe("require_goal, unrequire_goal: the other track's id; create_goal, update_goal: the tracks this one requires, in order, each { goal: id, required? }; given, it replaces the list"),
    required: z.boolean().optional().describe("require_goal: false means goal_id is met without it; true by default"),
    links: z.array(z.object({ url: z.string(), title: z.string().optional(), rel: z.string().optional() })).optional().describe(`create_goal, update_goal: tracker items the track refers to; given, it replaces the list. rel is free text, usually one of ${RELS}`),
    archived: z.boolean().optional().describe("create_goal, update_goal: hide the track from the overview"),
    url: z.string().optional().describe("add_link, remove_link: the item's URL"),
    rel: z.string().optional().describe(`add_link: how the item relates: ${RELS}, or other text`),
    playtests: z.array(z.string()).optional().describe("run_plan: clips to run, by asset path such as /Game/Tests/DoorOpens"),
    attended: z.boolean().optional().describe("run_plan, run: a person is watching, so manual steps run too"),
    ops: z.union([z.string(), z.array(z.record(z.string(), z.unknown()))]).optional().describe("edit_set: op lines, one per line, or a list of op objects"),
    scope: z.string().optional().describe("run: track <track>, scene <scene>, or arrangement, each optionally followed by from <seconds>s or from <frame>"),
    replay: z.boolean().optional().describe("run_plan: replay each clip's last recording"),
    arrangement: z.string().optional().describe("read_set, edit_set, list_performers, run, read_run: the arrangement, by id or exact name (read_set lists them); the project's first one when absent"),
  },
  { label: { text: "playtest hub", description: "tracks, scenes, clip health, plans run under the editor lease", keywords: ["track", "scene", "clip", "goal", "milestone", "playtest", "qa"] } },
);
