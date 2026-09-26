import { readFileSync } from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import { REGISTERED_HANDLER_TIMEOUT_SECONDS } from "../../src/bridge/bridge-timeouts.js";
import type { ToolContext } from "../../src/core/types.js";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const HANDLERS = path.join(HERE, "..", "..", "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private", "Handlers");
const AUDIT_CPP = readFileSync(path.join(HANDLERS, "BlueprintHandlers_Audit.cpp"), "utf8");
const REGISTRATION_CPP = readFileSync(path.join(HANDLERS, "BlueprintHandlers.cpp"), "utf8");

/** Every parameter key the file reads through the recorded helpers. */
function cppParamReads(source: string): Set<string> {
  const reads = new Set<string>();
  const re = /(?:Optional\w+|TryGet\w+Param|HasParam|Require\w+)\(\s*Params\s*,\s*TEXT\("([A-Za-z0-9_]+)"\)/g;
  for (const m of source.matchAll(re)) reads.add(m[1]);
  return reads;
}

async function forwarded(action: string, args: Record<string, unknown>) {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  await blueprintTool.handler(ctx, { action, ...args });
  return call.mock.calls[0] as [string, Record<string, unknown>];
}

const EXPORT_ARGS = {
  assetPaths: ["/Game/BP_A", "/Game/Maps/Level"],
  directory: "/Game/AI",
  recursive: false,
  parentClass: "/Script/Engine.Actor",
  includeLevelScripts: true,
  outputDir: "Saved/BPExport",
  maxAssets: 50,
  includeT3D: true,
};

const AUDIT_ARGS = {
  directory: "/Game/AI",
  assetPaths: ["/Game/BP_A"],
  recursive: false,
  includeLevelScripts: true,
  scanReferencers: false,
  maxBlueprints: 300,
  maxSamples: 5,
  limit: 25,
  dumpToFile: true,
  outputPath: "UE_MCP/dead_code.json",
};

describe("blueprint.export_batch (#1166)", () => {
  it("forwards every selection, output and cap parameter by the name the handler reads", async () => {
    const [method, params] = await forwarded("export_batch", EXPORT_ARGS);
    expect(method).toBe("export_blueprint_batch");
    expect(params).toEqual(EXPORT_ARGS);
  });

  it("leaves unset parameters out so the handler defaults apply", async () => {
    const [, params] = await forwarded("export_batch", { directory: "/Game" });
    expect(params.directory).toBe("/Game");
    for (const key of ["assetPaths", "recursive", "outputDir", "maxAssets", "includeT3D", "parentClass"]) {
      expect(params[key]).toBeUndefined();
    }
  });

  it("is declared a mutation, because it writes files", () => {
    expect(blueprintTool.actions.export_batch.effect).toBe("mutate");
  });
});

describe("blueprint.audit_dead_code (#1166)", () => {
  it("forwards every scope, cap and dump parameter by the name the handler reads", async () => {
    const [method, params] = await forwarded("audit_dead_code", AUDIT_ARGS);
    expect(method).toBe("audit_blueprint_dead_code");
    expect(params).toEqual(AUDIT_ARGS);
  });

  it("is declared a read", () => {
    expect(blueprintTool.actions.audit_dead_code.effect).toBe("read");
  });

  it("declares the new parameters with the right types", () => {
    const s = blueprintTool.schema;
    expect(s.scanReferencers.safeParse(false).success).toBe(true);
    expect(s.maxSamples.safeParse(0).success).toBe(true);
    expect(s.maxSamples.safeParse(-1).success).toBe(false);
    expect(s.maxAssets.safeParse(0).success).toBe(false);
    expect(s.includeT3D.safeParse(true).success).toBe(true);
    expect(s.outputDir.safeParse("Saved/X").success).toBe(true);
    expect(s.recursive.safeParse(true).success).toBe(true);
  });
});

describe("TS and C++ agree on #1166 (#1057)", () => {
  const reads = cppParamReads(AUDIT_CPP);

  it("the handler reads every key either action forwards", () => {
    const sent = new Set([...Object.keys(EXPORT_ARGS), ...Object.keys(AUDIT_ARGS)]);
    expect([...sent].filter((k) => !reads.has(k))).toEqual([]);
  });

  it("forwards every key the handler reads", () => {
    const sent = new Set([...Object.keys(EXPORT_ARGS), ...Object.keys(AUDIT_ARGS)]);
    expect([...reads].filter((k) => !sent.has(k))).toEqual([]);
  });

  it("never reads Params directly", () => {
    expect(AUDIT_CPP).not.toMatch(/(?<![\w.>])Params->(TryGet\w*Field|Get\w*Field|HasField)/);
  });

  it("registers both methods with a timeout the client mirrors", () => {
    for (const method of ["export_blueprint_batch", "audit_blueprint_dead_code"]) {
      expect(REGISTRATION_CPP).toContain(`TEXT("${method}")`);
      expect(REGISTERED_HANDLER_TIMEOUT_SECONDS[method]).toBe(600);
    }
  });
});
