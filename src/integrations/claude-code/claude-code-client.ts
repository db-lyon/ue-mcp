// @session independent: Describes Claude Code as an agent client; no editor involved.
/**
 * Claude Code as the agent client (see mcp-clients/agent-client.ts): its hook
 * and the unattended run `ue-mcp resolve` starts.
 */

/**
 * What claude may do while it works an issue: read and edit files in the
 * repository and run the exact verify and commit commands the prompt asks
 * for. dontAsk denies everything else without a prompt, since nobody is
 * there to answer one, and no MCP server from the user's config is loaded.
 */
export const RESOLVE_ALLOWED_TOOLS = [
  "Read",
  "Glob",
  "Grep",
  "Edit",
  "Write",
  "Bash(npx tsc --noEmit)",
  "Bash(git status)",
  "Bash(git diff:*)",
  "Bash(git add:*)",
  "Bash(git commit:*)",
] as const;

export const CLAUDE_CODE = {
  name: "Claude Code",
  shortName: "Claude",
  command: "claude",
  installHint: "npm i -g @anthropic-ai/claude-code",
  resolveArgs(): string[] {
    return [
      "--print",
      "--permission-mode", "dontAsk",
      "--strict-mcp-config",
      "--allowedTools", ...RESOLVE_ALLOWED_TOOLS,
      "--disallowedTools", "WebFetch", "WebSearch",
    ];
  },
  hookSummary: "Claude Code hook handler",
  loadHookHandler: () => import("./hook-handler.js"),
  uninstallAllRegisteredHooks: async (projectDir: string) =>
    (await import("./hook-installer.js")).uninstallAllRegisteredHooks(projectDir),
};
