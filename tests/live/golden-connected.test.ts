/**
 * The editor-connected golden baseline (#817, plan item 1.10).
 *
 * Plan item 1.10 asked for the single-editor surface recorded twice, connected
 * and down, because the surface used to differ between those states: Unreal's
 * toolsets were read from a live editor at startup, falling back to a project
 * cache and then to a snapshot baked into the package, so one baseline could
 * not tell a regression from a cold start.
 *
 * It does not differ any more. Those tools are declared actions, so the whole
 * advertised surface is decided before any editor is contacted, and the two
 * recordings are identical apart from the label saying which is which. That
 * equality is now what this file asserts, and it is a stronger property than
 * the one it replaces: the two baselines cannot drift apart at all, rather
 * than being separately maintained and compared by eye.
 *
 * The recording still happens against a live editor, because a surface that
 * only holds while nothing is connected would be worth nothing. What is gone
 * is the assertion about WHERE the toolset catalog came from, which described
 * a startup step that no longer runs.
 *
 * To re-record on purpose:  npm run golden:record -- --connected
 */
import * as fs from "node:fs";
import * as os from "node:os";
import { beforeAll, describe, expect, it } from "vitest";
import {
  GOLDEN_EDITOR_CONNECTED,
  GOLDEN_EDITOR_DOWN,
  GOLDEN_SCHEMA_VERSION,
  actionOrderProblems,
  canonicalizeActionOrder,
  captureEditorConnectedSurface,
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
import { closeLiveBridges, liveTarget } from "./harness.js";

/** Set by `npm run golden:record -- --connected`. */
const RECORDING = process.env.UE_MCP_RECORD_GOLDEN === "1";

/** Each recording spawns a real server and pulls a live catalog. */
const CAPTURE_TIMEOUT_MS = 300_000;

const target = await liveTarget();

let recording: GoldenRecording;
let serialized: string;

beforeAll(async () => {
  recording = await captureEditorConnectedSurface(target.port, target.host);
  serialized = serializeGolden(recording.surface);
  if (RECORDING) writeGoldenBaseline(serialized, "editor-connected");
}, CAPTURE_TIMEOUT_MS);

describe("golden baseline: single editor, editor connected", () => {
  it("advertises exactly what it advertises with no editor attached", () => {
    // The point of the pair. Every action this server exposes is declared, so
    // an editor being reachable at startup cannot add one, remove one, or
    // change a schema. A difference here means something reintroduced a
    // surface that depends on the editor, which is the failure the two
    // baselines exist to catch and the reason there are two of them.
    const connected = JSON.parse(serialized) as Record<string, unknown>;
    const down = JSON.parse(fs.readFileSync(GOLDEN_EDITOR_DOWN, "utf-8")) as Record<string, unknown>;
    delete connected.scenario;
    delete down.scenario;
    expect(
      connected,
      "The connected surface differs from the editor-down one. Both are recorded from the "
      + "same declaration, so they can only diverge if something is reading the editor at "
      + "startup and changing what the server advertises.",
    ).toEqual(down);
  });

  it("records a surface worth guarding", () => {
    const captured = recording.surface;
    expect(captured.scenario).toBe("editor-connected");
    expect(captured.schemaVersion).toBe(GOLDEN_SCHEMA_VERSION);
    expect(captured.server.name).toBe("ue-mcp");
    expect(captured.instructions.length).toBeGreaterThan(500);
    expect(captured.toolCount).toBeGreaterThan(10);
    expect(captured.tools.every((t) => t.description.length > 0)).toBe(true);
    expect(captured.tools.every((t) => t.inputSchema !== undefined)).toBe(true);
  });

  it("advertises no targeting parameter, because this is one editor", () => {
    for (const tool of recording.surface.tools) {
      const schema = tool.inputSchema as { properties?: Record<string, unknown> };
      expect(Object.keys(schema.properties ?? {})).not.toContain("editor");
    }
  });

  it("carries nothing from the recording machine, including its port", () => {
    const machinePaths = [recording.sandbox, recording.projectDir, recording.repoRoot, os.homedir()];
    for (const dir of machinePaths) {
      if (dir.length < 4) continue;
      for (const spelling of [dir, dir.replace(/\\/g, "/")]) {
        expect(serialized.toLowerCase()).not.toContain(spelling.toLowerCase());
      }
    }
    // The editor rebinds its port whenever the collision walk moves it, and a
    // baseline that recorded one would churn for a reason that has nothing to
    // do with the surface.
    expect(serialized).not.toContain(String(target.port));
  });

  it("keeps every category the editor-down half advertises", () => {
    // Enrichment adds; it must never take away. A connected surface missing a
    // category the cold one has means the live path dropped something.
    const down = readGoldenBaseline("editor-down");
    expect(down, "the editor-down baseline is missing; run npm run golden:record").toBeTruthy();
    const downNames = (JSON.parse(down!) as { tools: Array<{ name: string }> }).tools.map((t) => t.name);
    const connectedNames = recording.surface.tools.map((t) => t.name);
    for (const name of downNames) {
      expect(connectedNames).toContain(name);
    }
  });

  it("is deterministic: two recordings in one run are byte-identical", async () => {
    const second = serializeGolden((await captureEditorConnectedSurface(target.port, target.host)).surface);
    expect(second).toBe(serialized);
  }, CAPTURE_TIMEOUT_MS);

  it("orders the enrichment-injected actions canonically, in the recording and in the file", () => {
    // Two recordings in one run read the same catalog in the same session, so
    // they agree on an order that a restart is free to change. Unreal's toolset
    // registry promises the set, not the sequence, which is why the recording
    // sorts the injected actions rather than trusting the enumeration.
    expect(actionOrderProblems(recording.surface)).toEqual([]);
    expect(unsortedEnrichedActions(recording.surface)).toEqual([]);

    const baseline = readGoldenBaseline("editor-connected");
    expect(baseline, "record one with npm run golden:record -- --connected").toBeTruthy();
    const committed = JSON.parse(baseline!) as GoldenSurface;
    expect(actionOrderProblems(committed)).toEqual([]);
    expect(unsortedEnrichedActions(committed)).toEqual([]);
  });

  it("records the same bytes from a catalog enumerated in a different order", () => {
    // The failure this half kept hitting: a healthy editor, restarted, handing
    // the same tools back in another sequence. Reproduced here from one editor
    // by permuting the recorded catalog and normalizing it again.
    const permuted = permuteEnrichedActions(recording.surface, 20250817);
    expect(
      serializeGolden(permuted),
      "the permutation changed nothing, so this proves nothing",
    ).not.toBe(serialized);
    canonicalizeActionOrder(permuted);
    expect(serializeGolden(permuted)).toBe(serialized);
  });

  it("matches the committed baseline", () => {
    const baseline = readGoldenBaseline("editor-connected");
    if (baseline === null) {
      throw new Error(
        `No connected baseline at ${GOLDEN_EDITOR_CONNECTED}.\n` +
          `Record one with:  npm run golden:record -- --connected\n` +
          `then review and commit the file.`,
      );
    }
    if (baseline !== serialized) {
      throw new Error(
        "The editor-connected surface changed.\n\n" +
          "This compares the `initialize` instructions and every `tools/list` input schema,\n" +
          "recorded with a live editor attached, against tests/golden/editor-connected.json.\n\n" +
          "A difference here that the editor-down baseline does not also show is a change in\n" +
          "what enrichment contributes: the editor's toolset catalog, or how it is injected.\n" +
          "If it is intentional, re-record and read the diff before committing it:\n\n" +
          "    npm run golden:record -- --connected\n\n" +
          firstDifference(baseline, serialized),
      );
    }
    expect(serialized).toBe(baseline);
  });
});

describe("golden baseline: the editor-down half, with an editor running", () => {
  it("still matches the committed baseline", async () => {
    // Recorded against a privileged port, so it is the cold surface even
    // though an editor is up. Running it here as well as in the unit tests is
    // what plan 7.3 means by "both golden baselines": the two files have to
    // hold at the same moment, or a surface change has been split across them.
    const down = await captureEditorDownSurface();
    const captured = serializeGolden(down.surface);
    const baseline = readGoldenBaseline("editor-down");
    expect(baseline).toBeTruthy();
    if (baseline !== captured) {
      throw new Error(
        "The editor-down surface changed. Re-record it with npm run golden:record.\n\n" +
          firstDifference(baseline!, captured),
      );
    }
    expect(captured).toBe(baseline);
    closeLiveBridges();
  }, CAPTURE_TIMEOUT_MS);
});
