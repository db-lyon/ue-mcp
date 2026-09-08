/**
 * `blueprint(add_node)` binding a function the Blueprint declares itself
 * (#996, gap 1).
 *
 * The failure this covers is quiet, which is why it needs a live assertion.
 * add_node reported `created: true` and returned a node - just an unbound
 * stub, title "None" and no pins, that cannot be wired or called. Nothing in
 * the response said so.
 *
 * So "the call succeeded" is not the assertion here. The node has to come back
 * bound, and two readers have to agree that it is: search_call_sites has to
 * see a call site for the function, and the node's own title has to be the
 * function's name rather than "None".
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

const BP = `${TEST_PREFIX}/BP_SelfCall`;
const SELF_FUNCTION = "ComputeAimOffset";
/** Declared by AActor, so it exercises the parent-class path unchanged. */
const PARENT_FUNCTION = "K2_DestroyActor";

type CallSite = { memberName?: string; nodeTitle?: string; assetPath?: string };
type FoundNode = { nodeTitle?: string; nodeClass?: string };

let bridge: EditorBridge;

/** Call sites as search_call_sites reports them: the authoritative reader for
 *  a K2Node_CallFunction, and the one that names the member it resolved to. */
async function callSites(functionName: string): Promise<CallSite[]> {
  const found = await callBridge(bridge, "search_blueprint_call_sites", {
    functionNames: [functionName],
    directory: TEST_PREFIX,
    // Narrowing rules a Blueprint out by its Asset Registry dependencies on
    // the declaring package, which does not resolve for a function declared
    // in /Script/Engine. Off here so the reader is deterministic rather than
    // dependent on what the registry recorded for a just-created asset.
    narrowByRegistry: false,
  });
  expect(found.ok, found.error).toBe(true);
  return ((resultArray(found.result, "callSites") ?? []) as CallSite[])
    .filter((c) => (c.assetPath ?? "").includes("BP_SelfCall"));
}

async function callNodeTitles(): Promise<string[]> {
  const read = await callBridge(bridge, "search_blueprint_nodes", {
    assetPath: BP,
    nodeClasses: ["K2Node_CallFunction"],
  });
  expect(read.ok, read.error).toBe(true);
  return ((resultArray(read.result, "nodes") ?? []) as FoundNode[]).map((n) => String(n.nodeTitle ?? ""));
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  const created = await callBridge(bridge, "create_blueprint", { path: BP, parentClass: "Actor" });
  expect(created.ok, created.error).toBe(true);

  const fn = await callBridge(bridge, "create_function", { path: BP, functionName: SELF_FUNCTION });
  expect(fn.ok, fn.error).toBe(true);
});

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
});

describe("calling a function the Blueprint declares itself (#996)", () => {
  beforeAll(async () => {
    const added = await callBridge(bridge, "add_node", {
      path: BP,
      graphName: "EventGraph",
      nodeClass: "K2Node_CallFunction",
      nodeParams: { functionName: SELF_FUNCTION },
    });
    // created:true was already true when this was broken, so it is a
    // precondition here rather than the thing being asserted.
    expect(added.ok, added.error).toBe(true);
  });

  it("is a real call site, not an unbound stub", async () => {
    const sites = await callSites(SELF_FUNCTION);
    expect(sites.length).toBeGreaterThan(0);
    expect(sites[0].memberName).toBe(SELF_FUNCTION);
  });

  it("carries the function's name as its title rather than 'None'", async () => {
    // "None" is exactly what the unbound stub reported, and it is the symptom
    // the report leads with.
    const titles = await callNodeTitles();
    expect(titles).not.toContain("None");
    expect(titles.some((t) => t.includes(SELF_FUNCTION))).toBe(true);
  });
});

describe("the paths that already worked (#996)", () => {
  it("still binds a function the parent class declares", async () => {
    // The self lookup is ExcludeSuper so an inherited name keeps resolving
    // through the parent-class step. A regression here would mean the new step
    // swallowed the old one.
    const added = await callBridge(bridge, "add_node", {
      path: BP,
      graphName: "EventGraph",
      nodeClass: "K2Node_CallFunction",
      nodeParams: { functionName: PARENT_FUNCTION },
    });
    expect(added.ok, added.error).toBe(true);

    const sites = await callSites(PARENT_FUNCTION);
    expect(sites.length).toBeGreaterThan(0);
    expect(sites[0].memberName).toBe(PARENT_FUNCTION);
  });
});
