/**
 * ue-mcp ontology adapter.
 *
 * Thin wrapper around cairn's generic Registry that adds the
 * UE-specific semantics ue-mcp relies on in the dispatch layer:
 *   - checkRequires: preflight plugin-availability gate
 *   - resolveApproval: declared approval marker for auto-directive
 *
 * The core engine (parse/compose/select/emit/types/Registry) lives
 * in @db-lyon/cairn. Everything in this file is UE-specific.
 */

import { Registry, type LayerSources } from "@db-lyon/cairn";

export class OntologyRegistry extends Registry {
  constructor(
    resolveOutputDir: () => string,
    resolveSources: () => LayerSources,
  ) {
    super(
      resolveOutputDir,
      resolveSources,
      { rootAnchor: "UE", extension: ".cairn", ignoreFiles: ["stack.cairn"] },
    );
  }

  /**
   * Resolve the declared approval marker for an action, or undefined
   * if the action is unknown or has no approval field.
   */
  resolveApproval(tool: string, actionName: string): string | undefined {
    const hits = this.query(
      `/UE/Mediation/Registry/Tools/${tool}/Actions/${actionName}`,
    ).matches;
    if (hits.length === 0) return undefined;
    const field = hits[0].point.fields?.approval;
    if (typeof field === "object" && field && "marker" in field) {
      return (field as { marker?: string }).marker;
    }
    return undefined;
  }

  /**
   * Check whether an action's declared `requires` are satisfied by the
   * composed /UE/Plugins/Catalog view.
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

// Re-export cairn primitives that ue-mcp code and tests need.
export type {
  Point,
  Signal,
  Scalar,
  EmittedFragment,
  Projector,
  ProjectorEvent,
  Fragment,
  Layer,
  ComposedView,
  MatchResult,
} from "@db-lyon/cairn";
export { parse, parseFile, parseSelector, serializeFragment, write } from "@db-lyon/cairn";

// ue-mcp always composes against the /UE address space. Wrap cairn's
// compose/select with the UE rootAnchor as the default so callers
// do not have to pass it every time.
import {
  compose as cairnCompose,
  select as cairnSelect,
  type ComposeOptions,
  type SelectOptions,
  type Layer as CairnLayer,
  type Point as CairnPoint,
  type ComposedView as CairnComposedView,
  type MatchResult as CairnMatchResult,
} from "@db-lyon/cairn";

export function compose(layers: readonly CairnLayer[], options?: ComposeOptions): CairnComposedView {
  return cairnCompose(layers, { rootAnchor: "UE", ...options });
}
export function select(root: CairnPoint, selector: string, options?: SelectOptions): CairnMatchResult[] {
  return cairnSelect(root, selector, { rootAnchor: "UE", ...options });
}

// Projector factories (ue-mcp specific).
export { createHandlerRegistryProjector } from "./projectors/handler-registry.js";
export { createWorkaroundProjector } from "./projectors/workarounds.js";
export { createPluginProjector, type PluginProjectorInput } from "./projectors/plugins.js";
export {
  createEngineSymbolProjector,
  type EngineSymbolProjectorInput,
  type EngineTree,
} from "./projectors/engine-symbols.js";
export { createInvocationProjector } from "./projectors/invocations.js";
export {
  createProjectConfigProjector,
  type ProjectConfigProjectorInput,
} from "./projectors/project-config.js";

export { Registry, type LayerSources, type LayerSource, type ProjectionResult, type RegistryOptions } from "@db-lyon/cairn";
