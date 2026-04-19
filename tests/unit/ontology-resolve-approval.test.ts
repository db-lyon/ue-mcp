import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import { OntologyRegistry, createHandlerRegistryProjector } from "../../src/ontology/index.js";

function buildTools(): ToolDef[] {
  return [
    categoryTool("editor", "Editor.", {
      execute_python: {
        bridge: "execute_python",
        classification: "destructive",
        approval: "explicit",
        risk: "catastrophic",
      },
      get_status: bp("Read", "editor_get_status"),
    }),
    categoryTool(
      "gas",
      "GAS.",
      { create_ability: bp("Create ability", "create_ability") },
      undefined,
      undefined,
      { approval: "required" },
    ),
  ];
}

describe("ontology registry: resolveApproval", () => {
  let tmpDir: string;
  let registry: OntologyRegistry;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-appr-"));
    const projectedDir = path.join(tmpDir, "projected");
    fs.mkdirSync(projectedDir, { recursive: true });
    registry = new OntologyRegistry(
      () => projectedDir,
      () => ({
        kernel: { priority: 0, paths: [] },
        projected: { priority: 1, paths: [projectedDir] },
        repoLocal: { priority: 2, paths: [] },
      }),
    );
    registry.register(createHandlerRegistryProjector(buildTools()), () => undefined);
    registry.projectAll();
  });

  afterEach(() => fs.rmSync(tmpDir, { recursive: true, force: true }));

  it("returns explicit for a declared-explicit action", () => {
    expect(registry.resolveApproval("editor", "execute_python")).toBe("explicit");
  });

  it("returns required via category-level defaults", () => {
    expect(registry.resolveApproval("gas", "create_ability")).toBe("required");
  });

  it("returns heuristic-derived approval for un-declared actions", () => {
    // get_status heuristic: read -> approval=auto
    expect(registry.resolveApproval("editor", "get_status")).toBe("auto");
  });

  it("returns undefined for unknown actions", () => {
    expect(registry.resolveApproval("editor", "does_not_exist")).toBeUndefined();
  });
});
