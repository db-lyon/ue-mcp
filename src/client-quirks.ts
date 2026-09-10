/**
 * Client-specific rendering limits worth explaining in a tool result.
 *
 * The server does the spec-compliant thing and streams `notifications/progress`
 * during long calls; that is the canonical channel and it works - verified
 * against the reference MCP SDK client, which receives every update. Some
 * clients drop them on the floor, and from the user's seat that is
 * indistinguishable from a hung tool. Rather than invent a side channel, say
 * so once, in the result, and only to the client that actually has the
 * problem.
 */

export interface ClientInfo {
  name: string;
  version?: string;
}

/** "2.1.116" -> [2, 1, 116]; missing or odd parts sort low. */
function parseVersion(version: string | undefined): number[] {
  if (!version) return [0, 0, 0];
  return version
    .split(".")
    .slice(0, 3)
    .map((part) => {
      const n = Number.parseInt(part, 10);
      return Number.isFinite(n) ? n : 0;
    });
}

function atLeast(version: string | undefined, target: number[]): boolean {
  const actual = parseVersion(version);
  for (let i = 0; i < target.length; i++) {
    const a = actual[i] ?? 0;
    const t = target[i] ?? 0;
    if (a > t) return true;
    if (a < t) return false;
  }
  return true;
}

/**
 * Claude Code collapsed MCP tool calls unconditionally between 2.1.116 and
 * 2.1.152, so a call that streamed progress rendered as a motionless line for
 * its whole duration (anthropics/claude-code#51713; 2.1.101 was the last
 * version before it, and 2.1.153 fixed it). Server stderr does not reach the
 * transcript on any version - the client writes it to a log file.
 *
 * The window is closed at BOTH ends deliberately. An open-ended "2.1.116 and
 * up" would keep telling users on current builds that their client cannot draw
 * progress, long after it could - a wrong explanation is worse than none.
 *
 * Returns a one-line explanation, or null when the client renders progress
 * normally. Delete this once no one is plausibly running an affected build.
 */
const CLAUDE_CODE_COLLAPSE_FIRST = [2, 1, 116];
const CLAUDE_CODE_COLLAPSE_FIXED_IN = [2, 1, 153];

export function progressRenderingNote(client: ClientInfo | undefined): string | null {
  if (!client) return null;
  const name = client.name.toLowerCase();
  const isClaudeCode = name === "claude-code" || name.includes("claude code");
  if (!isClaudeCode) return null;
  if (!atLeast(client.version, CLAUDE_CODE_COLLAPSE_FIRST)) return null;
  if (atLeast(client.version, CLAUDE_CODE_COLLAPSE_FIXED_IN)) return null;

  return (
    `Note: progress was streamed throughout this call, but Claude Code ${client.version ?? "2.1.116"} ` +
    "collapses MCP tool calls and does not render notifications/progress " +
    "(anthropics/claude-code#51713, fixed in 2.1.153), so the call looked frozen. " +
    "It was not - it returned as soon as the editor was ready, and the phase " +
    "timeline above is what you would have watched live. Upgrading Claude Code " +
    "restores the live view."
  );
}

/**
 * Clients that render a whole elicitation message, so the dialog does not need
 * handing over on a call of its own first.
 *
 * The relay costs a round trip. It is worth it against a client that draws the
 * opening line or two of an elicitation and collapses the rest behind "(+N
 * more lines)", because there the person is otherwise choosing between buttons
 * for a question they cannot read. Against a client that draws the lot it buys
 * nothing and delays the form.
 *
 * The DEFAULT IS TO RELAY, and an unknown client relays. The two failure modes
 * are not equal: relaying needlessly costs one call, and not relaying against a
 * client that truncates puts a question in front of someone with the question
 * missing. A client earns its way onto this list by being checked, not by being
 * absent from the other one.
 *
 * Names are matched against the `clientInfo.name` a client sends at
 * initialize, lowercased. Each entry needs verifying against the real string
 * that client sends rather than the name it goes by in conversation.
 */
const RENDERS_WHOLE_ELICITATION = [
  "pi-coding-agent",
  // pi-mcp-adapter, which names its client pi-mcp-<server> rather than after
  // pi itself. It passes the elicitation message through whole into pi's TUI,
  // so the handover buys nothing and only delays the form.
  "pi-mcp-",
];

/**
 * Whether the elicitation form has to be preceded by handing the dialog over.
 *
 * `UE_MCP_DIALOG_RELAY=off` turns it off whatever the client is, so a client
 * whose rendering is fine does not have to wait for a release to say so, and
 * `=on` forces it back on. Anything else, including unset, leaves the decision
 * to the client.
 */
export function elicitationNeedsRelay(client: ClientInfo | undefined): boolean {
  const override = (process.env.UE_MCP_DIALOG_RELAY ?? "").trim().toLowerCase();
  if (override === "off" || override === "0" || override === "false") return false;
  if (override === "on" || override === "1" || override === "true") return true;
  if (!client) return true;
  const name = client.name.toLowerCase();
  return !RENDERS_WHOLE_ELICITATION.some((known) => name.includes(known));
}
