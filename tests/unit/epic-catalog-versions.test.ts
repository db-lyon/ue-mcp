/**
 * The catalog holds one snapshot per engine, and the generated actions are
 * their union. Driven by a synthetic second engine written to a temp dir, so
 * no invented recording is ever committed as real data: against the 5.8
 * shape, "5.9" adds a toolset, drops one, drops one tool from a kept toolset
 * and changes another tool's schema.
 */
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import {
  compareEngines,
  engineKey,
  loadSnapshots,
  mergeSchemas,
  mergeSnapshots,
} from "../../scripts/lib/epic-catalog.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

const BP = "editor_toolset.toolsets.blueprint.BlueprintTools";
const NEW_BP = "aaa_toolset.toolsets.blueprint.NewBlueprintTools";
const SEARCH = "editor_toolset.toolsets.semanticsearch.SemanticSearchTools";

const ref = { type: "object", title: "/Script/Engine.Blueprint", properties: { refPath: { type: "string" } }, required: ["refPath"] };

function tool(qualified: string, description: string, properties: Record<string, unknown>, required: string[]) {
  return { name: qualified, description, inputSchema: { type: "object", properties, required } };
}

function snapshot(engine: string, toolsets: Array<{ name: string; tools: ReturnType<typeof tool>[] }>) {
  return {
    engineAssociation: engine,
    recordedToolsets: toolsets.length,
    recordedTools: toolsets.reduce((n, t) => n + t.tools.length, 0),
    toolsets: toolsets.map((t) => ({ name: t.name, version: "1.0", description: t.name, toolCount: t.tools.length, tools: t.tools })),
  };
}

const V58 = snapshot("5.8", [
  {
    name: BP,
    tools: [
      tool(`${BP}.add_variable`, "Adds a variable.", { blueprint: ref, name: { type: "string" } }, ["blueprint", "name"]),
      tool(`${BP}.compile_blueprint`, "Compiles a blueprint.", { blueprint: ref }, ["blueprint"]),
      tool(`${BP}.list_graphs`, "Lists graphs.", { blueprint: ref }, ["blueprint"]),
    ],
  },
  { name: SEARCH, tools: [tool(`${SEARCH}.search`, "Searches assets.", { query: { type: "string" } }, ["query"])] },
]);

const V59 = snapshot("5.9", [
  {
    name: NEW_BP,
    tools: [tool(`${NEW_BP}.compile_blueprint`, "Compiles with the new toolset.", { blueprint: ref }, ["blueprint"])],
  },
  {
    name: BP,
    tools: [
      tool(
        `${BP}.add_variable`,
        "Adds a variable, with a category.",
        { blueprint: ref, name: { type: "integer" }, category: { type: "string" } },
        ["blueprint", "category"],
      ),
      tool(`${BP}.compile_blueprint`, "Compiles a blueprint.", { blueprint: ref }, ["blueprint"]),
    ],
  },
]);

const EFFECTS = `export const EPIC_TOOL_EFFECTS = {
  "${BP}.add_variable": "mutate",
  "${BP}.compile_blueprint": "mutate",
  "${BP}.list_graphs": "read",
  "${SEARCH}.search": "read",
  "${NEW_BP}.compile_blueprint": "mutate",
};
`;

let dir: string;
let catalogDir: string;
let outDir: string;
const gen = (f: string) => fs.readFileSync(path.join(outDir, f), "utf8");

/** The `epicTool` line of one generated action. */
function epicToolLine(file: string, key: string): string {
  const src = gen(file);
  const at = src.indexOf(`  ${key}: {`);
  expect(at, `${key} in ${file}`).toBeGreaterThan(-1);
  return src.slice(at, src.indexOf("\n  },", at));
}

beforeAll(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "epic-versions-"));
  catalogDir = path.join(dir, "catalog");
  outDir = path.join(dir, "out");
  fs.mkdirSync(catalogDir);
  fs.writeFileSync(path.join(catalogDir, "5.8.json"), JSON.stringify(V58));
  fs.writeFileSync(path.join(catalogDir, "5.9.json"), JSON.stringify(V59));
  fs.writeFileSync(path.join(dir, "effects.ts"), EFFECTS);
  execFileSync(
    process.execPath,
    [
      "--import", "tsx", path.join(ROOT, "scripts", "generate-epic-actions.mjs"),
      "--catalog-dir", catalogDir, "--out-dir", outDir, "--effects", path.join(dir, "effects.ts"),
    ],
    { cwd: ROOT, stdio: "pipe" },
  );
}, 60_000);

afterAll(() => {
  fs.rmSync(dir, { recursive: true, force: true });
});

describe("engine keys", () => {
  it("reads major.minor out of every spelling an editor reports", () => {
    for (const read of [engineKey]) {
      expect(read("5.9")).toBe("5.9");
      expect(read("5.9.1")).toBe("5.9");
      expect(read("5.9.0-45678+++UE5+Main")).toBe("5.9");
      expect(read("{F1A2B3C4-GUID}")).toBeNull();
      expect(read(undefined)).toBeNull();
    }
  });

  it("orders 5.10 after 5.9", () => {
    expect(["5.10", "5.8", "5.9"].sort(compareEngines)).toEqual(["5.8", "5.9", "5.10"]);
  });
});

describe("loading the snapshots", () => {
  it("returns every engine oldest first", () => {
    expect(loadSnapshots(catalogDir).map((s: { engine: string }) => s.engine)).toEqual(["5.8", "5.9"]);
  });

  it("refuses a file named for another engine", () => {
    const bad = fs.mkdtempSync(path.join(os.tmpdir(), "epic-bad-"));
    fs.writeFileSync(path.join(bad, "5.9.json"), JSON.stringify(V58));
    expect(() => loadSnapshots(bad)).toThrow(/records engine 5.8/);
    fs.rmSync(bad, { recursive: true, force: true });
  });
});

describe("merging", () => {
  it("takes the union of properties, the newest definition, and only what every engine requires", () => {
    const merged = mergeSchemas([V58.toolsets[0].tools[0].inputSchema, V59.toolsets[1].tools[0].inputSchema]);
    expect(Object.keys(merged.properties)).toEqual(["blueprint", "name", "category"]);
    expect(merged.properties.name).toEqual({ type: "integer" });
    expect(merged.required).toEqual(["blueprint"]);
  });

  it("tags every toolset and tool with the engines carrying it", () => {
    const merged = mergeSnapshots(loadSnapshots(catalogDir));
    expect(merged.engines).toEqual(["5.8", "5.9"]);
    const ts = Object.fromEntries(merged.toolsets.map((t: { name: string; engines: string[] }) => [t.name, t.engines]));
    expect(ts).toEqual({ [NEW_BP]: ["5.9"], [BP]: ["5.8", "5.9"], [SEARCH]: ["5.8"] });
    const bp = merged.toolsets.find((t: { name: string }) => t.name === BP);
    const tools = Object.fromEntries(bp.tools.map((t: { name: string; engines: string[] }) => [t.name, t.engines]));
    expect(tools).toEqual({
      [`${BP}.add_variable`]: ["5.8", "5.9"],
      [`${BP}.compile_blueprint`]: ["5.8", "5.9"],
      [`${BP}.list_graphs`]: ["5.8"],
    });
    expect(bp.tools[0].description).toBe("Adds a variable, with a category.");
  });
});

describe("the generated union", () => {
  it("declares every tool any engine carries, each with its engines", () => {
    expect(epicToolLine("blueprint.generated.ts", "epic_add_variable")).toContain('engines: ["5.8","5.9"]');
    expect(epicToolLine("blueprint.generated.ts", "epic_list_graphs")).toContain('engines: ["5.8"]');
    expect(epicToolLine("asset.generated.ts", "epic_search")).toContain('engines: ["5.8"]');
    expect(gen("catalog-engines.ts")).toContain('EPIC_CATALOG_ENGINES: readonly string[] = ["5.8","5.9"];');
  });

  it("keeps an existing action's key when a newer engine adds a same-named tool", () => {
    // The 5.9 toolset sorts first, but the 5.8 tool claimed the plain key.
    expect(epicToolLine("blueprint.generated.ts", "epic_compile_blueprint")).toContain(`name: "${BP}.compile_blueprint"`);
    expect(epicToolLine("blueprint.generated.ts", "epic_compile_blueprint__new_blueprint_tools"))
      .toContain(`name: "${NEW_BP}.compile_blueprint", engines: ["5.9"]`);
  });

  it("states availability only where it is not every engine", () => {
    expect(epicToolLine("blueprint.generated.ts", "epic_list_graphs")).toContain("Available on UE 5.8; absent from UE 5.9. Params: blueprint");
    expect(epicToolLine("blueprint.generated.ts", "epic_add_variable")).not.toContain("Available on");
  });

  it("generates the merged schema and declares every property", () => {
    const src = gen("blueprint.generated.ts");
    expect(src).toContain(
      'const S_epic_add_variable = {"properties":{"blueprint":{"type":"object","properties":{"refPath":{}}},"name":{"type":"integer"},"category":{"type":"string"}},"required":["blueprint"]} as const;',
    );
    expect(epicToolLine("blueprint.generated.ts", "epic_add_variable")).toContain("Params: blueprint, name?, category?");
    expect(src).toMatch(/^ {2}category: z\.string\(\)\.optional\(\)/m);
    // string on 5.8, integer on 5.9: either is accepted.
    expect(src).toMatch(/^ {2}name: z\.unknown\(\)\.optional\(\)/m);
  });
});
