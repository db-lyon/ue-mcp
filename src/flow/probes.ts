/**
 * Probes: named facts about the editor that `when:`, checks and the gates of a
 * flow read as `probe.<name>`.
 *
 * They are a view over the editor's EditorFacts, the one facts source: a probe
 * reading and an `editor.*` reading share one cache, dropped on the same
 * events. A run with no editor session reads through facts of its own.
 *
 * A probe that cannot be read fails the expression that named it. It never
 * reads as false, because a missing fact taken as false turns "the editor is
 * gone" into "nothing is dirty".
 */
import type { ToolContext } from "../core/types.js";
import { EditorFacts, type FactName } from "../sessions/editor-facts.js";
import type { ProbeFacts } from "./condition.js";

/** A probe that could not be read. Its message names the probe. */
export class ProbeError extends Error {
  constructor(public readonly probe: string, message: string) {
    super(`probe.${probe}: ${message}`);
    this.name = "ProbeError";
  }
}

type ProbeReader = (ctx: ToolContext, fact: (name: FactName) => Promise<unknown>, read: (name: string) => Promise<unknown>) => Promise<unknown>;

export interface ProbeDeclaration {
  description: string;
  read: ProbeReader;
}

function record(value: unknown, what: string): Record<string, unknown> {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(`${what} answered ${JSON.stringify(value)}, not an object`);
  }
  const body = value as Record<string, unknown>;
  if (body.success === false) {
    throw new Error(String(body.error ?? body.message ?? `${what} answered success: false`));
  }
  return body;
}

function requireConnected(ctx: ToolContext): void {
  if (ctx.bridge.isConnected !== true) throw new Error("no editor is connected");
}

/** The shipped probes. Each is a read; none changes the editor. */
export const PROBES: Readonly<Record<string, ProbeDeclaration>> = {
  connected: {
    description: "Whether this run's editor bridge is connected. Never fails.",
    read: async (ctx) => ctx.bridge.isConnected === true,
  },
  world: {
    description: "editor(get_world_state): editorWorldName, persistentLevelPackage, mode (editor, play, simulate), "
      + "playInEditor, dirtyPackages and dirtyPackageCount.",
    read: async (ctx, fact) => {
      requireConnected(ctx);
      return record(await fact("world"), "get_world_state");
    },
  },
  dirty: {
    description: "How many content and map packages are unsaved, from probe.world.",
    read: async (_ctx, _fact, read) => {
      const count = (await read("world") as Record<string, unknown>).dirtyPackageCount;
      if (typeof count !== "number") throw new Error("get_world_state carried no dirtyPackageCount");
      return count;
    },
  },
  playing: {
    description: "Whether a PIE or SIE session is running, from probe.world.",
    read: async (_ctx, _fact, read) => (await read("world") as Record<string, unknown>).mode !== "editor",
  },
  engine: {
    description: "The connected editor's engine version, from the bridge handshake.",
    read: async (ctx, fact) => {
      requireConnected(ctx);
      const version = await fact("engineVersion");
      if (!version) throw new Error("the bridge handshake carried no engine version");
      return version;
    },
  },
};

export const PROBE_NAMES = Object.keys(PROBES);

/** The editor's own facts, or facts of the run's own when it has no editor session. */
export function factsOf(ctx: ToolContext): EditorFacts {
  return ctx.session?.facts ?? new EditorFacts({
    bridge: ctx.bridge,
    call: (method, params) => ctx.bridge.call(method, params ?? {}),
    project: ctx.project,
  });
}

/** The facts a fetch reads from the editor, which a change can move. */
const FETCHED: readonly FactName[] = ["pie", "map", "dirtyPackages", "world"];

/**
 * Probes read through one context's EditorFacts. The facts cache each reading
 * and drop it on the events that move it; a flow run also calls `invalidate`
 * after every step that may change the editor, so a check after a step whose
 * calls the facts did not see still reads anew.
 */
export class BridgeFacts implements ProbeFacts {
  readonly names = PROBE_NAMES;
  private readonly facts: EditorFacts;

  constructor(private readonly ctx: ToolContext) {
    this.facts = factsOf(ctx);
  }

  read(name: string): Promise<unknown> {
    const declared = Object.hasOwn(PROBES, name) ? PROBES[name] : undefined;
    if (!declared) {
      return Promise.reject(new ProbeError(name, `no such probe. Probes: ${PROBE_NAMES.join(", ")}.`));
    }
    return declared.read(this.ctx, (fact) => this.facts.get(fact), (n) => this.read(n)).catch((e: unknown) => {
      if (e instanceof ProbeError) throw e;
      throw new ProbeError(name, e instanceof Error ? e.message : String(e));
    });
  }

  invalidate(): void {
    this.facts.invalidate(...FETCHED);
  }
}
