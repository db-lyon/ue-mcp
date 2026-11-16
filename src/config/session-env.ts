// @session per-session: Reports which environment variables flatten the registered editors.
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
import { ENV_VARS, SETTINGS, readEnv, type EnvVar } from "../core/env.js";

interface CollapsingEnvVar {
  env: EnvVar;
  /** The per-project key that differentiates the sessions when it is unset. */
  configKey: string;
  /** What it decides, in the reader's terms. */
  decides: string;
}

const modeKey = (name: string): string => `${name} mode (npx ue-mcp ${name} mode <m> --editor <name>)`;

const COLLAPSING: CollapsingEnvVar[] = [
  // First, because it decides first: engine-root.ts makes it candidate #1,
  // ahead of every editor and build tool setting, so a value left over from an
  // earlier build session builds a 5.6 project and a 5.8 project against one engine.
  {
    env: "testEngineRoot",
    configKey: "editor.path / editor.buildToolPath",
    decides: "the engine tree every session launches and builds against, ahead of every other engine setting",
  },
  { env: "port", configKey: SETTINGS.bridgePort.config!, decides: "the bridge port every session connects on" },
  { env: "host", configKey: SETTINGS.bridgeHost.config!, decides: "the host every session's bridge is reached on" },
  { env: "editorPath", configKey: SETTINGS.editorPath.config!, decides: "the editor binary every session launches" },
  { env: "buildToolPath", configKey: SETTINGS.buildToolPath.config!, decides: "the build tool every session compiles with" },
  { env: "env", configKey: "env", decides: "the ue-mcp.<env>.yml overlay every session merges" },
  { env: "contextStrategy", configKey: "context.strategy", decides: "the context strategy for the whole server" },
  { env: "feedbackMode", configKey: modeKey("feedback"), decides: "the feedback approval mode every session uses" },
  { env: "dialogMode", configKey: modeKey("dialog"), decides: "how a modal dialog blocking any session's editor is handled, including whether the agent gets to answer it" },
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
    const value = readEnv(entry.env, env);
    if (typeof value !== "string" || value.trim() === "") continue;
    out.push(
      `${ENV_VARS[entry.env]}=${value} decides ${entry.decides}, so it applies to all ${sessionNames.length} editors ` +
        `(${names}) and overrides what any of them configured. Unset it and give each project its own ` +
        `'${entry.configKey}' to tell them apart.`,
    );
  }
  return out;
}
