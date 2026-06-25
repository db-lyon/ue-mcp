#!/usr/bin/env node
import { EditorBridge } from "../dist/bridge.js";

const assetPath = process.argv[2] ?? process.env.UE_MCP_SOCKET_REPRO_ASSET;
if (!assetPath) {
  console.error("Usage: node scripts/repro-skeletal-mesh-sockets.mjs /Game/Path/To/SkeletalMesh");
  console.error("Optional: UE_MCP_EXPECT_MESH_SOCKETS=Ejector,Muzzle UE_MCP_EXPECT_SKELETON_SOCKETS=AimPoint");
  process.exit(2);
}

const split = (value) => (value ?? "").split(",").map((s) => s.trim()).filter(Boolean);
const expectedMesh = split(process.env.UE_MCP_EXPECT_MESH_SOCKETS);
const expectedSkeleton = split(process.env.UE_MCP_EXPECT_SKELETON_SOCKETS);

const bridge = new EditorBridge(
  process.env.UE_MCP_TEST_HOST ?? process.env.UE_MCP_HOST ?? "127.0.0.1",
  Number(process.env.UE_MCP_TEST_PORT ?? process.env.UE_MCP_PORT ?? 9877),
);

try {
  await bridge.connect(5000);
  const result = await bridge.call("list_asset_sockets", { assetPath }, 120000);
  const sockets = Array.isArray(result?.sockets) ? result.sockets : [];
  const has = (name, source) => sockets.some((s) => s?.name === name && s?.source === source);

  for (const name of expectedMesh) {
    if (!has(name, "mesh")) throw new Error(`Missing mesh socket: ${name}`);
  }
  for (const name of expectedSkeleton) {
    if (!has(name, "skeleton")) throw new Error(`Missing skeleton socket: ${name}`);
  }
  if (sockets.some((s) => !s?.source)) throw new Error("At least one socket is missing source metadata");

  console.log(JSON.stringify({
    assetPath,
    socketCount: sockets.length,
    meshSocketCount: result.meshSocketCount,
    skeletonSocketCount: result.skeletonSocketCount,
    sockets: sockets.map((s) => ({ name: s.name, source: s.source, boneName: s.boneName })),
  }, null, 2));
} finally {
  bridge.disconnect();
}
