import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, expect, it } from "vitest";

import {
  buildTestGraph,
  calledCategories,
  keyedCategory,
  parseImports,
  parseIncludes,
  parsePackageImports,
  resolveImport,
  resolveInclude,
  specMethods,
  type TestGraph,
  type TestNode,
} from "../../scripts/lib/test-graph.js";
import { changedPackageKeys, dependencyDelta, packageOf, resolveFrom, treeOf, type DependencySnapshot } from "../../scripts/lib/lockfile-delta.js";
import { opaqueReason, selectTests } from "../../scripts/lib/test-selection.js";
import { descriptorStampOnly } from "../../scripts/lib/test-selection.js";
import { LIFECYCLE_FILE, toSelectionFile } from "../../scripts/select-tests.js";

const H = "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge";

/**
 * A small plugin and server: two categories (pcg, level), a shared header every handler includes,
 * a dispatch path, a registry that gathers the category modules, and tests over each.
 */
function fixture(): TestGraph {
  const deps: Record<string, string[]> = {
    [`${H}/Public/HandlerUtils.h`]: [],
    [`${H}/Private/MCPBridgeModule.cpp`]: [],
    [`${H}/Private/Handlers/PCG/PCGHandlers.h`]: [`${H}/Private/Handlers/PCG/PCGHandlers.cpp`],
    [`${H}/Private/Handlers/PCG/PCGHandlers.cpp`]: [`${H}/Public/HandlerUtils.h`, `${H}/Private/Handlers/PCG/PCGHandlers.h`],
    [`${H}/Private/Handlers/PCG/PCGHandlers_Graph.cpp`]: [`${H}/Public/HandlerUtils.h`],
    [`${H}/Private/Handlers/Level/LevelHandlers.cpp`]: [`${H}/Public/HandlerUtils.h`],
    [`${H}/Private/Tests/PCGHandlerTests.cpp`]: [`${H}/Private/Handlers/PCG/PCGHandlers.h`],
    [`${H}/Private/Tests/LevelTests.cpp`]: [],
    "src/core/log.ts": [],
    "src/surface/pagination.ts": [],
    "src/tools/pcg.ts": ["src/tools/pcg-wait.ts", "src/tools/specs/pcg.generated.ts"],
    "src/tools/pcg-wait.ts": [],
    "src/tools/level.ts": ["src/surface/pagination.ts"],
    "src/tools/specs/pcg.generated.ts": [],
    "src/tools/specs/index.ts": ["src/tools/specs/pcg.generated.ts"],
    "src/tools.ts": ["src/tools/pcg.ts", "src/tools/level.ts"],
    "src/dispatch/server-dispatch.ts": ["src/core/log.ts", "src/tools/specs/index.ts"],
    "src/server/mcp-server.ts": ["src/dispatch/server-dispatch.ts", "src/tools.ts"],
    "src/index.ts": ["src/server/mcp-server.ts", "src/cli/doctor.ts"],
    "src/cli/doctor.ts": [],
    "src/editor/editor-control.ts": [],
    "tests/live/server.ts": ["src/index.ts"],
    "tests/live/bcov-pcg.test.ts": [],
    "tests/live/bcov-level.test.ts": [],
    "tests/live/addressing.test.ts": ["tests/live/server.ts"],
    "tests/live/golden-connected.test.ts": [],
    "tests/live/editor-lifecycle.test.ts": ["src/editor/editor-control.ts"],
    "tests/live/niagara-surface.test.ts": ["src/tools/pcg.ts"],
    "scripts/live-tests.mjs": ["scripts/live-shards.mjs"],
    "scripts/live-shards.mjs": [],
  };
  const live = (file: string, actions: TestNode["actions"], categories: TestNode["categories"] = [], kind = "headless"): TestNode =>
    ({ id: file, suite: "live", file, kind, actions, categories });
  const auto = (id: string, file: string, actions: string[]): TestNode => ({ id, suite: "automation", file, actions, categories: [] });
  return {
    deps,
    actions: {
      add_pcg_node: { files: [`${H}/Private/Handlers/PCG/PCGHandlers.cpp`, `${H}/Private/Handlers/PCG/PCGHandlers_Graph.cpp`, "src/tools/pcg.ts"], categories: ["pcg"] },
      list_pcg_graphs: { files: [`${H}/Private/Handlers/PCG/PCGHandlers.cpp`, "src/tools/pcg.ts"], categories: ["pcg"] },
      spawn_actor: { files: [`${H}/Private/Handlers/Level/LevelHandlers.cpp`, "src/tools/level.ts"], categories: ["level"] },
    },
    keyed: {
      "src/tools/pcg.ts": { categories: ["pcg"], methods: [] },
      "src/tools/pcg-wait.ts": { categories: ["pcg"], methods: [] },
      "src/tools/level.ts": { categories: ["level"], methods: [] },
      "src/tools/specs/pcg.generated.ts": { categories: [], methods: ["add_pcg_node"] },
    },
    dispatch: ["src/server/mcp-server.ts", "src/dispatch/server-dispatch.ts"],
    runners: ["scripts/live-tests.mjs", "scripts/live-shards.mjs"],
    tests: [
      live("tests/live/bcov-pcg.test.ts", ["add_pcg_node", "list_pcg_graphs"]),
      live("tests/live/bcov-level.test.ts", ["spawn_actor"]),
      live("tests/live/addressing.test.ts", [], ["level"]),
      live("tests/live/golden-connected.test.ts", "*", "*"),
      live("tests/live/editor-lifecycle.test.ts", [], [], "lifecycle"),
      live("tests/live/niagara-surface.test.ts", []),
      auto("UE.MCP.PCG.Graph", `${H}/Private/Tests/PCGHandlerTests.cpp`, ["add_pcg_node"]),
      auto("UE.MCP.Level.Spawn", `${H}/Private/Tests/LevelTests.cpp`, ["spawn_actor"]),
    ],
  };
}

const ids = (graph: TestGraph, changed: string[]) => selectTests(graph, changed).tests.map((t) => t.id).sort();

describe("selectTests on a fixture graph", () => {
  it("a category handler change selects the tests that send its actions and include its header", () => {
    const sel = selectTests(fixture(), [`${H}/Private/Handlers/PCG/PCGHandlers_Graph.cpp`]);
    expect(sel.full).toBe(false);
    expect(sel.actions).toEqual(["add_pcg_node"]);
    expect(sel.tests.map((t) => t.id).sort()).toEqual(["UE.MCP.PCG.Graph", "tests/live/bcov-pcg.test.ts", "tests/live/golden-connected.test.ts"]);
    expect(sel.tests.find((t) => t.id === "tests/live/bcov-pcg.test.ts")!.reason).toMatch(/sends add_pcg_node.*PCGHandlers_Graph\.cpp/);
  });

  it("a header pulls in the .cpp of its name, so a test including it runs when that .cpp changes", () => {
    const sel = selectTests(fixture(), [`${H}/Private/Handlers/PCG/PCGHandlers.cpp`]);
    expect(sel.tests.find((t) => t.id === "UE.MCP.PCG.Graph")!.reason).toMatch(/^includes .*PCGHandlers\.cpp/);
    expect(sel.actions).toEqual(["add_pcg_node", "list_pcg_graphs"]);
  });

  it("a shared header that reaches every action selects the full suite through the graph", () => {
    const sel = selectTests(fixture(), [`${H}/Public/HandlerUtils.h`]);
    expect(sel.full).toBe(true);
    expect(sel.fullReason).toBe(`${H}/Public/HandlerUtils.h reaches every action`);
    expect(sel.tests).toHaveLength(fixture().tests.length);
  });

  it("a change on the dispatch path selects the full suite through the graph", () => {
    const sel = selectTests(fixture(), ["src/core/log.ts"]);
    expect(sel.full).toBe(true);
    expect(sel.fullReason).toMatch(/src\/core\/log\.ts reaches the dispatch path/);
  });

  it("a category-keyed module reaches its category, not every importer of the registry", () => {
    const sel = selectTests(fixture(), ["src/tools/pcg-wait.ts"]);
    expect(sel.full).toBe(false);
    expect(sel.actions).toEqual(["add_pcg_node", "list_pcg_graphs"]);
    // niagara-surface imports src/tools/pcg.ts directly; addressing reaches it only through the registry.
    expect(ids(fixture(), ["src/tools/pcg-wait.ts"])).toEqual([
      "UE.MCP.PCG.Graph", "tests/live/bcov-pcg.test.ts", "tests/live/golden-connected.test.ts", "tests/live/niagara-surface.test.ts",
    ]);
  });

  it("a spec module reaches only the methods it lists", () => {
    const sel = selectTests(fixture(), ["src/tools/specs/pcg.generated.ts"]);
    expect(sel.actions).toEqual(["add_pcg_node"]);
    expect(sel.full).toBe(false);
  });

  it("a shared file behind one category module reaches that category, and a test calling it", () => {
    const sel = selectTests(fixture(), ["src/surface/pagination.ts"]);
    expect(sel.actions).toEqual(["spawn_actor"]);
    expect(sel.tests.map((t) => t.id).sort()).toEqual([
      "UE.MCP.Level.Spawn", "tests/live/addressing.test.ts", "tests/live/bcov-level.test.ts", "tests/live/golden-connected.test.ts",
    ]);
  });

  it("server code off the dispatch path selects the tests that start the server", () => {
    expect(ids(fixture(), ["src/cli/doctor.ts"])).toEqual(["tests/live/addressing.test.ts"]);
  });

  it("lifecycle runs only when the graph reaches it", () => {
    expect(ids(fixture(), ["src/editor/editor-control.ts"])).toEqual(["tests/live/editor-lifecycle.test.ts"]);
    expect(ids(fixture(), [`${H}/Private/Handlers/Level/LevelHandlers.cpp`])).not.toContain("tests/live/editor-lifecycle.test.ts");
  });

  it("a changed test runs, and a file that reaches nothing selects nothing", () => {
    const sel = selectTests(fixture(), ["tests/live/bcov-level.test.ts", "docs/index.md"]);
    expect(sel.tests.map((t) => t.id)).toEqual(["tests/live/bcov-level.test.ts"]);
    expect(sel.tests[0].reason).toBe("tests/live/bcov-level.test.ts changed");
    expect(sel.inert).toEqual(["docs/index.md"]);
  });

  it.each([
    [`${H}/UE_MCP_Bridge.Build.cs`, "build rules"],
    ["plugin/ue_mcp_bridge/UE_MCP_Bridge.uplugin", "the plugin descriptor"],
    ["plugin/ue_mcp_bridge/Config/DefaultUE_MCP_Bridge.ini", "engine config"],
    ["tests/ue_mcp/Content/Maps/Home.umap", "test project content"],
    ["package.json", "dependencies"],
    ["package-lock.json", "dependencies"],
  ])("%s selects the full suite (%s)", (file, why) => {
    expect(opaqueReason(file)).toBe(why);
    const sel = selectTests(fixture(), [file]);
    expect(sel.full).toBe(true);
    expect(sel.fullReason).toContain(why);
  });

  it("a test runner change selects the full suite", () => {
    expect(selectTests(fixture(), ["scripts/live-shards.mjs"]).fullReason).toMatch(/live-shards\.mjs changed, and it runs every test/);
  });

  it("a plugin file no action or test reaches selects the full suite", () => {
    expect(selectTests(fixture(), [`${H}/Private/MCPBridgeModule.cpp`]).fullReason).toMatch(/reaches no action or test/);
  });

  it("a plugin file the graph has no node for, such as a deleted one, selects the full suite", () => {
    expect(selectTests(fixture(), [`${H}/Private/Gone.cpp`]).fullReason).toMatch(/no node for it/);
  });

  it("a stable release runs the full suite whatever changed", () => {
    const sel = selectTests(fixture(), ["docs/index.md"], { stable: true });
    expect(sel.full).toBe(true);
    expect(sel.fullReason).toBe("a stable release runs the full suite");
  });

  it("the selection file splits lifecycle out of the live list and names the automation tests", () => {
    const g = fixture();
    const sel = selectTests(g, ["src/editor/editor-control.ts", `${H}/Private/Handlers/Level/LevelHandlers.cpp`]);
    const file = toSelectionFile(sel, [], null, { "tests/live/bcov-level.test.ts": { durationMs: 12_400 } });
    expect(file.lifecycle).toBe(true);
    expect(file.live).not.toContain(LIFECYCLE_FILE);
    expect(file.live).toContain("tests/live/bcov-level.test.ts");
    expect(file.automation).toEqual(["UE.MCP.Level.Spawn"]);
    expect(file.liveEstimateS).toBe(12);
    expect(toSelectionFile(selectTests(g, ["package.json"]), [], null, {}).automation).toBe("all");
  });
});

// ---------------------------------------------------------------------------
// Dependency changes
// ---------------------------------------------------------------------------

type Entry = { version: string; dependencies?: Record<string, string> };
const manifest = (deps: Record<string, string>, dev: Record<string, string>, extra: Record<string, unknown> = {}) =>
  JSON.stringify({ name: "ue-mcp", version: "1.0.0", dependencies: deps, devDependencies: dev, ...extra });
const lockOf = (deps: Record<string, string>, dev: Record<string, string>, pkgs: Record<string, Entry>) =>
  JSON.stringify({ lockfileVersion: 3, packages: { "": { name: "ue-mcp", dependencies: deps, devDependencies: dev }, ...pkgs } });

const RUNTIME: Record<string, string> = { "level-lib": "^1.0.0", "log-lib": "^1.0.0" };
const DEV: Record<string, string> = { vitest: "^3.0.0" };
/** level-lib and vitest each pull in their own copy of shared; log-lib stands alone. */
const BASE_PKGS: Record<string, Entry> = {
  "node_modules/level-lib": { version: "1.0.0", dependencies: { shared: "^1.0.0" } },
  "node_modules/shared": { version: "1.0.0" },
  "node_modules/log-lib": { version: "1.0.0" },
  "node_modules/vitest": { version: "3.0.0", dependencies: { shared: "^2.0.0" } },
  "node_modules/vitest/node_modules/shared": { version: "2.0.0" },
};
const snap = (pkgs: Record<string, Entry>, deps = RUNTIME, dev = DEV, extra: Record<string, unknown> = {}): DependencySnapshot =>
  ({ manifest: manifest(deps, dev, extra), lock: lockOf(deps, dev, pkgs) });
const BASE = snap(BASE_PKGS);
const withPackages = (): TestGraph => ({
  ...fixture(),
  packages: {
    "src/tools/level.ts": ["level-lib"],
    "src/core/log.ts": ["log-lib"],
    "tests/live/server.ts": ["vitest"],
  },
});
const LOCK = ["package-lock.json"];

describe("lockfile delta", () => {
  it("reads the package a bare specifier loads, and skips builtins and relative paths", () => {
    expect(packageOf("isomorphic-git")).toBe("isomorphic-git");
    expect(packageOf("@modelcontextprotocol/sdk/server/mcp.js")).toBe("@modelcontextprotocol/sdk");
    expect(packageOf("js-yaml/dist/x.js")).toBe("js-yaml");
    expect(packageOf("node:fs")).toBeNull();
    expect(packageOf("fs")).toBeNull();
    expect(packageOf("./a.js")).toBeNull();
    const source = 'import git from "isomorphic-git";\nimport fs from "node:fs";\nimport { a } from "./a.js";\nconst y = await import("js-yaml");\n';
    expect(parsePackageImports(source)).toEqual(["isomorphic-git", "js-yaml"]);
  });

  it("resolves a dependency the way node does, nearest node_modules first", () => {
    const pkgs = JSON.parse(lockOf(RUNTIME, DEV, BASE_PKGS)).packages;
    expect(resolveFrom(pkgs, "node_modules/vitest", "shared")).toBe("node_modules/vitest/node_modules/shared");
    expect(resolveFrom(pkgs, "node_modules/level-lib", "shared")).toBe("node_modules/shared");
    expect([...treeOf(pkgs, "level-lib")].sort()).toEqual(["node_modules/level-lib", "node_modules/shared"]);
    const moved = { ...pkgs, "node_modules/shared": { version: "1.0.1" } };
    expect(changedPackageKeys({ packages: pkgs }, { packages: moved })).toEqual(["node_modules/shared"]);
  });

  it("maps a transitive change to the top-level dependency that pulls it in", () => {
    const d = dependencyDelta(BASE, snap({ ...BASE_PKGS, "node_modules/shared": { version: "1.0.1" } }));
    expect(d.opaque).toBeNull();
    expect([...d.runtime]).toEqual([["level-lib", ["shared"]]]);
    expect(d.dev).toEqual([]);
  });

  it("a change in a devDependency's own copy reaches only that devDependency", () => {
    const d = dependencyDelta(BASE, snap({ ...BASE_PKGS, "node_modules/vitest/node_modules/shared": { version: "2.0.1" } }));
    expect([...d.runtime]).toEqual([]);
    expect(d.dev).toEqual(["vitest"]);
  });

  it("a removed dependency is followed through the base tree", () => {
    const rest = Object.fromEntries(Object.entries(BASE_PKGS).filter(([k]) => k !== "node_modules/log-lib"));
    const d = dependencyDelta(BASE, snap(rest, { "level-lib": "^1.0.0" }));
    expect([...d.runtime.keys()]).toEqual(["log-lib"]);
  });

  it("a manifest change outside the dependency fields cannot be followed", () => {
    expect(dependencyDelta(BASE, snap(BASE_PKGS, RUNTIME, DEV, { scripts: { test: "vitest" } })).opaque).toMatch(/outside its dependencies \(scripts\)/);
    expect(dependencyDelta(BASE, snap(BASE_PKGS, RUNTIME, DEV, { description: "stamped count" })).opaque).toBeNull();
    expect(dependencyDelta({ manifest: BASE.manifest, lock: null }, BASE).opaque).toMatch(/no packages tree/);
  });

  it("v1.4.11-beta.3 to v1.4.11-beta.4: isomorphic-git is a runtime dependency, npm a devDependency", () => {
    const fx = JSON.parse(fs.readFileSync(path.join(__dirname, "fixtures", "lockfile-beta3-beta4.json"), "utf8"));
    const side = (s: { manifest: unknown; lock: unknown }): DependencySnapshot => ({ manifest: JSON.stringify(s.manifest), lock: JSON.stringify(s.lock) });
    const d = dependencyDelta(side(fx.base), side(fx.head));
    expect(d.opaque).toBeNull();
    expect([...d.runtime.keys()]).toEqual(["isomorphic-git"]);
    expect(d.runtime.get("isomorphic-git")).toEqual(expect.arrayContaining(["isomorphic-git", "pako", "sha.js"]));
    expect(d.dev).toEqual(["npm"]);
  });
});

describe("selectTests on a dependency change", () => {
  it("a runtime package change selects what its importers reach, as a change to them would", () => {
    const head = snap({ ...BASE_PKGS, "node_modules/shared": { version: "1.0.1" } });
    const sel = selectTests(withPackages(), LOCK, { dependencies: { base: BASE, head } });
    expect(sel.full).toBe(false);
    expect(sel.tests.map((t) => t.id).sort()).toEqual(ids(fixture(), ["src/tools/level.ts"]));
    expect(sel.tests.find((t) => t.id === "tests/live/bcov-level.test.ts")!.reason).toContain("src/tools/level.ts (imports level-lib, changed in package-lock.json)");
    expect(sel.dependencies!.importers).toEqual({ "level-lib": ["src/tools/level.ts"] });
    expect(sel.inert).toEqual([]);
  });

  it("a devDependency change selects no editor test", () => {
    const head = snap({ ...BASE_PKGS, "node_modules/vitest": { version: "3.1.0", dependencies: { shared: "^2.0.0" } } });
    const sel = selectTests(withPackages(), ["package.json", ...LOCK], { dependencies: { base: BASE, head } });
    expect(sel.full).toBe(false);
    expect(sel.tests).toEqual([]);
    expect(sel.dependencies!.dev).toEqual(["vitest"]);
    expect(sel.inert).toEqual(["package.json", "package-lock.json"]);
  });

  it("a new runtime package nothing in src/ imports selects no editor test", () => {
    const head = snap({ ...BASE_PKGS, "node_modules/unused": { version: "1.0.0" } }, { ...RUNTIME, unused: "^1.0.0" });
    const sel = selectTests(withPackages(), ["package.json", ...LOCK], { dependencies: { base: BASE, head } });
    expect(sel.full).toBe(false);
    expect(sel.tests).toEqual([]);
    expect(sel.dependencies!.unimported).toEqual(["unused"]);
  });

  it("a package imported on the dispatch path selects the full suite through the graph", () => {
    const head = snap({ ...BASE_PKGS, "node_modules/log-lib": { version: "1.0.1" } });
    const sel = selectTests(withPackages(), LOCK, { dependencies: { base: BASE, head } });
    expect(sel.full).toBe(true);
    expect(sel.fullReason).toMatch(/src\/core\/log\.ts \(imports log-lib.*reaches the dispatch path/);
  });

  it("a manifest change it cannot follow, and a stable release, select the full suite", () => {
    const head = snap(BASE_PKGS, RUNTIME, DEV, { bin: { x: "dist/x.js" } });
    expect(selectTests(withPackages(), ["package.json"], { dependencies: { base: BASE, head } }).fullReason).toMatch(/outside its dependencies \(bin\)/);
    const moved = snap({ ...BASE_PKGS, "node_modules/vitest": { version: "3.1.0" } });
    expect(selectTests(withPackages(), LOCK, { stable: true, dependencies: { base: BASE, head: moved } }).full).toBe(true);
  });
});

describe("graph parsing", () => {
  it("reads quoted includes and skips engine and generated headers", () => {
    expect(parseIncludes('#include "HandlerUtils.h"\n  #  include "PCG/PCGHandlers.h"\n#include <CoreMinimal.h>\n#include "Foo.generated.h"\n')).toEqual([
      "HandlerUtils.h", "PCG/PCGHandlers.h",
    ]);
  });

  it("reads static, dynamic and re-export imports, and the spawn of src/index.ts", () => {
    const specs = parseImports('import { a } from "./a.js";\nexport * from "../b.js";\nconst c = await import("./c.mjs");\nimport fs from "node:fs";\nconst entry = path.join(ROOT, "src", "index.ts");\n');
    expect(specs).toEqual(["./a.js", "../b.js", "./c.mjs", "=src/index.ts"]);
  });

  it("resolves .js specifiers to .ts sources", () => {
    const known = new Set(["src/tools/pcg.ts", "src/index.ts", "scripts/lib/x.mjs"]);
    expect(resolveImport("src/tools.ts", "./tools/pcg.js", known)).toBe("src/tools/pcg.ts");
    expect(resolveImport("tests/live/a.ts", "../../scripts/lib/x.mjs", known)).toBe("scripts/lib/x.mjs");
    expect(resolveImport("tests/live/server.ts", "=src/index.ts", known)).toBe("src/index.ts");
    expect(resolveImport("src/a.ts", "./missing.js", known)).toBeNull();
  });

  it("resolves includes beside the includer, then in the module's folders, then by path suffix", () => {
    const files = [`${H}/Public/HandlerUtils.h`, `${H}/Private/Handlers/PCG/PCGHandlers.h`, `${H}/Private/Handlers/PCG/PCGHandlers.cpp`, "plugin/ue_mcp_bridge/Source/UE_MCP_BridgeStatus/Public/HandlerUtils.h"];
    const known = new Set(files);
    const byName = new Map<string, string[]>();
    for (const f of files) byName.set(path.posix.basename(f), [...(byName.get(path.posix.basename(f)) ?? []), f]);
    const from = `${H}/Private/Handlers/PCG/PCGHandlers.cpp`;
    expect(resolveInclude(from, "PCGHandlers.h", known, byName)).toBe(`${H}/Private/Handlers/PCG/PCGHandlers.h`);
    expect(resolveInclude(from, "HandlerUtils.h", known, byName)).toBe(`${H}/Public/HandlerUtils.h`);
    expect(resolveInclude(`${H}/Private/Tests/T.cpp`, "Handlers/PCG/PCGHandlers.h", known, byName)).toBe(`${H}/Private/Handlers/PCG/PCGHandlers.h`);
    expect(resolveInclude(from, "Engine/World.h", known, byName)).toBeNull();
  });

  it("keys modules under src/tools by category and leaves shared ones alone", () => {
    const cats = new Set(["pcg", "widget", "project", "source_control"]);
    expect(keyedCategory("src/tools/pcg.ts", cats)).toBe("pcg");
    expect(keyedCategory("src/tools/pcg-wait.ts", cats)).toBe("pcg");
    expect(keyedCategory("src/tools/widget-params.ts", cats)).toBe("widget");
    expect(keyedCategory("src/tools/source_control.ts", cats)).toBe("source_control");
    expect(keyedCategory("src/tools/epic/pcg.generated.ts", cats)).toBe("pcg");
    expect(keyedCategory("src/tools/project/install.ts", cats)).toBe("project");
    expect(keyedCategory("src/tools/specs/index.ts", cats)).toBeNull();
    expect(keyedCategory("src/tools/epic/effects.ts", cats)).toBeNull();
    expect(keyedCategory("src/surface/category-tool.ts", cats)).toBeNull();
  });

  it("reads a spec module's methods and a live file's categories", () => {
    expect(specMethods('export const handlerSpecs = {\n  "add_impulse": {\n    "category": "physics",\n    "params": []\n  },\n  "set_mass": {\n  }\n};')).toEqual(["add_impulse", "set_mass"]);
    const cats = new Set(["pcg", "level"]);
    expect(calledCategories('await server.call("pcg", { action: "x" }); await s.call("level", {});', cats)).toEqual(["level", "pcg"]);
    expect(calledCategories("const names = await server.listToolNames();", cats)).toBe("*");
  });
});

describe("the real graph", () => {
  const cacheFile = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-graph-")), "graph.json");
  const repo = path.resolve(__dirname, "..", "..");
  const graph = buildTestGraph(repo, { cacheFile });

  it("a shared plugin header runs the full suite", () => {
    const sel = selectTests(graph, [`${H}/Public/HandlerUtils.h`]);
    expect(sel.full).toBe(true);
  });

  it("one category handler runs that category's tests, not the suite", () => {
    const sel = selectTests(graph, [`${H}/Private/Handlers/PCG/PCGHandlers.cpp`]);
    expect(sel.full).toBe(false);
    const live = sel.tests.filter((t) => t.suite === "live").map((t) => t.file);
    expect(live).toContain("tests/live/bcov-pcg.test.ts");
    expect(live.length).toBeLessThan(graph.tests.filter((t) => t.suite === "live").length / 4);
    expect(live).not.toContain(LIFECYCLE_FILE);
  });

  it("records the packages src/ imports, so a lockfile change can start from them", () => {
    expect(graph.packages?.["src/checkpoints/shadow-repo.ts"]).toContain("isomorphic-git");
    expect(graph.packages?.["src/bridge/bridge.ts"]).toContain("ws");
  });

  it("v1.4.11-beta.3 to v1.4.11-beta.4 starts from the module that imports isomorphic-git", () => {
    const fx = JSON.parse(fs.readFileSync(path.join(__dirname, "fixtures", "lockfile-beta3-beta4.json"), "utf8"));
    const side = (s: { manifest: unknown; lock: unknown }): DependencySnapshot => ({ manifest: JSON.stringify(s.manifest), lock: JSON.stringify(s.lock) });
    const sel = selectTests(graph, ["package.json", "package-lock.json"], { dependencies: { base: side(fx.base), head: side(fx.head) } });
    // The checkpoint service the server runtime imports holds the shadow repository, so it is on the dispatch path.
    expect(sel.fullReason).toMatch(/^src\/checkpoints\/shadow-repo\.ts \(imports isomorphic-git, changed in package\.json and package-lock\.json\) reaches the dispatch path/);
  });

  it("rebuilds from the cache without reparsing an unchanged file", () => {
    const stats = { files: 0, reparsed: 0 };
    buildTestGraph(repo, { cacheFile, stats });
    expect(stats.files).toBeGreaterThan(1000);
    expect(stats.reparsed).toBe(0);
  });
});

describe("a plugin descriptor change", () => {
  const descriptor = (o: Record<string, unknown>) => JSON.stringify({ FileVersion: 3, Version: 1, VersionName: "1.4.11-beta.3", Description: "2038+ editor actions", Modules: [{ Name: "UE_MCP_Bridge", Type: "Editor" }], ...o }, null, "\t");

  it("that only restamps the version and the action count selects nothing", () => {
    expect(descriptorStampOnly(descriptor({}), descriptor({ VersionName: "1.4.11-beta.4", Version: 2, Description: "2044+ editor actions" }))).toBe(true);
  });

  it("that changes anything else is still opaque", () => {
    expect(descriptorStampOnly(descriptor({}), descriptor({ Modules: [{ Name: "UE_MCP_Bridge", Type: "Runtime" }] }))).toBe(false);
    expect(descriptorStampOnly(descriptor({}), null)).toBe(false);
    expect(descriptorStampOnly("not json", descriptor({}))).toBe(false);
  });
});
