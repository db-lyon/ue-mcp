import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import yaml from "js-yaml";
import {
  targetFile,
  readLayerGroups,
  readEffectiveGroups,
  writeLayerGroups,
} from "../../src/plugin/plugin-config-store.js";

let projectDir: string;

beforeEach(() => {
  projectDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-cfg-"));
  // Route the "global" layer into the temp dir so the test never touches the
  // real ~/.ue-mcp/config.yml.
  process.env.UE_MCP_GLOBAL_CONFIG = path.join(projectDir, "global.yml");
});

afterEach(() => {
  delete process.env.UE_MCP_GLOBAL_CONFIG;
  fs.rmSync(projectDir, { recursive: true, force: true });
});

describe("writeLayerGroups / readLayerGroups round-trip", () => {
  it("writes groups under ue-mcp.pluginConfig.<slug> and reads them back", () => {
    const file = targetFile(projectDir, "local");
    writeLayerGroups(file, "recipes", () => ({ gas: false, niagara: true }));

    expect(readLayerGroups(file, "recipes")).toEqual({ gas: false, niagara: true });

    const doc = yaml.load(fs.readFileSync(file, "utf-8")) as any;
    expect(doc["ue-mcp"].pluginConfig.recipes.groups).toEqual({ gas: false, niagara: true });
  });

  it("preserves unrelated keys in the file", () => {
    const file = targetFile(projectDir, "project");
    fs.writeFileSync(
      file,
      "ue-mcp:\n  version: 1\nplugins:\n  - name: ue-mcp-recipes\n",
    );
    writeLayerGroups(file, "recipes", () => ({ gas: false }));

    const doc = yaml.load(fs.readFileSync(file, "utf-8")) as any;
    expect(doc["ue-mcp"].version).toBe(1);
    expect(doc.plugins).toEqual([{ name: "ue-mcp-recipes" }]);
    expect(doc["ue-mcp"].pluginConfig.recipes.groups).toEqual({ gas: false });
  });

  it("applies deltas over existing layer values", () => {
    const file = targetFile(projectDir, "local");
    writeLayerGroups(file, "recipes", () => ({ gas: false, material: false }));
    writeLayerGroups(file, "recipes", (existing) => ({ ...existing, gas: true }));
    expect(readLayerGroups(file, "recipes")).toEqual({ gas: true, material: false });
  });

  it("prunes empty containers when the last group is cleared", () => {
    const file = targetFile(projectDir, "local");
    writeLayerGroups(file, "recipes", () => ({ gas: false }));
    writeLayerGroups(file, "recipes", () => ({}));
    const doc = yaml.load(fs.readFileSync(file, "utf-8")) as any;
    expect(doc?.["ue-mcp"]?.pluginConfig).toBeUndefined();
  });
});

describe("readEffectiveGroups layering", () => {
  it("local overrides project overrides global", () => {
    writeLayerGroups(targetFile(projectDir, "global"), "recipes", () => ({ gas: false, niagara: false }));
    writeLayerGroups(targetFile(projectDir, "project"), "recipes", () => ({ niagara: true }));
    writeLayerGroups(targetFile(projectDir, "local"), "recipes", () => ({ pcg: false }));

    const { state } = readEffectiveGroups(projectDir, "recipes");
    expect(state).toEqual({ gas: false, niagara: true, pcg: false });
  });

  it("records which layer set each group", () => {
    writeLayerGroups(targetFile(projectDir, "global"), "recipes", () => ({ gas: false }));
    writeLayerGroups(targetFile(projectDir, "local"), "recipes", () => ({ gas: true }));
    const { state, source } = readEffectiveGroups(projectDir, "recipes");
    expect(state.gas).toBe(true);
    expect(source.gas).toContain("ue-mcp.local.yml");
  });
});
