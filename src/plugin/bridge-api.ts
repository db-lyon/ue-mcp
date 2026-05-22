import * as fs from "node:fs";
import * as path from "node:path";

/**
 * Resolve the bridge ABI version (UEMCP_BRIDGE_API_VERSION) for the bridge
 * that's currently deployed into the user's project. The constant lives in
 * `Plugins/UE_MCP_Bridge/Source/UE_MCP_Bridge/Public/MCPHandlerRegistration.h`,
 * which `ue-mcp init`/`update` deploy alongside the rest of the bridge
 * source. Returns null when the header can't be located - the gate then
 * degrades to a warning rather than blocking install.
 */
export function readDeployedBridgeApiVersion(projectDir: string): number | null {
  const candidates = [
    path.join(
      projectDir,
      "Plugins",
      "UE_MCP_Bridge",
      "Source",
      "UE_MCP_Bridge",
      "Public",
      "MCPHandlerRegistration.h",
    ),
  ];
  for (const file of candidates) {
    if (!fs.existsSync(file)) continue;
    try {
      const text = fs.readFileSync(file, "utf-8");
      const match = /#define\s+UEMCP_BRIDGE_API_VERSION\s+(\d+)/.exec(text);
      if (match) return parseInt(match[1], 10);
    } catch {
      // ignore - fall through and return null
    }
  }
  return null;
}
