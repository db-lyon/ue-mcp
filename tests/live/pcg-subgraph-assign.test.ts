// @live-editor headless
/**
 * `pcg(set_subgraph)` and `pcg(set_subgraph_parameters)`, against a real editor (#1253).
 *
 * A reflection write to SubgraphInstance.Graph gives a Subgraph node its pins but no parameter bag, so every
 * Get Graph Parameter inside the subgraph fails at execution. These assert the bag exists after set_subgraph and
 * that overrides land in it. The subgraph is Epic's SG_CopyPointsWithHierarchy, which ships with the PCG plugin
 * and declares the bool parameter IgnoreTargetPointXYRotations.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOST = `${TEST_PREFIX}/PCG_SubgraphHost`;
const SUBGRAPH = "/PCG/Utilities/Assemblies/ActorTagger/Debug/Graphs/SG_CopyPointsWithHierarchy";
const PARAM = "IgnoreTargetPointXYRotations";

type Param = { name: string; value: unknown; overridden: boolean };
type Shape = { inputPins?: string[]; parameters?: Param[]; previous?: Record<string, unknown> };

let bridge: EditorBridge;
let nodeName = "";

const param = (result: unknown): Param | undefined =>
  ((result as Shape).parameters ?? []).find((p) => p.name === PARAM);

/** A handler refusal arrives as a result with success:false; only a transport failure sets ok:false. */
const refusal = (r: { ok: boolean; error?: string; result?: unknown }): string | undefined => {
  const body = r.result as { success?: boolean; error?: string } | undefined;
  if (!r.ok) return r.error;
  return body?.success === false ? body.error ?? "" : undefined;
};

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: HOST, force: true });
  const created = await callBridge(bridge, "create_pcg_graph", { name: "PCG_SubgraphHost", packagePath: TEST_PREFIX });
  expect(created.ok, created.error).toBe(true);
  const added = await callBridge(bridge, "add_pcg_node", { assetPath: HOST, nodeType: "PCGSubgraphSettings" });
  expect(added.ok, added.error).toBe(true);
  nodeName = (added.result as { nodeName: string }).nodeName;
});

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: HOST, force: true });
    disconnectBridge();
  }
});

describe("assigning a subgraph builds its parameters (#1253)", () => {
  it("reports the subgraph's pins and its parameter bag", async () => {
    const set = await callBridge(bridge, "set_pcg_subgraph", { assetPath: HOST, nodeName, subgraphPath: SUBGRAPH });
    expect(set.ok, set.error).toBe(true);
    expect((set.result as Shape).inputPins).toContain("CopyPoints Source");
    expect(param(set.result)?.overridden).toBe(false);
  });

  it("overrides a parameter and reports the previous state", async () => {
    const set = await callBridge(bridge, "set_pcg_subgraph_parameters", { assetPath: HOST, nodeName, parameters: { [PARAM]: true } });
    expect(set.ok, set.error).toBe(true);
    expect(param(set.result)).toMatchObject({ value: true, overridden: true });
    expect((set.result as Shape).previous?.[PARAM]).toBeNull();
  });

  it("refuses an unknown parameter with nothing changed", async () => {
    const bad = await callBridge(bridge, "set_pcg_subgraph_parameters", { assetPath: HOST, nodeName, parameters: { [PARAM]: false, NotAParameter: 1 } });
    expect(refusal(bad)).toContain("NotAParameter");
    const read = await callBridge(bridge, "set_pcg_subgraph", { assetPath: HOST, nodeName, subgraphPath: SUBGRAPH });
    expect(param(read.result)).toMatchObject({ value: true, overridden: true });
  });

  it("clears an override with null", async () => {
    const cleared = await callBridge(bridge, "set_pcg_subgraph_parameters", { assetPath: HOST, nodeName, parameters: { [PARAM]: null } });
    expect(cleared.ok, cleared.error).toBe(true);
    expect(param(cleared.result)?.overridden).toBe(false);
  });

  it("clears the subgraph with an empty path", async () => {
    const cleared = await callBridge(bridge, "set_pcg_subgraph", { assetPath: HOST, nodeName, subgraphPath: "" });
    expect(cleared.ok, cleared.error).toBe(true);
    expect((cleared.result as Shape).parameters ?? []).toHaveLength(0);
  });
});
