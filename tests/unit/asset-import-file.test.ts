import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";
import type { ToolContext } from "../../src/core/types.js";

describe("asset.import_file", () => {
  it("is exposed through the asset action schema", () => {
    expect(assetTool.schema.action.safeParse("import_file").success).toBe(true);
  });

  it("forwards the factory selection and its properties to the bridge (#1096)", async () => {
    const action = assetTool.actions.import_file;
    expect(action.bridge).toBe("import_file");
    // #1057: the handler declares these names itself, so the bag goes as sent.
    expect(action.mapParams).toBeUndefined();

    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = {
      filePath: "C:/avatars/char.vrm",
      packagePath: "/Game/Avatars",
      name: "Char",
      factoryClass: "VRM4UImporterFactory",
      factoryProperties: { bUseProvidedImportOptions: true },
      replaceExisting: false,
      save: true,
      automated: true,
    };
    await assetTool.handler(ctx, { action: "import_file", ...params });
    expect(call).toHaveBeenCalledWith("import_file", params, undefined);
  });

  it("keeps the bridge's older spellings as aliases", () => {
    const aliases = Object.fromEntries(handlerSpecs.import_file.params.map((p) => [p.name, p.aliases ?? []]));
    expect(aliases.filePath).toContain("filename");
    expect(aliases.packagePath).toContain("destinationPath");
    expect(aliases.name).toContain("assetName");
  });

  it("accepts factoryProperties only as an object", () => {
    expect(assetTool.schema.factoryProperties.safeParse({ bUseProvidedImportOptions: true }).success).toBe(true);
    expect(assetTool.schema.factoryProperties.safeParse("bUseProvidedImportOptions=true").success).toBe(false);
  });
});
