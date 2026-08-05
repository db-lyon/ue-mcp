/**
 * Environment variables that flatten every editor into one (#817, plan 6.7).
 *
 * Each of these is read once per process and applied everywhere, which is
 * exactly right for a server driving one editor and exactly wrong for a server
 * driving several: one value cannot describe two projects on two engine
 * versions, two hosts, or two config overlays. Every one of them now has a
 * per-project equivalent, and the env var stays the global default that wins.
 *
 * Winning is the part that has to be said out loud. A user who sets one of
 * these and then writes the per-project key gets the env var, silently, in
 * every session. So while more than one editor is registered, a set variable is
 * reported by name along with the sessions it flattens and the key that would
 * tell them apart. At one editor nothing here runs: there is nothing to
 * flatten and no message to print.
 */

export interface CollapsingEnvVar {
  /** The variable that is set. */
  variable: string;
  /** The per-project key that differentiates the sessions when it is unset. */
  configKey: string;
  /** What it decides, in the reader's terms. */
  decides: string;
}

const COLLAPSING: CollapsingEnvVar[] = [
  { variable: "UE_MCP_PORT", configKey: "bridge.port", decides: "the bridge port every session connects on" },
  { variable: "UE_MCP_HOST", configKey: "bridge.host", decides: "the host every session's bridge is reached on" },
  { variable: "UE_EDITOR_PATH", configKey: "editor.path", decides: "the editor binary every session launches" },
  { variable: "UE_BUILD_TOOL_PATH", configKey: "editor.buildToolPath", decides: "the build tool every session compiles with" },
  { variable: "UE_MCP_ENV", configKey: "env", decides: "the ue-mcp.<env>.yml overlay every session merges" },
  { variable: "UE_MCP_CONTEXT_STRATEGY", configKey: "context.strategy", decides: "the context strategy for the whole server" },
  { variable: "UE_MCP_FEEDBACK_MODE", configKey: "feedback mode (npx ue-mcp feedback mode <m> --editor <name>)", decides: "the feedback approval mode every session uses" },
];

/**
 * One line per set variable, naming the sessions it applies to.
 *
 * Returns nothing at one editor, which is what keeps a single-editor server's
 * startup output byte-identical.
 */
export function collapsingEnvWarnings(
  sessionNames: string[],
  env: NodeJS.ProcessEnv = process.env,
): string[] {
  if (sessionNames.length <= 1) return [];
  const names = sessionNames.join(", ");
  const out: string[] = [];
  for (const entry of COLLAPSING) {
    const value = env[entry.variable];
    if (typeof value !== "string" || value.trim() === "") continue;
    out.push(
      `${entry.variable}=${value} decides ${entry.decides}, so it applies to all ${sessionNames.length} editors ` +
        `(${names}) and overrides what any of them configured. Unset it and give each project its own ` +
        `'${entry.configKey}' to tell them apart.`,
    );
  }
  return out;
}
