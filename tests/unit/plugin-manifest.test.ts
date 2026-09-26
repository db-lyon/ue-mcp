import { describe, it, expect } from "vitest";
import {
  PluginManifestSchema,
  prefixedActionName,
  compileSchemaFields,
  parseManifest,
} from "../../src/extensions/manifest.js";
import { satisfiesMinimum, compareVersions } from "../../src/extensions/version.js";

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

  it("accepts any JSON value when the type is omitted (#892)", () => {
    const out = compileSchemaFields({ value: { description: "coerced via ImportText" } });
    for (const v of [1, true, "EMyEnum::Foo", ["a"], { k: 1 }]) {
      expect(out.value.safeParse(v).success).toBe(true);
    }
    expect(out.value.isOptional()).toBe(true);
  });

  it("still demands presence for a required untyped param", () => {
    const out = compileSchemaFields({ value: { required: true } });
    expect(out.value.safeParse(7).success).toBe(true);
    expect(out.value.safeParse(undefined).success).toBe(false);
    expect(out.value.isOptional()).toBe(false);
  });

  it("compiles a list of types into a union", () => {
    const out = compileSchemaFields({ value: { type: ["number", "boolean"] } });
    expect(out.value.safeParse(1).success).toBe(true);
    expect(out.value.safeParse(true).success).toBe(true);
    expect(out.value.safeParse("nope").success).toBe(false);
  });

  it("compiles a one-element type list as that type", () => {
    const out = compileSchemaFields({ value: { type: ["string"] } });
    expect(out.value.safeParse("ok").success).toBe(true);
    expect(out.value.safeParse(3).success).toBe(false);
  });
});

describe("parseManifest salvage (#892)", () => {
  const nativeManifest = (handlers: Record<string, unknown>) => ({
    actionPrefix: "pie",
    nativeModule: {
      uePluginName: "PIE_Studio",
      minBridgeApi: 1,
      source: "ue/Plugins/PIE_Studio",
      category: "pie",
      handlers,
    },
  });

  it("reports nothing dropped for a manifest that validates as authored", () => {
    const { manifest, dropped } = parseManifest(
      nativeManifest({ actor_set: { schema: { value: { type: "string" } } } }),
    );
    expect(dropped).toEqual([]);
    expect(Object.keys(manifest.nativeModule!.handlers)).toEqual(["actor_set"]);
  });

  it("drops only the offending handler, keeping the rest of the category", () => {
    const { manifest, dropped } = parseManifest(
      nativeManifest({
        actor_spawn: { schema: { class: { type: "string" } } },
        actor_set: { schema: { value: { type: "not-a-type" } } },
        snapshot: { schema: { target: { type: "string" } } },
      }),
    );
    expect(Object.keys(manifest.nativeModule!.handlers)).toEqual(["actor_spawn", "snapshot"]);
    expect(dropped).toHaveLength(1);
    expect(dropped[0].path).toBe("nativeModule.handlers.actor_set");
  });

  it("drops one inject action without taking its category down", () => {
    const { manifest, dropped } = parseManifest({
      actionPrefix: "vpp",
      inject: {
        pcg: {
          good: { task: "vpp.good" },
          bad: { task: "" },
        },
      },
    });
    expect(Object.keys(manifest.inject.pcg)).toEqual(["good"]);
    expect(dropped.map((d) => d.path)).toEqual(["inject.pcg.bad"]);
  });

  it("prunes a task entry by its full dotted name", () => {
    const { manifest, dropped } = parseManifest({
      actionPrefix: "vpp",
      tasks: {
        "vpp.good": { class_path: "voxel/Good" },
        "vpp.bad": { class_path: "" },
      },
    });
    expect(Object.keys(manifest.tasks)).toEqual(["vpp.good"]);
    expect(dropped.map((d) => d.path)).toEqual(["tasks.vpp.bad"]);
  });

  it("still fails the whole plugin on a structural error", () => {
    expect(() => parseManifest({ actionPrefix: "NOT-VALID" })).toThrow();
    expect(() => parseManifest({ nativeModule: { uePluginName: "X" } })).toThrow();
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

  it("orders prerelease identifiers per semver, numerically where numeric", () => {
    expect(compareVersions("1.0.0-beta.10", "1.0.0-beta.9")).toBe(1);
    expect(compareVersions("1.0.0-beta.9", "1.0.0-beta.10")).toBe(-1);
    expect(compareVersions("1.0.0-beta", "1.0.0-beta.2")).toBe(-1);
    expect(compareVersions("1.0.0-1", "1.0.0-alpha")).toBe(-1);
    expect(compareVersions("1.0.0-alpha", "1.0.0-beta")).toBe(-1);
    expect(compareVersions("1.0.0+build.5", "1.0.0")).toBe(0);
    expect(satisfiesMinimum("1.3.10-beta.10", "1.3.10-beta.9")).toBe(true);
  });

  it("satisfiesMinimum returns true for equal", () => {
    expect(satisfiesMinimum("1.0.0", "1.0.0")).toBe(true);
    expect(satisfiesMinimum("1.0.1", "1.0.0")).toBe(true);
    expect(satisfiesMinimum("1.0.0", "1.0.1")).toBe(false);
  });
});
