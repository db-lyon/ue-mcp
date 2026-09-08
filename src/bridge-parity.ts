/**
 * Does the plugin we reached actually implement what we advertise? (#1021)
 *
 * `pluginBuildStale` answers a different question. It compares timestamps: is
 * the compiled bridge older than its source. That catches the common cause of
 * a missing handler and is worth reporting, but it is not evidence about the
 * handlers themselves, and a report against 1.2.4 said so exactly: a session
 * reported `pluginBuildStale: false` while an advertised method was absent,
 * and every discovery of that came one failed call at a time.
 *
 * The evidence has been on the wire the whole time. The handshake carries the
 * registered method list read out of the running binary, which the C++ that
 * builds it describes as the only answer a stale DLL cannot fake. Nothing
 * compared it to the surface, so it was used to word an error AFTER a call had
 * already failed and never to say anything beforehand.
 *
 * This compares them. What comes out is not a guess about why a method is
 * missing, and deliberately so: a plugin behind its source, a plugin from
 * another checkout, a partially deployed one and a handler that failed to
 * register all look identical from here, and naming one of them would be
 * inventing a cause. It reports WHICH advertised methods the running plugin
 * does not have, which is the fact, and leaves the diagnosis to the reader.
 */
import type { BridgeCapabilities } from "./bridge.js";
import type { ToolDef } from "./types.js";

export interface BridgeParity {
  /** False when the plugin published no action list, so nothing was compared. */
  checked: boolean;
  /** Distinct bridge methods the advertised surface can dispatch to. */
  advertised: number;
  /** Methods the running plugin registered. */
  registered: number;
  /**
   * Advertised methods the running plugin does not have, sorted. Every one is
   * a call that will come back "Unknown method".
   */
  missing: string[];
  /** One line for a caller to surface, or null when there is nothing to say. */
  message: string | null;
}

/** How many missing methods to name before summarising the rest. */
const NAMED = 8;

/**
 * Compare the advertised surface against what the connected plugin registered.
 *
 * `graph` is the tool graph this server actually dispatches, so a category the
 * user disabled and a plugin's injected actions are counted as they really
 * are rather than from the pristine declaration.
 */
export function checkBridgeParity(
  graph: ToolDef[],
  capabilities: BridgeCapabilities | null | undefined,
): BridgeParity {
  const registered = capabilities?.actions;
  if (!Array.isArray(registered) || registered.length === 0) {
    // An older plugin that predates the action list in the handshake. Silent
    // rather than alarming: nothing was compared, so nothing is known, and
    // saying "0 missing" would be the same false assurance this module exists
    // to remove.
    return { checked: false, advertised: 0, registered: 0, missing: [], message: null };
  }

  const have = new Set(registered);
  const advertised = new Set<string>();
  for (const tool of graph) {
    for (const spec of Object.values(tool.actions)) {
      if (spec.kind === "bridge") advertised.add(spec.bridge);
    }
  }

  const missing = [...advertised].filter((m) => !have.has(m)).sort();
  return {
    checked: true,
    advertised: advertised.size,
    registered: registered.length,
    missing,
    message: missing.length === 0 ? null : describe(missing, advertised.size, registered.length),
  };
}

function describe(missing: string[], advertised: number, registered: number): string {
  const named = missing.slice(0, NAMED).join(", ");
  const rest = missing.length > NAMED ? `, and ${missing.length - NAMED} more` : "";
  return (
    `${missing.length} of the ${advertised} bridge methods this server advertises are not `
    + `registered by the plugin it is connected to (${registered} registered): ${named}${rest}. `
    + "Calling one returns 'Unknown method'. The usual cause is a deployed plugin behind this "
    + "package: redeploy and rebuild it (npm run up:build, or ue-mcp deploy then a build). "
    + "This is read from the running binary's own handler list, so it holds whatever the build "
    + "timestamps say."
  );
}

/**
 * What the binary that answered says about itself, or undefined when nothing
 * answered or it had nothing to add.
 *
 * `project(get_status)` already reports `pluginBuildStale` and
 * `bridgeApiVersion`, and both are read off the SOURCE and the header on disk.
 * `docs/architecture.md` states that distinction and it is the useful one, so
 * the running binary's own account lives here as one field rather than as
 * three more siblings next to them.
 *
 * Undefined on a healthy connected session, so a status payload only grows
 * when there is something to say.
 */
export function deployedPlugin(
  capabilities: BridgeCapabilities | null | undefined,
  parity: BridgeParity,
): { builtAt?: string; missingActions?: number; warning?: string } | undefined {
  if (!capabilities) return undefined;
  const missing = parity.missing.length > 0 ? parity.missing.length : undefined;
  // The build time is the half that catches a handler which is PRESENT and
  // old. #1002 was a DataTable resolution fix that shipped on 2026-08-28 and
  // was reported as broken three days later by a session whose deployed plugin
  // predated it, and nothing in reach said so: parity sees an absent method,
  // not a stale one.
  const builtAt = capabilities.builtAt;
  if (!builtAt && missing === undefined) return undefined;
  return { builtAt, missingActions: missing, warning: parity.message ?? undefined };
}

/**
 * The same comparison the other way: methods the plugin has that nothing
 * advertises.
 *
 * Not a defect and not reported by default. A handler can exist under an
 * alternate spelling, or ship ahead of the TypeScript that will expose it, and
 * `scripts/audit-handlers.mjs` is where that question is asked properly with
 * the alternate-spelling table to hand. Here for a caller that wants the whole
 * picture rather than only the half that breaks calls.
 */
export function unadvertisedMethods(
  graph: ToolDef[],
  capabilities: BridgeCapabilities | null | undefined,
): string[] {
  const registered = capabilities?.actions;
  if (!Array.isArray(registered)) return [];
  const advertised = new Set<string>();
  for (const tool of graph) {
    for (const spec of Object.values(tool.actions)) {
      if (spec.kind === "bridge") advertised.add(spec.bridge);
    }
  }
  return registered.filter((m) => !advertised.has(m)).sort();
}
