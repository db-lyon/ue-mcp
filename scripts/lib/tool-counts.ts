/**
 * The surface counts and the `<!-- count:* -->` markers they are stamped into.
 *
 * Read by the docs generator (scripts/generate-tool-metadata.ts), the package
 * stamp (scripts/stamp-package.ts), the overview image renderer and the unit
 * tests that hold the stamped copies to the surface.
 */
import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { ALL_TOOLS, enumerateBridgeActions } from "../../src/tools.js";
import { loadMergedCatalog } from "./epic-catalog.mjs";
import { SUPPORTED_ENGINE_RANGE, compareEngines, engineRangeText } from "./engine-range.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/** Every wrapped Epic tool is spelled `epic_<name>`; nothing else is. */
const EPIC_ACTION_PREFIX = "epic_";

export interface Counts {
  tools: number;
  /** Every action the server advertises, ue-mcp's own AND the wrapped Epic
   *  tools. This is a TOTAL, not an addend: since commit 023d86d4 the `epic_*`
   *  actions are declared in `ALL_TOOLS` like any other, so quoting this
   *  number next to `nativeToolActions` with an "also" or a "plus" claims a
   *  surface larger, by every wrapped Epic tool, than the one that ships. */
  actions: number;
  /** ue-mcp's own actions: `actions` minus the wrapped Epic tools. This is the
   *  number to quote when the copy names Epic's tools separately. */
  ownActions: number;
  /** Distinct bridge methods the actions dispatch to: the handlers a smoke run calls. */
  bridgeActions: number;
  /** Actions the server answers itself, without the bridge. */
  localActions: number;
  perTool: Record<string, number>;
  /** Official Unreal ToolsetRegistry toolsets ue-mcp wraps, across every recorded engine. */
  nativeToolsets: number;
  /** Official Epic tools wrapped across those toolsets, counted inside
   *  `actions`. */
  nativeToolActions: number;
  /** The newest engine with a recorded Epic catalog, and the number of Epic
   *  tools that one engine's registry offers. Public copy quotes
   *  `nativeToolActions`, every Epic tool ue-mcp wraps across engines; these
   *  are for a reader that wants the single-engine figure. */
  nativeEngine: string;
  nativeEngineToolActions: number;
  /** The supported engine range, oldest to newest, as declared in
   *  scripts/lib/engine-range.mjs. */
  engineRange: { min: string; max: string };
  generatedAt: string;
  version: string;
}

/**
 * Count the wrapped Epic tools.
 *
 * The tool total comes from `ALL_TOOLS`, the same graph the server advertises,
 * so it cannot disagree with what a client is handed. The toolset total comes
 * from the recorded catalogs, merged across engine versions, which are the only
 * place that grouping is written down, and the two are cross-checked.
 *
 * A missing or disagreeing catalog throws. This once read a moved file inside a
 * `try` that returned zeros, and the landing site rendered "all 0 of Epic's
 * native 5.8 tools wrapped in-process" for every release after that.
 */
function computeNativeCounts(): { nativeToolsets: number; nativeToolActions: number } {
  const epicSpecs = ALL_TOOLS.flatMap((t) => Object.entries(t.actions).filter(([a]) => a.startsWith(EPIC_ACTION_PREFIX)));
  const declared = epicSpecs.length;
  // Engine tools reached: an action whose tool a newer engine renamed reaches
  // that successor too, under the same action name, so the catalog is checked
  // against actions plus their successors.
  const reached = epicSpecs.reduce(
    (n, [, spec]) => n + 1 + (spec.kind === "bridge" ? spec.epicTool?.successors?.length ?? 0 : 0),
    0,
  );
  const toolsets = (loadMergedCatalog() as { toolsets: Array<{ tools?: unknown[] }> }).toolsets;
  const recorded = toolsets.reduce((n, t) => n + (t.tools?.length ?? 0), 0);
  if (recorded !== reached || declared === 0) {
    throw new Error(
      `Wrapped Epic tool count disagrees: ALL_TOOLS reaches ${reached} engine tools through ${declared} ` +
        `${EPIC_ACTION_PREFIX}* actions, tests/golden/epic-catalog/ records ${recorded} across its engines. ` +
        `Run \`npm run generate\`, which regenerates the Epic actions before the metadata.`,
    );
  }
  return { nativeToolsets: toolsets.length, nativeToolActions: declared };
}

/**
 * The newest engine's recorded Epic catalog. Reads either layout: one file per
 * engine under `tests/golden/epic-catalog/<major.minor>.json`, or the single
 * `tests/golden/epic-catalog.json`, which names its engine in
 * `engineAssociation`.
 */
export function newestEpicCatalog(): { engine: string; toolActions: number } {
  const golden = path.join(repo, "tests", "golden");
  const dir = path.join(golden, "epic-catalog");
  const files = fs.existsSync(dir)
    ? fs.readdirSync(dir).filter((f) => /^\d+\.\d+\.json$/.test(f)).map((f) => path.join(dir, f))
    : [path.join(golden, "epic-catalog.json")];
  const catalogs = files.map((f) => {
    const c = JSON.parse(fs.readFileSync(f, "utf8")) as {
      engineAssociation?: string;
      toolsets?: Array<{ tools?: unknown[] }>;
    };
    const engine = /^(\d+)\.(\d+)/.exec(String(c.engineAssociation ?? ""));
    if (!engine) throw new Error(`${path.relative(repo, f)} does not name its engine in engineAssociation.`);
    return {
      engine: `${Number(engine[1])}.${Number(engine[2])}`,
      toolActions: (c.toolsets ?? []).reduce((n, t) => n + (t.tools?.length ?? 0), 0),
    };
  });
  const newest = catalogs.sort((a, b) => compareEngines(a.engine, b.engine)).at(-1);
  if (!newest || newest.toolActions === 0) throw new Error("No recorded Epic catalog with any tools in tests/golden.");
  return newest;
}

export function computeCounts(): Counts {
  const perTool: Record<string, number> = {};
  let total = 0;
  for (const t of ALL_TOOLS) {
    const n = Object.keys(t.actions).length;
    perTool[t.name] = n;
    total += n;
  }
  // Actions that reach the bridge, each counted once: every wrapped Epic tool
  // dispatches to the same epic_call_tool handler, so counting TS actions would
  // count that one handler two thousand times.
  const bridgeActionNames = enumerateBridgeActions();
  const bridge = new Set(bridgeActionNames.map((a) => a.bridge)).size;
  const bridgeBacked = bridgeActionNames.length;
  const pkg = JSON.parse(fs.readFileSync(path.join(repo, "package.json"), "utf8"));
  const native = computeNativeCounts();
  const newest = newestEpicCatalog();
  // A catalog recorded from an engine past the declared range means the range
  // was not moved with it, and the overview image would show Epic's tools for
  // an engine the copy calls unsupported.
  if (compareEngines(newest.engine, SUPPORTED_ENGINE_RANGE.max) > 0) {
    throw new Error(
      `tests/golden holds an Epic catalog for UE ${newest.engine}, past the supported range ` +
        `${engineRangeText()}. Move SUPPORTED_ENGINE_RANGE in scripts/lib/engine-range.mjs.`,
    );
  }
  return {
    tools: ALL_TOOLS.length,
    actions: total,
    ownActions: total - native.nativeToolActions,
    bridgeActions: bridge,
    localActions: total - bridgeBacked,
    perTool,
    nativeToolsets: native.nativeToolsets,
    nativeToolActions: native.nativeToolActions,
    nativeEngine: newest.engine,
    nativeEngineToolActions: newest.toolActions,
    engineRange: { min: SUPPORTED_ENGINE_RANGE.min, max: SUPPORTED_ENGINE_RANGE.max },
    generatedAt: new Date().toISOString(),
    version: pkg.version,
  };
}

/** Matches any count marker, whatever its name. */
const ANY_COUNT_MARKER = /<!--\s*count:\w+\s*-->/;

export function applyCountMarkers(text: string, counts: Counts): string {
  return text
    .replace(/<!--\s*count:tools\s*-->[^<]*<!--\s*\/count\s*-->/g, `<!-- count:tools -->${counts.tools}<!-- /count -->`)
    .replace(/<!--\s*count:actions\s*-->[^<]*<!--\s*\/count\s*-->/g, `<!-- count:actions -->${counts.actions}+<!-- /count -->`)
    // ue-mcp's own actions, for the sentences that also name Epic's wrapped
    // tools. `count:actions` is the total and already contains them.
    .replace(
      /<!--\s*count:ownActions\s*-->[^<]*<!--\s*\/count\s*-->/g,
      `<!-- count:ownActions -->${counts.ownActions}+<!-- /count -->`,
    )
    .replace(
      /<!--\s*count:nativeToolActions\s*-->[^<]*<!--\s*\/count\s*-->/g,
      `<!-- count:nativeToolActions -->${counts.nativeToolActions}<!-- /count -->`,
    )
    // The actions that dispatch to the C++ bridge, which is what the smoke
    // suite exercises and therefore the number the agent guide quotes for it.
    .replace(
      /<!--\s*count:bridgeActions\s*-->[^<]*<!--\s*\/count\s*-->/g,
      `<!-- count:bridgeActions -->${counts.bridgeActions}<!-- /count -->`,
    )
    .replace(
      /<!--\s*count:engineRange\s*-->[^<]*<!--\s*\/count\s*-->/g,
      `<!-- count:engineRange -->${engineRangeText(counts.engineRange)}<!-- /count -->`,
    )
    .replace(
      /<!--\s*count:engineMax\s*-->[^<]*<!--\s*\/count\s*-->/g,
      `<!-- count:engineMax -->${counts.engineRange.max}<!-- /count -->`,
    );
}

/** Markdown files git tracks or would track (untracked but not ignored),
 *  repo-relative with forward slashes. Falls back to a walk when git is
 *  unavailable (an unpacked tarball). */
function trackedMarkdown(): string[] {
  try {
    const out = execFileSync("git", ["ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", "*.md"], {
      cwd: repo,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    });
    return out.split("\0").filter(Boolean);
  } catch {
    const skip = new Set(["node_modules", "dist", ".git", "tests/.shards", "tests/ue_mcp"]);
    const found: string[] = [];
    const walk = (rel: string) => {
      for (const e of fs.readdirSync(path.join(repo, rel), { withFileTypes: true })) {
        const child = rel ? `${rel}/${e.name}` : e.name;
        if (skip.has(child)) continue;
        if (e.isDirectory()) walk(child);
        else if (e.name.endsWith(".md")) found.push(child);
      }
    };
    walk("");
    return found;
  }
}

/**
 * Every tracked Markdown file that carries a count marker. Found by scanning,
 * so a page that gains a marker is stamped without being registered anywhere.
 */
export function markerFiles(): string[] {
  return [
    ...trackedMarkdown()
      .filter((rel) => fs.existsSync(path.join(repo, rel)))
      .filter((rel) => ANY_COUNT_MARKER.test(fs.readFileSync(path.join(repo, rel), "utf8"))),
    ...pluginMarkerFiles(),
  ].sort();
}

/**
 * Plugin sources that carry a count marker. A header comment saying which
 * engines the version gates cover is a claim like any other, and goes stale
 * the same way, so it is stamped from the same source.
 */
export function pluginMarkerFiles(): string[] {
  const root = path.join(repo, "plugin", "ue_mcp_bridge", "Source");
  const out: string[] = [];
  const walk = (dir: string) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (/\.(h|cpp|cs)$/.test(entry.name) && ANY_COUNT_MARKER.test(fs.readFileSync(full, "utf8"))) {
        out.push(path.relative(repo, full).split(path.sep).join("/"));
      }
    }
  };
  if (fs.existsSync(root)) walk(root);
  return out.sort();
}
