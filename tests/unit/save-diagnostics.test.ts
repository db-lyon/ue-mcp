import { readFileSync } from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

const utils = readFileSync(
  path.resolve("plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Public/HandlerUtils.h"),
  "utf8",
);
const assetHandlers = readFileSync(
  path.resolve("plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AssetHandlers.cpp"),
  "utf8",
);

function body(source: string, signature: string): string {
  const start = source.indexOf(signature);
  expect(start, signature).toBeGreaterThanOrEqual(0);
  const end = source.indexOf("\n}", start);
  return source.slice(start, end);
}

describe("save failures carry the engine's reason (#1120)", () => {
  it("routes the bridge's own save through GWarn, not the fatal default", () => {
    expect(body(utils, "inline bool SaveAssetPackage(UObject* Asset)")).toContain("SaveArgs.Error = GWarn;");
  });

  it("quotes the captured reason from SaveAssetPackageChecked", () => {
    const checked = body(utils, "inline bool SaveAssetPackageChecked(");
    expect(checked).toContain("FMCPSaveDiagnostics Diagnostics;");
    expect(checked).toContain("MCPDescribeIllegalReference(EngineReason)");
  });

  it("parses the sentence SavePackage writes for an illegal reference", () => {
    // Fragments of the engine's format string in ValidateIllegalReferences.
    for (const fragment of [
      "Illegal reference to private object: '",
      "' referenced by '",
      "' (at '",
      "') in its '",
      "' property",
    ]) {
      expect(body(utils, "inline TSharedPtr<FJsonObject> MCPDescribeIllegalReference(")).toContain(fragment);
    }
  });

  it("attaches the diagnostics to asset(save) and save_all_dirty", () => {
    expect(body(assetHandlers, "TSharedPtr<FJsonValue> FAssetHandlers::SaveAsset(")).toContain("MCPAttachSaveDiagnostics(Result, SaveDiagnostics);");
    expect(body(assetHandlers, "TSharedPtr<FJsonValue> FAssetHandlers::SaveAllDirty(")).toContain("MCPAttachSaveDiagnostics(Result, SaveDiagnostics);");
  });
});
