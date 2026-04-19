/**
 * Ontology registry.
 *
 * Owns projector registrations, projected-layer emission, kernel +
 * repo-local layer discovery, composition into a single view, and
 * selector queries. Everything except projection is an in-process
 * TS walker over a strict subset of kantext's .kant shape. When the
 * kantext binary is available we swap these in-process walkers for
 * calls into it; the emitted .kant files and the projector surface
 * do not change.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import type { KantFragment, Projector, ProjectorEvent } from "./types.js";
import { writeFragment } from "./emit.js";
import { parseKantFile, type ParsedFragment } from "./parse.js";
import { compose, type ComposedView, type Layer } from "./compose.js";
import { select, type MatchResult } from "./select.js";

export interface ProjectionResult {
  readonly projector: string;
  readonly basePath: string;
  readonly outputPath: string;
  readonly pointCount: number;
  readonly producedAt: string;
}

interface RegisteredProjector {
  readonly projector: Projector<unknown>;
  readonly input: () => unknown;
}

function countPoints(fragment: KantFragment): number {
  let n = 0;
  function walk(point: { children?: Record<string, unknown> }): void {
    n += 1;
    if (point.children) {
      for (const child of Object.values(point.children)) {
        walk(child as { children?: Record<string, unknown> });
      }
    }
  }
  for (const point of Object.values(fragment.points)) {
    walk(point);
  }
  return n;
}

export interface LayerSource {
  readonly priority: number;
  readonly paths: readonly string[];
}

export interface LayerSources {
  readonly kernel: LayerSource;
  readonly projected: LayerSource;
  readonly repoLocal: LayerSource;
}

export class OntologyRegistry {
  private readonly projectors: RegisteredProjector[] = [];

  constructor(
    private readonly resolveOutputDir: () => string,
    private readonly resolveLayerSources: () => LayerSources,
  ) {}

  register<T>(projector: Projector<T>, input: () => T): void {
    this.projectors.push({
      projector: projector as Projector<unknown>,
      input: input as () => unknown,
    });
  }

  get projectorCount(): number {
    return this.projectors.length;
  }

  listProjectors(): ReadonlyArray<{ name: string; basePath: string; triggerEvents: readonly ProjectorEvent[] }> {
    return this.projectors.map((r) => ({
      name: r.projector.name,
      basePath: r.projector.basePath,
      triggerEvents: r.projector.triggerEvents,
    }));
  }

  projectAll(): ProjectionResult[] {
    const outputDir = this.resolveOutputDir();
    const results: ProjectionResult[] = [];
    for (const { projector, input } of this.projectors) {
      const fragment = projector.project(input());
      const outputPath = writeFragment(fragment, outputDir);
      results.push({
        projector: projector.name,
        basePath: fragment.basePath,
        outputPath,
        pointCount: countPoints(fragment),
        producedAt: fragment.producedAt,
      });
    }
    return results;
  }

  projectByEvent(event: ProjectorEvent): ProjectionResult[] {
    const outputDir = this.resolveOutputDir();
    const results: ProjectionResult[] = [];
    for (const { projector, input } of this.projectors) {
      if (!projector.triggerEvents.includes(event)) continue;
      const fragment = projector.project(input());
      const outputPath = writeFragment(fragment, outputDir);
      results.push({
        projector: projector.name,
        basePath: fragment.basePath,
        outputPath,
        pointCount: countPoints(fragment),
        producedAt: fragment.producedAt,
      });
    }
    return results;
  }

  listLayers(): Array<{ file: string; size: number; mtime: string }> {
    const outputDir = this.resolveOutputDir();
    if (!fs.existsSync(outputDir)) return [];
    return fs
      .readdirSync(outputDir, { withFileTypes: true })
      .filter((d) => d.isFile() && d.name.endsWith(".kant"))
      .map((d) => {
        const full = path.join(outputDir, d.name);
        const stat = fs.statSync(full);
        return { file: d.name, size: stat.size, mtime: stat.mtime.toISOString() };
      })
      .sort((a, b) => a.file.localeCompare(b.file));
  }

  get outputDirectory(): string {
    return this.resolveOutputDir();
  }

  private loadLayers(): Layer[] {
    const sources = this.resolveLayerSources();
    const layers: Layer[] = [];
    for (const group of [sources.kernel, sources.projected, sources.repoLocal]) {
      for (const p of group.paths) {
        if (!fs.existsSync(p)) continue;
        const stat = fs.statSync(p);
        if (stat.isDirectory()) {
          const files = fs.readdirSync(p).filter((f) => f.endsWith(".kant") && f !== "stack.kant");
          for (const f of files) {
            layers.push({
              priority: group.priority,
              fragment: parseKantFile(path.join(p, f)),
            });
          }
        } else if (p.endsWith(".kant")) {
          layers.push({ priority: group.priority, fragment: parseKantFile(p) });
        }
      }
    }
    return layers;
  }

  composeView(): ComposedView & { layerCount: number } {
    const layers = this.loadLayers();
    const view = compose(layers);
    return { ...view, layerCount: layers.length };
  }

  query(selector: string): { selector: string; matches: MatchResult[] } {
    const view = this.composeView();
    const matches = select(view.root, selector);
    return { selector, matches };
  }

  /**
   * Check whether an action's declared `requires` are satisfied by the
   * composed Plugin catalog. Returns a structured result the caller
   * can use to short-circuit dispatch when prerequisites are missing.
   */
  checkRequires(tool: string, actionName: string): {
    tool: string;
    actionName: string;
    declared: readonly string[];
    missing: readonly string[];
    disabled: readonly string[];
    ok: boolean;
  } {
    const actionHits = this.query(
      `/UE/Mediation/Registry/Tools/${tool}/Actions/${actionName}`,
    ).matches;
    if (actionHits.length === 0) {
      return { tool, actionName, declared: [], missing: [], disabled: [], ok: true };
    }
    const requires = Object.keys(actionHits[0].point.children?.requires?.children ?? {});
    if (requires.length === 0) {
      return { tool, actionName, declared: [], missing: [], disabled: [], ok: true };
    }
    const missing: string[] = [];
    const disabled: string[] = [];
    for (const dep of requires) {
      const hits = this.query(`/UE/Plugins/Catalog/${dep}`).matches;
      if (hits.length === 0) {
        missing.push(dep);
        continue;
      }
      const enabledField = hits[0].point.fields?.enabled;
      if (typeof enabledField === "object" && enabledField && "value" in enabledField) {
        if ((enabledField as { value: number }).value !== 1) disabled.push(dep);
      }
    }
    return {
      tool,
      actionName,
      declared: requires,
      missing,
      disabled,
      ok: missing.length === 0 && disabled.length === 0,
    };
  }
}

export type { Projector, KantFragment, ProjectorEvent } from "./types.js";
export { createHandlerRegistryProjector } from "./projectors/handler-registry.js";
export { createWorkaroundProjector } from "./projectors/workarounds.js";
export { createPluginProjector, type PluginProjectorInput } from "./projectors/plugins.js";
export {
  createEngineSymbolProjector,
  type EngineSymbolProjectorInput,
  type EngineTree,
} from "./projectors/engine-symbols.js";
export { createInvocationProjector } from "./projectors/invocations.js";
export { parseKant, parseKantFile, type ParsedFragment } from "./parse.js";
export { compose, type ComposedView, type Layer } from "./compose.js";
export { select, parseSelector, type MatchResult } from "./select.js";
