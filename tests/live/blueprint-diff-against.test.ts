// @live-editor headless
/**
 * blueprint(diff) against an earlier state of the same Blueprint, and
 * blueprint(focus_node) (#1487), through the shipped server.
 *
 * A Blueprint with a function graph is saved and checkpointed; a node is then
 * added inside the function graph. Diffing against the checkpoint, by
 * checkpoint:<id> and by the bare id, reports that node as added under its
 * GUID. focus_node then opens the Blueprint editor on it, and the editor reads
 * back the function graph as focused and the node as selected.
 *
 * The source control revision form is covered with a provider in
 * tests/live/source-control-provider.test.ts.
 *
 * Checkpoints are turned on through ue-mcp.local.yml, put back afterwards.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { LiveServer, resultJson, type LiveCallResult } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

type Json = Record<string, any>;
const DIR = "/Game/MCPTest/BpDiffAgainst";
const BP = `${DIR}/BP_DiffAgainst`;
const FN = "DiffAgainstFn";

const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const guid = (v: unknown): string => String(v ?? "").replace(/[^0-9a-f]/gi, "").toLowerCase();

let bridge: EditorBridge;
let projectDir: string;
let server: LiveServer | null = null;
let localYml: string;
let localYmlBefore: string | null = null;
let checkpoint = "";
let printId = "";

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function tool(category: string, action: string, args: Json = {}): Promise<Json> {
  const r: LiveCallResult = await server!.call(category, { action, args });
  expect(r.isError, `${category}.${action}: ${r.text}`).toBe(false);
  return resultJson<Json>(r);
}

/** The nodes a diff reports added in one graph. */
const addedIn = (diff: Json, graph: string): Json[] =>
  ((diff.graphs as Json[]) ?? []).filter((g) => g.graph === graph).flatMap((g) => (g.nodesAdded as Json[]) ?? []);

beforeAll(async () => {
  bridge = await getBridge();
  projectDir = (await liveTarget()).projectDir;
  localYml = path.join(projectDir, "ue-mcp.local.yml");
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });

  localYmlBefore = fs.existsSync(localYml) ? fs.readFileSync(localYml, "utf8") : null;
  if (localYmlBefore !== null && /checkpoints\s*:/.test(localYmlBefore)) throw new Error(`${localYml} already sets checkpoints`);
  const block = localYmlBefore?.includes("ue-mcp:") ? localYmlBefore.replace(/ue-mcp:\s*\n/, "ue-mcp:\n  checkpoints: on\n") : `${localYmlBefore ?? ""}\nue-mcp:\n  checkpoints: on\n`;
  fs.writeFileSync(localYml, block);

  server = await LiveServer.start({ projects: [(await liveTarget()).uproject] });
  await tool("editor", "clear_checkpoints");
  // The Blueprint before the turn, saved while the recorder is armed so the history holds it.
  await tool("blueprint", "create", { assetPath: BP, parentClass: "Actor" });
  await tool("blueprint", "create_function", { assetPath: BP, functionName: FN });
  await tool("blueprint", "compile", { assetPath: BP });
  await tool("asset", "save", { assetPath: BP });
}, 300_000);

afterAll(async () => {
  await server?.close();
  if (bridge) {
    await callBridge(bridge, "checkpoint_arm", { enabled: false });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
  if (localYml) {
    if (localYmlBefore === null) fs.rmSync(localYml, { force: true });
    else fs.writeFileSync(localYml, localYmlBefore);
  }
  if (projectDir) {
    for (const d of ["shadow.git", "shadow-pending", "shadow-large", "shadow-thumbs", "shadow-diffs"]) {
      fs.rmSync(path.join(projectDir, "Saved", "UE_MCP_Bridge", d), { recursive: true, force: true });
    }
    fs.rmSync(path.join(projectDir, "Saved", "Diff", "UE_MCP_Checkpoints"), { recursive: true, force: true });
  }
  closeLiveBridges();
}, 300_000);

describe.sequential("blueprint(diff) against a checkpoint, and focus_node (#1487)", () => {
  it("diffing against a checkpoint reports a node added after it, by GUID", async () => {
    checkpoint = String((await tool("editor", "checkpoint", { label: "before the print node" })).id);
    expect(checkpoint).toMatch(/^[0-9a-f]{40}$/);

    const added = await tool("blueprint", "add_node", {
      assetPath: BP,
      graphName: FN,
      nodeClass: "CallFunction",
      nodeParams: { functionName: "PrintString", targetClass: "/Script/Engine.KismetSystemLibrary" },
    });
    printId = String(added.nodeId);
    expect(guid(printId)).toHaveLength(32);
    await tool("asset", "save", { assetPath: BP });

    const diff = await tool("blueprint", "diff", { assetPath: BP, against: `checkpoint:${checkpoint}` });
    expect(diff.against).toMatchObject({ kind: "checkpoint", id: checkpoint, label: "before the print node" });
    expect(String(diff.from)).toBe(`${BP}@checkpoint:${checkpoint.slice(0, 10)}`);
    expect(String(diff.summary)).not.toContain("UE_MCP_Checkpoints");
    const nodes = addedIn(diff, FN);
    expect(nodes.map((n) => guid(n.id)), JSON.stringify(diff.graphs)).toContain(guid(printId));
    expect(diff.changeCount).toBeGreaterThanOrEqual(1);
    // The checkpoint's copy is removed once the diff is read.
    const left = path.join(projectDir, "Saved", "Diff", "UE_MCP_Checkpoints", "blueprint");
    expect(fs.existsSync(left) ? fs.readdirSync(left) : []).toEqual([]);
  }, 300_000);

  it("a bare checkpoint id prefix resolves to the checkpoint, and an unsaved node counts", async () => {
    const unsaved = await tool("blueprint", "add_node", { assetPath: BP, graphName: FN, nodeClass: "Branch" });
    const diff = await tool("blueprint", "diff", { assetPath: BP, against: checkpoint.slice(0, 12) });
    expect(diff.against?.kind).toBe("checkpoint");
    const ids = addedIn(diff, FN).map((n) => guid(n.id));
    expect(ids).toContain(guid(printId));
    expect(ids).toContain(guid(unsaved.nodeId));
  }, 300_000);

  it("focus_node opens the Blueprint on a node inside a function graph, focused and selected", async () => {
    const focused = await tool("blueprint", "focus_node", { assetPath: BP, nodeId: printId });
    expect(guid(focused.nodeId)).toBe(guid(printId));
    expect(focused.graphName).toBe(FN);
    expect(focused.focusedGraph).toBe(FN);
    expect(focused.graphFocused).toBe(true);
    expect(focused.selected).toBe(true);
    const open = await call("get_open_asset_editors", {});
    expect(JSON.stringify(open.editors)).toContain("BP_DiffAgainst");
  }, 300_000);

  it("focus_node refuses a node the Blueprint does not have", async () => {
    // A handler's refusal comes back as a result whose body says success false.
    const r = await server!.call("blueprint", { action: "focus_node", args: { assetPath: BP, nodeId: "00000000000000000000000000000001" } });
    expect(resultJson<Json>(r).success, r.text).toBe(false);
    expect(r.text).toContain("No node");
  }, 120_000);
});
