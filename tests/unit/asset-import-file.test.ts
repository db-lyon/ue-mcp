import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";

describe("asset.import_file", () => {
  it("is exposed through the asset action schema", () => {
    expect(assetTool.schema.action.safeParse("import_file").success).toBe(true);
  });

  it("forwards the factory selection and its properties to the bridge (#1096)", () => {
    const action = assetTool.actions.import_file;
    expect(action.bridge).toBe("import_file");
    expect(action.mapParams?.({
      action: "import_file",
      filePath: "C:/avatars/char.vrm",
      packagePath: "/Game/Avatars",
      name: "Char",
      factoryClass: "VRM4UImporterFactory",
      factoryProperties: { bUseProvidedImportOptions: true },
      replaceExisting: false,
      save: true,
      automated: true,
      unrelated: "ignored",
    })).toEqual({
      filename: "C:/avatars/char.vrm",
      destinationPath: "/Game/Avatars",
      assetName: "Char",
      factoryClass: "VRM4UImporterFactory",
      factoryProperties: { bUseProvidedImportOptions: true },
      replaceExisting: false,
      save: true,
      automated: true,
    });
  });

  it("accepts factoryProperties only as an object", () => {
    expect(assetTool.schema.factoryProperties.safeParse({ bUseProvidedImportOptions: true }).success).toBe(true);
    expect(assetTool.schema.factoryProperties.safeParse("bUseProvidedImportOptions=true").success).toBe(false);
  });
});
