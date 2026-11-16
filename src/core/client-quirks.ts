// @session independent: About the connected MCP client, not about any editor.
/**
 * Client-specific rendering limits worth explaining in a tool result, declared
 * once in CLIENT_QUIRKS.
 *
 * The server does the spec-compliant thing (it streams `notifications/progress`
 * and sends elicitation forms) and verifies it against the reference MCP SDK
 * client. Some clients drop part of that on the floor, and from the user's seat
 * that is indistinguishable from a hung tool or an unanswerable question.
 * Rather than invent a side channel, the server says so once, in the result,
 * and only to the client that actually has the problem.
 *
 * Every entry names the upstream report it tracks and when it can be deleted.
 * A version window is closed at the fixed end deliberately: telling a user on a
 * current build that their client cannot do something it now does is a wrong
 * explanation, which is worse than none.
 */
import { readEnv } from "./env.js";
import { parseVersion, satisfiesMinimum } from "./semver.js";

export interface ClientInfo {
  name: string;
  version?: string;
}

/** What a quirk changes about how the server treats the client. */
export type ClientQuirkEffect =
  /** Progress is streamed but not drawn, so the result explains the frozen-looking call. */
  | "progress-not-rendered"
  /** Elicitation messages are drawn whole, so the dialog needs no handover call first. */
  | "renders-whole-elicitation"
  /** Elicitation is advertised but forms are never shown, so dialogs are handled as defer. */
  | "elicitation-unrendered";

export interface ClientQuirk {
  effect: ClientQuirkEffect;
  /**
   * How the `clientInfo.name` a client sends at initialize is matched,
   * lowercased: equal to one of `exact`, or containing one of `contains`.
   * Each needs checking against the string the client really sends, not the
   * name it goes by in conversation.
   */
  match: { exact?: readonly string[]; contains?: readonly string[] };
  /**
   * The affected versions: from `from` (inclusive) up to `fixedIn`
   * (exclusive), either end open when absent. `unknownAffected` decides a
   * missing or unparseable version: true where acting on the quirk is the safe
   * miss, false where it would mislead.
   */
  versions?: { from?: string; fixedIn?: string; unknownAffected: boolean };
  /** The upstream report this follows. */
  tracking: string;
  /** When the entry can be deleted. */
  removeWhen: string;
  /** The explanation shown to an affected client, when the effect has one. */
  note?: (client: ClientInfo) => string;
}

export const CLIENT_QUIRKS: readonly ClientQuirk[] = [
  {
    // Claude Code collapsed MCP tool calls unconditionally, so a call that
    // streamed progress rendered as a motionless line for its whole duration.
    // 2.1.101 was the last version before it. Server stderr does not reach the
    // transcript on any version: the client writes it to a log file.
    effect: "progress-not-rendered",
    match: { exact: ["claude-code"], contains: ["claude code"] },
    versions: { from: "2.1.116", fixedIn: "2.1.153", unknownAffected: false },
    tracking: "anthropics/claude-code#51713",
    removeWhen: "No one is plausibly running a Claude Code build from 2.1.116 to 2.1.152.",
    note: (client) =>
      `Note: progress was streamed throughout this call, but Claude Code ${client.version ?? "2.1.116"} ` +
      "collapses MCP tool calls and does not render notifications/progress " +
      "(anthropics/claude-code#51713, fixed in 2.1.153), so the call looked frozen. " +
      "It was not - it returned as soon as the editor was ready, and the phase " +
      "timeline above is what you would have watched live. Upgrading Claude Code " +
      "restores the live view.",
  },
  {
    // pi's coding agent, and pi-mcp-adapter, which names its client
    // pi-mcp-<server> rather than after pi itself. Both pass the elicitation
    // message through whole into pi's TUI.
    effect: "renders-whole-elicitation",
    match: { contains: ["pi-coding-agent", "pi-mcp-"] },
    tracking: "Checked by hand against pi-coding-agent and pi-mcp-adapter; no upstream issue, this is correct behaviour.",
    removeWhen: "Either client starts truncating elicitation messages, or the relay is removed.",
  },
  {
    // opencode advertised the elicitation capability from 2.0.18 but dropped
    // every form it was sent, so the call waited on a question nobody could
    // see. It sends "opencode" at initialize from 2.0.26
    // (anomalyco/opencode#53471); earlier 2.0 builds sent "cli", "desktop" or
    // "acp", too generic to match. An unparseable version counts as affected,
    // because defer is the safe miss.
    effect: "elicitation-unrendered",
    match: { exact: ["opencode"] },
    versions: { fixedIn: "2.1.4", unknownAffected: true },
    tracking: "anomalyco/opencode#58184",
    removeWhen: "No one is plausibly running an opencode build before 2.1.4.",
    note: (client) =>
      `opencode ${client.version ?? "(unknown version)"} advertises MCP elicitation but does not show the form ` +
      "(anomalyco/opencode#58184, fixed in 2.1.4), so dialogs are handled as defer. " +
      "Upgrade opencode to 2.1.4 or later to answer them in a form.",
  },
];

function matches(quirk: ClientQuirk, client: ClientInfo): boolean {
  const name = client.name.toLowerCase();
  return !!quirk.match.exact?.includes(name) || !!quirk.match.contains?.some((part) => name.includes(part));
}

function inWindow(quirk: ClientQuirk, client: ClientInfo): boolean {
  const window = quirk.versions;
  if (!window) return true;
  const version = client.version;
  if (!version || parseVersion(version) === null) return window.unknownAffected;
  if (window.from && !satisfiesMinimum(version, window.from)) return false;
  if (window.fixedIn && satisfiesMinimum(version, window.fixedIn)) return false;
  return true;
}

/** The quirk with this effect that applies to the client, if any. */
export function clientQuirk(effect: ClientQuirkEffect, client: ClientInfo | undefined): ClientQuirk | null {
  if (!client) return null;
  return CLIENT_QUIRKS.find((q) => q.effect === effect && matches(q, client) && inWindow(q, client)) ?? null;
}

function noteFor(effect: ClientQuirkEffect, client: ClientInfo | undefined): string | null {
  const quirk = clientQuirk(effect, client);
  return quirk?.note && client ? quirk.note(client) : null;
}

/** Why the call looked frozen on this client, or null when it renders progress. */
export function progressRenderingNote(client: ClientInfo | undefined): string | null {
  return noteFor("progress-not-rendered", client);
}

/**
 * Whether the elicitation form has to be preceded by handing the dialog over.
 *
 * The relay costs a round trip, worth it against a client that draws the
 * opening line or two of an elicitation and collapses the rest, because there
 * the person is otherwise choosing between buttons for a question they cannot
 * read. The default is to relay, and an unknown client relays: relaying
 * needlessly costs one call, not relaying against a client that truncates puts
 * a question in front of someone with the question missing. A client earns
 * "renders-whole-elicitation" by being checked.
 *
 * `UE_MCP_DIALOG_RELAY=off` turns it off whatever the client is, so a client
 * whose rendering is fine does not have to wait for a release to say so, and
 * `=on` forces it back on. Anything else, including unset, leaves the decision
 * to the client.
 */
export function elicitationNeedsRelay(client: ClientInfo | undefined): boolean {
  const override = (readEnv("dialogRelay") ?? "").trim().toLowerCase();
  if (override === "off" || override === "0" || override === "false") return false;
  if (override === "on" || override === "1" || override === "true") return true;
  return clientQuirk("renders-whole-elicitation", client) === null;
}

/** Why this client cannot be shown a form despite advertising it, or null when its forms render. */
export function elicitationUnrenderedNote(client: ClientInfo | undefined): string | null {
  return noteFor("elicitation-unrendered", client);
}
