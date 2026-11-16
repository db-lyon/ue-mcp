import { describe, it, expect, afterEach } from "vitest";
import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { ENV_VARS, readEnv, resolveSetting } from "../../src/core/env.js";

const srcDir = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "src");

function sources(dir: string, exts: string[] = [".ts"]): string[] {
  return readdirSync(dir).flatMap((name) => {
    const full = join(dir, name);
    if (statSync(full).isDirectory()) return sources(full, exts);
    return exts.some((e) => full.endsWith(e)) ? [full] : [];
  });
}

const savedLogLevel = process.env.UE_MCP_LOG_LEVEL;
afterEach(() => {
  if (savedLogLevel === undefined) delete process.env.UE_MCP_LOG_LEVEL;
  else process.env.UE_MCP_LOG_LEVEL = savedLogLevel;
});

const registered = new Set<string>(Object.values(ENV_VARS));
/** A read (or write) of a named variable on process.env or an env object handed in for testing. */
const ENV_ACCESS = /\benv(?:\.|\[\s*["'])((?:UE_|ANTHROPIC_|REGISTRY_)[A-Z0-9_]*)/g;

describe("core/env.ts", () => {
  it("is the only place src reads a setting variable by name", () => {
    const direct = sources(srcDir)
      .filter((f) => relative(srcDir, f).split(sep).join("/") !== "core/env.ts")
      .flatMap((f) => [...readFileSync(f, "utf8").matchAll(ENV_ACCESS)].map((m) => `${relative(srcDir, f)}: ${m[1]}`));
    expect(direct).toEqual([]);
  });

  it("registers every variable the editor plugin reads", () => {
    const pluginDir = join(srcDir, "..", "plugin");
    const read = sources(pluginDir, [".cpp", ".h"]).flatMap((f) =>
      [...readFileSync(f, "utf8").matchAll(/GetEnvironmentVariable\(\s*TEXT\("(UE_MCP_[A-Z0-9_]+)"\)/g)].map((m) => m[1]),
    );
    expect(read.length).toBeGreaterThan(0);
    expect([...new Set(read)].filter((name) => !registered.has(name))).toEqual([]);
  });

  it("registers every variable the repo's build scripts read", () => {
    const text = readFileSync(join(srcDir, "..", "scripts", "build-utils.js"), "utf8");
    const read = [
      ...[...text.matchAll(/\benv\.((?:UE_)[A-Z0-9_]+)/g)].map((m) => m[1]),
      ...[...text.matchAll(/envFlag\(\s*env\s*,\s*'([A-Z0-9_]+)'/g)].map((m) => m[1]),
    ];
    expect(read.length).toBeGreaterThan(0);
    expect([...new Set(read)].filter((name) => !registered.has(name))).toEqual([]);
  });

  it("resolves a setting with one precedence: explicit, env, config, project, user, default", () => {
    const env = { UE_MCP_HOST: "10.0.0.2" };
    expect(resolveSetting("bridgeHost", { explicit: "h1", config: "h2" }, env)).toEqual({ value: "h1", source: "explicit" });
    expect(resolveSetting("bridgeHost", { config: "h2" }, env)).toEqual({ value: "10.0.0.2", source: "env" });
    expect(resolveSetting("bridgeHost", { config: " h2 " }, {})).toEqual({ value: "h2", source: "config" });
    expect(resolveSetting("bridgeHost", {}, {})).toEqual({ value: "127.0.0.1", source: "default" });
    expect(resolveSetting("feedbackMode", { project: "defer", user: "auto-approve" }, {})).toEqual({ value: "defer", source: "project" });
    expect(resolveSetting("feedbackMode", { user: " AUTO-APPROVE " }, {})).toEqual({ value: "auto-approve", source: "user" });
    expect(resolveSetting("bridgePort", {}, {})).toEqual({ value: undefined, source: "default" });
  });

  it("skips a layer that names no value, and says when the env var was the one skipped", () => {
    expect(resolveSetting("dialogMode", { user: "defer" }, { UE_MCP_DIALOG_MODE: "sometimes" })).toEqual({
      value: "defer",
      source: "user",
      ignoredEnv: "sometimes",
    });
    expect(resolveSetting("bridgePort", { config: 0 }, { UE_MCP_PORT: "abc" })).toMatchObject({ value: undefined, source: "default" });
    expect(resolveSetting("bridgePort", { config: 9001 }, { UE_MCP_PORT: "9100" })).toEqual({ value: 9100, source: "env" });
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
    for (const name of names) expect(name).toMatch(/^[A-Z][A-Z0-9_]+$/);
  });
});
