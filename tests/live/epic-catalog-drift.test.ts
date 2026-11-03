// @live-editor headless
/**
 * The recorded toolset catalog for this editor's engine matches what its
 * registry answers now. One snapshot per engine lives in
 * tests/golden/epic-catalog/; the editor is compared against its own engine's,
 * so recording 5.9 never makes a 5.8 editor drift. A difference means Epic's
 * surface moved: re-record with `npm run epic:record`, then `npm run epic:generate`.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import { callBridge, disconnectBridge, getBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { engineKey, snapshotPath } from "../../scripts/lib/epic-catalog.mjs";

type Json = Record<string, unknown>;
interface Toolset { name: string; tools?: Array<{ name: string }> }

let bridge: EditorBridge;

async function answer(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  const body = (r.result && typeof r.result === "object" ? r.result : {}) as Json;
  expect(r.ok && body.success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body;
}

/** Sorted the way the recorder sorts, since the registry promises a set, not an order. */
function sorted(toolsets: Toolset[]): Toolset[] {
  return [...toolsets]
    .map((ts) => ({ ...ts, tools: [...(ts.tools ?? [])].sort((a, b) => a.name.localeCompare(b.name)) }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

beforeAll(async () => {
  bridge = await getBridge();
}, 60_000);

afterAll(() => {
  if (bridge) disconnectBridge();
});

describe("epic catalog drift", () => {
  it("the live registry matches the snapshot recorded for this editor's engine", async () => {
    const caps = await answer("get_bridge_capabilities");
    const engine = engineKey(caps.engineVersion);
    expect(engine, `engine version ${String(caps.engineVersion)}`).toBeTruthy();
    const file = snapshotPath(engine!);
    expect(fs.existsSync(file), `no snapshot for UE ${engine}; record one with npm run epic:record`).toBe(true);
    const snapshot = JSON.parse(fs.readFileSync(file, "utf8")) as { toolsets: Toolset[] };

    const live = sorted(((await answer("epic_list_toolsets", { includeSchemas: true })).toolsets as Toolset[]) ?? []);
    const names = (list: Toolset[]) => list.flatMap((ts) => (ts.tools ?? []).map((t) => t.name));
    // Names first, so a drift reads as which tools moved before the full diff.
    expect(names(live)).toEqual(names(snapshot.toolsets));
    expect(live).toEqual(snapshot.toolsets);
  }, 120_000);
});
