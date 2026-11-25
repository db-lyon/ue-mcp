// @session per-session: Every category tool is cloned per session; the hub it reaches is the project's one, found from the session's project.
/**
 * hub: the Playtest Hub as an MCP category. Goals and milestones with their
 * status, playtests and their health, links to tracker items, and plans that
 * run playtests under the editor lease. The same data and rules as the
 * daemon's /v1/hub routes (docs/hub.md); status is worked out from the runs
 * on this computer and never stored.
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
  "The Playtest Hub: goals and milestones the game must meet, the playtests (PIE test scripts) that prove them, their runs as evidence, and plans that run playtests under the editor lease. Goals are kept in one store per project outside the game repo, and reference playtests by asset path. Status (failing, ready_to_sign_off, at_risk, running, in_progress, not_started, met) and a playtest's health are worked out on every read from the runs recorded on this computer, never stored. Needs the project's daemon.",
  {
    list_goals: action("list_goals", "read", "List the hub's goals and milestones with their status, progress, owner, priority, due date, the goals each requires and is part of, and links. unused: no milestone or goal requires it. Archived goals are left out unless include_archived is true.", { optional: ["include_archived"] }),
    read_goal: action("read_goal", "read", "Read one goal: its fields, its status, and each playtest with its health, latest run and links. A playtest missing on this checkout shows health missing; that is no failure of the goal.", { required: ["id"] }),
    read_check: action("read_check", "read", "Read one playtest: its health, the goals that list it, its links, and its latest runs with their ids and links.", { required: ["check_id"], optional: ["limit"], notes: { limit: "runs to return, 10 by default, at most 50" } }),
    create_goal: action("create_goal", "mutate", "Create a goal in the project's goal store. Its id is made from the title and a short random ending unless id is given. Only title is required.", { required: ["title"], optional: ["id", ...GOAL_FIELDS] }),
    update_goal: action("update_goal", "mutate", "Change a goal's fields. Only the fields given change; checks, requires and links, when given, replace the lists. To add or remove one link, use add_link or remove_link.", { required: ["id"], optional: ["title", ...GOAL_FIELDS] }),
    add_link: action("add_link", "mutate", `Link a goal, a playtest or a run to an item in a tracker or anywhere else: an issue, a ticket, a card, a document. Give exactly one of goal_id, check_id and run_id. Any URL with a scheme works except javascript:, data: and file:; the hub stores it and never contacts it. A link to a URL already there is replaced. rel says how the item relates, free text, usually one of ${RELS}.`, { required: ["url"], optional: ["title", "rel", "goal_id", "check_id", "run_id"], choices: [oneTarget] }),
    remove_link: action("remove_link", "mutate", "Remove a link, by its URL, from a goal, a playtest or a run. Give exactly one of goal_id, check_id and run_id.", { required: ["url"], optional: ["goal_id", "check_id", "run_id"], choices: [oneTarget] }),
    require_goal: action("require_goal", "mutate", "Make one goal require another: goal_id (often a milestone) is met only when requires is. Added at the end of goal_id's list; a goal it requires already keeps its place and takes the required given. A goal can be required by any number of goals, so the same goal can sit under several milestones. A loop is refused.", { required: ["goal_id", "requires"], optional: ["required"] }),
    unrequire_goal: action("unrequire_goal", "mutate", "Stop one goal requiring another. Only goal_id changes; the other goal stays.", { required: ["goal_id", "requires"] }),
    run_plan: action("run_plan", "mutate", "Queue a plan that runs a goal's playtests (and those of the goals it requires), or the playtests named, one at a time. The plan runs on the editor under the hub's lease: it starts only while nothing else holds the editor (Play In Editor in use, a dialog, another client's calls, a build), starts and ends only the Play In Editor session its own run started, and a manual step holds the run for a person's verdict. Returns the queued plan at once; follow it with read_queue. Refused when a client has set agent plans to Manual.", {
      optional: ["goal_id", "playtests", "attended", "replay"],
      choices: [{ mode: "exactlyOne", branches: [["goal_id"], ["playtests"]] }],
      notes: { attended: "a person is watching, so manual steps run as well; false (default) skips playtests that need a person", replay: "replay each playtest's last recording instead of playing it fresh" },
    }, "Queues a plan the hub runs on the editor under its lease; the call itself returns at once with the queued plan."),
    read_queue: action("read_queue", "read", "The plan queue: whether it is paused, why the next item waits (waiting), the plan running now, those queued, and the last few finished, each with its items' state and run id.", {}),
    read_set: action("read_set", "read", "Read the set as text: each track (a goal) with its color, player, mute and solo and the clip instances on its timeline (kind, id, asset path, start frame, in, out, length), then each scene (a milestone) with the tracks it plays. Also each track's and scene's status, and every finding with candidates.", {}),
    edit_set: action("edit_set", "mutate", "Edit the set with ops, one per line, all or none. Tracks: track add \"Name\" [color=C] [player=N] | track delete|rename|color|move|mute|solo|player <track> .... Scenes: scene add|insert \"Name\" [at N] | scene delete|rename|color|duplicate|move <scene> ... | scene track <scene> add|remove <track>. Instances: add <track> <clip|script> <asset path> [at <frame>] | duplicate|delete|unique <track>/<id> | copy <track>/<id> to <track> [at <frame>] | move <track>/<id> to <frame> | by <frames> | to <track> [at <frame>] | trim <track>/<id> in=N out=N | length=N | rename <track>/<id> \"Name\" | color <track>/<id> <color> | retarget <track>/<id> [clip|script] <asset path>. Tracks and scenes are named by id or exact name. unique copies the clip asset and points the instance at the copy; retarget points it at another clip, keeping its place, name, color and trim. Frames are at the track's rate (60). A name or clip that matches nothing fails with candidates.", { required: ["ops"] }),
    run: action("run", "mutate", "Queue one run of a track's timeline (track <track>), a scene's tracks together (scene <scene>), or every timeline (arrangement), in one PIE session under the hub's lease. Append from <seconds>s or from <frame> to play the timelines from that point, as from the Arrangement's playhead. Each track drives its own player; muted tracks are left out and, when any track is soloed, only soloed ones play. Returns the queued plan at once; follow it with read_queue, then read_run.", { required: ["scope"], optional: ["attended"] }, "Queues a run the hub plays on the editor under its lease; the call itself returns at once."),
    read_run: action("read_run", "read", "Read a run's result: for a track, scene or arrangement run, each instance at its place (track/id, status, start and end, checks passed), each track's status, and why it stopped.", { required: ["run_id"] }),
  },
  {
    include_archived: z.boolean().optional().describe("list_goals: also list archived goals"),
    id: z.string().optional().describe("read_goal, update_goal: the goal's id; create_goal: an id to use (lowercase letters, digits and dashes)"),
    limit: z.number().int().optional().describe("read_check: how many runs to return, 10 by default, at most 50"),
    check_id: z.string().optional().describe("read_check, add_link, remove_link: a playtest, as script:<asset path> (as read_goal gives it)"),
    goal_id: z.string().optional().describe("add_link, remove_link: a goal to link; require_goal, unrequire_goal: the goal or milestone that requires; run_plan: the goal whose playtests to run"),
    run_id: z.string().optional().describe("add_link, remove_link: a run to link (from read_check); read_run: the run to read (from read_queue)"),
    title: z.string().optional().describe("create_goal, update_goal: what the game must do, in a sentence; add_link: what the link is shown as, such as the issue's title"),
    kind: z.enum(["goal", "milestone"]).optional().describe("create_goal, update_goal: goal (default), or milestone: a goal shown as a heading, usually one that requires other goals"),
    description: z.string().optional().describe("create_goal, update_goal: what done means, in Markdown"),
    owner: z.string().nullable().optional().describe("create_goal, update_goal: who owns the goal, as the team names people; null clears it"),
    priority: z.enum(GOAL_PRIORITIES as [string, ...string[]]).nullable().optional().describe("create_goal, update_goal: p0 (most urgent) to p3; null clears it"),
    due: z.string().nullable().optional().describe("create_goal, update_goal: YYYY-MM-DD, or null"),
    acceptance: z.object({
      stableRuns: z.number().int().optional().describe("how many passes in a row each required playtest needs, 1 to 10"),
      signOff: z.boolean().optional().describe("a person signs the goal off before it is met"),
    }).optional().describe("create_goal, update_goal: when the goal is met"),
    checks: z.array(z.object({
      kind: z.literal("script"),
      target: z.string().describe("the script's asset path without the object name"),
      required: z.boolean().optional(),
    })).optional().describe("create_goal, update_goal: the playtests that prove the goal, in order; given, it replaces the list"),
    requires: z.union([
      z.string(),
      z.array(z.object({ goal: z.string(), required: z.boolean().optional() })),
    ]).optional().describe("require_goal, unrequire_goal: the other goal's id; create_goal, update_goal: the goals this goal requires, in order, each { goal: id, required? }; given, it replaces the list"),
    required: z.boolean().optional().describe("require_goal: false means goal_id is met without it; true by default"),
    links: z.array(z.object({ url: z.string(), title: z.string().optional(), rel: z.string().optional() })).optional().describe(`create_goal, update_goal: tracker items the goal refers to; given, it replaces the list. rel is free text, usually one of ${RELS}`),
    archived: z.boolean().optional().describe("create_goal, update_goal: hide the goal from the overview"),
    url: z.string().optional().describe("add_link, remove_link: the item's URL"),
    rel: z.string().optional().describe(`add_link: how the item relates: ${RELS}, or other text`),
    playtests: z.array(z.string()).optional().describe("run_plan: playtests to run, by asset path such as /Game/Tests/DoorOpens"),
    attended: z.boolean().optional().describe("run_plan, run: a person is watching, so manual steps run too"),
    ops: z.union([z.string(), z.array(z.record(z.string(), z.unknown()))]).optional().describe("edit_set: op lines, one per line, or a list of op objects"),
    scope: z.string().optional().describe("run: track <track>, scene <scene>, or arrangement, each optionally followed by from <seconds>s or from <frame>"),
    replay: z.boolean().optional().describe("run_plan: replay each playtest's last recording"),
  },
  { label: { text: "playtest hub", description: "goals, milestones, playtest health, plans run under the editor lease", keywords: ["goal", "milestone", "playtest", "qa"] } },
);
