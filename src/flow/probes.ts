/**
 * Probes: named facts about the editor that `when:`, checks and the gates of a
 * flow read as `probe.<name>`, one reading per run.
 *
 * A probe that cannot be read fails the expression that named it. It never
 * reads as false, because a missing fact taken as false turns "the editor is
 * gone" into "nothing is dirty".
 *
 * `EditorFacts` is the seam: the bridge-backed implementation here reads
 * through the run's own context, and a richer facts object can satisfy the
 * same interface later.
 */
import type { ToolContext } from "../core/types.js";
import type { ProbeFacts } from "./condition.js";

/** Named, memoized facts about one editor. */
export interface EditorFacts extends ProbeFacts {
  /** Every name `read` answers. */
  readonly names: readonly string[];
  /** Forget every reading, so the next read asks again. */
  invalidate(): void;
}

/** A probe that could not be read. Its message names the probe. */
export class ProbeError extends Error {
  constructor(public readonly probe: string, message: string) {
    super(`probe.${probe}: ${message}`);
    this.name = "ProbeError";
  }
}

type ProbeReader = (ctx: ToolContext, read: (name: string) => Promise<unknown>) => Promise<unknown>;

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

async function bridgeRead(ctx: ToolContext, method: string): Promise<Record<string, unknown>> {
  if (ctx.bridge.isConnected !== true) throw new Error("no editor is connected");
  return record(await ctx.bridge.call(method, {}), method);
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
    read: async (ctx) => bridgeRead(ctx, "get_world_state"),
  },
  dirty: {
    description: "How many content and map packages are unsaved, from probe.world.",
    read: async (_ctx, read) => {
      const count = (await read("world") as Record<string, unknown>).dirtyPackageCount;
      if (typeof count !== "number") throw new Error("get_world_state carried no dirtyPackageCount");
      return count;
    },
  },
  playing: {
    description: "Whether a PIE or SIE session is running, from probe.world.",
    read: async (_ctx, read) => (await read("world") as Record<string, unknown>).mode !== "editor",
  },
  engine: {
    description: "The connected editor's engine version, from the bridge handshake.",
    read: async (ctx) => {
      if (ctx.bridge.isConnected !== true) throw new Error("no editor is connected");
      const version = ctx.bridge.capabilities?.engineVersion;
      if (!version) throw new Error("the bridge handshake carried no engine version");
      return version;
    },
  },
};

export const PROBE_NAMES = Object.keys(PROBES);

/**
 * Facts read through one context, each at most once until `invalidate`. A
 * flow run invalidates after every step that may change the editor, so reads
 * between two writes share one reading and a read after a write sees it.
 */
export class BridgeFacts implements EditorFacts {
  readonly names = PROBE_NAMES;
  private readings = new Map<string, Promise<unknown>>();

  constructor(private readonly ctx: ToolContext) {}

  read(name: string): Promise<unknown> {
    const declared = Object.hasOwn(PROBES, name) ? PROBES[name] : undefined;
    if (!declared) {
      return Promise.reject(new ProbeError(name, `no such probe. Probes: ${PROBE_NAMES.join(", ")}.`));
    }
    let reading = this.readings.get(name);
    if (!reading) {
      reading = declared.read(this.ctx, (n) => this.read(n)).catch((e: unknown) => {
        if (e instanceof ProbeError) throw e;
        throw new ProbeError(name, e instanceof Error ? e.message : String(e));
      });
      this.readings.set(name, reading);
    }
    return reading;
  }

  invalidate(): void {
    this.readings.clear();
  }
}
