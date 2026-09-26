// #983: editor labels are not unique. actorPath is the unambiguous selector,
// and the bridge already returned one in its results before it would accept one
// as input. These assertions cover the schema half of closing that round trip:
// the parameter is advertised, and a param mapper that whitelists keys forwards
// it instead of dropping it on the way to the bridge.
//
// A dropped parameter is the failure mode this guards: the schema advertises
// actorPath, the caller sends it, the mapper does not list it, and the handler
// reports the label as missing. That is exactly the response the report carried
// ("Missing required parameter 'actorLabel'" after passing actorPath).

import { describe, expect, it, vi } from "vitest";
import { levelTool } from "../../src/tools/level.js";
import { editorTool } from "../../src/tools/editor.js";
import { gameplayTool } from "../../src/tools/gameplay.js";
import { gasTool } from "../../src/tools/gas.js";
import { animationTool } from "../../src/tools/animation.js";
import { niagaraTool } from "../../src/tools/niagara.js";
import { pcgTool } from "../../src/tools/pcg.js";
import { landscapeTool } from "../../src/tools/landscape.js";
import type { ToolDef, ToolContext } from "../../src/core/types.js";

const ACTOR_PATH = "/Game/Maps/Main.Main:PersistentLevel.BP_SnappyRoad2_3";

function bridgeContext() {
  const call = vi.fn().mockResolvedValue({ success: true });
  return { call, ctx: { bridge: { call } } as unknown as ToolContext };
}

/** Every category that resolves an actor advertises the selector. Keyed by
 *  name rather than held as tuples, because vitest renders each it.each row
 *  into the test name and a whole ToolDef there is unreadable. */
const TOOLS: Record<string, ToolDef> = {
  level: levelTool,
  editor: editorTool,
  gameplay: gameplayTool,
  gas: gasTool,
  animation: animationTool,
  niagara: niagaraTool,
  pcg: pcgTool,
  landscape: landscapeTool,
};
const CATEGORIES = Object.keys(TOOLS);

describe("actorPath is advertised wherever an actor is selected", () => {
  it.each(CATEGORIES)("%s declares actorPath as an optional string", (name) => {
    const schema = TOOLS[name].schema.actorPath;
    expect(schema).toBeDefined();
    expect(schema!.safeParse(ACTOR_PATH).success).toBe(true);
    expect(schema!.safeParse(undefined).success).toBe(true);
    expect(schema!.safeParse(42).success).toBe(false);
  });

  it.each(CATEGORIES)("%s explains that a label is not unique", (name) => {
    // The description is where a caller learns the rule, so it has to say both
    // halves: the path wins, and an ambiguous label is refused rather than
    // resolved at random.
    const described = (TOOLS[name].schema.actorPath as { description?: string }).description ?? "";
    expect(described).toMatch(/object path/i);
    expect(described).toMatch(/not unique|ambiguous/i);
  });

  it("declares the paired path selectors level actions accept", () => {
    for (const key of [
      "actorPaths",
      "targetPath",
      "referencePath",
      "targetActorPath",
      "childPath",
      "parentPath",
      "actorPathA",
      "actorPathB",
    ]) {
      expect(levelTool.schema[key], `level.${key}`).toBeDefined();
    }
    expect(editorTool.schema.focusActorPath).toBeDefined();
    expect(editorTool.schema.cameraActorPath).toBeDefined();
    expect(gameplayTool.schema.pathfindingContextPath).toBeDefined();
  });
});

describe("actorPath survives the param mappers", () => {
  // Every action whose mapper whitelists keys: an omission here is invisible
  // until a caller passes actorPath and is told actorLabel is missing.
  const CASES: Array<[string, string, string]> = [
    ["level", "get_spline_info", "get_spline_info"],
    ["level", "move_actor", "move_actor"],
    ["level", "get_component_tree", "get_component_tree"],
    ["level", "set_actor_property", "set_actor_property"],
    ["level", "get_instance_transforms", "get_instance_transforms"],
    ["level", "get_component_details", "get_component_details"],
    ["level", "list_actor_tags", "list_actor_tags"],
    ["level", "snap_actor_to_floor", "snap_actor_to_floor"],
    ["editor", "read_bone_transforms", "read_bone_transforms"],
    ["editor", "invoke_function", "invoke_function"],
    ["editor", "teleport_runtime_actor", "teleport_runtime_actor"],
    ["gameplay", "add_impulse", "add_impulse"],
    ["gameplay", "get_state_tree_runtime", "get_state_tree_runtime"],
    ["gas", "get_asc_state", "get_asc_state"],
    ["gas", "get_live_attribute_value", "get_live_attribute_value"],
    ["animation", "get_bone_transform", "get_bone_transform"],
    ["animation", "preview_animation", "preview_animation"],
    ["niagara", "reactivate", "reactivate_niagara"],
    ["pcg", "cleanup", "cleanup_pcg"],
    ["landscape", "sculpt", "sculpt_landscape"],
  ];

  it.each(CASES)("%s(%s) forwards actorPath", async (name, action, bridgeMethod) => {
    const { call, ctx } = bridgeContext();
    await TOOLS[name].handler(ctx, { action, actorPath: ACTOR_PATH });
    expect(call).toHaveBeenCalledTimes(1);
    expect(call.mock.calls[0][0]).toBe(bridgeMethod);
    expect(call.mock.calls[0][1]).toMatchObject({ actorPath: ACTOR_PATH });
  });

  it("sends both selectors when both are given, and lets the bridge prefer the path", async () => {
    // The precedence rule lives in the handler, which is the only place that
    // can see the world. The client must not resolve it by dropping one. An
    // action whose spec makes the pair a choice refuses both instead.
    const { call, ctx } = bridgeContext();
    await levelTool.handler(ctx, {
      action: "move_actor",
      actorLabel: "BP_SnappyRoad2",
      actorPath: ACTOR_PATH,
    });
    expect(call.mock.calls[0][1]).toMatchObject({
      actorLabel: "BP_SnappyRoad2",
      actorPath: ACTOR_PATH,
    });
  });

  it("forwards the paired selectors on the two-ended actions", async () => {
    const { call, ctx } = bridgeContext();
    await levelTool.handler(ctx, {
      action: "attach_actor",
      childPath: ACTOR_PATH,
      parentPath: "/Game/Maps/Main.Main:PersistentLevel.Building_0",
    });
    expect(call.mock.calls[0][1]).toMatchObject({
      childPath: ACTOR_PATH,
      parentPath: "/Game/Maps/Main.Main:PersistentLevel.Building_0",
    });

    const second = bridgeContext();
    await levelTool.handler(second.ctx, {
      action: "get_relative_transform",
      targetPath: ACTOR_PATH,
      referencePath: "/Game/Maps/Main.Main:PersistentLevel.Building_0",
    });
    expect(second.call.mock.calls[0][1]).toMatchObject({
      targetPath: ACTOR_PATH,
      referencePath: "/Game/Maps/Main.Main:PersistentLevel.Building_0",
    });

    const third = bridgeContext();
    await levelTool.handler(third.ctx, {
      action: "test_component_overlap",
      actorPathA: ACTOR_PATH,
      actorPathB: "/Game/Maps/Main.Main:PersistentLevel.Building_0",
    });
    expect(third.call.mock.calls[0][1]).toMatchObject({
      actorPathA: ACTOR_PATH,
      actorPathB: "/Game/Maps/Main.Main:PersistentLevel.Building_0",
    });
  });

  it("keeps the gas live-attribute actions from folding the path into the label", async () => {
    // These two mappers used to send `actorLabel: p.actorLabel ?? p.actorPath`,
    // which handed a path to the label parameter. The handler reads them as
    // separate selectors, so both have to arrive under their own names.
    const { call, ctx } = bridgeContext();
    await gasTool.handler(ctx, {
      action: "get_live_attribute_value",
      actorPath: ACTOR_PATH,
      attributeSet: "MyAttributeSet",
      attribute: "Health",
    });
    const sent = call.mock.calls[0][1] as Record<string, unknown>;
    expect(sent.actorPath).toBe(ACTOR_PATH);
    expect(sent.actorLabel).toBeUndefined();
  });

  it("forwards the plural path list where the plural answer is the correct one", async () => {
    const { call, ctx } = bridgeContext();
    await levelTool.handler(ctx, {
      action: "read_actor_motion",
      actorPaths: [ACTOR_PATH],
    });
    expect(call.mock.calls[0][1]).toMatchObject({ actorPaths: [ACTOR_PATH] });

    const second = bridgeContext();
    await levelTool.handler(second.ctx, {
      action: "batch_translate",
      offset: { x: 0, y: 0, z: 100 },
      actorPaths: [ACTOR_PATH],
    });
    expect(second.call.mock.calls[0][1]).toMatchObject({ actorPaths: [ACTOR_PATH] });
  });
});
