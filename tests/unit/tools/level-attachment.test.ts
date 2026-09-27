import { describe, expect, it } from "vitest";
import { levelTool } from "../../../src/tools/level.js";

// A spec'd action has no mapper and forwards its bag as sent (#1057).
const forwarded = (action: string, params: Record<string, unknown>) =>
  levelTool.actions[action].mapParams?.(params) ?? params;

describe("level attachment surface", () => {
  it("forwards component, actor, socket, and transform-rule selectors", () => {
    const mapped = forwarded("attach_component", {
      childLabel: "Preview",
      parentLabel: "Character",
      childComponentName: "StaticMeshComponent0",
      parentComponentName: "CharacterMesh0",
      socketName: "weapon_r",
      attachRule: "SnapToTarget",
      weldSimulatedBodies: false,
    });

    expect(mapped).toEqual({
      childLabel: "Preview",
      parentLabel: "Character",
      childComponentName: "StaticMeshComponent0",
      parentComponentName: "CharacterMesh0",
      socketName: "weapon_r",
      attachRule: "SnapToTarget",
      weldSimulatedBodies: false,
    });
  });

  it("declares the component selectors and welding flag in the shared schema", () => {
    expect(levelTool.schema.childComponentName.safeParse("PreviewMesh").success).toBe(true);
    expect(levelTool.schema.parentComponentName.safeParse("CharacterMesh0").success).toBe(true);
    expect(levelTool.schema.weldSimulatedBodies.safeParse(false).success).toBe(true);
    expect(levelTool.schema.weldSimulatedBodies.safeParse("false").success).toBe(false);
  });

  it("forwards the child component selector when detaching", () => {
    const mapped = forwarded("detach_component", {
      childLabel: "Preview",
      childComponentName: "PreviewMesh",
    });

    expect(mapped).toEqual({
      childLabel: "Preview",
      childComponentName: "PreviewMesh",
    });
  });
});
