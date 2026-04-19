/**
 * Ontology registry.
 *
 * Owns the set of registered projectors and the output directory
 * where projected .kant fragments are written.
 *
 * Kantext sidecar integration is deferred: today we emit fragments
 * and list them. Once the sidecar is wired, the same registry will
 * drive composition and HQL queries.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import type { KantFragment, Projector, ProjectorEvent } from "./types.js";
import { writeFragment } from "./emit.js";

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

export class OntologyRegistry {
  private readonly projectors: RegisteredProjector[] = [];

  constructor(private readonly resolveOutputDir: () => string) {}

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
}

export type { Projector, KantFragment, ProjectorEvent } from "./types.js";
export { createHandlerRegistryProjector } from "./projectors/handler-registry.js";
