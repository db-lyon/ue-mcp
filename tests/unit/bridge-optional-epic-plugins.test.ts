/**
 * Epic's off-by-default plugins whose headers the bridge includes are compiled
 * in only where the project enables them: their UHT-generated headers exist
 * only then, and a source-built engine has none for a plugin it never built.
 * The published binaries' host project enables them, so the binaries keep the
 * handlers. A plugin added to one list and not the other fails here.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { describe, expect, it } from "vitest";

const root = path.resolve(__dirname, "../..");
const buildCs = fs.readFileSync(path.join(root, "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/UE_MCP_Bridge.Build.cs"), "utf-8");
const binaries = fs.readFileSync(path.join(root, "scripts/binaries.mjs"), "utf-8");
const uplugin = JSON.parse(fs.readFileSync(path.join(root, "plugin/ue_mcp_bridge/UE_MCP_Bridge.uplugin"), "utf-8")) as { Plugins: Array<{ Name: string }> };

const gated = [...buildCs.matchAll(/ProjectEnables\(Target, "([A-Za-z]+)"\)/g)].map((m) => m[1]);
const hosted = JSON.parse((/const HOST_OPTIONAL_PLUGINS = (\[[^\]]*\])/.exec(binaries) ?? [, "[]"])[1]) as string[];

describe("the bridge and Epic's experimental plugins", () => {
  it("gates each one it includes on the project enabling it", () => {
    expect(gated.sort()).toEqual(["ModelContextProtocol", "SemanticSearch", "ToolsetRegistry"]);
  });

  it("enables every gated one in the host project the published binaries are built in", () => {
    expect([...hosted].sort()).toEqual([...gated].sort());
  });

  it("does not force them on in every project through its own descriptor", () => {
    for (const name of gated) expect(uplugin.Plugins.map((p) => p.Name)).not.toContain(name);
  });
});
