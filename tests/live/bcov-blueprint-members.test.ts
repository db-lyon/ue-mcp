// @live-editor headless
/**
 * Blueprint member authoring read back through the listing actions: variables
 * and their metadata, functions, locals, macros, dispatchers, interfaces,
 * timeline tracks and overridable functions.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Blueprint";
const BP = `${DIR}/BP_BCovMembers`;
const BPI = `${DIR}/BPI_BCovMembers`;

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (j: Json, key: string): Row[] => (Array.isArray(j[key]) ? (j[key] as Row[]) : []);
const names = (j: Json, key: string): string[] => rows(j, key).map((r) => String(r.name ?? ""));

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function variables(): Promise<Row[]> {
  return rows(await call("list_blueprint_variables", { assetPath: BP }), "variables");
}

async function functionRows(): Promise<Row[]> {
  return rows(await call("list_blueprint_functions", { assetPath: BP, limit: 2000 }), "functions");
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: BPI, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: BPI, force: true });
    disconnectBridge();
  }
});

describe("blueprint member variables", () => {
  it("add, list, set properties, default, metadata, rename and delete a variable", async () => {
    await call("add_variable", { assetPath: BP, name: "BCovHealth", varType: "float", tooltip: "Initial tip" });
    let vars = await variables();
    const added = vars.find((v) => v.name === "BCovHealth");
    expect(added, JSON.stringify(vars)).toBeDefined();
    // Blueprint "float" is a real pin with double precision in UE5.
    expect(added?.type).toBe("real");
    expect(added?.typeSpec).toBe("double");

    await call("set_variable_properties", {
      assetPath: BP, name: "BCovHealth", tooltip: "Hit points", instanceEditable: true, exposeOnSpawn: true,
    });
    vars = await variables();
    const props = vars.find((v) => v.name === "BCovHealth");
    expect(props?.tooltip).toBe("Hit points");
    expect(props?.instanceEditable).toBe(true);
    expect(props?.exposeOnSpawn).toBe(true);

    await call("set_variable_default", { assetPath: BP, name: "BCovHealth", value: "250" });
    const def = await call("get_blueprint_variable_default", { assetPath: BP, name: "BCovHealth" });
    expect(Number(def.valueText)).toBe(250);

    await call("set_blueprint_variable_metadata", { assetPath: BP, name: "BCovHealth", metadata: { ClampMin: "0", UIMax: "500" } });
    const meta = await call("get_blueprint_variable_metadata", { assetPath: BP, name: "BCovHealth" });
    const metaObj = (meta.metadata ?? {}) as Record<string, string>;
    expect(metaObj.ClampMin).toBe("0");
    expect(metaObj.UIMax).toBe("500");
    expect(meta.scope).toBe("member");

    const renamed = await call("rename_blueprint_variable", { assetPath: BP, oldName: "BCovHealth", newName: "BCovHitPoints" });
    expect(renamed.newName).toBe("BCovHitPoints");
    vars = await variables();
    expect(vars.map((v) => v.name)).toContain("BCovHitPoints");
    expect(vars.map((v) => v.name)).not.toContain("BCovHealth");

    const deleted = await call("delete_variable", { assetPath: BP, name: "BCovHitPoints" });
    expect(deleted.deleted).toBe(true);
    expect((await variables()).map((v) => v.name)).not.toContain("BCovHitPoints");
  }, 120_000);
});

describe("blueprint functions and locals", () => {
  it("set_function_properties and list_graph_parameters read back a function's flags and signature", async () => {
    await call("create_function", { assetPath: BP, functionName: "BCovCompute" });
    await call("add_function_parameter", { assetPath: BP, functionName: "BCovCompute", parameterName: "Speed", parameterType: "float" });
    const sig = await call("list_graph_parameters", { assetPath: BP, functionName: "BCovCompute" });
    expect(names(sig, "inputs")).toContain("Speed");
    expect(sig.kind).toBe("function");

    const set = await call("set_function_properties", {
      assetPath: BP, functionName: "BCovCompute", pure: true, category: "BCov", tooltip: "Computes",
    });
    expect(set.pure).toBe(true);
    expect(set.category).toBe("BCov");
    // Writing the same values again is a no-op only if they were stored.
    const again = await call("set_function_properties", {
      assetPath: BP, functionName: "BCovCompute", pure: true, category: "BCov", tooltip: "Computes",
    });
    expect(again.unchanged).toBe(true);
  }, 120_000);

  it("rename_function and delete_function change what list_blueprint_functions reports", async () => {
    await call("create_function", { assetPath: BP, functionName: "BCovOld" });
    expect((await functionRows()).map((f) => f.name)).toContain("BCovOld");

    await call("rename_function", { assetPath: BP, oldName: "BCovOld", newName: "BCovNew" });
    let fns = (await functionRows()).map((f) => f.name);
    expect(fns).toContain("BCovNew");
    expect(fns).not.toContain("BCovOld");

    const deleted = await call("delete_function", { assetPath: BP, functionName: "BCovNew" });
    expect(deleted.deleted).toBe(true);
    fns = (await functionRows()).map((f) => f.name);
    expect(fns).not.toContain("BCovNew");
  }, 120_000);

  it("add, list, edit and remove a local variable", async () => {
    await call("create_function", { assetPath: BP, functionName: "BCovLocals" });
    await call("add_local_variable", { assetPath: BP, functionName: "BCovLocals", name: "BCovCounter", varType: "int" });
    let locals = rows(await call("list_local_variables", { assetPath: BP, functionName: "BCovLocals" }), "variables");
    const counter = locals.find((l) => l.name === "BCovCounter");
    expect(counter, JSON.stringify(locals)).toBeDefined();
    expect(String(counter?.type)).toBe("int");

    await call("edit_local_variable", { assetPath: BP, functionName: "BCovLocals", name: "BCovCounter", op: "rename", newName: "BCovTally" });
    locals = rows(await call("list_local_variables", { assetPath: BP, functionName: "BCovLocals" }), "variables");
    expect(locals.map((l) => l.name)).toEqual(expect.arrayContaining(["BCovTally"]));
    expect(locals.map((l) => l.name)).not.toContain("BCovCounter");

    const setDefault = await call("edit_local_variable", {
      assetPath: BP, functionName: "BCovLocals", name: "BCovTally", op: "set_default", defaultValue: "5",
    });
    expect(setDefault.defaultValue).toBe("5");
    const sameDefault = await call("edit_local_variable", {
      assetPath: BP, functionName: "BCovLocals", name: "BCovTally", op: "set_default", defaultValue: "5",
    });
    expect(sameDefault.unchanged).toBe(true);

    await call("edit_local_variable", { assetPath: BP, functionName: "BCovLocals", name: "BCovTally", op: "remove" });
    locals = rows(await call("list_local_variables", { assetPath: BP, functionName: "BCovLocals" }), "variables");
    expect(locals.map((l) => l.name)).not.toContain("BCovTally");
  }, 120_000);

  it("create_macro and delete_macro add and remove a macro graph", async () => {
    await call("create_macro", {
      assetPath: BP, macroName: "BCovMacro",
      inputs: [{ name: "MacroIn", type: "float" }],
      outputs: [{ name: "MacroOut", type: "float" }],
    });
    const macro = (await functionRows()).find((f) => f.name === "BCovMacro");
    expect(macro?.kind).toBe("macro");
    const sig = await call("list_graph_parameters", { assetPath: BP, functionName: "BCovMacro" });
    expect(names(sig, "inputs")).toContain("MacroIn");
    expect(names(sig, "outputs")).toContain("MacroOut");

    const deleted = await call("delete_macro", { assetPath: BP, macroName: "BCovMacro" });
    expect(deleted.deleted).toBe(true);
    expect((await functionRows()).map((f) => f.name)).not.toContain("BCovMacro");
  }, 120_000);

  it("list_overridable_functions names the parent's overridable events", async () => {
    const res = await call("list_overridable_functions", { assetPath: BP });
    expect(res.parentClass).toBe("Actor");
    const fns = names(res, "functions");
    expect(fns).toContain("ReceiveEndPlay");
    expect(fns).toContain("ReceiveHit");
    // The default event graph already places Tick and BeginPlay, so they are not offered again.
    expect(fns).not.toContain("ReceiveTick");
    expect(fns).not.toContain("ReceiveBeginPlay");
  }, 120_000);
});

describe("blueprint dispatchers, interfaces and timelines", () => {
  it("add, list and remove an event dispatcher with a typed signature", async () => {
    await call("add_event_dispatcher", { blueprintPath: BP, name: "BCovOnChanged", parameters: [{ name: "Amount", type: "float" }] });
    let listed = rows(await call("list_event_dispatchers", { assetPath: BP }), "dispatchers");
    const entry = listed.find((d) => d.name === "BCovOnChanged");
    expect(entry, JSON.stringify(listed)).toBeDefined();
    expect(entry?.hasSignatureGraph).toBe(true);
    expect(((entry?.parameters ?? []) as Row[]).map((p) => p.name)).toContain("Amount");

    const removed = await call("remove_event_dispatcher", { assetPath: BP, name: "BCovOnChanged" });
    expect(removed.removedVariable).toBe(true);
    listed = rows(await call("list_event_dispatchers", { assetPath: BP }), "dispatchers");
    expect(listed.map((d) => d.name)).not.toContain("BCovOnChanged");
  }, 120_000);

  it("create, add, list and remove a Blueprint Interface", async () => {
    const created = await call("create_blueprint_interface", { assetPath: BPI });
    expect(created.name).toBe("BPI_BCovMembers");

    // interfacePath is the generated class path, not the asset path.
    await call("add_blueprint_interface", { blueprintPath: BP, interfacePath: `${BPI}.BPI_BCovMembers_C` });
    let ifaces = rows(await call("list_blueprint_interfaces", { assetPath: BP }), "interfaces");
    const iface = ifaces.find((i) => String(i.interfacePath ?? "").includes("BPI_BCovMembers"));
    expect(iface, JSON.stringify(ifaces)).toBeDefined();
    expect(iface?.removable).toBe(true);

    const removed = await call("remove_blueprint_interface", { assetPath: BP, interfacePath: String(iface?.interfacePath) });
    expect(String(removed.interfacePath)).toContain("BPI_BCovMembers");
    ifaces = rows(await call("list_blueprint_interfaces", { assetPath: BP }), "interfaces");
    expect(ifaces.some((i) => String(i.interfacePath ?? "").includes("BPI_BCovMembers"))).toBe(false);
  }, 120_000);

  it("add_timeline_track stores a track that a second add sees as taken", async () => {
    const added = await call("add_timeline_track", {
      assetPath: BP, timelineName: "BCovTimeline", trackName: "Alpha", trackType: "float",
      keyframes: [{ time: 0, value: 0 }, { time: 2, value: 1 }],
    });
    expect(added.keyCount).toBe(2);
    expect(added.trackType).toBe("float");

    const again = await call("add_timeline_track", {
      assetPath: BP, timelineName: "BCovTimeline", trackName: "Alpha", trackType: "vector",
    });
    expect(again.alreadyExists).toBe(true);
    expect(again.existingTrackType).toBe("float");
  }, 120_000);
});
