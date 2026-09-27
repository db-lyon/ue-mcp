/**
 * A plugin action is described from its manifest's `schema:`, the way a
 * built-in action is described from its declared options or C++ spec, never
 * from the prose of its description.
 */
import { describe, it, expect } from "vitest";
import { z } from "zod";
import type { ToolDef } from "../../src/core/types.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import { parseManifest } from "../../src/extensions/manifest.js";
import { mergeInjectionsIntoTool } from "../../src/extensions/injection.js";
import { buildProvidedTool } from "../../src/extensions/provision.js";
import { actionSchema, UNDECLARED_PLUGIN_PARAMS } from "../../src/surface/action-schema.js";
import { actionSignature } from "../../src/surface/action-signature.js";
import { discoveryResults, fullSurfaceDescription } from "../../src/surface/context/lean-context.js";

/** A plugin manifest as it would be read from ue-mcp.plugin.yml. */
const { manifest } = parseManifest({
  actionPrefix: "vox",
  inject: {
    pcg: {
      scatter: {
        task: "vox.scatter",
        effect: "mutate",
        // The prose disagrees with the schema on purpose: the schema wins.
        description: "Scatter meshes across a voxel terrain. Params: prosePath, proseCount?",
        schema: {
          graphPath: { type: "string", required: true, description: "PCG graph to scatter with" },
          density: { type: "number", description: "Points per square metre" },
          tags: { type: "array" },
          seed: { type: ["number", "string"] },
          payload: {},
        },
      },
      legacy_sweep: {
        task: "vox.legacy_sweep",
        description: "Sweep the terrain. Params: terrainPath, radius?",
      },
    },
  },
  provides: {
    voxel: {
      description: "Voxel terrain editing.",
      actions: {
        sculpt: {
          task: "vox.sculpt",
          effect: "mutate",
          description: "Sculpt the terrain. Params: brushName",
          schema: {
            terrainPath: { type: "string", required: true },
            strength: { type: "number" },
          },
        },
        stats: { task: "vox.stats", effect: "read", description: "Terrain statistics.", schema: {} },
        erode: { task: "vox.erode", description: "Erode the terrain. Params: iterations?" },
      },
    },
  },
});

function fakePcg(): ToolDef {
  return categoryTool(
    "pcg",
    "Fake PCG tool for testing.",
    {
      list_graphs: bp("read", "List graphs. Params: directory?", "pcg_list_graphs"),
      add_node: {
        kind: "handler",
        effect: "mutate",
        options: { params: ["graphPath", "nodeType", "density?"] },
        description: "Add a node. Params: graphPath, nodeType, density?",
        handler: async () => ({}),
      },
    },
    {
      graphPath: z.string().optional().describe("Graph asset path"),
      nodeType: z.string().optional(),
      density: z.string().optional().describe("Built-in density preset"),
      directory: z.string().optional(),
    },
  );
}

function injected(): ToolDef {
  return mergeInjectionsIntoTool(fakePcg(), [{
    category: "pcg",
    prefix: manifest.actionPrefix,
    pluginName: "ue-mcp-voxel",
    actions: manifest.inject.pcg,
  }]).tool;
}

function provided(): ToolDef {
  return buildProvidedTool({ category: "voxel", pluginName: "ue-mcp-voxel", spec: manifest.provides.voxel });
}

const own = (tool: ToolDef, action: string) =>
  actionSchema(tool, action).params.filter((p) => p.sources.includes("documented"));

describe("an injected plugin action with a schema", () => {
  it("describes every schema field with the manifest's type, requiredness and description", () => {
    const params = own(injected(), "vox_scatter");
    expect(params.map((p) => [p.name, p.type, p.required])).toEqual([
      ["graphPath", "string", true],
      ["density", "string", false],
      ["payload", "any", false],
      ["seed", "number|string", false],
      ["tags", "any[]", false],
    ]);
    expect(params.find((p) => p.name === "graphPath")?.description).toBe("PCG graph to scatter with");
  });

  it("reports the wire's type, and the manifest's description, for a key the category already declares", () => {
    const density = own(injected(), "vox_scatter").find((p) => p.name === "density");
    expect(density?.type).toBe("string");
    expect(density?.required).toBe(false);
    expect(density?.description).toBe("Points per square metre");
  });

  it("ignores the Params prose in its description", () => {
    const schema = actionSchema(injected(), "vox_scatter");
    const names = schema.params.map((p) => p.name);
    expect(names).not.toContain("prosePath");
    expect(names).not.toContain("proseCount");
    expect(schema.drift).toEqual([]);
    expect(schema.paramsNote).toBeUndefined();
  });

  it("signs itself from the schema, in describe, search and the tools/list line", () => {
    const tool = injected();
    const sig = "vox_scatter(graphPath, density?, payload?:*, seed?:n/s, tags?:[*])";
    expect(actionSignature(tool, "vox_scatter")).toBe(sig);
    expect(fullSurfaceDescription(tool).split("\n")).toContain(sig);
    expect(discoveryResults([tool], "scatter voxel terrain", 5)).toContain(`pcg.${sig}`);
  });
});

describe("a provided plugin category", () => {
  it("describes and signs its actions from the schema", () => {
    const tool = provided();
    expect(own(tool, "sculpt").map((p) => [p.name, p.type, p.required])).toEqual([
      ["terrainPath", "string", true],
      ["strength", "number", false],
    ]);
    expect(actionSignature(tool, "sculpt")).toBe("sculpt(terrainPath, strength?:n)");
    expect(fullSurfaceDescription(tool)).toContain("\nsculpt(terrainPath, strength?:n)");
  });

  it("reads an empty schema as no parameters, not as a missing schema", () => {
    const tool = provided();
    expect(actionSignature(tool, "stats")).toBe("stats()");
    expect(actionSchema(tool, "stats").paramsNote).toBeUndefined();
  });
});

describe("a plugin action without a schema", () => {
  it("keeps working and says in describe that its parameters live in the description", () => {
    for (const [tool, action, prose] of [
      [injected(), "vox_legacy_sweep", "terrainPath"],
      [provided(), "erode", "iterations"],
    ] as const) {
      const schema = actionSchema(tool, action);
      expect(schema.paramsNote).toBe(UNDECLARED_PLUGIN_PARAMS);
      expect(schema.description).toContain(`Params: ${prose}`);
      expect(own(tool, action)).toEqual([]);
      expect(actionSignature(tool, action)).toBe(`${action}(*)`);
    }
  });
});

describe("built-in actions beside injected ones", () => {
  it("describe and sign exactly as they did before the injection", () => {
    const before = fakePcg();
    const after = injected();
    for (const action of Object.keys(before.actions)) {
      expect(actionSchema(after, action)).toEqual(actionSchema(before, action));
      expect(actionSignature(after, action)).toBe(actionSignature(before, action));
    }
  });
});
