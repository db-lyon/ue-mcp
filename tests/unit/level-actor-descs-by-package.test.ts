import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { levelTool } from "../../src/tools/level.js";
import { handlerParamReads, readAllSources } from "../../scripts/lib/handler-param-reads.mjs";
import { readRegistrations } from "../../scripts/audit-handler-conventions.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const SNAPSHOT = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "golden", "handler-specs.json"), "utf8")) as {
  handlers: Record<string, { params: Array<{ name: string; type: string; required: boolean; items?: string }> }>;
};
const WP_SOURCE = fs.readFileSync(
  path.join(ROOT, "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private", "Handlers", "Level", "LevelHandlers_WorldPartition.cpp"),
  "utf8",
);

function fakeCtx(seen: Array<{ method: string; params: Record<string, unknown> }>) {
  return {
    bridge: {
      isConnected: true,
      call: async (method: string, params: Record<string, unknown>) => { seen.push({ method, params }); return { success: true }; },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
      connect: async () => {},
      retargetProject: () => ({}),
    },
    project: {},
  } as never;
}

const ACTIONS = ["list_actor_descs", "load_actor_descs"] as const;
const PACKAGES = [
  "/Game/__ExternalActors__/Maps/Origins/0/AB/CDEFGHIJKLMNOPQRSTUVWX",
  "1/CD/EFGHIJKLMNOPQRSTUVWXYZ",
];

describe("actor descriptors by package: the advertised spec", () => {
  it("records packages as an optional string array on both actions, next to guids", () => {
    for (const name of ACTIONS) {
      const params = SNAPSHOT.handlers[name].params;
      const names = params.map((p) => p.name);
      expect(names.indexOf("packages"), name).toBe(names.indexOf("guids") + 1);
      expect(params.find((p) => p.name === "packages"), name).toMatchObject({ type: "array", required: false, items: "string" });
    }
  });

  it("takes packages from the recorded spec on the TS surface", () => {
    for (const name of ACTIONS) {
      const action = levelTool.actions[name];
      const spec = action.kind === "bridge" ? action.paramSpec : undefined;
      expect(spec?.map((p) => p.name), name).toContain("packages");
      expect(action.description, name).toContain("missingPackages");
    }
    expect(levelTool.actions.list_actor_descs.description).toContain("folderPath");
  });

  it("accepts a packages list and sends it through unchanged", async () => {
    for (const name of ACTIONS) {
      const seen: Array<{ method: string; params: Record<string, unknown> }> = [];
      await levelTool.handler(fakeCtx(seen), { action: name, packages: PACKAGES });
      expect(seen, name).toEqual([{ method: name, params: { packages: PACKAGES } }]);
    }
  });

  it("refuses a packages value that is not a string array before anything is sent", async () => {
    for (const bad of ["/Game/__ExternalActors__/Maps/Origins/0/AB/CDEF", [42]]) {
      const seen: Array<{ method: string; params: Record<string, unknown> }> = [];
      await expect(levelTool.handler(fakeCtx(seen), { action: "list_actor_descs", packages: bad })).rejects.toThrow(/packages/);
      expect(seen).toEqual([]);
    }
  });
});

describe("actor descriptors by package: the C++ handlers", () => {
  const registrations = readRegistrations();
  const sources = readAllSources();

  it("read packages, and nothing their recorded spec does not declare", () => {
    for (const name of ACTIONS) {
      const reads = handlerParamReads(name, { registrations, sources });
      expect(reads, `${name}: handler body not found`).not.toBeNull();
      expect([...reads!.keys], name).toContain("packages");
      const declared = SNAPSHOT.handlers[name].params.map((p) => p.name);
      expect([...reads!.keys].filter((k) => !declared.includes(k)), `${name}: reads an undeclared parameter`).toEqual([]);
    }
  });

  it("keys a paged cursor on the package set, so a cursor cannot be replayed against other packages", () => {
    expect(WP_SOURCE).toMatch(/packages=%s\|bounds=%s/);
    expect(WP_SOURCE).toContain("FMCPDescPackageFilter(Params).CollectionKey()");
  });

  it("filters by package before every other filter, so missingPackages means the map does not hold it", () => {
    const loop = WP_SOURCE.slice(WP_SOURCE.indexOf("const FMCPActorDescSnapshot& Desc = AllDescs[DescIndex];"));
    const packageCheck = loop.indexOf("PackageFilter.Matches(Desc.ActorPackage)");
    expect(packageCheck).toBeGreaterThan(-1);
    for (const later of ["WantedGuids.Contains", "ActorDescMatchesFilter(Desc", "bLoadedOnly && !bLoaded"]) {
      expect(loop.indexOf(later), later).toBeGreaterThan(packageCheck);
    }
  });

  it("reports missingPackages from list and from every load_actor_descs answer, matched or not", () => {
    expect(WP_SOURCE.match(/SetArrayField\(TEXT\("missingPackages"\)/g)?.length).toBe(3);
  });

  it("resolves folderPath the way the Outliner does, through the exported engine lookup", () => {
    expect(WP_SOURCE).toContain('#include "EditorActorFolders.h"');
    expect(WP_SOURCE).toContain("FActorFolders::GetActorDescInstanceFolder(World, &Instance).GetPath()");
    expect(WP_SOURCE).toContain('Obj->SetStringField(TEXT("folderPath")');
    // Only for the descriptors the filters keep, so a package lookup does not resolve every folder in the map.
    expect(WP_SOURCE).toContain("OutMatches.Add_GetRef(Desc).FolderPath = ResolveActorDescFolder(*World, *AllInstances[DescIndex]);");
  });
});
