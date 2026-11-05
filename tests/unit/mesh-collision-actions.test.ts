/**
 * asset(set_mesh_simple_collision) and level(resave_mesh_users): the TS surface
 * their C++ specs generate, the validation that surface applies before a call
 * reaches the editor, and the engine calls the handlers make in the order the
 * editor's own collision tools make them. The live half is
 * tests/live/mesh-collision-users.test.ts.
 */
import { readFileSync } from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { registeredTimeoutMs } from "../../src/bridge/bridge-timeouts.js";
import { classifyWrite } from "../../src/flow/write-methods.js";
import { declaredActionEffect } from "../../src/surface/action-effects.js";
import { assetTool } from "../../src/tools/asset.js";
import { levelTool } from "../../src/tools/level.js";

const HANDLERS = path.join(
  path.dirname(fileURLToPath(import.meta.url)),
  "..", "..", "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private", "Handlers",
);

/** The body of one handler definition, from its signature to the closing brace at column 0. */
function handlerBody(file: string, qualifiedName: string): string {
  const text = readFileSync(path.join(HANDLERS, file), "utf8").replace(/\r\n/g, "\n");
  const start = text.indexOf(`TSharedPtr<FJsonValue> ${qualifiedName}(`);
  expect(start, `${qualifiedName} is defined in ${file}`).toBeGreaterThanOrEqual(0);
  const end = text.indexOf("\n}\n", start);
  return text.slice(start, end);
}

const BOX = { type: "box", center: { x: 0, y: 0, z: 0 }, extent: { x: 100, y: 300, z: 50 } };
const SPHERE = { type: "sphere", center: { x: 0, y: 0, z: 50 }, radius: 25 };
const CAPSULE = { type: "capsule", center: { x: 0, y: 0, z: 0 }, rotation: { pitch: 90, yaw: 0, roll: 0 }, radius: 20, length: 60 };

describe("asset.set_mesh_simple_collision", () => {
  const action = assetTool.actions.set_mesh_simple_collision;

  it("is exposed through the asset action schema as a mutation", () => {
    expect(assetTool.schema.action.safeParse("set_mesh_simple_collision").success).toBe(true);
    expect(declaredActionEffect("asset", "set_mesh_simple_collision")).toBe("mutate");
  });

  it("takes its parameters from its C++ spec and forwards the bag as sent", () => {
    expect(action.bridge).toBe("set_mesh_simple_collision");
    expect(action.mapParams).toBeUndefined();
    expect(action.description).toContain("Params: assetPath, shapes, replace?, collisionTraceFlag?, save?, dryRun?");
    const spec = action.kind === "bridge" ? action.paramSpec : undefined;
    expect(spec?.map((p) => [p.name, p.required])).toEqual([
      ["assetPath", true], ["shapes", true], ["replace", false],
      ["collisionTraceFlag", false], ["save", false], ["dryRun", false],
    ]);
  });

  it("accepts boxes, spheres and capsules in the shapes[] form", () => {
    expect(assetTool.schema.shapes.safeParse([BOX, SPHERE, CAPSULE]).success).toBe(true);
    // An empty list is how replace=true clears every simple shape.
    expect(assetTool.schema.shapes.safeParse([]).success).toBe(true);
  });

  it("refuses a shape with an unknown type, no center, or a value of the wrong type", () => {
    const shapes = assetTool.schema.shapes;
    expect(shapes.safeParse([{ ...BOX, type: "convex" }]).success).toBe(false);
    expect(shapes.safeParse([{ type: "box", extent: { x: 1, y: 1, z: 1 } }]).success).toBe(false);
    expect(shapes.safeParse([{ ...SPHERE, radius: "25" }]).success).toBe(false);
    expect(shapes.safeParse([{ ...BOX, extent: { x: 1, y: 1 } }]).success).toBe(false);
    expect(shapes.safeParse([{ ...CAPSULE, rotation: { yaw: 0 } }]).success).toBe(false);
    expect(shapes.safeParse(BOX).success).toBe(false);
  });

  it("types the flags it reads", () => {
    expect(assetTool.schema.replace.safeParse(false).success).toBe(true);
    expect(assetTool.schema.replace.safeParse("false").success).toBe(false);
    expect(assetTool.schema.collisionTraceFlag.safeParse("CTF_UseSimpleAsComplex").success).toBe(true);
    expect(assetTool.schema.collisionTraceFlag.safeParse(2).success).toBe(false);
  });

  it("documents full-length box extents and why the BodySetupGuid has to change", () => {
    const description = action.description ?? "";
    expect(description).toMatch(/FULL length/);
    expect(description).toContain("BodySetupGuid");
    expect(description).toContain("SplineMeshComponent");
    expect(description).toContain("resave_mesh_users");
  });

  it("writes the mesh it names, for the source-control guard", () => {
    expect(classifyWrite("set_mesh_simple_collision", { assetPath: "/Game/Props/SM_Rail", shapes: [BOX] }))
      .toEqual({ writes: true, contentPaths: ["/Game/Props/SM_Rail"] });
  });

  it("is given the timeout the C++ registration waits", () => {
    expect(registeredTimeoutMs("set_mesh_simple_collision")).toBe(300_000);
  });

  it("follows the editor's collision write: Modify, AggGeom, new GUID, cooked shapes, PostEditChange, components", () => {
    const body = handlerBody("Asset/AssetHandlers_Mesh.cpp", "FAssetHandlers::SetMeshSimpleCollision");
    const order = [
      "Setup->Modify()",
      "Setup->AggGeom.BoxElems.Append",
      "Setup->InvalidatePhysicsData()",
      "Setup->CreatePhysicsMeshes()",
      "Mesh->PostEditChange()",
      "Mesh->MarkPackageDirty()",
      "RecreatePhysicsState()",
    ].map((call) => {
      const at = body.indexOf(call);
      expect(at, call).toBeGreaterThan(0);
      return at;
    });
    expect([...order].sort((a, b) => a - b)).toEqual(order);
    // Every parameter is read before the asset load can refuse (#1057).
    expect(body.indexOf("MCPReadParamsAhead")).toBeLessThan(body.indexOf("MCPLoadAssetObject"));
    expect(body).toContain(`MCPSetRollback(Result, TEXT("set_mesh_simple_collision")`);
  });

  it("reports the GUID in get_mesh_collision, so a caller can see it change", () => {
    const body = handlerBody("Asset/AssetHandlers_Mesh.cpp", "FAssetHandlers::GetMeshCollision");
    expect(body).toContain(`TEXT("bodySetupGuid")`);
    expect(body).toContain(`TEXT("shapes")`);
  });
});

describe("level.resave_mesh_users", () => {
  const action = levelTool.actions.resave_mesh_users;

  it("is exposed through the level action schema as a mutation", () => {
    expect(levelTool.schema.action.safeParse("resave_mesh_users").success).toBe(true);
    expect(declaredActionEffect("level", "resave_mesh_users")).toBe("mutate");
  });

  it("takes its parameters from its C++ spec and forwards the bag as sent", () => {
    expect(action.bridge).toBe("resave_mesh_users");
    expect(action.mapParams).toBeUndefined();
    expect(action.description).toContain("Params: meshPaths, componentClass?, recreateCollision?, save?, dryRun?, maxActors?");
    const spec = action.kind === "bridge" ? action.paramSpec : undefined;
    expect(spec?.map((p) => [p.name, p.required])).toEqual([
      ["meshPaths", true], ["componentClass", false], ["recreateCollision", false],
      ["save", false], ["dryRun", false], ["maxActors", false],
    ]);
  });

  it("validates meshPaths as a list of paths and the flags as booleans", () => {
    expect(levelTool.schema.meshPaths.safeParse(["/Game/Props/SM_Rail", "/Game/Props/SM_Post.SM_Post"]).success).toBe(true);
    expect(levelTool.schema.meshPaths.safeParse("/Game/Props/SM_Rail").success).toBe(false);
    expect(levelTool.schema.meshPaths.safeParse([42]).success).toBe(false);
    expect(levelTool.schema.recreateCollision.safeParse(true).success).toBe(true);
    expect(levelTool.schema.recreateCollision.safeParse("yes").success).toBe(false);
    expect(levelTool.schema.maxActors.safeParse(1.5).success).toBe(false);
  });

  it("documents the dryRun default, the residency scope and the targeted save", () => {
    const description = action.description ?? "";
    expect(description).toContain("dryRun DEFAULTS TO TRUE");
    expect(description).toContain("load_actor_descs");
    expect(description).toContain("asset(save, assetPaths)");
    expect(description).toContain("RecreateCollision");
    expect(description).toMatch(/PIE/);
  });

  it("names no content it writes: the meshes are read and the actor packages are found in the editor", () => {
    expect(classifyWrite("resave_mesh_users", { meshPaths: ["/Game/Props/SM_Rail"], dryRun: false }))
      .toEqual({ writes: false, contentPaths: [] });
  });

  it("is given the timeout the C++ registration waits", () => {
    expect(registeredTimeoutMs("resave_mesh_users")).toBe(300_000);
    expect(action.kind === "bridge" && action.timeoutMs).toBe(300_000);
  });

  it("refuses PIE, rebuilds spline copies and saves through the targeted save only", () => {
    const body = handlerBody("Level/LevelHandlers_Refresh.cpp", "FLevelHandlers::ResaveMeshUsers");
    expect(body.indexOf("PlayWorld")).toBeGreaterThan(0);
    expect(body.indexOf("PlayWorld")).toBeLessThan(body.indexOf("RecreateCollision()"));
    expect(body).toContain("Spline->RecreateCollision()");
    expect(body).toContain("FAssetHandlers::SaveNamedPackages(");
    // No sweep: nothing but the matched actors' packages may be written.
    expect(body).not.toMatch(/SaveDirtyPackages|SavePackages\(|SaveDirectory|SaveCurrentLevel/);
    expect(body).toContain(`OptionalBool(Params, TEXT("dryRun"), true)`);
    expect(body).toContain("MCPSetNoRollback");
  });
});
