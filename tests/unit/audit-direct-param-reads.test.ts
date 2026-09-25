import { describe, it, expect } from "vitest";
import { readFileSync } from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import {
  auditDirectParamReads,
  categoryOfFile,
  findDirectReads,
  reportingCategories,
} from "../../scripts/audit-direct-param-reads.mjs";
import { ROUTING_PARAM_NAMES } from "../../src/routing-params.js";

const REPO_ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const HANDLER_UTILS = path.join(
  REPO_ROOT, "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Public", "HandlerUtils.h",
);

describe("the #1057 C++ read tracking", () => {
  it("pilots on animation", () => {
    expect(reportingCategories()).toContain("animation");
  });

  it("leaves no direct Params read in a reporting category", () => {
    // A direct read is invisible to the tracking, so its key would be reported
    // as unread on every call that sends it.
    const reporting = new Set(reportingCategories());
    const offenders = auditDirectParamReads()
      .filter((c) => reporting.has(c.category))
      .flatMap((c) => c.files.flatMap((f) => f.reads.map((r) => `${f.file}:${r.line} Params->${r.method}`)));
    expect(offenders).toEqual([]);
  });

  it("ignores the same routing names as the server", () => {
    const source = readFileSync(HANDLER_UTILS, "utf8");
    const block = source.match(/MCPRoutingParamNames\(\)\s*\{[\s\S]*?Names\s*=\s*\{([\s\S]*?)\};/);
    expect(block).not.toBeNull();
    const names = [...block![1].matchAll(/TEXT\("([^"]+)"\)/g)].map((m) => m[1]);
    expect(names).toEqual(ROUTING_PARAM_NAMES);
  });
});

describe("findDirectReads", () => {
  it("finds reads of the handler's own Params and names the key", () => {
    const reads = findDirectReads([
      'double X = 0;',
      'Params->TryGetNumberField(TEXT("x"), X);',
      'if (Params->HasField(Key)) {}',
      'const auto* V = Params->Values.Find(Field);',
    ].join("\n"));
    expect(reads).toEqual([
      { line: 2, method: "TryGetNumberField", key: "x" },
      { line: 3, method: "HasField", key: "Key" },
      { line: 4, method: "Values", key: null },
    ]);
  });

  it("skips comments, other objects and the tracked helpers", () => {
    const reads = findDirectReads([
      '// Params->TryGetStringField(TEXT("a"), A);',
      '/* Params->HasField(TEXT("b")) */',
      'CreateParams->SetStringField(TEXT("c"), C);',
      'Item->TryGetStringField(TEXT("d"), D);',
      'TryGetStringParam(Params, TEXT("e"), E);',
      'Log(TEXT("Params->HasField is not code"));',
    ].join("\n"));
    expect(reads).toEqual([]);
  });
});

describe("categoryOfFile", () => {
  it("names the category a handler file belongs to", () => {
    expect(categoryOfFile("AnimationHandlers.cpp")).toBe("animation");
    expect(categoryOfFile("AnimationHandlers_Pose.cpp")).toBe("animation");
    expect(categoryOfFile("WidgetHandlers_Animation.cpp")).toBe("widget");
    expect(categoryOfFile("VolumeHelpers_Internal.cpp")).toBeNull();
    expect(categoryOfFile("AnimationHandlers.h")).toBeNull();
  });
});
