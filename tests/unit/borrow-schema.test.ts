import { describe, it, expect } from "vitest";
import { borrowSchema } from "../../src/surface/handler-spec.js";
import { levelTool } from "../../src/tools/level.js";
import { assetTool } from "../../src/tools/asset.js";
import { schema as editorSpecSchema } from "../../src/tools/specs/editor.generated.js";
import { schema as gameplaySpecSchema } from "../../src/tools/specs/gameplay.generated.js";

describe("keys borrowed from another category's spec", () => {
  it("level declares the editor spec's own templateLevel and quality", () => {
    expect(levelTool.schema.templateLevel).toBe(editorSpecSchema.templateLevel);
    expect(levelTool.schema.quality).toBe(editorSpecSchema.quality);
  });

  it("asset declares the gameplay spec's IMC keys", () => {
    for (const key of ["mappingContext", "inputAction", "imcPath", "inputActionPath", "mappingIndex"]) {
      expect(assetTool.schema[key]).toBe(gameplaySpecSchema[key]);
    }
  });

  it("refuses a key the spec does not declare", () => {
    expect(() => borrowSchema(editorSpecSchema, ["noSuchKey"])).toThrow(/noSuchKey/);
  });
});
