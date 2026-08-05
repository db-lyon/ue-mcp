/**
 * One parameter contract for the whole `widget` category (#798).
 *
 * The category used to take `assetPath` in some actions, `path` in others,
 * and `name` + `packagePath` in the create actions, so a caller could not
 * tell from the schema which spelling an action wanted. These tests pin the
 * canonical names, pin the accepted aliases, and fail when a new action is
 * added without being classified, which is how the contract stays whole.
 */
import { describe, expect, it, vi } from "vitest";
import { widgetTool } from "../../src/tools/widget.js";
import { normalizeUnrealAssetPath } from "../../src/asset-path.js";
import type { ToolContext } from "../../src/types.js";

const ASSET = "/Game/_Project/UI/WBP_Example";

function stubCtx(): { ctx: ToolContext; call: ReturnType<typeof vi.fn> } {
  const call = vi.fn().mockResolvedValue({ success: true });
  return { ctx: { bridge: { call } } as unknown as ToolContext, call };
}

async function sent(params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const { ctx, call } = stubCtx();
  await widgetTool.handler(ctx, params);
  return call.mock.calls[0][1] as Record<string, unknown>;
}

/** Actions addressed by a Widget Blueprint / Editor Utility asset. */
const ASSET_ACTIONS = [
  "read_tree", "get_details", "get_properties", "list_bindings", "clear_binding",
  "set_property", "set_style", "reorder_child", "bulk_set_properties",
  "read_animations", "create", "create_utility_widget", "run_utility_widget",
  "create_utility_blueprint", "run_utility_blueprint", "add_widget", "remove_widget",
  "move_widget", "set_root", "wrap_root", "add_to_viewport",
];

/** Actions that address a live instance or the project, never a single asset. */
const NON_ASSET_ACTIONS = [
  "list", "list_classes", "list_runtime", "get_runtime", "get_runtime_delegates",
  "invoke_runtime_function",
];

describe("widget category parameter contract", () => {
  it("classifies every action, so a new one cannot skip the contract", () => {
    const classified = new Set([...ASSET_ACTIONS, ...NON_ASSET_ACTIONS]);
    const declared = Object.keys(widgetTool.actions);
    const unclassified = declared.filter((a) => !classified.has(a));
    const stale = [...classified].filter((a) => !declared.includes(a));
    expect({ unclassified, stale }).toEqual({ unclassified: [], stale: [] });
  });

  it("every asset action accepts the canonical assetPath", async () => {
    for (const action of ASSET_ACTIONS) {
      const params = await sent({ action, assetPath: ASSET, widgetName: "Root" });
      expect(params.assetPath, `${action} dropped assetPath`).toBe(ASSET);
    }
  });

  it("every asset action accepts the legacy path alias", async () => {
    for (const action of ASSET_ACTIONS) {
      const params = await sent({ action, path: ASSET, widgetName: "Root" });
      expect(params.assetPath, `${action} dropped the path alias`).toBe(ASSET);
    }
  });

  it("declares every accepted alias in the public schema, so none is stripped", () => {
    for (const alias of ["path", "widgetBlueprintPath", "widgetBlueprint", "widgetDisplayName", "parentWidget"]) {
      expect(widgetTool.schema[alias], `${alias} is not declared`).toBeDefined();
    }
  });

  it("accepts the engine's object reference spellings for the asset", async () => {
    for (const value of [
      { widgetBlueprint: ASSET },
      { widgetBlueprint: { refPath: ASSET } },
      { widgetBlueprint: JSON.stringify({ refPath: ASSET }) },
      { widgetBlueprintPath: ASSET },
    ]) {
      const params = await sent({ action: "read_tree", ...value });
      expect(params.assetPath).toBe(ASSET);
    }
  });

  it("repairs the path spellings callers actually send", async () => {
    for (const raw of [
      `${ASSET}.uasset`,
      `${ASSET}.WBP_Example`,
      `${ASSET}.WBP_Example_C`,
      ASSET.replace(/\//g, "\\"),
      `${ASSET}/`,
      `  ${ASSET}  `,
    ]) {
      const params = await sent({ action: "read_tree", assetPath: raw });
      expect(params.assetPath, `failed to normalize ${raw}`).toBe(ASSET);
    }
  });

  it("names the offending field when a path cannot be repaired", () => {
    expect(() => normalizeUnrealAssetPath("C:/Projects/UI/WBP_Example.uasset"))
      .toThrow(/assetPath .* is a filesystem path/);
    expect(() => normalizeUnrealAssetPath("WBP_Example"))
      .toThrow(/not a mount-rooted path/);
    expect(() => normalizeUnrealAssetPath("", "path"))
      .toThrow(/^path must not be empty/);
    expect(() => normalizeUnrealAssetPath("/Game/UI/WBP_Example"))
      .not.toThrow();
  });

  it("mirrors the canonical asset path onto the legacy wire field", async () => {
    const params = await sent({ action: "read_tree", assetPath: ASSET });
    expect(params.path).toBe(ASSET);
  });

  describe("create actions", () => {
    it("splits the canonical assetPath into the name and package the bridge takes", async () => {
      for (const action of ["create", "create_utility_widget", "create_utility_blueprint"]) {
        const params = await sent({ action, assetPath: `${ASSET}.uasset` });
        expect(params.assetPath).toBe(ASSET);
        expect(params.name).toBe("WBP_Example");
        expect(params.packagePath).toBe("/Game/_Project/UI");
      }
    });

    it("composes the older name plus packagePath spelling into assetPath", async () => {
      const params = await sent({
        action: "create",
        name: "WBP_Example",
        packagePath: "/Game/_Project/UI",
        parentClass: "/Script/UMG.UserWidget",
      });
      expect(params.assetPath).toBe(ASSET);
      expect(params.parentClass).toBe("/Script/UMG.UserWidget");
    });

    it("keeps a bare name working, since the bridge supplies the default package", async () => {
      const params = await sent({ action: "create", name: "WBP_Example" });
      expect(params.name).toBe("WBP_Example");
      expect(params.assetPath).toBeUndefined();
    });

    it("rejects a name that disagrees with assetPath instead of creating the wrong asset", async () => {
      await expect(sent({
        action: "create",
        assetPath: "/Game/UI/WBP_ComputerDesktop.uasset",
        name: "ComputerDesktop",
        packagePath: "/Game/UI",
      })).rejects.toThrow(/Conflicting parameters/);
    });

    it("rejects a name carrying a path or extension, which the editor reports as a generic failure", async () => {
      await expect(sent({ action: "create", name: "WBP_Example.uasset" }))
        .rejects.toThrow(/is not a bare asset name/);
    });
  });

  describe("widget identity", () => {
    it("folds widgetDisplayName into widgetName", async () => {
      const params = await sent({ action: "remove_widget", assetPath: ASSET, widgetDisplayName: "StartButton" });
      expect(params.widgetName).toBe("StartButton");
    });

    it("folds parentWidget into parentWidgetName", async () => {
      const params = await sent({
        action: "add_widget", assetPath: ASSET, widgetClass: "/Script/UMG.Button", parentWidget: "HorizontalBox_59",
      });
      expect(params.parentWidgetName).toBe("HorizontalBox_59");
    });

    it("prefers the canonical name over the alias", async () => {
      const params = await sent({
        action: "remove_widget", assetPath: ASSET, widgetName: "Canonical", widgetDisplayName: "Alias",
      });
      expect(params.widgetName).toBe("Canonical");
    });
  });
});
