// @live-editor headless
/**
 * Sequencer channels and bindings (#1407, #1232, #1356), Datasmith (#1383)
 * and MetaHuman (#1354), each read back from the editor.
 *
 * Datasmith and MetaHuman are reached by reflection. With their plugins off
 * (the test project's default) every action refuses with
 * PLUGIN_NOT_AVAILABLE, which is what is asserted then; with them on, the
 * real import, reimport and character edits are.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/SeqDsMh`;
const MPC = `${DIR}/MPC_Probe`;
const MPC_SEQ = `${DIR}/LS_MpcProbe`;
const BIND_SEQ = `${DIR}/LS_BindProbe`;
const ACTOR_A = "SeqProbeA";
const ACTOR_B = "SeqProbeB";
const CHARACTER = `${DIR}/MH_Probe`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let scratch = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function expectUnavailable(method: string, params: Json): Promise<void> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r)).toBe(false);
  expect(body(r).errorCode).toBe("PLUGIN_NOT_AVAILABLE");
}

function writeScene(lampZ: number): string {
  const file = path.join(scratch, "Probe.udatasmith");
  fs.writeFileSync(file, `<?xml version="1.0" encoding="utf-8"?>
<DatasmithUnrealScene>
  <Version>0.24</Version>
  <SDKVersion>5.8.0</SDKVersion>
  <Host>ue-mcp-live</Host>
  <Application Vendor="ue-mcp" ProductName="LiveProbe" ProductVersion="1.0"/>
  <User ID="" OS="Windows"/>
  <Actor name="Root" label="DsProbe_Root" layer="Layer0">
    <Transform tx="0" ty="0" tz="0" sx="1" sy="1" sz="1" qx="0" qy="0" qz="0" qw="1"/>
    <tag value="Probe.Element.Id.100"/>
    <children visible="true">
      <Light name="Lamp" label="DsProbe_Lamp" layer="Layer0" type="PointLight" enabled="1">
        <Transform tx="0" ty="0" tz="${lampZ}" sx="1" sy="1" sz="1" qx="0" qy="0" qz="0" qw="1"/>
        <tag value="Probe.Element.Id.101"/>
        <Color usetemp="0" temperature="6500" R="1" G="1" B="1"/>
        <Intensity value="500"/>
      </Light>
    </children>
  </Actor>
  <MetaData name="Meta_Lamp" reference="Actor.Lamp">
    <KeyValueProperty name="Category" type="String" val="Lighting Fixtures"/>
  </MetaData>
</DatasmithUnrealScene>
`);
  return file.split(path.sep).join("/");
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-datasmith-"));
  await callBridge(bridge, "demo_go_home", {});
  for (const label of [ACTOR_A, ACTOR_B, "DsProbe_Root", "DsProbe_Lamp", "Probe"]) await callBridge(bridge, "delete_actor", { actorLabel: label });
  for (const p of [MPC_SEQ, BIND_SEQ, MPC, CHARACTER, "/Game/DatasmithLiveProbe"]) await callBridge(bridge, "delete_asset", { assetPath: p, force: true });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const label of [ACTOR_A, ACTOR_B, "DsProbe_Root", "DsProbe_Lamp", "Probe"]) await callBridge(bridge, "delete_actor", { actorLabel: label });
    disconnectBridge();
  }
  if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
});

describe("sequencer channels and bindings", () => {
  it("keys an MPC parameter channel through its proxy and samples the constant hold (#1407)", async () => {
    await call("create_asset_by_class", { name: "MPC_Probe", className: "MaterialParameterCollection", packagePath: DIR });
    await call("set_asset_property", { assetPath: MPC, propertyName: "ScalarParameters", value: "((ParameterName=\"Intensity\",DefaultValue=1.0))" });
    await call("create_level_sequence", { name: "LS_MpcProbe", packagePath: DIR });
    await call("add_sequence_section", { sequencePath: MPC_SEQ, trackType: "MaterialParameterCollection", materialParameterCollection: MPC, startSeconds: 0, endSeconds: 4 });

    const set = await call("set_sequence_channel_keys", {
      sequencePath: MPC_SEQ, trackType: "MaterialParameterCollection", channel: "Intensity",
      keyframes: [{ frame: 0, value: 0 }, { frame: 30, value: 5, interpolation: "constant" }],
    });
    expect(set.keysVerified).toBe(2);

    const read = await call("get_sequence_channel_keys", {
      sequencePath: MPC_SEQ, trackType: "MaterialParameterCollection", channel: "Intensity",
      sampleStartFrame: 30, sampleEndFrame: 40, sampleStep: 10,
    });
    const channel = (read.channels as Array<{ keyCount: number; keys: Array<{ interpolation: string }>; samples: Array<{ value: number }> }>)[0];
    expect(channel.keyCount).toBe(2);
    expect(channel.keys[1].interpolation).toBe("constant");
    expect(channel.samples.map((s) => s.value)).toEqual([5, 5]);
  }, 180_000);

  it("removes a section with an undo rollback that restores it (#1232)", async () => {
    await call("create_level_sequence", { name: "LS_BindProbe", packagePath: DIR });
    await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: ACTOR_A });
    await call("add_sequence_section", { sequencePath: BIND_SEQ, trackType: "Transform", actorLabel: ACTOR_A, startSeconds: 0, endSeconds: 4 });
    const second = await call("add_sequence_section", { sequencePath: BIND_SEQ, trackType: "Transform", actorLabel: ACTOR_A, startSeconds: 4, endSeconds: 6 });
    expect(second.sectionCount).toBe(2);

    const removed = await call("remove_sequence_section", { sequencePath: BIND_SEQ, trackType: "Transform", actorLabel: ACTOR_A, sectionIndex: 1 });
    expect(removed.sectionCount).toBe(1);
    const rollback = removed.rollback as { method: string; payload: Json };
    await call(rollback.method, rollback.payload);
    const after = await call("get_sequence_channel_keys", { sequencePath: BIND_SEQ, trackType: "Transform", actorLabel: ACTOR_A, sectionIndex: 1, channel: "Location.Z" });
    expect((after.track as { sectionCount: number }).sectionCount).toBe(2);
  }, 180_000);

  it("reports a binding stale once its actor is deleted, and rebinds it keeping the guid (#1356)", async () => {
    const before = await call("resolve_sequence_bindings", { sequencePath: BIND_SEQ });
    const binding = (before.bindings as Array<{ guid?: string; bindingGuid?: string; stale: boolean }>)[0];
    expect(binding.stale).toBe(false);

    await call("delete_actor", { actorLabel: ACTOR_A });
    const stale = await call("resolve_sequence_bindings", { sequencePath: BIND_SEQ });
    expect(stale.staleCount).toBe(1);

    await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: ACTOR_B });
    const rebound = await call("rebind_possessable", { sequencePath: BIND_SEQ, bindingName: ACTOR_A, actorLabel: ACTOR_B });
    expect(rebound.verified).toBe(true);
    const resolved = await call("resolve_sequence_bindings", { sequencePath: BIND_SEQ });
    const objects = (resolved.bindings as Array<{ boundObjects: Array<{ actorLabel: string }> }>)[0].boundObjects;
    expect(objects.map((o) => o.actorLabel)).toEqual([ACTOR_B]);
    expect(resolved.staleCount).toBe(0);
  }, 180_000);

  it("lists level sequence actors with their bound objects (#1232)", async () => {
    const bindings = await call("get_sequence_runtime_bindings", { world: "editor" });
    expect(Array.isArray(bindings.sequenceActors)).toBe(true);
    expect(typeof bindings.world).toBe("string");
  }, 60_000);
});

describe("datasmith (#1383)", () => {
  it("imports a scene, finds its actors by tag with metadata, and reimports a source change", async () => {
    const status = await call("datasmith_status", {});
    const file = writeScene(300);
    if (status.available !== true) {
      await expectUnavailable("datasmith_inspect", { filePath: file });
      await expectUnavailable("datasmith_import", { filePath: file, destinationPath: "/Game/DatasmithLiveProbe" });
      await expectUnavailable("datasmith_reimport", { sceneAssetPath: "/Game/DatasmithLiveProbe/Probe/Probe" });
      // Queries need only DatasmithContent, which ships enabled; without it a metadata query is refused.
      if (status.contentLoaded === true) await call("datasmith_query_actors", { metadataKey: "Category" });
      else await expectUnavailable("datasmith_query_actors", { metadataKey: "Category" });
      return;
    }
    const inspected = await call("datasmith_inspect", { filePath: file });
    expect((inspected.counts as Json).lights).toBe(1);

    const imported = await call("datasmith_import", { filePath: file, destinationPath: "/Game/DatasmithLiveProbe" });
    expect(String(imported.sceneAsset)).toContain("/Game/DatasmithLiveProbe");

    const found = await call("datasmith_query_actors", { tagPrefix: "Probe.Element.Id." });
    const lamp = (found.actors as Array<{ actorLabel: string; metadata: Json }>).find((a) => a.actorLabel === "DsProbe_Lamp");
    expect(lamp?.metadata.Category).toBe("Lighting Fixtures");

    writeScene(700);
    const reimported = await call("datasmith_reimport", { sceneAssetPath: imported.sceneAsset });
    expect((reimported.actorChanges as { moved: string[] }).moved).toContain("DsProbe_Lamp");
    const details = await call("get_actor_details", { actorLabel: "DsProbe_Lamp" });
    expect((details.location as { z: number }).z).toBeCloseTo(700, 0);
  }, 300_000);
});

describe("metahuman (#1354)", () => {
  it("creates a character, sets body constraints with a rollback, and refuses cloud steps it cannot run", async () => {
    const status = await call("metahuman_status", {});
    if (status.available !== true) {
      await expectUnavailable("metahuman_create_character", { name: "MH_Probe", packagePath: DIR });
      await expectUnavailable("metahuman_read_character", { assetPath: CHARACTER });
      await expectUnavailable("metahuman_set_body", { assetPath: CHARACTER, constraints: { Height: 180 } });
      await expectUnavailable("metahuman_set_wardrobe", { assetPath: CHARACTER, items: {} });
      await expectUnavailable("metahuman_request_rig", { assetPath: CHARACTER });
      await expectUnavailable("metahuman_request_textures", { assetPath: CHARACTER });
      await expectUnavailable("metahuman_assemble", { assetPath: CHARACTER });
      return;
    }
    await call("metahuman_create_character", { name: "MH_Probe", packagePath: DIR });
    const read = await call("metahuman_read_character", { assetPath: CHARACTER });
    const height = (read.bodyConstraints as Array<{ name: string; target: number }>).find((c) => c.name === "Height");
    expect(height).toBeDefined();

    const set = await call("metahuman_set_body", { assetPath: CHARACTER, constraints: { Height: 180 } });
    const row = (set.constraints as Array<{ after: { target: number; active: boolean } }>)[0];
    expect(row.after.target).toBeCloseTo(180, 0);
    expect(row.after.active).toBe(true);
    const rollback = set.rollback as { method: string; payload: Json };
    const restored = await call(rollback.method, rollback.payload);
    expect((restored.constraints as Array<{ after: { active: boolean } }>)[0].after.active).toBe(false);

    const textures = await callBridge(bridge, "metahuman_request_textures", { assetPath: CHARACTER });
    expect(succeeded(textures)).toBe(false);
    expect(body(textures).reason ?? body(textures).loginState).toMatch(/optional_content_missing|not-logged-in/);

    const wardrobe = await callBridge(bridge, "metahuman_set_wardrobe", { assetPath: CHARACTER, items: { "Top Garment": "/Game/Nope/NotAWardrobeItem" } });
    expect(JSON.stringify(wardrobe.result ?? wardrobe.error)).toMatch(/NotAWardrobeItem|not found|applied/i);

    const assembled = await callBridge(bridge, "metahuman_assemble", { assetPath: CHARACTER, pipelineType: "Optimized", quality: "Medium" });
    if (read.canBuild !== true) expect(succeeded(assembled)).toBe(false);
  }, 600_000);
});
