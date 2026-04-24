import { describe, it, expect } from "vitest";
import { UProjectSchema, UeMcpConfigSchema, UPluginSchema } from "../../src/schemas.js";

describe("UProjectSchema", () => {
  it("accepts a minimal .uproject", () => {
    const r = UProjectSchema.safeParse({ EngineAssociation: "5.7" });
    expect(r.success).toBe(true);
  });

  it("accepts a .uproject with Plugins array", () => {
    const r = UProjectSchema.safeParse({
      EngineAssociation: "{0000-0000}",
      Plugins: [{ Name: "UE_MCP_Bridge", Enabled: true }],
    });
    expect(r.success).toBe(true);
  });

  it("accepts unknown top-level fields (passthrough)", () => {
    const r = UProjectSchema.safeParse({
      EngineAssociation: "5.7",
      FileVersion: 3,
      Modules: [{ Name: "MyGame", Type: "Runtime" }],
    });
    expect(r.success).toBe(true);
  });

  it("rejects non-object input", () => {
    expect(UProjectSchema.safeParse("not an object").success).toBe(false);
    expect(UProjectSchema.safeParse(null).success).toBe(false);
    expect(UProjectSchema.safeParse(42).success).toBe(false);
  });

  it("rejects Plugins entries with wrong Enabled type", () => {
    const r = UProjectSchema.safeParse({ Plugins: [{ Name: "X", Enabled: "yes" }] });
    expect(r.success).toBe(false);
  });
});

describe("UeMcpConfigSchema", () => {
  it("accepts an empty object", () => {
    expect(UeMcpConfigSchema.safeParse({}).success).toBe(true);
  });

  it("accepts contentRoots / disable / http", () => {
    const r = UeMcpConfigSchema.safeParse({
      contentRoots: ["/Game/", "/MyPlugin/"],
      disable: ["gas"],
      http: { enabled: true, port: 7723, host: "127.0.0.1" },
    });
    expect(r.success).toBe(true);
  });

  it("rejects http.port out of range", () => {
    const r = UeMcpConfigSchema.safeParse({ http: { port: 99999 } });
    expect(r.success).toBe(false);
  });

  it("rejects disable as non-array", () => {
    const r = UeMcpConfigSchema.safeParse({ disable: "gas" });
    expect(r.success).toBe(false);
  });
});

describe("UPluginSchema", () => {
  it("reads VersionName", () => {
    const r = UPluginSchema.safeParse({ VersionName: "1.0.0" });
    expect(r.success).toBe(true);
    if (r.success) expect(r.data.VersionName).toBe("1.0.0");
  });

  it("accepts .uplugin without VersionName", () => {
    expect(UPluginSchema.safeParse({ FriendlyName: "X" }).success).toBe(true);
  });
});
