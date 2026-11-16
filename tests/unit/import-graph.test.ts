/**
 * The folder dependency graph of src/ runs one way.
 *
 * Every top-level folder under src/ (and every file directly in src/, as its
 * own node) has a place in LAYERS, lowest first. A module may import a module
 * in its own folder or in a folder on a lower layer, never one on its own
 * layer or above. That keeps the folders free of cycles: once two folders
 * import each other, a change to either is a change to both, and every folder
 * reachable from the cycle comes with them.
 *
 * Type-only imports count. A type pulls its module's whole neighbourhood into
 * the reader's compile and review, and the largest cycle this repo had ran
 * through a file of shared types.
 *
 * EXCEPTIONS lists the imports that still run the wrong way, by file, each with
 * the reason it has not moved yet. The list only shrinks: an entry that no
 * longer occurs fails the test, so it is deleted rather than left to excuse a
 * new import later. A new upward import is fixed by moving the code it needs
 * to the layer that owns it, not by adding a row here.
 */
import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import ts from "typescript";

const here = path.dirname(fileURLToPath(import.meta.url));
const srcDir = path.join(here, "..", "..", "src");

/** Lowest first. A folder may import only folders on an earlier line. */
const LAYERS: readonly (readonly string[])[] = [
  ["contract"],
  ["core"],
  ["terminal"],
  ["bridge"],
  ["config"],
  ["github"],
  ["codeintel"],
  ["editor"],
  ["surface"],
  ["dispatch"],
  ["flow"],
  ["extensions"],
  ["feedback"],
  ["integrations"],
  ["sessions"],
  ["(root)/task"],
  ["(root)/guard-task"],
  ["checkpoints"],
  ["journal"],
  ["pie"],
  ["diagram"],
  ["tools"],
  ["(root)/tools"],
  ["server"],
  ["daemon"],
  ["machine"],
  ["cli"],
  ["(root)/index", "(root)/daemon-api", "(root)/init", "(root)/update", "(root)/deploy-cli"],
];

/** `from -> to`, src-relative, with the reason it still points up. */
const EXCEPTIONS: Record<string, string> = {
  "bridge/bridge.ts -> integrations/epic/unreal-canvas.ts":
    "The bridge avoids the port Unreal Canvas claims; the port rule belongs beside bridge/port.ts.",
  "config/project.ts -> editor/engine-root.ts":
    "Engine resolution reads the editor log for the running engine; the log reader has to move below config first.",
  "dispatch/editor-gate.ts -> sessions/session.ts":
    "The gate routes over the session registry; it needs a narrow registry interface of its own.",
  "editor/bridge-update.ts -> integrations/mcp-clients/mcp-client-config.ts":
    "Bridge update rewrites the pinned client entries; that step belongs with the update command, not the editor layer.",
  "editor/dialog-guard.ts -> dispatch/client-quirks.ts":
    "The client quirk table is moving behind one client-integration interface (Fix 16).",
  "flow/guards.ts -> guard-task.ts":
    "guard-task.ts is the public `ue-mcp/guard` surface; its context names EditorSession, so it moves with flow/guard.ts.",
  "flow/guard.ts -> sessions/session.ts":
    "A guard's public CallContext carries the EditorSession; narrowing it changes the `ue-mcp/guard` contract.",
  "flow/guarded-bridge.ts -> sessions/session.ts":
    "Builds the guard CallContext, which carries the EditorSession; goes with flow/guard.ts.",
  "flow/write-methods.ts -> tools/specs/index.ts":
    "Reads the recorded handler specs, which are generated under tools/; the recording belongs below surface.",
  "integrations/claude-code/hook-session.ts -> cli/editor-flag.ts":
    "The hook resolves --editor the way the CLI does; the resolver belongs in config, the flag parsing in cli.",
  "sessions/session-load.ts -> tools.ts":
    "Each session's surface is cloned from ALL_TOOLS; the declaration should be handed in by the server.",
  "surface/action-effects.ts -> flow/flow-surface.ts":
    "Effects of flow actions are read from the flow surface; the flow tool should declare them.",
  "surface/action-effects.ts -> tools.ts":
    "The effect index defaults to ALL_TOOLS when handed no graph; callers should hand it one.",
  "surface/action-effects.ts -> tools/epic/effects.ts":
    "The generated Epic effect table lives under tools/epic; generated tables the surface reads belong below it.",
  "surface/epic-engines.ts -> tools/epic/catalog-engines.ts": "As surface/action-effects.ts -> tools/epic/effects.ts.",
  "surface/target-params.ts -> sessions/session.ts":
    "Type only: re-points a ToolContext at an EditorSession, so it goes with surface/tool-types.ts.",
  "surface/tool-types.ts -> sessions/session.ts":
    "Type only: ToolContext.session hands handlers the whole EditorSession.",
  "surface/category-tool.ts -> flow/context.ts":
    "A category's dispatcher runs actions through the flow runner; dispatch belongs above the surface it reads.",
  "surface/category-tool.ts -> flow/run-action.ts": "As surface/category-tool.ts -> flow/context.ts.",
  "surface/context/instructions.ts -> sessions/session-surface.ts":
    "The initialize instructions are server output; they belong in server/.",
  "surface/context/instructions.ts -> tools.ts": "As surface/context/instructions.ts -> sessions/session-surface.ts.",
  "surface/context/micro-context.ts -> dispatch/call-pipeline.ts":
    "The micro gateway tool dispatches calls; the gateway belongs above dispatch and flow, the projection below.",
  "surface/context/micro-context.ts -> flow/flow-tool.ts": "As surface/context/micro-context.ts -> dispatch/call-pipeline.ts.",
};

function walk(dir: string, out: string[] = []): string[] {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else if (e.name.endsWith(".ts") && !e.name.endsWith(".d.ts")) out.push(p);
  }
  return out;
}

const rel = (p: string): string => path.relative(srcDir, p).split(path.sep).join("/");

function folderOf(file: string): string {
  const parts = file.split("/");
  return parts.length === 1 ? `(root)/${parts[0].replace(/\.ts$/, "")}` : parts[0];
}

function resolve(fromFile: string, spec: string): string | null {
  if (!spec.startsWith(".")) return null;
  const base = path.resolve(path.dirname(path.join(srcDir, fromFile)), spec).replace(/\.js$/, "");
  for (const c of [`${base}.ts`, path.join(base, "index.ts")]) {
    if (fs.existsSync(c)) return rel(c);
  }
  return null;
}

/** Every relative import, re-export, dynamic import and import type of every src module. */
function fileEdges(): Array<[string, string]> {
  const edges: Array<[string, string]> = [];
  for (const abs of walk(srcDir)) {
    const from = rel(abs);
    const info = ts.preProcessFile(fs.readFileSync(abs, "utf8"), true, true);
    for (const imp of info.importedFiles) {
      const to = resolve(from, imp.fileName);
      if (to) edges.push([from, to]);
    }
  }
  return edges;
}

const rank = new Map<string, number>();
LAYERS.forEach((layer, i) => layer.forEach((f) => rank.set(f, i)));
const edges = fileEdges();

describe("the src folder graph", () => {
  it("places every folder on a layer", () => {
    const folders = new Set(walk(srcDir).map((f) => folderOf(rel(f))));
    expect([...folders].filter((f) => !rank.has(f)).sort()).toEqual([]);
  });

  it("names each folder on one layer only", () => {
    const all = LAYERS.flat();
    expect(all.filter((f, i) => all.indexOf(f) !== i)).toEqual([]);
  });

  it("imports only downward, apart from the listed exceptions", () => {
    const upward = new Set<string>();
    for (const [from, to] of edges) {
      const a = folderOf(from);
      const b = folderOf(to);
      if (a === b) continue;
      if ((rank.get(b) ?? Infinity) >= (rank.get(a) ?? -Infinity)) upward.add(`${from} -> ${to}`);
    }
    const unexplained = [...upward].filter((e) => !(e in EXCEPTIONS)).sort();
    expect(unexplained, "move the imported code down to a layer the importer may reach").toEqual([]);
  });

  it("lists no exception that no longer occurs, so the list only shrinks", () => {
    const present = new Set(edges.map(([a, b]) => `${a} -> ${b}`));
    expect(Object.keys(EXCEPTIONS).filter((e) => !present.has(e)).sort()).toEqual([]);
  });
});
