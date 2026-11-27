// @session per-session: Every category tool is cloned per session; the hub it reaches is the project's one, found from the session's project.
/**
 * hub: the Playtest Hub as an MCP category (#2146). Compose with clips,
 * tracks and arrangements; test with tests and suites. The same data and
 * rules as the daemon's /v1/hub routes (docs/hub.md).
 */
import { z } from "zod";
import { CHECK_MODES, HUB_DIR } from "../contract/hub.js";
import { runHubAction } from "../hub/hub-tools.js";
import { hubOf } from "../hub/hub-client.js";
import type { ActionSpec, ParamDecl, ToolContext, ToolDef } from "../surface/tool-types.js";
import { categoryTool } from "../surface/category-tool.js";

function hubFor(ctx: ToolContext) {
  const hub = hubOf(ctx.project.projectDir ?? "");
  if (!hub) throw new Error("The Playtest Hub runs in the project's daemon. This server runs in process, without one: start the daemon (ue-mcp daemon start) and connect through it.");
  return hub;
}

const FILES = `Reads and writes the hub's text files under ${HUB_DIR}/ in the project, and this machine's runs.`;

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

const MODES = CHECK_MODES.join(", ");

export const hubTool: ToolDef = categoryTool(
  "hub",
  `The Playtest Hub. Composing: a clip is a reusable player performance (Input Action lanes) and never asserts anything; an arrangement is the scenario: a level, tracks (each one player and a timeline of clip instances), one Checks track (modes ${MODES}) and named regions. play auditions one on the open editor, from a playhead and looping, without grading it. Testing: a test is an arrangement (its id) or one region (<arrangement>:<region>) and passes when all its checks pass; a suite lists tests and suites and owns health, pass-rate history, flaky marking and quarantine. Clips, arrangements and suites are JSON files under ${HUB_DIR}/ in the project; runs, the queue and schedules stay on this machine. Runs play on a hidden editor ue-mcp launches on a copy of the project when one fits in memory, else on the open editor under the hub's lease; manual checks always play on the open editor.`,
  {
    overview: action("overview", "read", "Everything at once: clips, arrangements, suites with health, every test, the queue, the lease, recent runs, schedules and files that do not read.", {}),
    list_clips: action("list_clips", "read", "List the clips: id, title, performer kind, rate, length in frames, lane count and the arrangements that place each.", {}),
    read_clip: action("read_clip", "read", "Read one clip whole.", { required: ["id"] }),
    write_clip: action("write_clip", "mutate", "Write a clip whole: with id, replace that clip (or create it with that id); without, create one with an id made from body.title. body: { title, performer?: player, rate?, length?, description?, input: [{ action, type: bool|axis1d|axis2d|axis3d, notes?: [{id,start,length,value?}], keys?: [{id,t,v,interp?}] }] }. A clip holds Input Action lanes only and has no checks.", { required: ["body"], optional: ["id"] }),
    delete_clip: action("delete_clip", "mutate", "Delete a clip. Refused while an arrangement places it.", { required: ["id"] }),
    record_start: action("record_start", "mutate", "Record the editor's player's Input Actions into a new clip, on the open editor. With clip (an id) and from/to frames, punch in: the clip plays up to from with the person's input locked out, then only that span is replaced. Stop with record_stop.", { optional: ["performer", "clip", "from", "to", "rate"] }, "Starts recording on the editor."),
    record_stop: action("record_stop", "mutate", "Stop recording and save the take: into the punched clip, or as a new clip with id or title.", { optional: ["id", "title"] }, "Stops the recording on the editor."),
    list_arrangements: action("list_arrangements", "read", "List the arrangements: id, title, level, track, check and region counts, length in frames and findings.", {}),
    read_arrangement: action("read_arrangement", "read", "Read one arrangement whole, with the clips it places, its length, findings and the health of each test it makes (itself and each region).", { required: ["id"] }),
    write_arrangement: action("write_arrangement", "mutate", `Write an arrangement whole: with id, replace (or create with that id); without, create from body.title. body: { title, level?: map package path, rate?: 60, description?, tracks: [{ id, title, performer: {kind:"player",client}, mute?, solo?, instances: [{ id, clip, at }] }], checks: [{ id, title, at, length, mode: ${MODES}, expect?: { value, op, target?, subject? }, instruction? (manual), personOnly?, timeout? (hold of length 0) }], regions: [{ id, name, start, end }] }. Frames are at the arrangement's rate. An instance's clip must exist and be for its track's performer kind, and instances on one track may not overlap.`, { required: ["body"], optional: ["id"] }),
    delete_arrangement: action("delete_arrangement", "mutate", "Delete an arrangement. Refused while a suite tests it or the queue holds it.", { required: ["id"] }),
    list_performers: action("list_performers", "read", "What a track can drive: the editor's player (client 0) and the first remote clients.", {}),
    play: action("play", "mutate", "Audition a test on the open editor to watch it while composing: test is an arrangement or <arrangement>:<region>; from starts at that frame (the playhead), skipping checks before it; loop goes back to the loop start at the end until stop_play. Checks are judged so they show, manual ones are left out, and nothing goes into the run history. Refused while the editor is in use.", { required: ["test"], optional: ["from", "loop"] }, "Starts Play In Editor on the open editor."),
    read_play: action("read_play", "read", "The audition playing, or the last one: state, frame, loop pass and each check's status.", {}, "Reads the audition's state from the editor."),
    stop_play: action("stop_play", "mutate", "Stop the audition and the Play In Editor session it started.", {}, "Stops the audition on the editor."),
    list_tests: action("list_tests", "read", "List every test (each arrangement and each region) with its health, pass rate, flakiness and last run.", {}),
    read_test: action("read_test", "read", "Read one test: its health, the checks it judges and its latest runs.", { required: ["test"], optional: ["limit"] }),
    list_suites: action("list_suites", "read", "List the suites with health, pass rate, pass-rate history, the tests they hold (nested suites' included), quarantine and flaky marks.", {}),
    read_suite: action("read_suite", "read", "Read one suite.", { required: ["id"] }),
    write_suite: action("write_suite", "mutate", "Write a suite whole: with id, replace (or create with that id); without, create from body.title. body: { title, description?, items: [{ test: \"<arrangement>[:<region>]\" } | { suite: \"<id>\" }], quarantined?: [test ids], flaky?: [test ids] }. Suites may nest, never in a loop.", { required: ["body"], optional: ["id"] }),
    delete_suite: action("delete_suite", "mutate", "Delete a suite. Refused while another suite or a schedule names it.", { required: ["id"] }),
    quarantine: action("quarantine", "mutate", "Quarantine a test in a suite: it still runs but does not count toward the suite's health. on false lets it count again.", { required: ["suite", "test"], optional: ["on"] }),
    mark_flaky: action("mark_flaky", "mutate", "Mark a test flaky in a suite, whatever its runs say. on false clears the mark. Runs that flip on one revision are flagged flaky on their own.", { required: ["suite", "test"], optional: ["on"] }),
    run: action("run", "mutate", "Queue a test, or every test of a suite, to play. Without runner, each plays on a hidden editor ue-mcp launches on its own copy of the project when one fits in memory, else on the open editor (runnerReason says why); a test with manual checks always plays on the open editor. runner editor plays under the hub's lease, starting only while nothing else holds it. Returns at once with what was queued and what was skipped and why.", { optional: ["test", "suite", "runner", "next"], choices: [{ mode: "exactlyOne", branches: [["test"], ["suite"]] }] }, "Queues runs the hub plays on the editor; the call returns at once."),
    read_queue: action("read_queue", "read", "The queue: paused, why the next editor item waits, what is playing and what is queued.", {}),
    list_runs: action("list_runs", "read", "This machine's runs, newest first, optionally for one test or a suite's tests.", { optional: ["test", "suite", "limit"] }),
    read_run: action("read_run", "read", "Read one run: its state, each check's status, frame and message, and why it ended.", { required: ["run_id"] }),
    verdict: action("verdict", "mutate", "Give a manual check's verdict, pass or fail, while its run waits for one. A check marked personOnly takes only a person's.", { required: ["run_id", "check", "verdict"], optional: ["note"] }, "Sends the verdict to the run on the editor."),
    stop_run: action("stop_run", "mutate", "Stop a run the hub is playing.", { required: ["run_id"] }, "Stops the run on the editor."),
  },
  {
    id: z.string().optional().describe("the clip, arrangement or suite id (lowercase letters, digits and dashes); record_stop: the new clip's id"),
    body: z.union([z.record(z.string(), z.unknown()), z.string()]).optional().describe("write_clip, write_arrangement, write_suite: the file's JSON, as an object or text"),
    test: z.string().optional().describe("a test id: <arrangement> or <arrangement>:<region>"),
    loop: z.boolean().optional().describe("play: loop until stop_play"),
    suite: z.string().optional().describe("a suite id"),
    on: z.boolean().optional().describe("quarantine, mark_flaky: false undoes it; true by default"),
    runner: z.enum(["editor", "headless"]).optional().describe("run: where it plays; unset, headless when a hidden editor fits, else the open editor"),
    next: z.boolean().optional().describe("run: put it at the front of the queue"),
    limit: z.number().int().optional().describe("read_test, list_runs: how many runs"),
    run_id: z.string().optional().describe("read_run, verdict, stop_run: the run"),
    check: z.string().optional().describe("verdict: the manual check's id"),
    verdict: z.enum(["pass", "fail"]).optional().describe("verdict: pass or fail"),
    note: z.string().optional().describe("verdict: what was seen"),
    performer: z.object({ kind: z.enum(["player"]), client: z.number().int().optional() }).optional().describe("record_start: who to record, {kind: player, client: 0}, the default"),
    clip: z.string().optional().describe("record_start: the clip to punch into"),
    from: z.number().optional().describe("record_start: the punch-in's first frame; play: the playhead"),
    to: z.number().optional().describe("record_start: the punch-in's last frame"),
    rate: z.number().optional().describe("record_start: frames per second of a new clip, 60 by default"),
    title: z.string().optional().describe("record_stop: the new clip's title"),
  },
  { label: { text: "playtest hub", description: "clips, arrangements, tests and suites; runs under the editor lease", keywords: ["clip", "arrangement", "track", "test", "suite", "playtest", "qa", "flaky", "quarantine"] } },
);
