import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const SOURCE_MESH = "/Engine/EngineMeshes/SkeletalCube";
const TEST_MESH = `${TEST_PREFIX}/SKM_ListSocketsComponent`;
const AT_BONE = "MCPTest_AtBone";
const OFFSET = "MCPTest_Offset";
const OFFSET_LOCATION = { x: 10, y: 2, z: -3 };

type Vec = { x: number; y: number; z: number };
type Socket = {
  name: string;
  boneName: string;
  componentTransform?: { location: Vec; rotation: Record<string, number>; scale: Vec };
  componentTransformError?: string;
};

let bridge: EditorBridge;
let boneName = "";
let boneLocation: Vec = { x: 0, y: 0, z: 0 };

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: TEST_MESH, force: true });
  const duplicate = await callBridge(bridge, "duplicate_asset", { sourcePath: SOURCE_MESH, destinationPath: TEST_MESH });
  expect(duplicate.ok, duplicate.error).toBe(true);

  // The last bone sits away from the origin, so a wrong space shows up as a wrong location.
  const bones = await callBridge(bridge, "list_skeleton_bones", { assetPath: TEST_MESH });
  expect(bones.ok, bones.error).toBe(true);
  const list = resultArray(bones.result, "bones") as Array<Record<string, unknown>>;
  const bone = list[list.length - 1];
  boneName = String(bone.name);
  boneLocation = bone.componentSpaceLocation as Vec;

  for (const [socketName, relativeLocation] of [[AT_BONE, { x: 0, y: 0, z: 0 }], [OFFSET, OFFSET_LOCATION]] as const) {
    const added = await callBridge(bridge, "add_socket", { assetPath: TEST_MESH, socketName, boneName, relativeLocation });
    expect(added.ok, added.error).toBe(true);
  }
});

afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: TEST_MESH, force: true });
  disconnectBridge();
});

async function sockets(): Promise<Map<string, Socket>> {
  const listed = await callBridge(bridge, "list_asset_sockets", { assetPath: TEST_MESH });
  expect(listed.ok, listed.error).toBe(true);
  const result = listed.result as Record<string, unknown>;
  expect(result.componentTransformPose).toBe("SkeletalMesh reference pose");
  const all = (resultArray(result, "sockets") ?? []) as Socket[];
  return new Map(all.map((socket) => [socket.name, socket]));
}

describe("asset.list_sockets componentTransform", () => {
  it("puts a socket with no offset on its bone's reference-pose location", async () => {
    const socket = (await sockets()).get(AT_BONE)!;
    expect(socket.componentTransformError).toBeUndefined();
    const location = socket.componentTransform!.location;
    expect(location.x).toBeCloseTo(boneLocation.x, 3);
    expect(location.y).toBeCloseTo(boneLocation.y, 3);
    expect(location.z).toBeCloseTo(boneLocation.z, 3);
  });

  it("applies the relative offset through the bone's rotation", async () => {
    // The bind pose has unit scale, so the offset keeps its length whatever the bone's rotation.
    const socket = (await sockets()).get(OFFSET)!;
    const location = socket.componentTransform!.location;
    const distance = Math.hypot(location.x - boneLocation.x, location.y - boneLocation.y, location.z - boneLocation.z);
    expect(distance).toBeCloseTo(Math.hypot(OFFSET_LOCATION.x, OFFSET_LOCATION.y, OFFSET_LOCATION.z), 3);
    // SkeletalCube's bones are rotated, so a correct result differs from the unrotated sum.
    const unrotated = Math.hypot(
      location.x - (boneLocation.x + OFFSET_LOCATION.x),
      location.y - (boneLocation.y + OFFSET_LOCATION.y),
      location.z - (boneLocation.z + OFFSET_LOCATION.z),
    );
    expect(unrotated).toBeGreaterThan(1);
  });
});
