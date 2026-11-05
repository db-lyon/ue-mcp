// @live-editor headless
/**
 * Enhanced Input asset authoring in the editor: actions, mapping contexts,
 * an action's own triggers and modifiers, player-mappable settings and the
 * input audit, each read back through the matching read action.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Gameplay/Input";
const IA_JUMP = `${DIR}/IA_BCovJump`;
const IA_MOVE = `${DIR}/IA_BCovMove`;
const IA_ORPHAN = `${DIR}/IA_BCovOrphan`;
const IMC = `${DIR}/IMC_BCov`;

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Row[] => (Array.isArray(v) ? (v as Row[]) : []);

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function wipe(): Promise<void> {
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await wipe();
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await wipe();
    disconnectBridge();
  }
});

describe("Enhanced Input authoring", () => {
  it("create_input_action and create_input_mapping_context make assets list_input_assets and read_input_action report", async () => {
    const jump = await call("create_input_action", { name: "IA_BCovJump", packagePath: DIR });
    expect(String(jump.path)).toContain("IA_BCovJump");
    await call("create_input_action", { name: "IA_BCovMove", packagePath: DIR, valueType: "Axis2D" });
    await call("create_input_action", { name: "IA_BCovOrphan", packagePath: DIR });
    const imc = await call("create_input_mapping_context", { name: "IMC_BCov", packagePath: DIR });
    expect(String(imc.path)).toContain("IMC_BCov");

    expect((await call("read_input_action", { inputActionPath: IA_JUMP })).valueType).toBe("Boolean");
    expect((await call("read_input_action", { inputActionPath: IA_MOVE })).valueType).toBe("Axis2D");

    const listed = await call("list_input_assets", { limit: 2000 });
    const assets = rows(listed.assets);
    expect(assets.some((a) => a.kind === "inputAction" && String(a.path).startsWith(IA_MOVE))).toBe(true);
    expect(assets.some((a) => a.kind === "inputMappingContext" && String(a.path).startsWith(IMC))).toBe(true);
  }, 120_000);

  it("list_imc_mappings reads back the mappings a context carries", async () => {
    const empty = await call("list_imc_mappings", { imcPath: IMC });
    expect(empty.count).toBe(0);
    await call("add_imc_mapping", { imcPath: IMC, inputActionPath: IA_JUMP, key: "SpaceBar" });
    await call("add_imc_mapping", { imcPath: IMC, inputActionPath: IA_MOVE, key: "W" });
    const listed = await call("list_imc_mappings", { imcPath: IMC });
    expect(listed.count).toBe(2);
    const mappings = rows(listed.mappings);
    expect(mappings.map((m) => m.key)).toEqual(["SpaceBar", "W"]);
    expect(mappings.map((m) => m.inputActionName)).toEqual(["IA_BCovJump", "IA_BCovMove"]);
  }, 120_000);

  it("set_action_triggers replaces the action's own triggers and modifiers, and clear empties them", async () => {
    const set = await call("set_action_triggers", {
      inputActionPath: IA_JUMP,
      triggers: [{ type: "Hold", HoldTimeThreshold: 0.75 }],
      modifiers: [{ type: "Negate" }],
    });
    expect(set.triggerCount).toBe(1);

    let read = await call("read_input_action", { inputActionPath: IA_JUMP });
    expect(read.triggerCount).toBe(1);
    expect(read.modifierCount).toBe(1);
    const trigger = rows(read.triggers)[0];
    expect(trigger.className).toBe("InputTriggerHold");
    expect(String((trigger.properties as Row).HoldTimeThreshold)).toMatch(/^0\.75/);
    expect(rows(read.modifiers)[0].className).toBe("InputModifierNegate");

    await call("set_action_triggers", { inputActionPath: IA_JUMP, clear: true });
    read = await call("read_input_action", { inputActionPath: IA_JUMP });
    expect(read.triggerCount).toBe(0);
    expect(read.modifierCount).toBe(0);
  }, 120_000);

  it("set_player_mappable_settings attaches settings read_input_action points at", async () => {
    const before = await call("read_input_action", { inputActionPath: IA_MOVE });
    expect(String(before.playerMappableKeySettings)).toBe("None");
    await call("set_player_mappable_settings", {
      inputActionPath: IA_MOVE, mappingName: "BCovMove", displayName: "Move", displayCategory: "Movement",
    });
    const after = await call("read_input_action", { inputActionPath: IA_MOVE });
    const settings = String(after.playerMappableKeySettings);
    expect(settings).not.toBe("None");
    expect((await call("get_property", { objectPath: settings, propertyName: "Name" })).value).toBe("BCovMove");
    expect(String((await call("get_property", { objectPath: settings, propertyName: "DisplayCategory" })).value)).toContain("Movement");
  }, 120_000);

  it("validate_input flags the 2D action a single key drives, and the action nothing maps", async () => {
    const one = await call("validate_input", { imcPath: IMC });
    expect(one.contextCount).toBe(1);
    expect(one.mappingsChecked).toBe(2);
    const widths = rows(one.problems).filter((p) => p.code === "axis_width_mismatch");
    expect(widths).toHaveLength(1);
    expect(widths[0].mappingIndex).toBe(1);
    expect(one.valid).toBe(true);

    const swept = await call("validate_input", { directory: DIR });
    const unmapped = (swept.unmappedActions as string[]).map(String);
    expect(unmapped.some((p) => p.startsWith(IA_ORPHAN))).toBe(true);
    expect(unmapped.some((p) => p.startsWith(IA_JUMP))).toBe(false);
  }, 120_000);
});
