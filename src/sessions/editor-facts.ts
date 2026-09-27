/**
 * Facts about one editor, fetched when first asked for and kept until an
 * event says they moved: a reconnect (new capabilities) drops everything, a
 * PIE start or stop drops the PIE state, loading a level drops the map, and a
 * save or any other change drops the dirty package list.
 *
 * Gates and flow conditions read them through the `editor` namespace, so a
 * `when:` can say `editor.pie` or `editor.engineVersion`.
 */
import type { IBridge } from "../bridge/bridge.js";
import type { ProjectContext } from "../config/project.js";
import type { ToolDef } from "../core/types.js";
import { UProjectSchema } from "../surface/schemas.js";
import { bridgeMethodEffect } from "../surface/action-effects.js";
import * as fs from "node:fs";

export const FACT_NAMES = [
  "engineVersion",
  "enabledPlugins",
  "bridgeVersion",
  "handlers",
  "pie",
  "map",
  "dirtyPackages",
] as const;

export type FactName = (typeof FACT_NAMES)[number];

/** Facts answered from the handshake or the .uproject, with no call of their own. */
const LOCAL_FACTS: ReadonlySet<FactName> = new Set(["engineVersion", "enabledPlugins", "bridgeVersion", "handlers"]);

/** What a fact read needs from the editor it describes. */
export interface EditorFactsSource {
  /** The bridge the facts describe; its capabilities identify one connection. */
  readonly bridge: Pick<IBridge, "isConnected" | "capabilities">;
  /** How a fetch reaches the editor. Supplied, so it goes through the dialog gate. */
  call(method: string, params?: Record<string, unknown>): Promise<unknown>;
  readonly project: ProjectContext;
  /** The graph that declares each method's effect. */
  readonly toolGraph?: readonly ToolDef[];
}

/** Which facts a call invalidates, by its method. */
function invalidatedBy(method: string, params: Record<string, unknown> | undefined, graph?: readonly ToolDef[]): FactName[] {
  if (method === "pie_control" || method === "pie_start_ignoring_blueprint_errors") return ["pie", "dirtyPackages"];
  if (method === "load_level" || method === "create_new_level") return ["map", "dirtyPackages"];
  if (method.startsWith("save_")) return ["dirtyPackages"];
  if (bridgeMethodEffect(method, params, graph).effect === "read") return [];
  // Any other change can dirty a package.
  return ["dirtyPackages"];
}

function asRecord(value: unknown): Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value) ? (value as Record<string, unknown>) : {};
}

export class EditorFacts {
  private readonly values = new Map<FactName, unknown>();
  private readonly pending = new Map<FactName, Promise<unknown>>();
  private connection: unknown;

  constructor(private readonly source: EditorFactsSource) {}

  /** Drop the named facts, or every fact when none is named. */
  invalidate(...names: FactName[]): void {
    const drop = names.length > 0 ? names : [...FACT_NAMES];
    for (const name of drop) {
      this.values.delete(name);
      this.pending.delete(name);
    }
  }

  /** Called with every reply the editor gives, so a known event drops what it moved. */
  observe(method: string, params?: Record<string, unknown>): void {
    const moved = invalidatedBy(method, params, this.source.toolGraph);
    if (moved.length > 0) this.invalidate(...moved);
  }

  /** A reconnect is a new editor as far as every fact is concerned. */
  private checkConnection(): void {
    const current = this.source.bridge.capabilities ?? null;
    if (current !== this.connection) {
      this.connection = current;
      this.invalidate();
    }
  }

  /** The value already known, without fetching. Undefined when it would need a call. */
  peek(name: FactName): unknown {
    this.checkConnection();
    if (this.values.has(name)) return this.values.get(name);
    if (!LOCAL_FACTS.has(name)) return undefined;
    const value = this.readLocal(name);
    this.values.set(name, value);
    return value;
  }

  /** The fact, fetched once and kept until invalidated. A failed fetch throws and is not kept. */
  async get(name: FactName): Promise<unknown> {
    this.checkConnection();
    if (this.values.has(name) || LOCAL_FACTS.has(name)) return this.peek(name);
    if (!this.source.bridge.isConnected) return undefined;
    const inFlight = this.pending.get(name);
    if (inFlight) return inFlight;
    const fetch = this.fetch(name).then((value) => {
      if (this.pending.get(name) === fetch) {
        this.pending.delete(name);
        this.values.set(name, value);
      }
      return value;
    }, (e) => {
      if (this.pending.get(name) === fetch) this.pending.delete(name);
      throw e;
    });
    this.pending.set(name, fetch);
    return fetch;
  }

  /** Several facts at once, every one by default. */
  async snapshot(names: readonly FactName[] = FACT_NAMES): Promise<Record<string, unknown>> {
    const out: Record<string, unknown> = {};
    for (const name of names) out[name] = await this.get(name);
    return out;
  }

  private readLocal(name: FactName): unknown {
    const caps = this.source.bridge.capabilities;
    if (name === "engineVersion") return caps?.engineVersion;
    if (name === "bridgeVersion") {
      return caps ? { protocol: caps.protocolVersion, handlerApi: caps.handlerApiVersion, builtAt: caps.builtAt } : undefined;
    }
    if (name === "handlers") return caps?.actions ? [...caps.actions] : undefined;
    return this.enabledPlugins();
  }

  private enabledPlugins(): string[] | undefined {
    const file = this.source.project.projectPath;
    if (!file) return undefined;
    try {
      const parsed = UProjectSchema.safeParse(JSON.parse(fs.readFileSync(file, "utf-8")));
      if (!parsed.success) return undefined;
      return (parsed.data.Plugins ?? [])
        .filter((p) => p.Enabled !== false)
        .map((p) => p.Name)
        .filter((n): n is string => typeof n === "string");
    } catch {
      return undefined;
    }
  }

  private async fetch(name: FactName): Promise<unknown> {
    if (name === "pie") {
      const reply = asRecord(await this.source.call("list_pie_instances", {}));
      return typeof reply.count === "number" ? reply.count > 0 : Array.isArray(reply.instances) && reply.instances.length > 0;
    }
    if (name === "map") {
      const reply = asRecord(await this.source.call("get_current_level", {}));
      return typeof reply.levelPath === "string" ? reply.levelPath : undefined;
    }
    const reply = asRecord(await this.source.call("list_dirty_packages", {}));
    const packages = [...(Array.isArray(reply.content) ? reply.content : []), ...(Array.isArray(reply.maps) ? reply.maps : [])];
    return packages.map((entry) => asRecord(entry).package).filter((p): p is string => typeof p === "string");
  }
}
