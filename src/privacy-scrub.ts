import * as os from "node:os";

/**
 * Server-side redaction of personal/project identifiers from feedback bodies
 * before they hit the elicitation prompt or the GitHub POST. Applied
 * unconditionally; the agent has no surface to bypass it and the user never
 * sees the pre-scrubbed bytes outside the in-process call stack.
 *
 * Threat model: feedback issues post to a public tracker. The body the agent
 * assembled may contain absolute paths, the OS username, the project name,
 * etc. None of those carry signal a reader needs to understand the issue,
 * and all of them carry privacy/confidentiality risk (NDA projects, machine
 * fingerprinting, doxxing).
 *
 * What gets redacted:
 *
 *   - The absolute project root path                  → REDACTED_PROJECT_ROOT
 *   - The OS home directory path                      → REDACTED_HOME
 *   - The project name as a whole word                → REDACTED_PROJECT
 *   - The OS username as a whole word                 → REDACTED_USER
 *
 * Project/username matches use `\b` word boundaries (case-insensitive) so
 * "david" inside paths or sentences matches but "davidson" does not. Class,
 * component, and actor names are NOT redacted — the user can use the
 * "Revise first" path on the approval prompt to request specific further
 * redactions.
 */

export interface PrivacyScrubContext {
  /** Absolute path to the .uproject's containing directory. */
  projectRoot?: string;
  /** Project name without the .uproject extension. */
  projectName?: string;
  /** OS username. Defaults to os.userInfo().username; tests pass an explicit value. */
  username?: string;
  /** OS home directory. Defaults to os.homedir(); tests pass an explicit value. */
  homeDir?: string;
}

export interface PrivacyScrubResult {
  text: string;
  hits: Array<{ rule: string; count: number }>;
}

// Names shorter than this are not redacted as whole-word matches because the
// false-positive surface explodes. Project name "AI" would otherwise nuke
// every occurrence of "ai" in the body.
const MIN_WORD_LEN = 3;

function escapeRegex(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

export function privacyScrub(
  input: string,
  ctx: PrivacyScrubContext = {},
): PrivacyScrubResult {
  let text = input;
  const hits: Array<{ rule: string; count: number }> = [];

  function record(rule: string, count: number) {
    if (count > 0) hits.push({ rule, count });
  }

  // Replace an absolute path under multiple separator conventions in one
  // call. Source text may use \, /, or mixed paths depending on whether it
  // came from execute_python output, JSON config, or a console log.
  function replacePath(rawPath: string | undefined, replacement: string, rule: string) {
    if (!rawPath || rawPath.length < MIN_WORD_LEN) return;
    const variants = new Set<string>([
      rawPath,
      rawPath.replace(/\\/g, "/"),
      rawPath.replace(/\//g, "\\"),
    ]);
    let total = 0;
    for (const v of variants) {
      const re = new RegExp(escapeRegex(v), "gi");
      text = text.replace(re, () => {
        total++;
        return replacement;
      });
    }
    record(rule, total);
  }

  function replaceWord(word: string | undefined, replacement: string, rule: string) {
    if (!word || word.length < MIN_WORD_LEN) return;
    const re = new RegExp(`\\b${escapeRegex(word)}\\b`, "gi");
    let count = 0;
    text = text.replace(re, () => {
      count++;
      return replacement;
    });
    record(rule, count);
  }

  // ORDER MATTERS: replace longest/most-specific patterns first so the
  // word-boundary passes don't fragment a string the path pass would
  // otherwise have matched cleanly.
  const home = ctx.homeDir ?? os.homedir();
  const user = ctx.username ?? os.userInfo().username;

  replacePath(ctx.projectRoot, "REDACTED_PROJECT_ROOT", "project-root-path");
  replacePath(home,            "REDACTED_HOME",         "home-path");
  replaceWord(ctx.projectName, "REDACTED_PROJECT", "project-name");
  replaceWord(user,            "REDACTED_USER",    "username");

  return { text, hits };
}
