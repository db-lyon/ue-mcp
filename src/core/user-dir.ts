import * as os from "node:os";
import * as path from "node:path";

/**
 * `~/.ue-mcp`, where per-user state lives. Resolved on every call so a test
 * that redirects HOME sees the change; each file under it keeps its own env
 * override.
 */
export function userDir(): string {
  return path.join(os.homedir(), ".ue-mcp");
}
