/**
 * The editor-down golden baseline (#817, plan item 1.10).
 *
 * Plan item 1.10 asks for the single-editor surface recorded twice, once with
 * an editor connected and once with it down, because Epic-toolset enrichment
 * legitimately changes the surface between those two states and one baseline
 * cannot tell a regression apart from a cold start. The connected half needs
 * an Unreal install and cannot run on a CI runner. The editor-down half needs
 * nothing but Node, so it is recorded here and it gates merges.
 *
 * What it guards: the `initialize` instructions and every tool in
 * `tools/list` with its full input schema. That is the entire contract a
 * client sees before it makes a single call, and it is the thing the
 * multi-editor work is required to leave byte-identical at one editor (plan
 * items 2.1, 3.1, 4.2, 5.1).
 *
 * To re-record on purpose:  npm run golden:record
 */
import * as os from "node:os";
import { beforeAll, describe, expect, it } from "vitest";
import {
  GOLDEN_EDITOR_DOWN,
  GOLDEN_SCHEMA_VERSION,
  captureEditorDownSurface,
  readGoldenBaseline,
  serializeGolden,
  writeGoldenBaseline,
  type GoldenRecording,
} from "../golden/capture.js";

/**
 * Set by `npm run golden:record`. Rewrites the baseline instead of asserting
 * against it, so an intentional surface change is a reviewable diff in the
 * committed file rather than an edit nobody sees.
 */
const RECORDING = process.env.UE_MCP_RECORD_GOLDEN === "1";

/** Recording spawns a real server process, so the budget is generous. */
const CAPTURE_TIMEOUT_MS = 180_000;

let recording: GoldenRecording;
let serialized: string;

beforeAll(async () => {
  recording = await captureEditorDownSurface();
  serialized = serializeGolden(recording.surface);
  if (RECORDING) writeGoldenBaseline(serialized);
}, CAPTURE_TIMEOUT_MS);

describe("golden baseline: single editor, editor down", () => {
  it("records a surface worth guarding", () => {
    const captured = recording.surface;
    expect(captured.scenario).toBe("editor-down");
    expect(captured.schemaVersion).toBe(GOLDEN_SCHEMA_VERSION);
    expect(captured.server.name).toBe("ue-mcp");
    // A capture that lost the instructions or the tool list would still
    // compare equal to a baseline recorded from the same broken capture.
    expect(captured.instructions.length).toBeGreaterThan(500);
    expect(captured.toolCount).toBeGreaterThan(10);
    expect(captured.tools.every((t) => t.description.length > 0)).toBe(true);
    expect(captured.tools.every((t) => t.inputSchema !== undefined)).toBe(true);
  });

  it("carries no directory from the recording machine", () => {
    // Portability is the only reason this file can be verified anywhere but
    // the machine that wrote it, so assert it rather than assume it. Both
    // separator spellings, because JSON from Windows carries either.
    const machinePaths = [recording.sandbox, recording.projectDir, recording.repoRoot, os.homedir()];
    for (const dir of machinePaths) {
      if (dir.length < 4) continue;
      for (const spelling of [dir, dir.replace(/\\/g, "/")]) {
        expect(serialized.toLowerCase()).not.toContain(spelling.toLowerCase());
      }
    }
  });

  it("is deterministic: two recordings in one run are byte-identical", async () => {
    const second = serializeGolden((await captureEditorDownSurface()).surface);
    expect(second).toBe(serialized);
  }, CAPTURE_TIMEOUT_MS);

  it("matches the committed baseline", () => {
    const baseline = readGoldenBaseline();
    if (baseline === null) {
      throw new Error(
        `No golden baseline at ${GOLDEN_EDITOR_DOWN}.\n` +
          `Record one with:  npm run golden:record\n` +
          `then review and commit the file.`,
      );
    }
    if (baseline !== serialized) {
      throw new Error(
        "The editor-down surface changed.\n\n" +
          "This test compares the `initialize` instructions and every `tools/list` input\n" +
          "schema against the baseline committed at tests/golden/editor-down.json.\n\n" +
          "If the change is intentional, re-record and commit the diff:\n\n" +
          "    npm run golden:record\n\n" +
          "and read the resulting diff before you commit it: it is the contract every\n" +
          "client sees at startup. If the change is NOT intentional, something altered\n" +
          "the advertised surface at one editor, which plan item 1.10 of #817 exists to\n" +
          "catch.\n\n" +
          firstDifference(baseline, serialized),
      );
    }
    expect(serialized).toBe(baseline);
  });
});

/** Point at the first differing line, so a large diff names its own cause. */
function firstDifference(expected: string, actual: string): string {
  const a = expected.split("\n");
  const b = actual.split("\n");
  const clip = (line: string | undefined) =>
    line === undefined ? "<end of file>" : line.length > 200 ? `${line.slice(0, 200)}...` : line;
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    if (a[i] === b[i]) continue;
    return `First difference at line ${i + 1}:\n  baseline: ${clip(a[i])}\n  recorded: ${clip(b[i])}`;
  }
  return `Files differ in length only: baseline ${a.length} lines, recorded ${b.length} lines.`;
}
