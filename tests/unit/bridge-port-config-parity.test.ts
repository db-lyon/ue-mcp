import { describe, expect, it } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { UeMcpConfigSchema } from "../../src/schemas.js";

/**
 * `ue-mcp.bridge.port` is read twice.
 *
 * The client reads it through the layered YAML cascade in src/project.ts. The
 * C++ bridge reads it with its own single-key reader, because Unreal ships no
 * YAML parser and one integer does not justify a dependency. If the two
 * disagree about which files they consult, in what order, or what counts as a
 * valid port, a project that pins one gets a client aimed at one number and an
 * editor listening on another (#819).
 *
 * Neither file can import the other, so this test is the joint.
 */

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, "..", "..");

const BRIDGE_SERVER_CPP = path.join(
  REPO_ROOT,
  "plugin",
  "ue_mcp_bridge",
  "Source",
  "UE_MCP_Bridge",
  "Private",
  "BridgeServer.cpp",
);
const PROJECT_TS = path.join(REPO_ROOT, "src", "project.ts");

/** The body of a function, from its signature to the closing brace at column zero (TS) or one tab (C++). */
function functionBody(file: string, signature: string, closer: string): string {
  const text = fs.readFileSync(file, "utf8");
  const start = text.indexOf(signature);
  if (start < 0) throw new Error(`${path.basename(file)} no longer declares '${signature}'`);
  const end = text.indexOf(closer, start);
  if (end < 0) throw new Error(`could not find the end of '${signature}' in ${path.basename(file)}`);
  return text.slice(start, end);
}

/** Canonical layer names, in the order the given source visits them. */
function layerOrder(body: string, markers: Array<[string, string]>): string[] {
  return markers
    .map(([name, marker]) => [name, body.indexOf(marker)] as const)
    .filter(([, at]) => at >= 0)
    .sort((a, b) => a[1] - b[1])
    .map(([name]) => name);
}

const TS_LAYERS: Array<[string, string]> = [
  ["user-global", "readGlobalUeMcpBlock()"],
  ["project", `"ue-mcp.yml"`],
  ["env-overlay", "ue-mcp.${env}.yml"],
  ["local", `"ue-mcp.local.yml"`],
];

const CPP_LAYERS: Array<[string, string]> = [
  ["user-global", "UserGlobalConfigPath()"],
  ["project", `TEXT("ue-mcp.yml")`],
  ["env-overlay", `TEXT("ue-mcp.%s.yml")`],
  ["local", `TEXT("ue-mcp.local.yml")`],
];

describe("bridge.port config parity between the client and the plugin", () => {
  it("consults the same config layers, and the plugin walks them highest-first", () => {
    const tsOrder = layerOrder(
      functionBody(PROJECT_TS, "function loadLayeredUeMcpBlock(", "\n}"),
      TS_LAYERS,
    );
    const cppOrder = layerOrder(
      functionBody(BRIDGE_SERVER_CPP, "int32 ReadConfiguredBridgePort(", "\n\t}"),
      CPP_LAYERS,
    );

    // src/project.ts deep-merges lowest precedence first, so its last layer
    // wins. The plugin has no merge step: it takes the first hit, so it must
    // read the same list backwards.
    expect(tsOrder).toEqual(["user-global", "project", "env-overlay", "local"]);
    expect(cppOrder).toEqual([...tsOrder].reverse());
  });

  it("reads the same key path out of those files", () => {
    const cpp = fs.readFileSync(BRIDGE_SERVER_CPP, "utf8");
    // The client reaches `ue-mcp:` -> `bridge:` -> `port:`; the plugin spells
    // that path out as an array for its reader.
    expect(cpp).toContain(`{ TEXT("ue-mcp"), TEXT("bridge"), TEXT("port") }`);
    expect(UeMcpConfigSchema.safeParse({ bridge: { port: 50123 } }).success).toBe(true);
  });

  it("accepts the same range of port numbers as the config schema", () => {
    const cpp = fs.readFileSync(BRIDGE_SERVER_CPP, "utf8");
    expect(cpp).toContain("Parsed < 1 || Parsed > 65535");

    // The bound the plugin hard-codes has to be the bound the schema enforces,
    // or one side accepts a value the other refuses and the ports diverge.
    expect(UeMcpConfigSchema.safeParse({ bridge: { port: 1 } }).success).toBe(true);
    expect(UeMcpConfigSchema.safeParse({ bridge: { port: 65535 } }).success).toBe(true);
    expect(UeMcpConfigSchema.safeParse({ bridge: { port: 0 } }).success).toBe(false);
    expect(UeMcpConfigSchema.safeParse({ bridge: { port: 65536 } }).success).toBe(false);
  });
});
