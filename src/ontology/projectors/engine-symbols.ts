/**
 * EngineSymbolProjector
 *
 * Builds a queryable symbol index of engine C++ declarations into
 * /UE/Engine/Symbols/<Name>. Replaces the grep-based engine search
 * path (find_engine_symbol / search_engine_cpp) with a structured
 * address space: an agent can ask "what derives from AActor" or
 * "what module owns UCharacterMovementComponent" with a selector
 * instead of a full-tree scan.
 *
 * Extraction is pragmatic - regex over header text, not a C++
 * parser. The rule: if a declaration isn't a single-line `class`,
 * `struct`, or `enum class` with a conventional shape, we skip it.
 * We do not try to cover templates, nested classes, or preprocessor
 * cases. Tree-sitter can replace this later without changing the
 * emitted point shape.
 *
 * Scope defaults to Engine/Source/Runtime to keep the index
 * bounded. Wider trees (Editor, Developer, Plugins) can be opted
 * into via the input.
 *
 * triggerEvents is ["manual"] by default - building the index is
 * slow (thousands of headers) so startup prime skips it. Agents
 * opt in via ontology(project_by_event, event="manual") once.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import type { KantFragment, KantPoint, Projector } from "../types.js";

export interface EngineSymbolProjectorInput {
  engineRoot: string | null;
  trees?: readonly EngineTree[];
  includePlugins?: boolean;
  maxSymbols?: number;
}

export type EngineTree = "Runtime" | "Editor" | "Developer";

const SYMBOL_BASE = "/UE/Engine/Symbols";
const DEFAULT_MAX = 50000;

type SymbolKind = "class" | "struct" | "enum";

interface ExtractedSymbol {
  name: string;
  kind: SymbolKind;
  parent?: string;
  file: string;
  line: number;
  module: string;
}

const CLASS_RE = /^\s*class\s+(?:[A-Z][A-Z0-9_]*_API\s+)?([A-Za-z_][\w]*)(?:\s*:\s*(?:public|protected)\s+([\w:<>,\s]+?))?\s*(?:\{|$)/;
const STRUCT_RE = /^\s*struct\s+(?:[A-Z][A-Z0-9_]*_API\s+)?([A-Za-z_][\w]*)(?:\s*:\s*(?:public|protected)\s+([\w:<>,\s]+?))?\s*(?:\{|$)/;
const ENUM_CLASS_RE = /^\s*enum\s+class\s+([A-Za-z_][\w]*)(?:\s*:\s*\w+)?\s*(?:\{|$)/;

function inferModule(fullPath: string, engineSource: string): string {
  const rel = path.relative(engineSource, fullPath).replace(/\\/g, "/");
  const segs = rel.split("/");
  if (segs.length < 2) return "Unknown";
  // Runtime/Engine/Public/Foo.h → Engine
  // Runtime/Core/Public/Misc/Foo.h → Core
  if (segs[0] === "Runtime" || segs[0] === "Editor" || segs[0] === "Developer") {
    return segs[1] ?? "Unknown";
  }
  return segs[0];
}

function stripComments(line: string): string {
  // Strip /* ... */ on a single line and // trailing comments.
  return line.replace(/\/\*[^*]*\*\//g, "").replace(/\/\/.*$/, "");
}

function cleanParent(raw: string | undefined): string | undefined {
  if (!raw) return undefined;
  // Parent captures can include multiple base classes or trailing
  // whitespace before '{'. Take the first base, strip template args.
  const first = raw.split(",")[0].trim();
  const noTemplates = first.replace(/<[^>]*>/g, "").trim();
  return noTemplates || undefined;
}

function extractFromFile(filePath: string, module: string): ExtractedSymbol[] {
  let text: string;
  try {
    text = fs.readFileSync(filePath, "utf-8");
  } catch {
    return [];
  }
  const out: ExtractedSymbol[] = [];
  const lines = text.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const line = stripComments(lines[i]);
    if (line.length === 0) continue;
    const ec = ENUM_CLASS_RE.exec(line);
    if (ec) {
      out.push({ name: ec[1], kind: "enum", file: filePath, line: i + 1, module });
      continue;
    }
    const c = CLASS_RE.exec(line);
    if (c && !line.includes(";")) {
      const parent = cleanParent(c[2]);
      out.push({ name: c[1], kind: "class", parent, file: filePath, line: i + 1, module });
      continue;
    }
    const s = STRUCT_RE.exec(line);
    if (s && !line.includes(";")) {
      const parent = cleanParent(s[2]);
      out.push({ name: s[1], kind: "struct", parent, file: filePath, line: i + 1, module });
    }
  }
  return out;
}

function walkHeaders(root: string, visit: (p: string) => void): void {
  let entries: fs.Dirent[];
  try {
    entries = fs.readdirSync(root, { withFileTypes: true });
  } catch {
    return;
  }
  for (const e of entries) {
    const full = path.join(root, e.name);
    if (e.isDirectory()) {
      if (e.name === "Intermediate" || e.name === "Binaries" || e.name === "ThirdParty" || e.name === "Tests") continue;
      walkHeaders(full, visit);
    } else if (e.isFile() && /\.(h|inl)$/i.test(e.name)) {
      visit(full);
    }
  }
}

function kindScore(kind: SymbolKind): number {
  return kind === "class" ? 0 : kind === "struct" ? 0.5 : 1;
}

export function extractSymbols(
  engineRoot: string,
  trees: readonly EngineTree[],
  includePlugins: boolean,
  maxSymbols: number,
): { symbols: ExtractedSymbol[]; truncated: boolean; scannedFiles: number } {
  const engineSource = path.join(engineRoot, "Engine", "Source");
  const symbols: ExtractedSymbol[] = [];
  let scannedFiles = 0;
  let truncated = false;

  const roots: string[] = [];
  for (const t of trees) {
    const dir = path.join(engineSource, t);
    if (fs.existsSync(dir)) roots.push(dir);
  }
  if (includePlugins) {
    const plug = path.join(engineRoot, "Engine", "Plugins");
    if (fs.existsSync(plug)) roots.push(plug);
  }

  outer: for (const root of roots) {
    const collect: string[] = [];
    walkHeaders(root, (p) => collect.push(p));
    for (const file of collect) {
      scannedFiles += 1;
      const module = inferModule(file, engineSource);
      const extracted = extractFromFile(file, module);
      for (const sym of extracted) {
        symbols.push(sym);
        if (symbols.length >= maxSymbols) {
          truncated = true;
          break outer;
        }
      }
    }
  }

  return { symbols, truncated, scannedFiles };
}

function symbolPoint(sym: ExtractedSymbol, engineRoot: string): KantPoint {
  const relFile = path.relative(engineRoot, sym.file).replace(/\\/g, "/");
  const fields: Record<string, string | number | { kind: "signal"; value: number; marker?: string }> = {
    name: sym.name,
    kind: { kind: "signal", value: kindScore(sym.kind), marker: sym.kind },
    module: sym.module,
    file: relFile,
    line: sym.line,
  };
  if (sym.parent) fields.parent = sym.parent;
  return {
    meaning: `${sym.kind} ${sym.name}`,
    purpose: sym.parent ? `${sym.kind} declared in ${sym.module}, derives from ${sym.parent}` : `${sym.kind} declared in ${sym.module}`,
    fields,
  };
}

export function createEngineSymbolProjector(): Projector<EngineSymbolProjectorInput> {
  return {
    name: "engine-symbols",
    basePath: SYMBOL_BASE,
    triggerEvents: ["manual"],
    project(input: EngineSymbolProjectorInput): KantFragment {
      if (!input.engineRoot) {
        return {
          basePath: SYMBOL_BASE,
          producer: "engine-symbols",
          producedAt: new Date().toISOString(),
          points: {
            Index: {
              meaning: "Engine Symbol Index",
              purpose: "Empty - engine root was not resolvable at projection time",
              fields: { symbolCount: 0, scannedFiles: 0, truncated: 0 },
              children: {},
            },
          },
        };
      }
      const trees = input.trees ?? (["Runtime"] as const);
      const includePlugins = input.includePlugins ?? false;
      const maxSymbols = input.maxSymbols ?? DEFAULT_MAX;

      const { symbols, truncated, scannedFiles } = extractSymbols(
        input.engineRoot,
        trees,
        includePlugins,
        maxSymbols,
      );

      // Deduplicate on name (first declaration wins). Engine has many
      // forward-declarations and repeated names; agents querying by
      // name want a canonical point, not a list.
      const dedup: Record<string, ExtractedSymbol> = {};
      for (const sym of symbols) {
        if (dedup[sym.name]) continue;
        dedup[sym.name] = sym;
      }

      const children: Record<string, KantPoint> = {};
      for (const [name, sym] of Object.entries(dedup)) {
        children[name] = symbolPoint(sym, input.engineRoot);
      }

      return {
        basePath: SYMBOL_BASE,
        producer: "engine-symbols",
        producedAt: new Date().toISOString(),
        points: {
          Index: {
            meaning: "Engine Symbol Index",
            purpose: `Indexed class/struct/enum declarations from ${trees.join("+")}${includePlugins ? "+Plugins" : ""}`,
            fields: {
              symbolCount: Object.keys(dedup).length,
              rawSymbolCount: symbols.length,
              scannedFiles,
              truncated: truncated ? 1 : 0,
              trees: trees.join(","),
            },
            children,
          },
        },
      };
    },
  };
}
