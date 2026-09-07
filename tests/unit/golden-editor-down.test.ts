/**
 * The editor-down golden baseline (#817, plan item 1.10).
 *
 * Plan item 1.10 asks for the single-editor surface recorded twice, once with
 * an editor connected and once with it down, because Epic-toolset enrichment
 * legitimately changes the surface between those two states and one baseline
 * cannot tell a regression apart from a cold start. The editor-down half needs
 * nothing but Node, so it is recorded here and it gates merges. The connected
 * half needs a running editor and lives in the live tests
 * (`tests/live/golden-connected.test.ts`, `npm run test:live`), which also
 * re-verifies this one.
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
  actionOrderProblems,
  canonicalizeActionOrder,
  captureEditorDownSurface,
  firstDifference,
  permuteEnrichedActions,
  readGoldenBaseline,
  serializeGolden,
  unsortedEnrichedActions,
  writeGoldenBaseline,
  type GoldenRecording,
  type GoldenSurface,
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
  if (RECORDING) writeGoldenBaseline(serialized, "editor-down");
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

  it("reached no editor, which is the whole point of this half", () => {
    // The scenario is a claim about where the surface came from. Port 1 is
    // privileged, so nothing can be listening behind it, and the recorded log
    // is where a server that somehow reached one would have said so.
    //
    // This used to read the enrichment source out of the startup log, because
    // the surface was assembled from a live editor's toolset catalog. Nothing
    // is read from an editor at startup now, so there is no source to check:
    // what makes this half meaningful is that no bridge answered at all.
    expect(recording.log).not.toMatch(/bridge connected|editor connected/i);
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

  it("orders the enrichment-injected actions canonically, in the recording and in the file", () => {
    // Two recordings in one run share whatever order the catalog came back in,
    // so they agree even when that order is not reproducible. The property that
    // survives a restart is that the recorded order is sorted, so assert that
    // instead: it is checkable from one run, and the connected half needs no
    // second editor to catch the same class of failure.
    expect(actionOrderProblems(recording.surface)).toEqual([]);
    expect(unsortedEnrichedActions(recording.surface)).toEqual([]);

    const baseline = readGoldenBaseline("editor-down");
    expect(baseline).toBeTruthy();
    const committed = JSON.parse(baseline!) as GoldenSurface;
    expect(actionOrderProblems(committed)).toEqual([]);
    expect(unsortedEnrichedActions(committed)).toEqual([]);
  });

  it("records the same bytes from a catalog enumerated in a different order", () => {
    // What an editor restart does to the recording, without a second editor:
    // the same actions, handed back in another sequence. The baseline is only
    // stable across sessions if that lands on identical bytes.
    const permuted = permuteEnrichedActions(recording.surface, 20250817);
    expect(
      serializeGolden(permuted),
      "the permutation changed nothing, so this proves nothing",
    ).not.toBe(serialized);
    canonicalizeActionOrder(permuted);
    expect(serializeGolden(permuted)).toBe(serialized);
  });

  it("matches the committed baseline", () => {
    const baseline = readGoldenBaseline("editor-down");
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
