import { describe, expect, it } from "vitest";
import { assetTool } from "../../../src/tools/asset.js";

describe("asset.bulk_set_properties", () => {
  it("is exposed through the asset action schema", () => {
    expect(assetTool.schema.action.safeParse("bulk_set_properties").success).toBe(true);
  });

  it("accepts a bounded asset property batch", () => {
    const items = [
      { assetPath: "/Game/Data/One.One", properties: { Category: "Weapons" } },
      { assetPath: "/Game/Data/Two.Two", properties: { "Config.Weight": 1.5 } },
    ];
    expect(assetTool.schema.items.safeParse(items).success).toBe(true);
  });

  it("rejects empty and oversized batches", () => {
    expect(assetTool.schema.items.safeParse([]).success).toBe(false);
    expect(assetTool.schema.items.safeParse(
      Array.from({ length: 501 }, (_, i) => ({ assetPath: `/Game/A${i}`, properties: { Value: i } })),
    ).success).toBe(false);
  });

  it("forwards its bag as sent, as its C++ spec declares (#1057)", () => {
    const action = assetTool.actions.bulk_set_properties;
    expect(action.bridge).toBe("bulk_set_asset_properties");
    expect(action.mapParams).toBeUndefined();
    expect(action.description).toContain("Params: items, save?, dryRun?, continueOnError?");
  });

  it("exposes continueOnError so a partial batch is reachable", () => {
    expect(assetTool.schema.continueOnError.safeParse(true).success).toBe(true);
    expect(assetTool.schema.continueOnError.safeParse("yes").success).toBe(false);
    // Omitting it must stay valid: the default is the all-or-nothing batch.
    expect(assetTool.schema.continueOnError.safeParse(undefined).success).toBe(true);
  });

  it("documents the per-item reporting contract in the action description", () => {
    const description = assetTool.actions.bulk_set_properties.description ?? "";
    expect(description).toContain("continueOnError");
    expect(description).toMatch(/per-item|its own ok/);
  });
});
