import { describe, it, expect, afterEach } from "vitest";
import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";
import { ENV_VARS, readEnv } from "../../src/env.js";

const srcDir = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "src");

function sources(dir: string): string[] {
  return readdirSync(dir).flatMap((name) => {
    const full = join(dir, name);
    if (statSync(full).isDirectory()) return sources(full);
    return full.endsWith(".ts") ? [full] : [];
  });
}

const savedLogLevel = process.env.UE_MCP_LOG_LEVEL;
afterEach(() => {
  if (savedLogLevel === undefined) delete process.env.UE_MCP_LOG_LEVEL;
  else process.env.UE_MCP_LOG_LEVEL = savedLogLevel;
});

describe("env.ts", () => {
  it("is the only place src reads a UE_MCP_* variable from process.env", () => {
    const direct = sources(srcDir)
      .filter((f) => relative(srcDir, f) !== "env.ts")
      .filter((f) => /process\.env(\.UE_MCP_|\[\s*["']UE_MCP_)/.test(readFileSync(f, "utf8")))
      .map((f) => relative(srcDir, f));
    expect(direct).toEqual([]);
  });

  it("reads a variable when asked, not when the module loaded", () => {
    delete process.env.UE_MCP_LOG_LEVEL;
    expect(readEnv("logLevel")).toBeUndefined();
    process.env.UE_MCP_LOG_LEVEL = "debug";
    expect(readEnv("logLevel")).toBe("debug");
  });

  it("reads from the source it is handed", () => {
    expect(readEnv("port", { UE_MCP_PORT: "9001" })).toBe("9001");
  });

  it("names every variable once", () => {
    const names = Object.values(ENV_VARS);
    expect(new Set(names).size).toBe(names.length);
    for (const name of names) expect(name).toMatch(/^UE_MCP_[A-Z_]+$/);
  });
});
