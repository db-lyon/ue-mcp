import { describe, expect, it, vi } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import type { ToolContext } from "../../src/surface/tool-types.js";

/**
 * Core PIE runtime actions. Each case asserts that the parameters a caller
 * passes reach the bridge method under the names the C++ handler reads.
 */
function mockContext() {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  return { call, ctx };
}

function forwarded(call: ReturnType<typeof vi.fn>, method: string): Record<string, unknown> {
  const hit = call.mock.calls.find((c) => c[0] === method);
  expect(hit, `bridge method ${method} was called`).toBeDefined();
  return hit![1] as Record<string, unknown>;
}

describe("PIE pause (#1215)", () => {
  it("forwards paused, mode and force to set_pie_paused", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, { action: "set_pie_paused", paused: false, mode: "game", force: true, pieInstance: 1 });
    const bag = forwarded(call, "set_pie_paused");
    expect(bag.paused).toBe(false);
    expect(bag.mode).toBe("game");
    expect(bag.force).toBe(true);
    expect(bag.pieInstance).toBe(1);
  });
});

describe("runtime target propertyPath (#1215)", () => {
  // Each action gets only the keys it reads, so a stray key is never what the test exercises.
  const own: Record<string, Record<string, unknown>> = {
    invoke_object_function: { functionName: "SetSomeOption" },
    get_object_properties: { propertyNames: ["bEnabled"] },
    set_object_property: { propertyName: "bEnabled", value: true },
  };
  it.each(Object.keys(own))(
    "%s forwards propertyPath",
    async (action) => {
      const { call, ctx } = mockContext();
      await editorTool.handler(ctx, {
        action,
        target: "gameinstance",
        propertyPath: "GameSettings.Game Settings",
        ...own[action],
      });
      expect(forwarded(call, action).propertyPath).toBe("GameSettings.Game Settings");
    },
  );

  it("is accepted on each invoke_object_functions entry", () => {
    const parsed = editorTool.schema.calls.safeParse([
      { functionName: "SetSomeOption", target: "gameinstance", propertyPath: "GameSettings.Game Settings" },
    ]);
    expect(parsed.success).toBe(true);
  });
});

describe("runtime property paths and physics reads (#1212)", () => {
  it("get_object_properties forwards dotted and indexed paths untouched", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, {
      action: "get_object_properties",
      target: "gamemode",
      propertyNames: ["StashWorld.KickRadiusCm", "Items[0].Count", "bIsActive"],
    });
    expect(forwarded(call, "get_object_properties").propertyNames).toEqual([
      "StashWorld.KickRadiusCm",
      "Items[0].Count",
      "bIsActive",
    ]);
  });

  it("query_components forwards the physics field group against the PIE world", async () => {
    const { levelTool } = await import("../../src/tools/level.js");
    const { call, ctx } = mockContext();
    await levelTool.handler(ctx, { action: "query_components", actorClass: "StaticMeshActor", fields: ["physics"], world: "pie" });
    const bag = forwarded(call, "query_components");
    expect(bag.fields).toEqual(["physics"]);
    expect(bag.world).toBe("pie");
  });
});

describe("configure_pie client windows (#1235)", () => {
  it("forwards client window size, position and centering", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, {
      action: "configure_pie",
      clientWindowWidth: 960,
      clientWindowHeight: 540,
      newWindowPosition: { x: 1920, y: 0 },
      centerNewWindow: false,
    });
    const bag = forwarded(call, "configure_pie");
    expect(bag.clientWindowWidth).toBe(960);
    expect(bag.clientWindowHeight).toBe(540);
    expect(bag.newWindowPosition).toEqual({ x: 1920, y: 0 });
    expect(bag.centerNewWindow).toBe(false);
  });

  it("refuses a position without both coordinates", () => {
    expect(editorTool.schema.newWindowPosition.safeParse({ x: 10 }).success).toBe(false);
  });
});

describe("PIE view target (#1203)", () => {
  it("forwards the actor selector, player and blend to set_pie_view_target", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, { action: "set_pie_view_target", actorLabel: "CameraActor_2", playerIndex: 0, blendTime: 0.5 });
    const bag = forwarded(call, "set_pie_view_target");
    expect(bag.actorLabel).toBe("CameraActor_2");
    expect(bag.playerIndex).toBe(0);
    expect(bag.blendTime).toBe(0.5);
  });
});

describe("struct read-modify-write (#1203)", () => {
  it("forwards modify to invoke_object_function", async () => {
    const { call, ctx } = mockContext();
    const modify = { argument: "NewSettings", getter: "GetSettings", patch: { Speed: 300, "Weights": [{ key: "Arm", value: 0.5 }] } };
    await editorTool.handler(ctx, { action: "invoke_object_function", objectPath: "/Game/Maps/UEDPIE_0_L.L:PersistentLevel.NPC_0.Body", functionName: "SetSettings", modify });
    expect(forwarded(call, "invoke_object_function").modify).toEqual(modify);
  });

  it("requires argument and getter", () => {
    expect(editorTool.schema.modify.safeParse({ getter: "GetSettings" }).success).toBe(false);
  });
});

describe("PIE debug camera, console route and socket calibration (#1250)", () => {
  it("forwards the pose to pose_debug_camera", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, {
      action: "pose_debug_camera",
      location: { x: 100, y: 0, z: 200 },
      lookAtActorLabel: "BP_Player_C_0",
      playerIndex: 0,
    });
    const bag = forwarded(call, "pose_debug_camera");
    expect(bag.location).toEqual({ x: 100, y: 0, z: 200 });
    expect(bag.lookAtActorLabel).toBe("BP_Player_C_0");
  });

  it("routes execute_command to a PIE player with world, pieInstance and playerIndex", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, { action: "execute_command", command: "slomo 0.25", world: "pie", pieInstance: 1, playerIndex: 0 });
    const bag = forwarded(call, "execute_command");
    expect(bag.command).toBe("slomo 0.25");
    expect(bag.world).toBe("pie");
    expect(bag.pieInstance).toBe(1);
    expect(bag.playerIndex).toBe(0);
  });

  it("forwards a corrective relative transform with attach_component", async () => {
    const { levelTool } = await import("../../src/tools/level.js");
    const { call, ctx } = mockContext();
    await levelTool.handler(ctx, {
      action: "attach_component",
      childLabel: "BP_Rod",
      parentLabel: "BP_Fisher",
      parentComponentName: "Mesh",
      socketName: "hand_r",
      relativeRotation: { pitch: 0, yaw: 90, roll: 0 },
    });
    const bag = forwarded(call, "attach_component");
    expect(bag.relativeRotation).toEqual({ pitch: 0, yaw: 90, roll: 0 });
    expect(bag.socketName).toBe("hand_r");
  });
});

describe("query_components per-actor state (#1303)", () => {
  it("forwards actorPropertyNames and the customPrimitiveData group", async () => {
    const { levelTool } = await import("../../src/tools/level.js");
    const { call, ctx } = mockContext();
    await levelTool.handler(ctx, {
      action: "query_components",
      actorClass: "BP_Crowd_C",
      componentClass: "SkeletalMeshComponent",
      fields: ["mesh", "materials", "customPrimitiveData"],
      actorPropertyNames: ["bIsSeated"],
      countBy: ["actorProps.bIsSeated", "materials.0.materialClass"],
    });
    const bag = forwarded(call, "query_components");
    expect(bag.actorPropertyNames).toEqual(["bIsSeated"]);
    expect(bag.fields).toEqual(["mesh", "materials", "customPrimitiveData"]);
  });
});

describe("Python line trace helper (#1283)", () => {
  it("is named where a run_python_file caller and a line_trace caller look", async () => {
    const { levelTool } = await import("../../src/tools/level.js");
    expect(editorTool.actions.run_python_file.description).toContain("unreal.MCPTraceLibrary.line_trace");
    expect(levelTool.actions.line_trace.description).toContain("unreal.MCPTraceLibrary.line_trace");
  });
});
