// @session independent: Declares which agent client the CLI drives; no editor involved.
/**
 * The agent client ue-mcp's CLI drives beyond writing its MCP config: the one
 * `ue-mcp hook` answers for, `ue-mcp uninstall-hooks` cleans up after, and
 * `ue-mcp resolve` launches to work an issue. Its MCP config locations are in
 * CLIENT_LOCATIONS with every other client's.
 */
import { CLAUDE_CODE } from "../claude-code/claude-code-client.js";

export interface AgentClient {
  /** The name shown to the user. */
  name: string;
  /** How messages about its process refer to it. */
  shortName: string;
  /** The executable `ue-mcp resolve` launches. */
  command: string;
  /** How to install it, shown when the executable is missing. */
  installHint: string;
  /** Arguments for an unattended run that reads its prompt on stdin. */
  resolveArgs(): string[];
  /** The summary `ue-mcp hook` shows in the command table. */
  hookSummary: string;
  /** `ue-mcp hook <event>`, loaded only when it runs. */
  loadHookHandler(): Promise<{ run: (argv: string[]) => Promise<number | void> }>;
  /** Remove every hook recorded for a project; the settings files it was removed from, and those it was already gone from. */
  uninstallAllRegisteredHooks(projectDir: string): Promise<{ removed: string[]; skipped: string[] }>;
}

export const AGENT_CLIENT: AgentClient = CLAUDE_CODE;
