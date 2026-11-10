/**
 * editor(summarize_csv_profile): the server-side reader for CSV profiler
 * captures, run against a fixture in the format FCsvStreamWriter writes.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { gzipSync } from "node:zlib";
import { afterEach, describe, expect, it } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { editorTool } from "../../src/tools/editor.js";
import {
  newestCsvProfile,
  parseCsvProfile,
  summarizeCsvProfile,
  summarizeCsvProfileAction,
} from "../../src/tools/editor-csv-profile.js";

const FIXTURE = path.join(__dirname, "..", "fixtures", "csv-profile", "sample.csv");
const sample = (): string => fs.readFileSync(FIXTURE, "utf8");

const temps: string[] = [];
function tempProject(): { dir: string; csvDir: string; ctx: ToolContext } {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "csv-profile-"));
  temps.push(dir);
  const csvDir = path.join(dir, "Saved", "Profiling", "CSV");
  fs.mkdirSync(csvDir, { recursive: true });
  const ctx = { project: { projectPath: path.join(dir, "Game.uproject") } } as unknown as ToolContext;
  return { dir, csvDir, ctx };
}

afterEach(() => {
  while (temps.length) fs.rmSync(temps.pop()!, { recursive: true, force: true });
});

describe("parseCsvProfile", () => {
  it("separates the header, the frame rows, the repeated header and the metadata row", () => {
    const parsed = parseCsvProfile(sample());
    expect(parsed.header[0]).toBe("EVENTS");
    expect(parsed.header).toContain("FrameTime");
    expect(parsed.rows).toHaveLength(5);
    expect(parsed.complete).toBe(true);
    expect(parsed.metadata.HasHeaderRowAtEnd).toBe("1");
    expect(parsed.metadata.platform).toBe("Windows");
    expect(parsed.metadata.config).toBe("Development");
  });

  it("keeps the commas inside the Commandline value, which the profiler writes last for that reason", () => {
    expect(parseCsvProfile(sample()).metadata.Commandline).toBe("-game -csvExec=stat unit,stat fps");
  });

  it("reads a capture with no closing rows as incomplete and keeps every frame", () => {
    const lines = sample().trim().split(/\r?\n/);
    const parsed = parseCsvProfile(lines.slice(0, -2).join("\n"));
    expect(parsed.complete).toBe(false);
    expect(parsed.rows).toHaveLength(5);
    expect(parsed.metadata).toEqual({});
  });

  it("refuses an empty file", () => {
    expect(() => parseCsvProfile("\n\n")).toThrow(/empty/);
  });
});

describe("summarizeCsvProfile", () => {
  it("computes average, nearest-rank percentiles and the frame each peak happened on", () => {
    const summary = summarizeCsvProfile(sample());
    expect(summary.frameCount).toBe(5);
    expect(summary.statCount).toBe(6);
    const frame = summary.stats.find((s) => s.name === "FrameTime")!;
    expect(frame.samples).toBe(5);
    expect(frame.avg).toBeCloseTo(40.58, 3);
    expect(frame.min).toBe(16.5);
    expect(frame.max).toBe(120);
    expect(frame.p50).toBe(16.8);
    expect(frame.p95).toBe(120);
    expect(frame.peakFrame).toBe(2);
  });

  it("counts only the frames that carry a value", () => {
    const ticks = summarizeCsvProfile(sample()).stats.find((s) => s.name === "Ticks/Actors")!;
    expect(ticks.samples).toBe(4);
    expect(ticks.avg).toBe(60.5);
  });

  it("orders by average by default and by the requested key otherwise", () => {
    expect(summarizeCsvProfile(sample()).stats[0].name).toBe("Ticks/Actors");
    expect(summarizeCsvProfile(sample(), { sortBy: "max" }).stats[0].name).toBe("FrameTime");
    expect(summarizeCsvProfile(sample(), { sortBy: "name" }).stats.map((s) => s.name)[0]).toBe("Exclusive/GameThread/Physics");
  });

  it("filters stats by case-insensitive substring and bounds the list with totals reported", () => {
    const filtered = summarizeCsvProfile(sample(), { stats: ["thread"] });
    expect(filtered.stats.map((s) => s.name).sort()).toEqual(
      ["Exclusive/GameThread/Physics", "GameThreadTime", "RenderThreadTime"].sort(),
    );
    expect(filtered.matchedStatCount).toBe(3);

    const capped = summarizeCsvProfile(sample(), { maxStats: 2 });
    expect(capped.stats).toHaveLength(2);
    expect(capped.statsTruncated).toBe(true);
    expect(capped.statCount).toBe(6);
  });

  it("reports frame-time hitches over the threshold and the average frame rate", () => {
    const at50 = summarizeCsvProfile(sample()).frameTime!;
    expect(at50.hitchThresholdMs).toBe(50);
    expect(at50.hitchCount).toBe(1);
    expect(at50.hitchFrames).toEqual([2]);
    expect(at50.avgFps).toBeCloseTo(1000 / 40.58, 2);

    const at30 = summarizeCsvProfile(sample(), { hitchThresholdMs: 30 }).frameTime!;
    expect(at30.hitchFrames).toEqual([2, 4]);
  });

  it("splits the EVENTS cell on semicolons and keeps each event's frame", () => {
    const events = summarizeCsvProfile(sample()).events;
    expect(events.count).toBe(2);
    expect(events.list).toEqual([{ frame: 2, text: "Hitch Start" }, { frame: 2, text: "LoadMap" }]);
    expect(events.truncated).toBe(false);
    expect(summarizeCsvProfile(sample(), { maxEvents: 1 }).events).toMatchObject({ count: 2, truncated: true });
  });

  it("does not treat EVENTS as a stat wherever the column sits", () => {
    const lines = sample().split(/\r?\n/).map((l) => {
      if (l.startsWith("[")) return l;
      const c = l.split(",");
      return [...c.slice(1), c[0]].join(",");
    });
    const summary = summarizeCsvProfile(lines.join("\n"));
    expect(summary.stats.map((s) => s.name)).not.toContain("EVENTS");
    expect(summary.events.count).toBe(2);
    expect(summary.frameTime?.max).toBe(120);
  });
});

describe("editor(summarize_csv_profile)", () => {
  it("is a read handler on the editor tool with its parameters on the schema", () => {
    const spec = editorTool.actions.summarize_csv_profile;
    expect(spec.kind).toBe("handler");
    expect(spec.effect).toBe("read");
    expect(editorTool.schema.csvPath.safeParse("a.csv").success).toBe(true);
    expect(editorTool.schema.sortBy.safeParse("p95").success).toBe(true);
    expect(editorTool.schema.sortBy.safeParse("median").success).toBe(false);
    expect(editorTool.schema.stats.safeParse(["Frame"]).success).toBe(true);
  });

  it("defaults to the newest capture in Saved/Profiling/CSV", async () => {
    const { csvDir, ctx } = tempProject();
    const older = path.join(csvDir, "older.csv");
    const newer = path.join(csvDir, "newer.csv");
    fs.writeFileSync(older, "EVENTS,FrameTime\n,10\n");
    fs.writeFileSync(newer, sample());
    const past = new Date(Date.now() - 60_000);
    fs.utimesSync(older, past, past);
    expect(newestCsvProfile(csvDir)).toBe(newer);

    const result = await summarizeCsvProfileAction(ctx, {});
    expect(result.success).toBe(true);
    expect(result.csvPath).toBe(newer);
    expect(result.frameCount).toBe(5);
    expect(result.csvBytes).toBe(fs.statSync(newer).size);
  });

  it("resolves a bare file name against Saved/Profiling/CSV and reads .csv.gz", async () => {
    const { csvDir, ctx } = tempProject();
    fs.writeFileSync(path.join(csvDir, "packed.csv.gz"), gzipSync(Buffer.from(sample())));
    const result = await summarizeCsvProfileAction(ctx, { csvPath: "packed.csv.gz", stats: "FrameTime" });
    expect(result.success).toBe(true);
    expect(result.frameCount).toBe(5);
    expect((result.stats as Array<{ name: string }>).map((s) => s.name)).toEqual(["FrameTime"]);
  });

  it("warns when the capture had not finished", async () => {
    const { csvDir, ctx } = tempProject();
    const lines = sample().trim().split(/\r?\n/);
    fs.writeFileSync(path.join(csvDir, "running.csv"), lines.slice(0, -2).join("\n"));
    const result = await summarizeCsvProfileAction(ctx, { csvPath: "running.csv" });
    expect(result.complete).toBe(false);
    expect(String(result.warning)).toMatch(/stop_csv_profile/);
  });

  it("fails with the folder it searched when there is nothing to read, and on a bad sortBy", async () => {
    const { csvDir, ctx } = tempProject();
    const none = await summarizeCsvProfileAction(ctx, {});
    expect(none.success).toBe(false);
    expect(String(none.error)).toContain(csvDir);

    fs.writeFileSync(path.join(csvDir, "a.csv"), sample());
    const bad = await summarizeCsvProfileAction(ctx, { sortBy: "median" });
    expect(bad.success).toBe(false);
    expect(String(bad.error)).toMatch(/sortBy/);

    const missing = await summarizeCsvProfileAction(ctx, { csvPath: path.join(csvDir, "nope.csv") });
    expect(missing).toMatchObject({ success: false });
  });
});
