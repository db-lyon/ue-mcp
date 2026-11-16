// @session per-session: Reads the bridge ABI version deployed in one project.
import * as fs from "node:fs";
import * as path from "node:path";

/**
 * Resolve the bridge ABI version (UEMCP_BRIDGE_API_VERSION) for the bridge
 * that's currently deployed into the user's project, from its Public/ headers,
 * which `ue-mcp init`/`update` deploy alongside the rest of the bridge source.
 * The generated MCPWireContract.h declares it; a bridge deployed before that
 * header existed declares it in MCPHandlerRegistration.h. Returns null when
 * neither can be read - the gate then degrades to a warning rather than
 * blocking install.
 */
export function readDeployedBridgeApiVersion(projectDir: string): number | null {
  const publicDir = path.join(projectDir, "Plugins", "UE_MCP_Bridge", "Source", "UE_MCP_Bridge", "Public");
  for (const header of ["MCPWireContract.h", "MCPHandlerRegistration.h"]) {
    const file = path.join(publicDir, header);
    if (!fs.existsSync(file)) continue;
    try {
      const text = fs.readFileSync(file, "utf-8");
      const match = /#define\s+UEMCP_BRIDGE_API_VERSION\s+(\d+)/.exec(text);
      if (match) return parseInt(match[1], 10);
    } catch {
      // An unreadable header is the same as a missing one: try the next.
    }
  }
  return null;
}
