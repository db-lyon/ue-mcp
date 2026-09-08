/**
 * IK Retargeter op settings, against a real editor (#1000/#1034).
 *
 * The settings on a retarget op are what decide what a retarget does, and
 * until this shipped they were the part only Python could reach. Two claims
 * have to hold and neither is checkable without a real op stack:
 *
 *   reading an op reports its settings struct in full, reflected rather than
 *   hand-listed, so a field this code never heard of still comes back;
 *   and a write lands on the live op. The obvious Python route in #1000
 *   mutated a struct copy, reported success and changed nothing, so "the call
 *   succeeded" proves nothing here - only reading the value back does.
 *
 * The fixture builds two IK rigs off an engine skeletal mesh and a retargeter
 * over them, which is enough to get the default op stack installed.
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

const SOURCE_MESH = "/Engine/EngineMeshes/SkeletalCube";
const SOURCE_RIG = `${TEST_PREFIX}/IK_RetargetOpsSource`;
const TARGET_RIG = `${TEST_PREFIX}/IK_RetargetOpsTarget`;
const RETARGETER = `${TEST_PREFIX}/RTG_RetargetOps`;

type Op = {
  index?: number;
  name?: string;
  enabled?: boolean;
  type?: string;
  settingsType?: string;
  settings?: Record<string, unknown>;
};

const ops = (result: unknown): Op[] => (resultArray(result, "retargetOps") ?? []) as Op[];

let bridge: EditorBridge;
/** Set when the fixture could not be built; every case then reports why. */
let fixtureError = "";
let opsWithSettings: Op[] = [];

async function readOps(): Promise<Op[]> {
  const read = await callBridge(bridge, "read_ik_retargeter", { assetPath: RETARGETER });
  expect(read.ok, read.error).toBe(true);
  return ops(read.result);
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of [RETARGETER, SOURCE_RIG, TARGET_RIG]) {
    await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  }

  for (const [path, name] of [[SOURCE_RIG, "IK_RetargetOpsSource"], [TARGET_RIG, "IK_RetargetOpsTarget"]] as const) {
    const rig = await callBridge(bridge, "create_ik_rig", {
      name,
      packagePath: TEST_PREFIX,
      skeletalMeshPath: SOURCE_MESH,
    });
    if (!rig.ok) {
      fixtureError = `create_ik_rig failed for ${path}: ${rig.error}`;
      return;
    }
  }

  const retargeter = await callBridge(bridge, "create_ik_retargeter", {
    name: "RTG_RetargetOps",
    packagePath: TEST_PREFIX,
    sourceRig: SOURCE_RIG,
    targetRig: TARGET_RIG,
  });
  if (!retargeter.ok) {
    fixtureError = `create_ik_retargeter failed: ${retargeter.error}`;
    return;
  }

  opsWithSettings = (await readOps()).filter((op) => op.settings && Object.keys(op.settings).length > 0);
});

afterAll(async () => {
  if (bridge) {
    for (const path of [RETARGETER, SOURCE_RIG, TARGET_RIG]) {
      await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    }
    disconnectBridge();
  }
});

describe("reading the op settings (#1000)", () => {
  it("built a retargeter with an op stack", () => {
    expect(fixtureError, fixtureError).toBe("");
    expect(opsWithSettings.length).toBeGreaterThan(0);
  });

  it("reports the settings struct type alongside the values", () => {
    expect(fixtureError, fixtureError).toBe("");
    for (const op of opsWithSettings) {
      expect(op.settingsType, `${op.name} reported settings with no type`).toBeTruthy();
    }
  });

  it("reports bEnabled, which lives on the settings struct rather than the op", () => {
    // The op's enabled state IS a settings field from 5.8 on, so a read that
    // could not see the settings could not see this either.
    expect(fixtureError, fixtureError).toBe("");
    const withEnabled = opsWithSettings.filter((op) => op.settings && "bEnabled" in op.settings);
    expect(withEnabled.length).toBeGreaterThan(0);
  });
});

describe("writing the op settings (#1000/#1034)", () => {
  it("turns an op off and reads the new state back", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const target = opsWithSettings[0];
    expect(target?.name).toBeTruthy();

    const before = (await readOps()).find((op) => op.name === target.name);
    expect(before?.enabled).toBe(true);

    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: target.name, enabled: false }],
    });
    expect(wrote.ok, wrote.error).toBe(true);

    // Reading it back is the whole assertion: the Python route this replaces
    // reported success against a copy and left the asset untouched.
    const after = (await readOps()).find((op) => op.name === target.name);
    expect(after?.enabled).toBe(false);

    const restored = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: target.name, enabled: true }],
    });
    expect(restored.ok, restored.error).toBe(true);
    expect((await readOps()).find((op) => op.name === target.name)?.enabled).toBe(true);
  });

  it("says which op it changed and which properties landed", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const target = opsWithSettings[0];
    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: target.name, enabled: true }],
    });
    expect(wrote.ok, wrote.error).toBe(true);
    const configured = (resultArray(wrote.result, "opsConfigured") ?? []) as Array<{ name?: string }>;
    expect(configured.map((c) => c.name)).toContain(target.name);
  });

  it("refuses a setting the op does not have, and changes nothing", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const target = opsWithSettings[0];
    const before = (await readOps()).find((op) => op.name === target.name);

    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: target.name, settings: { ThisSettingDoesNotExist: 1 } }],
    });
    const refused = !wrote.ok || (wrote.result as Record<string, unknown>)?.success === false;
    expect(refused).toBe(true);

    // A refusal that had already written half the request would be worse than
    // no validation at all, so the op has to come back unchanged.
    const after = (await readOps()).find((op) => op.name === target.name);
    expect(after?.enabled).toBe(before?.enabled);
  });

  it("refuses an op name the stack does not have, and names the ones it does", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: "NoSuchOpAnywhere", enabled: false }],
    });
    const message = String(wrote.error ?? JSON.stringify(wrote.result));
    expect(message).toMatch(/no retarget op named/i);
    // An op name is authored, so a caller guessing one has no other way to
    // learn the real set.
    expect(message.length).toBeGreaterThan("no retarget op named 'NoSuchOpAnywhere'".length);
  });

  it("refuses an entry that changes nothing", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: opsWithSettings[0].name }],
    });
    const refused = !wrote.ok || (wrote.result as Record<string, unknown>)?.success === false;
    expect(refused).toBe(true);
  });
});
