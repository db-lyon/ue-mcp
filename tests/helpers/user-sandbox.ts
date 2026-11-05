/**
 * The environment that keeps a spawned ue-mcp process out of the developer's
 * own user files: the home directory itself, and every per-user file the
 * server reads, each through the variable src/core/env.ts declares for it.
 */
import * as path from "node:path";

export function userSandboxEnv(home: string): Record<string, string> {
  const ueMcp = path.join(home, ".ue-mcp");
  return {
    HOME: home,
    USERPROFILE: home,
    APPDATA: path.join(home, "AppData", "Roaming"),
    UE_MCP_GLOBAL_CONFIG: path.join(ueMcp, "config.yml"),
    UE_MCP_USER_STATE: path.join(ueMcp, "state.json"),
    UE_MCP_AUTH_DIR: ueMcp,
    UE_MCP_DAEMON_DIR: path.join(ueMcp, "daemons"),
    UE_MCP_EXTENSIONS_DIR: path.join(ueMcp, "extensions"),
    UE_MCP_PENDING_DIR: path.join(ueMcp, "pending-feedback"),
    UE_MCP_REGISTRY_CACHE: path.join(ueMcp, "registry-catalog.json"),
    UE_MCP_VERSION_CACHE: path.join(ueMcp, "version-check.json"),
  };
}

/** The UE_MCP_* variables userSandboxEnv sets, for a keep-list that strips the rest. */
export const USER_SANDBOX_VARS: readonly string[] = Object.keys(userSandboxEnv("")).filter((k) => k.startsWith("UE_MCP_"));
