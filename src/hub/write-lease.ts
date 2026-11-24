// @session per-session: Holds the packages dirty in one editor, read through the call function it is handed.
/**
 * A fix loop's per-package write lease: its agents never save a package the
 * person has unsaved changes in. Before each attempt the loop takes the lease
 * (begin), which reads the packages dirty in the editor
 * (editor(list_dirty_packages)); the attempt's agents have changed nothing
 * yet, so those are the person's. Until the attempt ends (end), the session's
 * save calls are checked against that set:
 *
 * - asset(save) of one of those packages is refused;
 * - asset(save_all_dirty) and editor(save_dirty), which save everything
 *   dirty, are refused while any of them is still dirty;
 * - level(save) is refused while the open level's package is one of them;
 * - any other call with save: true that names one of them is refused.
 *
 * A refusal answers { blocked: true, reason }, which the loop reads as a call
 * refused by a guard, so it stops for the person rather than working around
 * it. Interim: ue-mcp knows who dirtied a package and Studio does not, so a
 * package the loop itself left unsaved in an earlier attempt counts as the
 * person's. Package write ownership in ue-mcp would end that.
 */
import type { ActionResult } from "../contract/daemon.js";
import { answerOf } from "./util.js";

/** What the lease wraps: a client's host for one fix loop's calls (Studio's AgentHost is one). Other members pass through. */
export interface CallingHost<O = unknown> {
  callAction(category: string, action: string, args?: Record<string, unknown>, opts?: O): Promise<ActionResult>;
}

type Call = (category: string, action: string, args?: Record<string, unknown>) => Promise<ActionResult>;

/** A package name from an asset or object path: /Game/A/B.B is /Game/A/B. */
export const packageOf = (p: string): string => p.trim().replace(/\.[^/]*$/, "");

const PACKAGE = /^\/[A-Za-z0-9_]+\/\S+/;

/** The package names editor(list_dirty_packages) lists under these keys, as { package } entries. */
function packagesIn(b: Record<string, unknown> | null, keys: string[]): string[] {
  const out: string[] = [];
  for (const k of keys) {
    const v = b?.[k];
    if (!Array.isArray(v)) continue;
    for (const x of v) {
      const s = typeof x === "string" ? x : x && typeof x === "object" ? (x as Record<string, unknown>).package : undefined;
      if (typeof s === "string" && PACKAGE.test(s)) out.push(packageOf(s));
    }
  }
  return out;
}

/** Every package path a call's arguments name, at any depth. */
function namedPackages(v: unknown, out = new Set<string>()): Set<string> {
  if (typeof v === "string") {
    if (PACKAGE.test(v)) out.add(packageOf(v));
  } else if (Array.isArray(v)) {
    for (const x of v) namedPackages(x, out);
  } else if (v && typeof v === "object") {
    for (const x of Object.values(v)) namedPackages(x, out);
  }
  return out;
}

const refused = (reason: string): ActionResult => ({ isError: true, content: [{ type: "text", text: JSON.stringify({ success: false, blocked: true, reason }) }] });

export class WriteLease {
  /** The person's dirty packages, by the session whose loop holds the lease. */
  private readonly leases = new Map<string, Set<string>>();

  constructor(private readonly call: Call, private readonly log?: { warn(message: string, detail?: unknown): void }) {}

  /** The packages dirty in the editor now. Null when the editor does not answer. */
  async dirty(): Promise<string[] | null> {
    try {
      const b = answerOf(await this.call("editor", "list_dirty_packages", {}));
      return b ? packagesIn(b, ["content", "maps"]) : null;
    } catch {
      return null;
    }
  }

  /** Take the lease for an attempt: the packages dirty now are the person's until end(). */
  async begin(sessionId: string): Promise<void> {
    const dirty = await this.dirty();
    if (dirty === null) this.log?.warn("hub: the dirty packages could not be read; the fix loop's saves are not checked this attempt");
    this.leases.set(sessionId, new Set(dirty ?? []));
  }

  end(sessionId: string): void {
    this.leases.delete(sessionId);
  }

  /** The person's packages a session may not save, while it holds the lease. */
  held(sessionId: string): ReadonlySet<string> | undefined {
    return this.leases.get(sessionId);
  }

  /** Why the call would save a package the person has dirty, or null. */
  async refusal(sessionId: string, category: string, action: string, args: Record<string, unknown>): Promise<string | null> {
    const theirs = this.leases.get(sessionId);
    if (!theirs || theirs.size === 0) return null;
    const name = (ps: string[]) => `${ps.slice(0, 3).join(", ")}${ps.length > 3 ? ` and ${ps.length - 3} more` : ""}`;
    const why = (ps: string[]) => `The person has unsaved changes in ${name(ps)}. Saving would save their work with yours; leave those packages unsaved and stop for them.`;
    if ((category === "asset" && action === "save_all_dirty") || (category === "editor" && action === "save_dirty")) {
      const now = await this.dirty();
      const still = now ? now.filter((p) => theirs.has(p)) : [...theirs];
      return still.length ? why(still) : null;
    }
    if (category === "level" && action === "save") {
      const b = answerOf(await this.call("editor", "get_world_state", {}).catch(() => null));
      const level = typeof b?.persistentLevelPackage === "string" ? packageOf(b.persistentLevelPackage) : null;
      // Not knowing which level it saves, the save may be the person's: refuse.
      if (!level) return why([...theirs]);
      return theirs.has(level) ? why([level]) : null;
    }
    const saves = (category === "asset" && action === "save") || args.save === true;
    if (!saves) return null;
    const hit = [...namedPackages(args)].filter((p) => theirs.has(p));
    return hit.length ? why(hit) : null;
  }

  /** The host a session's calls run on: a save the lease forbids is refused before it reaches the editor. */
  wrap<O, H extends CallingHost<O>>(host: H, sessionId: string): H {
    const callAction = async (category: string, action: string, args: Record<string, unknown> = {}, opts?: O): Promise<ActionResult> => {
      const why = this.leases.has(sessionId) ? await this.refusal(sessionId, category, action, args) : null;
      return why ? refused(why) : host.callAction(category, action, args, opts);
    };
    return new Proxy(host, { get: (target, prop, receiver) => (prop === "callAction" ? callAction : Reflect.get(target, prop, receiver)) });
  }
}
