/**
 * One owner for engine-version gating.
 *
 * Public/HandlerEngineVersion.h defines every version gate and feature macro.
 * scripts/audit-engine-gates.mjs reports a gate written anywhere else; this
 * holds the shipped tree to it and pins each shape the audit catches.
 */
import * as fs from "node:fs";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { EXEMPT, OWNER, findEngineGateViolations } from "../../scripts/audit-engine-gates.mjs";
import { markerFiles, pluginMarkerFiles } from "../../scripts/lib/tool-counts.js";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const source = path.join(repo, "plugin", "ue_mcp_bridge", "Source");

/** A Source root holding the given files, at paths relative to it. */
function sourceTree(files: Record<string, string>): string {
  const root = mkdtempSync(path.join(tmpdir(), "engine-gates-"));
  for (const [rel, body] of Object.entries(files)) {
    const full = path.join(root, rel);
    mkdirSync(path.dirname(full), { recursive: true });
    writeFileSync(full, body);
  }
  return root;
}

describe("the shipped plugin sources", () => {
  it("gate on the engine version only through HandlerEngineVersion.h", () => {
    const rendered = findEngineGateViolations()
      .map((v) => `${v.file}:${v.line} [${v.kind}] ${v.text}`)
      .join("\n");
    expect(rendered, "Run `npm run audit:engine-gates` for the advice per line.").toBe("");
  });

  it("keep the status module's one copy of the 5.8 gate identical to the header's", () => {
    // The status module cannot include the header, so it repeats this gate.
    const header = fs.readFileSync(path.join(source, OWNER), "utf8");
    const atLeast = /#define UE_MCP_ENGINE_AT_LEAST\(Major, Minor\) \\\r?\n\s*(.+)/.exec(header)?.[1];
    expect(atLeast).toBe(
      "((ENGINE_MAJOR_VERSION > (Major)) || (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))",
    );
    expect(header).toMatch(/#define UE_MCP_HAS_5_8_API UE_MCP_ENGINE_AT_LEAST\(5, 8\)/);
    const [status] = [...EXEMPT.keys()];
    const copy = /#define UE_MCP_HAS_5_8_API (.+)/.exec(fs.readFileSync(path.join(source, status), "utf8"))?.[1].trim();
    const expanded = atLeast!.replace(/\(Major\)/g, "5").replace(/\(Minor\)/g, "8");
    expect(copy).toBe(expanded);
  });

  it("state the supported range in the owner header, stamped by generate:metadata", () => {
    expect(pluginMarkerFiles()).toContain(`plugin/ue_mcp_bridge/Source/${OWNER}`);
    expect(markerFiles()).toContain(`plugin/ue_mcp_bridge/Source/${OWNER}`);
  });
});

describe("findEngineGateViolations", () => {
  it("reports a raw engine version read outside the header", () => {
    const root = sourceTree({
      "Mod/Private/A.cpp": "#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)\n#endif\n",
      "Mod/Private/B.cpp": "#if UE_VERSION_OLDER_THAN(5, 8, 0)\n#endif\n",
    });
    expect(findEngineGateViolations(root).map((v) => `${v.file}:${v.kind}`)).toEqual([
      "Mod/Private/A.cpp:raw-version",
      "Mod/Private/B.cpp:raw-version",
    ]);
  });

  it("allows the header itself, and a comment that only names a macro", () => {
    const root = sourceTree({
      [OWNER]: "#define UE_MCP_HAS_5_5_API (ENGINE_MINOR_VERSION >= 5)\n",
      "Mod/Private/A.cpp": "// Used to read ENGINE_MINOR_VERSION here.\n#if UE_MCP_HAS_5_5_API\n#endif\n",
    });
    expect(findEngineGateViolations(root)).toEqual([]);
  });

  it("reports a feature macro defined in a .cpp, which leaks across the unity blob", () => {
    const root = sourceTree({
      "Mod/Private/A.cpp": "#define UE_MCP_HAS_THING UE_MCP_HAS_5_7_API\n#define LOCAL_ONLY 1\n",
      "Mod/Private/A.h": "#define UE_MCP_HEADER_THING 1\n",
    });
    expect(findEngineGateViolations(root).map((v) => `${v.file}:${v.line}:${v.kind}`)).toEqual([
      "Mod/Private/A.cpp:1:cpp-define",
    ]);
  });

  it("reports a gate on the oldest supported engine, which is always true", () => {
    const root = sourceTree({ "Mod/Private/A.cpp": "#if WITH_X && UE_MCP_HAS_5_4_API\n#endif\n" });
    expect(findEngineGateViolations(root).map((v) => v.kind)).toEqual(["floor-gate"]);
  });

  it("reports a Build.cs test that a later major version fails, and accepts the at-least form", () => {
    const root = sourceTree({
      "Mod/Bad.Build.cs": "if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8) {}\n",
      "Mod/Good.Build.cs":
        "if (Target.Version.MajorVersion > 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8)) {}\n" +
        "if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5) {}\n",
    });
    expect(findEngineGateViolations(root).map((v) => `${v.file}:${v.kind}`)).toEqual(["Mod/Bad.Build.cs:major-equals"]);
  });
});
