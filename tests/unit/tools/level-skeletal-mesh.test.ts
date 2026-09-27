import { describe, expect, it } from "vitest";
import { levelTool } from "../../../src/tools/level.js";

function fakeCtx(seen: Array<Record<string, unknown>>) {
  return {
    bridge: {
      isConnected: true,
      call: async (_m: string, params: Record<string, unknown>) => { seen.push(params); return { success: true }; },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
      connect: async () => {},
      retargetProject: () => ({}),
    },
    project: {},
  } as never;
}

describe("level.set_component_skeletal_mesh (#1099)", () => {
  it("is exposed and takes its parameters from its spec, the actor as a choice", () => {
    expect(levelTool.schema.action.safeParse("set_component_skeletal_mesh").success).toBe(true);
    const action = levelTool.actions.set_component_skeletal_mesh;
    expect(action.bridge).toBe("set_component_skeletal_mesh");
    expect(action.kind === "bridge" && action.paramSpec?.map((p) => p.name)).toEqual(
      ["actorLabel", "actorPath", "skeletalMesh", "componentName", "world", "pieInstance"],
    );
    expect(action.kind === "bridge" && action.paramChoices).toEqual([
      { mode: "exactlyOne", branches: [["actorLabel"], ["actorPath"]] },
    ]);
    expect(action.description).toContain("Params: actorLabel OR actorPath, skeletalMesh, componentName?, world?, pieInstance?");
  });

  it("accepts null to clear the mesh, and sends it", async () => {
    expect(levelTool.schema.skeletalMesh.safeParse(null).success).toBe(true);
    const seen: Array<Record<string, unknown>> = [];
    await levelTool.handler(fakeCtx(seen), { action: "set_component_skeletal_mesh", actorLabel: "A", skeletalMesh: null });
    expect(seen).toEqual([{ actorLabel: "A", skeletalMesh: null }]);
  });

  it("refuses a call that names no actor, or names it twice, before sending", async () => {
    const seen: Array<Record<string, unknown>> = [];
    await expect(levelTool.handler(fakeCtx(seen), { action: "set_component_skeletal_mesh", skeletalMesh: null }))
      .rejects.toThrow(/needs actorLabel OR actorPath/);
    await expect(levelTool.handler(fakeCtx(seen), {
      action: "set_component_skeletal_mesh", actorLabel: "A", actorPath: "/Game/M.M:PersistentLevel.A", skeletalMesh: null,
    })).rejects.toThrow(/takes one side of actorLabel OR actorPath/);
    expect(seen).toEqual([]);
  });
});

describe("level.set_actor_hlod_layer (#985)", () => {
  it("clears with null across a selector, and refuses a call with no selector before sending", async () => {
    const seen: Array<Record<string, unknown>> = [];
    await levelTool.handler(fakeCtx(seen), { action: "set_actor_hlod_layer", hlodLayer: null, tag: "Foliage", labelPrefix: "Tree_" });
    expect(seen).toEqual([{ hlodLayer: null, tag: "Foliage", labelPrefix: "Tree_" }]);
    await expect(levelTool.handler(fakeCtx(seen), { action: "set_actor_hlod_layer", hlodLayer: null, dryRun: true }))
      .rejects.toThrow(/needs at least one of actorLabels\/labelPrefix\/labelContains\/tag\/classFilter\/folderPath\/folderPathPrefix/);
    expect(seen).toHaveLength(1);
  });
});

describe("level.set_component_materials null entries (#1099)", () => {
  it("accepts a null entry, which the description promises clears one slot", () => {
    // The spec declares the elements untyped: it has no nullable element type,
    // and a string element would refuse the null. The handler refuses an entry
    // that is neither a path nor null or empty.
    expect(levelTool.schema.materials.safeParse([null, "/Game/M_A", ""]).success).toBe(true);
  });
});
