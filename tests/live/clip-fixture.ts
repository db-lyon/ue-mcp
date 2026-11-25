/**
 * The probe arena the timeline playtest tests play in (#2097): Input Actions and a mapping context the test project's
 * AMCPProbeCharacter binds, and a level holding one AMCPProbeArena, which spawns the floor, the character (handed to
 * player 0), a physics crate and, unless told not to, an AI-controlled chaser.
 *
 *   IA_ProbeMove      Axis2D: W through a Swizzle mapping modifier, D as is.
 *   IA_ProbeJump      Boolean.
 *   IA_ProbeCharge    Boolean with an action-level Hold trigger (0.5 s, one shot): ChargeCount.
 *   IA_ProbeThrottle  Axis1D with an action-level Scalar modifier (2.5): ThrottleSum.
 *
 * A value fed through Charge's trigger or Throttle's modifier twice shows in the character's counters.
 */
import { callBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;

export interface ProbeActions {
  move: string;
  jump: string;
  charge: string;
  throttle: string;
}

export function probeActions(dir: string): ProbeActions {
  const ia = (n: string) => `${dir}/${n}.${n}`;
  return { move: ia("IA_ProbeMove"), jump: ia("IA_ProbeJump"), charge: ia("IA_ProbeCharge"), throttle: ia("IA_ProbeThrottle") };
}

async function must(bridge: EditorBridge, method: string, params: Json): Promise<Json> {
  const r: TestResult = await callBridge(bridge, method, params);
  const body = r.result && typeof r.result === "object" ? (r.result as Json) : {};
  if (!r.ok || body.success === false) throw new Error(`${method}: ${r.error ?? JSON.stringify(r.result)}`);
  return body;
}

/** Leaves the arena's level for a committed one, so its folder can be deleted, and deletes it. */
export async function teardownProbeArena(bridge: EditorBridge, dir: string): Promise<void> {
  await callBridge(bridge, "load_level", { levelPath: "/Game/UEMCPTests/ScriptMap/L_Other_C247A32A", discardUnsaved: true, endPIE: true });
  await callBridge(bridge, "delete_folder", { path: dir, force: true });
}

/**
 * Creates the probe's Input Actions and IMC_Probe under dir, and a level of its own (dir/L_<name>) holding the arena,
 * placed far from anything else in the level it was saved from. The character's ActionsRoot is dir.
 */
export async function setupProbeArena(bridge: EditorBridge, dir: string, name: string, opts: { chaser?: boolean } = {}): Promise<ProbeActions> {
  const a = probeActions(dir);
  // From a committed level with nothing near the arena, never the level open now, which may hold another arena or
  // be the one this folder's previous run saved.
  await must(bridge, "load_level", { levelPath: "/Game/UEMCPTests/ScriptMap/L_Other_C247A32A", discardUnsaved: true, endPIE: true });
  await callBridge(bridge, "delete_folder", { path: dir, force: true });
  for (const [n, valueType] of [
    ["IA_ProbeMove", "Axis2D"],
    ["IA_ProbeJump", "Boolean"],
    ["IA_ProbeCharge", "Boolean"],
    ["IA_ProbeThrottle", "Axis1D"],
  ]) {
    await must(bridge, "create_input_action", { name: n, packagePath: dir, valueType });
  }
  await must(bridge, "set_action_triggers", {
    inputActionPath: a.charge,
    triggers: [{ class: "/Script/EnhancedInput.InputTriggerHold", properties: { HoldTimeThreshold: 0.5, bIsOneShot: true } }],
  });
  await must(bridge, "set_action_triggers", {
    inputActionPath: a.throttle,
    modifiers: [{ class: "/Script/EnhancedInput.InputModifierScalar", properties: { Scalar: { X: 2.5, Y: 2.5, Z: 2.5 } } }],
  });
  await must(bridge, "create_input_mapping_context", { name: "IMC_Probe", packagePath: dir });
  const imc = `${dir}/IMC_Probe.IMC_Probe`;
  const mappings: [string, string, Json[]][] = [
    [a.move, "W", [{ class: "/Script/EnhancedInput.InputModifierSwizzleAxis", properties: { Order: "YXZ" } }]],
    [a.move, "D", []],
    [a.jump, "SpaceBar", []],
    [a.charge, "Q", []],
    [a.throttle, "E", []],
  ];
  for (let i = 0; i < mappings.length; i++) {
    const [action, key, modifiers] = mappings[i];
    await must(bridge, "add_imc_mapping", { imcPath: imc, inputActionPath: action, key });
    if (modifiers.length) await must(bridge, "set_mapping_modifiers", { imcPath: imc, mappingIndex: i, modifiers });
  }
  // A level once loaded can outlive the folder delete above, so each run saves a level of its own name.
  await must(bridge, "save_level_as", { levelPath: `${dir}/L_${name}_${Date.now().toString(36)}` });
  const label = `${name}Arena`;
  await must(bridge, "place_actor", { actorClass: "/Script/ue_mcp.MCPProbeArena", label, location: { x: 0, y: 60000, z: 4000 } });
  await must(bridge, "set_actor_property", { actorLabel: label, propertyName: "ActionsRoot", value: dir });
  await must(bridge, "set_actor_property", { actorLabel: label, propertyName: "bSpawnChaser", value: opts.chaser === true });
  await must(bridge, "set_actor_property", { actorLabel: label, propertyName: "bSpawnCrate", value: false });
  await must(bridge, "save_level", {});
  return a;
}
