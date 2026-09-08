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
/** The one retarget chain the fixture defines, so chainSettings has a target. */
const CHAIN = "Spine";

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
      // A chain, so the FK Chains op has a ChainsToRetarget entry to merge
      // into. Without one that array is empty and chainSettings cannot be
      // exercised at all, which is how it shipped untested.
      //
      // Deliberately NO retargetRoot: it gives the retargeter a pelvis at the
      // cube's ground plane, and configure_ik_retargeter's processor
      // validation then rejects every write with "source pelvis bone is very
      // near the ground plane" - masking whether the write itself worked.
      chains: [{ name: CHAIN, startBone: "Bone01", endBone: "Bone02" }],
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

describe("writing a named setting (#1000)", () => {
  /** Read one op fresh, by name. */
  const opNamed = async (match: RegExp): Promise<Op | undefined> =>
    (await readOps()).find((op) => match.test(op.name ?? ""));

  it("writes an enum setting and reads the new value back", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const before = await opNamed(/root motion/i);
    expect(before?.name, "no Root Motion op in the default stack").toBeTruthy();

    // #1000 names root_motion_source specifically. The target is chosen from
    // the current value so this always asks for a CHANGE.
    const was = String((before!.settings ?? {}).rootMotionSource ?? "");
    const want = was === "CopyFromSourceRoot" ? "GenerateFromTargetPelvis" : "CopyFromSourceRoot";

    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: before!.name, settings: { RootMotionSource: want } }],
    });
    expect(wrote.ok, wrote.error).toBe(true);

    const after = await opNamed(/root motion/i);
    expect(String((after!.settings ?? {}).rootMotionSource)).toBe(want);
  });

  it("writes a bool setting", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const before = await opNamed(/root motion/i);
    const was = Boolean((before!.settings ?? {}).bMaintainOffsetFromPelvis);

    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: before!.name, settings: { bMaintainOffsetFromPelvis: !was } }],
    });
    expect(wrote.ok, wrote.error).toBe(true);

    const after = await opNamed(/root motion/i);
    expect(Boolean((after!.settings ?? {}).bMaintainOffsetFromPelvis)).toBe(!was);
  });

  it("writes a float setting", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const before = await opNamed(/pelvis/i);
    expect(before?.name, "no Pelvis Motion op in the default stack").toBeTruthy();
    const was = Number((before!.settings ?? {}).floorConstraintWeight ?? 0);
    const want = was === 0.5 ? 0.25 : 0.5;

    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: before!.name, settings: { floorConstraintWeight: want } }],
    });
    expect(wrote.ok, wrote.error).toBe(true);

    const after = await opNamed(/pelvis/i);
    expect(Number((after!.settings ?? {}).floorConstraintWeight)).toBe(want);
  });

  it("refuses a value the enum does not have", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const root = await opNamed(/root motion/i);
    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: root!.name, settings: { RootMotionSource: "NotAnEnumerator" } }],
    });
    const message = String(wrote.error ?? JSON.stringify(wrote.result));
    // Naming the enum is what tells a caller their value was wrong rather
    // than the property.
    expect(message).toMatch(/unknown enum value/i);
  });
});

describe("writing one chain's FK settings (#1034)", () => {
  type Chain = { targetChainName?: string; rotationMode?: string; translationMode?: string };
  const fkChains = async (): Promise<{ op?: Op; chains: Chain[] }> => {
    const op = (await readOps()).find((o) => /fk chain/i.test(o.name ?? ""));
    return { op, chains: ((op?.settings ?? {}).chainsToRetarget ?? []) as Chain[] };
  };

  it("has a chain to write to", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const { chains } = await fkChains();
    // Guards the rest: an empty array made every assertion below vacuous.
    expect(chains.length).toBeGreaterThan(0);
    expect(chains[0].targetChainName).toBe(CHAIN);
  });

  it("sets the named chain's rotation mode", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const { op, chains } = await fkChains();
    const was = String(chains[0].rotationMode ?? "");
    const want = was === "OneToOne" ? "Interpolated" : "OneToOne";

    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: op!.name, chainSettings: [{ chain: CHAIN, RotationMode: want }] }],
    });
    expect(wrote.ok, wrote.error).toBe(true);

    const after = await fkChains();
    expect(String(after.chains[0].rotationMode)).toBe(want);
  });

  it("leaves the chain's other settings alone", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const { op, chains } = await fkChains();
    const translationWas = String(chains[0].translationMode ?? "");
    const rotationWant = String(chains[0].rotationMode) === "OneToOne" ? "Interpolated" : "OneToOne";

    await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: op!.name, chainSettings: [{ chain: CHAIN, RotationMode: rotationWant }] }],
    });

    // The point of merging rather than replacing ChainsToRetarget: writing one
    // property must not reset the ones the caller did not name.
    const after = await fkChains();
    expect(String(after.chains[0].translationMode)).toBe(translationWas);
  });

  it("names what it applied", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const { op } = await fkChains();
    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: op!.name, chainSettings: [{ chain: CHAIN, RotationMode: "OneToOne" }] }],
    });
    expect(wrote.ok, wrote.error).toBe(true);
    const configured = (resultArray(wrote.result, "opsConfigured") ?? []) as Array<{ settingsApplied?: string[] }>;
    expect(configured[0]?.settingsApplied).toContain(`${CHAIN}.RotationMode`);
  });

  it("refuses a chain the op does not have", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const { op } = await fkChains();
    const wrote = await callBridge(bridge, "configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      ops: [{ name: op!.name, chainSettings: [{ chain: "NoSuchChain", RotationMode: "OneToOne" }] }],
    });
    const message = String(wrote.error ?? JSON.stringify(wrote.result));
    expect(message).toMatch(/no retarget chain named/i);
  });
});
