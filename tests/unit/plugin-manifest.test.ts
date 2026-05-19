import { describe, it, expect } from "vitest";
import { PluginManifestSchema, prefixedActionName, compileSchemaFields } from "../../src/plugin/manifest.js";
import { satisfiesMinimum, compareVersions } from "../../src/plugin/version.js";

describe("PluginManifestSchema", () => {
  it("accepts a minimal manifest", () => {
    const r = PluginManifestSchema.safeParse({ actionPrefix: "vpp" });
    expect(r.success).toBe(true);
    if (r.success) {
      expect(r.data.inject).toEqual({});
      expect(r.data.knowledge).toEqual({});
      expect(r.data.tasks).toEqual({});
      expect(r.data.flows).toEqual({});
    }
  });

  it("rejects a missing actionPrefix", () => {
    const r = PluginManifestSchema.safeParse({});
    expect(r.success).toBe(false);
  });

  it("rejects an actionPrefix with disallowed characters", () => {
    const r = PluginManifestSchema.safeParse({ actionPrefix: "VPP-1" });
    expect(r.success).toBe(false);
  });

  it("accepts a full manifest", () => {
    const r = PluginManifestSchema.safeParse({
      actionPrefix: "vpp",
      minServerVersion: "1.0.0",
      uePluginDependency: "VoxelPro",
      inject: {
        pcg: {
          scatter_on_terrain: {
            task: "vpp.scatter_on_terrain",
            description: "Scatter on a voxel terrain",
            schema: {
              graphPath: { type: "string", required: true },
              cellSize: { type: "number" },
            },
          },
        },
      },
      knowledge: { pcg: "knowledge/pcg.md" },
      tasks: {
        "vpp.scatter_on_terrain": { class_path: "voxel-plugin-pro/ScatterOnTerrain" },
      },
      flows: {
        full_setup: {
          description: "Full setup",
          steps: { 1: { task: "vpp.scatter_on_terrain" } },
        },
      },
    });
    expect(r.success).toBe(true);
  });
});

describe("prefixedActionName", () => {
  it("joins prefix and action with underscore", () => {
    expect(prefixedActionName("vpp", "scatter_on_terrain")).toBe("vpp_scatter_on_terrain");
  });
});

describe("compileSchemaFields", () => {
  it("makes non-required fields optional", () => {
    const out = compileSchemaFields({
      a: { type: "string", required: true },
      b: { type: "number" },
    });
    expect(out.a.isOptional()).toBe(false);
    expect(out.b.isOptional()).toBe(true);
  });

  it("returns empty for undefined", () => {
    expect(compileSchemaFields(undefined)).toEqual({});
  });
});

describe("version comparison", () => {
  it("compares major.minor.patch", () => {
    expect(compareVersions("1.0.0", "1.0.0")).toBe(0);
    expect(compareVersions("1.0.1", "1.0.0")).toBe(1);
    expect(compareVersions("1.0.0", "1.0.1")).toBe(-1);
    expect(compareVersions("2.0.0", "1.99.99")).toBe(1);
  });

  it("treats prerelease as lower than non-pre", () => {
    expect(compareVersions("1.0.0-alpha", "1.0.0")).toBe(-1);
    expect(compareVersions("1.0.0", "1.0.0-beta")).toBe(1);
  });

  it("satisfiesMinimum returns true for equal", () => {
    expect(satisfiesMinimum("1.0.0", "1.0.0")).toBe(true);
    expect(satisfiesMinimum("1.0.1", "1.0.0")).toBe(true);
    expect(satisfiesMinimum("1.0.0", "1.0.1")).toBe(false);
  });
});
