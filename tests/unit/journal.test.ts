/**
 * The workflow journal (V16).
 *
 * Two properties are worth holding here, and they are the two that make the
 * record trustworthy rather than merely present:
 *
 *   1. A run folds back exactly as it was written, across a process boundary
 *      the file has to survive. Every test writes through the public API and
 *      reads through a fresh fold, never through in-memory state.
 *   2. Every write is idempotent and says whether it changed anything. A
 *      retried call after a timeout must not fork one run into two, attach an
 *      artifact twice, or overwrite the first verdict on a run.
 *
 * The action layer is exercised through `flowTool.handler`, which is the
 * dispatch route the server takes for the flow tool, so a test that passes
 * here is testing the path production uses.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { subscribeFlowEvents } from "../../src/flow/events.js";
import { FlowConfigSchema, type FlowConfig } from "../../src/flow/schema.js";
import { categoryTool, type ToolContext, type ToolDef } from "../../src/types.js";
import {
  activeRun,
  addArtifact,
  addNote,
  deleteRuns,
  endRun,
  filterRuns,
  journalEnabled,
  journalFile,
  parseSince,
  readRun,
  readRuns,
  startRun,
} from "../../src/journal.js";

let tmp: string;
let projectDir: string;
let tool: ToolDef;

const EMPTY_CONFIG = { flows: {}, tasks: {} } as unknown as FlowConfig;

function makeContext(dir: string | null): ToolContext {
  return {
    bridge: { isConnected: false } as unknown as ToolContext["bridge"],
    project: { projectDir: dir } as unknown as ToolContext["project"],
  };
}

/** Dispatch the way the server does: through the tool's own handler. */
async function call(params: Record<string, unknown>, dir: string | null = projectDir): Promise<Record<string, unknown>> {
  return (await tool.handler(makeContext(dir), params)) as Record<string, unknown>;
}

beforeEach(() => {
  tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-journal-"));
  projectDir = path.join(tmp, "project");
  fs.mkdirSync(projectDir, { recursive: true });
  process.env.UE_MCP_JOURNAL_DIR = path.join(tmp, "journal");
  delete process.env.UE_MCP_JOURNAL;
  tool = createFlowTool({} as never, () => EMPTY_CONFIG);
});

afterEach(() => {
  delete process.env.UE_MCP_JOURNAL_DIR;
  delete process.env.UE_MCP_JOURNAL;
  fs.rmSync(tmp, { recursive: true, force: true });
});

describe("journal storage", () => {
  it("keeps one file per project root, under the user directory and not the project", () => {
    const a = journalFile(path.join(tmp, "alpha"));
    const b = journalFile(path.join(tmp, "beta"));
    expect(a).not.toEqual(b);
    expect(path.dirname(a)).toEqual(path.join(tmp, "journal"));
    // The whole point of the user-scoped location: nothing lands in the
    // project that would show up in a colleague's diff.
    expect(a.startsWith(projectDir)).toBe(false);
  });

  it("gives two projects of the same name different files", () => {
    const one = journalFile(path.join(tmp, "one", "ue_mcp"));
    const two = journalFile(path.join(tmp, "two", "ue_mcp"));
    expect(one).not.toEqual(two);
    expect(path.basename(one).startsWith("ue_mcp-")).toBe(true);
  });

  it("folds notes and artifacts back onto the run that owns them", () => {
    const file = journalFile(projectDir);
    const { run } = startRun(file, { title: "shrine lighting", tags: ["lighting"] });
    addNote(file, run.runId, "swapped the key light for a rect light");
    addArtifact(file, run.runId, { path: "/Game/Materials/PBR/M_Rock", kind: "asset" });
    endRun(file, run.runId, { status: "completed", summary: "done" });

    const [folded] = readRuns(file);
    expect(folded.runId).toEqual(run.runId);
    expect(folded.status).toEqual("completed");
    expect(folded.tags).toEqual(["lighting"]);
    expect(folded.notes.map((n) => n.text)).toEqual(["swapped the key light for a rect light"]);
    expect(folded.artifacts.map((a) => a.path)).toEqual(["/Game/Materials/PBR/M_Rock"]);
    expect(folded.summary).toEqual("done");
  });

  it("survives a truncated final line rather than losing the history before it", () => {
    const file = journalFile(projectDir);
    const { run } = startRun(file, { title: "interrupted" });
    addNote(file, run.runId, "first");
    fs.appendFileSync(file, '{"v":1,"e":"note","t":1,"id":"j-x","te');

    const runs = readRuns(file);
    expect(runs).toHaveLength(1);
    expect(runs[0].notes.map((n) => n.text)).toEqual(["first"]);
  });

  it("reports a run that never closed as active rather than dropping it", () => {
    const file = journalFile(projectDir);
    startRun(file, { title: "killed mid-run" });
    expect(readRuns(file)[0].status).toEqual("active");
    expect(activeRun(file)?.title).toEqual("killed mid-run");
  });
});

describe("journal idempotency", () => {
  it("does not fork a run when the same runId is started twice", () => {
    const file = journalFile(projectDir);
    const first = startRun(file, { title: "retryable", runId: "fixed-1" });
    const second = startRun(file, { title: "retryable", runId: "fixed-1" });
    expect(first.existed).toBe(false);
    expect(second.existed).toBe(true);
    expect(readRuns(file)).toHaveLength(1);
  });

  it("does not attach the same artifact path twice to one run", () => {
    const file = journalFile(projectDir);
    const { run } = startRun(file, { title: "t" });
    const first = addArtifact(file, run.runId, { path: "/Game/Levels/Shrine" });
    const second = addArtifact(file, run.runId, { path: "/Game/Levels/Shrine" });
    expect(first.existed).toBe(false);
    expect(second.existed).toBe(true);
    expect(readRun(file, run.runId)?.artifacts).toHaveLength(1);
  });

  it("keeps the first verdict when a run is ended twice", () => {
    const file = journalFile(projectDir);
    const { run } = startRun(file, { title: "t" });
    endRun(file, run.runId, { status: "completed", summary: "first" });
    const again = endRun(file, run.runId, { status: "failed", summary: "second" });
    expect(again?.existed).toBe(true);
    const folded = readRun(file, run.runId);
    expect(folded?.status).toEqual("completed");
    expect(folded?.summary).toEqual("first");
  });
});

describe("journal deletion", () => {
  it("removes a run's notes and artifacts along with it, and reports the counts", () => {
    const file = journalFile(projectDir);
    const keep = startRun(file, { title: "keep" }).run;
    const drop = startRun(file, { title: "drop" }).run;
    addNote(file, drop.runId, "gone");
    addArtifact(file, drop.runId, { path: "/Game/x" });

    const result = deleteRuns(file, { runId: drop.runId });
    expect(result.deletedRuns).toEqual([drop.runId]);
    expect(result.deletedRecords).toEqual(3);
    expect(result.remainingRuns).toEqual(1);
    expect(readRuns(file).map((r) => r.runId)).toEqual([keep.runId]);
  });

  it("reports a delete that matched nothing as a delete that matched nothing", () => {
    const file = journalFile(projectDir);
    startRun(file, { title: "t" });
    const result = deleteRuns(file, { runId: "not-here" });
    expect(result.deletedRuns).toEqual([]);
    expect(result.remainingRuns).toEqual(1);
  });

  it("removes the file entirely once nothing is left in it", () => {
    const file = journalFile(projectDir);
    startRun(file, { title: "t" });
    deleteRuns(file, { all: true });
    expect(fs.existsSync(file)).toBe(false);
    expect(readRuns(file)).toEqual([]);
  });
});

describe("journal filtering", () => {
  it("applies every filter together", () => {
    const file = journalFile(projectDir);
    const a = startRun(file, { title: "sculpt the valley", tags: ["terrain"] }).run;
    endRun(file, a.runId, { status: "completed" });
    const b = startRun(file, { title: "paint the rock layer", tags: ["terrain"], flowName: "paint" }).run;
    startRun(file, { title: "unrelated", tags: ["audio"] });

    const runs = readRuns(file);
    expect(filterRuns(runs, { status: "active" }).map((r) => r.title))
      .toEqual(["unrelated", "paint the rock layer"]);
    expect(filterRuns(runs, { tag: "terrain" })).toHaveLength(2);
    expect(filterRuns(runs, { flowName: "paint" }).map((r) => r.runId)).toEqual([b.runId]);
    expect(filterRuns(runs, { contains: "VALLEY" }).map((r) => r.runId)).toEqual([a.runId]);
    expect(filterRuns(runs, { limit: 1 })).toHaveLength(1);
    expect(filterRuns(runs, { limit: 0 })).toHaveLength(3);
  });

  it("searches note text as well as titles", () => {
    const file = journalFile(projectDir);
    const { run } = startRun(file, { title: "opaque title" });
    addNote(file, run.runId, "the landscape material needed an RVT");
    expect(filterRuns(readRuns(file), { contains: "rvt" }).map((r) => r.runId)).toEqual([run.runId]);
  });

  it("reads every accepted spelling of since, and refuses the rest", () => {
    const now = 1_700_000_000_000;
    expect(parseSince(now, now)).toEqual(now);
    expect(parseSince("2h", now)).toEqual(now - 7_200_000);
    expect(parseSince("30m", now)).toEqual(now - 1_800_000);
    expect(parseSince("7d", now)).toEqual(now - 604_800_000);
    expect(parseSince("2026-08-29T00:00:00Z", now)).toEqual(Date.parse("2026-08-29T00:00:00Z"));
    expect(parseSince("last tuesday", now)).toBeUndefined();
  });
});

describe("flow(journal_*) actions", () => {
  it("starts, notes, attaches and finishes a run through the dispatch route", async () => {
    const started = await call({ action: "journal_start", title: "rebuild the shrine", tags: ["demo"] });
    expect(started.started).toBe(true);
    const runId = (started.run as Record<string, unknown>).runId as string;
    expect(started.undo).toContain("journal_delete");

    const noted = await call({ action: "journal_note", text: "swapped the emissive" });
    expect(noted.runId).toEqual(runId);
    expect(noted.resolvedFrom).toEqual("the open run");

    const attached = await call({
      action: "journal_attach",
      artifactPath: "/Game/Materials/PBR/M_Shrine",
      artifactKind: "asset",
    });
    expect(attached.attached).toBe(true);

    const finished = await call({ action: "journal_finish", summary: "looks right in PIE" });
    expect(finished.finished).toBe(true);

    const read = await call({ action: "journal_get", runId });
    const run = read.run as Record<string, unknown>;
    expect(run.status).toEqual("completed");
    expect(run.summary).toEqual("looks right in PIE");
    expect((run.notes as unknown[])).toHaveLength(1);
    expect((run.artifacts as unknown[])).toHaveLength(1);
  });

  it("reports a repeat start as existed rather than opening a second run", async () => {
    await call({ action: "journal_start", title: "t", runId: "retry-me" });
    const again = await call({ action: "journal_start", title: "t", runId: "retry-me" });
    expect(again.started).toBe(false);
    expect(again.existed).toBe(true);
    const listed = await call({ action: "journal_list" });
    expect(listed.totalRuns).toEqual(1);
  });

  it("names the fix when there is no open run to write to", async () => {
    await expect(call({ action: "journal_note", text: "orphan" })).rejects.toThrow(
      /No open journal run to note.*journal_start/s,
    );
  });

  it("lists the real ids when a named run does not exist", async () => {
    const started = await call({ action: "journal_start", title: "real" });
    const runId = (started.run as Record<string, unknown>).runId as string;
    await expect(call({ action: "journal_get", runId: "made-up" })).rejects.toThrow(
      new RegExp(`No journal run 'made-up'.*${runId}`, "s"),
    );
  });

  it("names the accepted values when a status or a since cannot be read", async () => {
    await expect(call({ action: "journal_list", status: "finished" })).rejects.toThrow(
      /must be one of active, completed, failed, cancelled/,
    );
    await expect(call({ action: "journal_list", since: "yesterday-ish" })).rejects.toThrow(
      /epoch milliseconds.*ISO date.*relative age/s,
    );
    await call({ action: "journal_start", title: "t" });
    await expect(call({ action: "journal_finish", status: "cancelled" })).rejects.toThrow(
      /journal_cancel/,
    );
  });

  it("refuses a delete with no target, and one with two", async () => {
    await expect(call({ action: "journal_delete" })).rejects.toThrow(/needs a target/);
    await expect(call({ action: "journal_delete", runId: "x", all: true })).rejects.toThrow(
      /never both/,
    );
  });

  it("cancels a run with its reason, and keeps that verdict", async () => {
    const started = await call({ action: "journal_start", title: "abandoned" });
    const runId = (started.run as Record<string, unknown>).runId as string;
    const cancelled = await call({ action: "journal_cancel", reason: "wrong approach" });
    expect(cancelled.cancelled).toBe(true);
    expect((cancelled.run as Record<string, unknown>).reason).toEqual("wrong approach");
    const again = await call({ action: "journal_finish", runId });
    expect(again.existed).toBe(true);
    expect((again.run as Record<string, unknown>).status).toEqual("cancelled");
  });

  it("reports where the journal lives and whether it is recording", async () => {
    const status = await call({ action: "journal_status" });
    expect(status.enabled).toBe(true);
    expect(status.journalFile).toEqual(journalFile(projectDir));
    expect(status.totalRuns).toEqual(0);
    expect(status.autoRecording).toContain("flow");
  });

  it("keeps two projects' journals apart", async () => {
    const other = path.join(tmp, "other-project");
    fs.mkdirSync(other, { recursive: true });
    await call({ action: "journal_start", title: "in project A" });
    const listed = await call({ action: "journal_list" }, other);
    expect(listed.totalRuns).toEqual(0);
  });

  it("refuses to write, rather than silently dropping, while recording is off", async () => {
    process.env.UE_MCP_JOURNAL = "0";
    expect(journalEnabled()).toBe(false);
    await expect(call({ action: "journal_start", title: "t" })).rejects.toThrow(
      /UE_MCP_JOURNAL is set to '0'/,
    );
    // Reading still answers, so a user can see what was recorded before.
    const status = await call({ action: "journal_status" });
    expect(status.enabled).toBe(false);
  });

  it("suggests the closest action on a typo", async () => {
    await expect(call({ action: "journal_lst" })).rejects.toThrow(/Did you mean.*journal_list/s);
  });

  it("refuses rather than sharing one journal between projectless sessions", async () => {
    await expect(call({ action: "journal_status" }, null)).rejects.toThrow(
      /no project directory.*keyed by project root/s,
    );
  });
});

describe("flow runs journal themselves", () => {
  /** A flow whose one step is a local handler, so nothing needs an editor. */
  function probeSetup(): { tool: ToolDef; ctx: ToolContext } {
    const probe = categoryTool(
      "probe",
      "Test-only category.",
      {
        ping: { kind: "handler", effect: "read", description: "Answer. Params: none", handler: async () => ({ pong: true }) },
        boom: {
          description: "Fail. Params: none",
          handler: async () => {
            throw new Error("step exploded");
          },
        },
      },
      undefined,
      {},
    );
    const config = FlowConfigSchema.parse({
      "ue-mcp": { version: 1 },
      flows: {
        good: { description: "one good step", steps: { "1": { task: "probe.ping" } } },
        bad: { description: "one failing step", steps: { "1": { task: "probe.boom" } } },
      },
    });
    return {
      tool: createFlowTool(buildFlowRegistry([probe]), () => config),
      ctx: makeContext(projectDir),
    };
  }

  it("records a successful run with its steps, without being asked", async () => {
    const { tool: flowTool, ctx } = probeSetup();
    const result = (await flowTool.handler(ctx, { action: "run", flowName: "good" })) as Record<string, unknown>;
    const runId = result.runId as string;

    const run = readRun(journalFile(projectDir), runId);
    expect(run).toBeDefined();
    expect(run!.status).toEqual("completed");
    expect(run!.flowName).toEqual("good");
    expect(run!.tags).toEqual(["flow"]);
    expect(run!.steps?.map((s) => s.name)).toEqual(["probe.ping"]);
    expect(run!.steps?.[0].success).toBe(true);
  });

  it("records a failed run as failed, naming the step that failed", async () => {
    const { tool: flowTool, ctx } = probeSetup();
    const result = (await flowTool.handler(ctx, { action: "run", flowName: "bad" })) as Record<string, unknown>;
    const run = readRun(journalFile(projectDir), result.runId as string);
    expect(run!.status).toEqual("failed");
    expect(run!.summary).toContain("probe.boom");
    expect(run!.steps?.[0].error).toContain("step exploded");
  });

  it("shares the runId the flow events carry, so the two describe one run", async () => {
    const { tool: flowTool, ctx } = probeSetup();
    const seen: string[] = [];
    const unsubscribe = subscribeFlowEvents((e) => {
      if (e.type === "run_completed") seen.push(e.runId);
    });
    const result = (await flowTool.handler(ctx, { action: "run", flowName: "good" })) as Record<string, unknown>;
    unsubscribe();
    expect(seen).toEqual([result.runId]);
    expect(readRun(journalFile(projectDir), result.runId as string)).toBeDefined();
  });

  it("does not journal a plan, which runs nothing", async () => {
    const { tool: flowTool, ctx } = probeSetup();
    await flowTool.handler(ctx, { action: "plan", flowName: "good" });
    expect(readRuns(journalFile(projectDir))).toEqual([]);
  });

  it("records nothing while recording is off, and still runs the flow", async () => {
    process.env.UE_MCP_JOURNAL = "0";
    const { tool: flowTool, ctx } = probeSetup();
    const result = (await flowTool.handler(ctx, { action: "run", flowName: "good" })) as Record<string, unknown>;
    expect(result.success).toBe(true);
    expect(readRuns(journalFile(projectDir))).toEqual([]);
  });
});
