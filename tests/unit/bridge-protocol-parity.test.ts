import { describe, expect, it } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { CLIENT_PROTOCOL_VERSION } from "../../src/bridge/bridge.js";

/**
 * The wire protocol version is declared twice: once in the plugin header the
 * editor compiles, once in the client that talks to it. Only one of the two is
 * ever edited when someone changes the wire, and the symptom of the other
 * lagging is a client that reports a mismatch against itself, or worse, one
 * that reports agreement across a change it cannot actually read.
 *
 * The two files cannot import from each other, so this test is the joint.
 */

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, "..", "..");

const REGISTRATION_HEADER = path.join(
  REPO_ROOT,
  "plugin",
  "ue_mcp_bridge",
  "Source",
  "UE_MCP_Bridge",
  "Public",
  "MCPHandlerRegistration.h",
);

function readDefine(name: string): number | null {
  const text = fs.readFileSync(REGISTRATION_HEADER, "utf8");
  const match = new RegExp(String.raw`#define\s+${name}\s+(\d+)`).exec(text);
  return match ? Number.parseInt(match[1], 10) : null;
}

describe("bridge protocol version parity", () => {
  it("declares the same wire protocol version in the plugin header and the client", () => {
    const pluginVersion = readDefine("UEMCP_BRIDGE_PROTOCOL_VERSION");
    expect(pluginVersion).not.toBeNull();
    expect(pluginVersion).toBe(CLIENT_PROTOCOL_VERSION);
  });

  it("keeps the wire version separate from the handler ABI version", () => {
    // Two numbers with two jobs. A test that reads one and calls it the other
    // would pass on a header where only the ABI number exists.
    expect(readDefine("UEMCP_BRIDGE_API_VERSION")).not.toBeNull();
  });
});
