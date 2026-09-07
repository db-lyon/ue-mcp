/**
 * The wrapped engine tools are declared, and the declaration is derivable.
 *
 * Three checked-in artifacts have to agree:
 *
 *   tests/golden/epic-catalog.json     what Unreal's registry contains
 *   src/tools/epic/effects.ts          what each tool does, decided by a person
 *   src/tools/epic/*.generated.ts      the actions, generated from both
 *
 * These hold them together. The one that matters most is the last: running the
 * generator must produce exactly the files that are committed, so a generated
 * module cannot be hand-edited into disagreeing with its inputs and nobody
 * notices. Everything else here would pass just as well against a hand-forged
 * module.
 */
import { describe, it, expect } from "vitest";
import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { ALL_TOOLS } from "../../src/tools.js";
import { EPIC_TOOL_EFFECTS } from "../../src/tools/epic/effects.js";
import { EPIC_CATEGORIES } from "../../src/tools/epic/index.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const CATALOG = path.join(ROOT, "tests", "golden", "epic-catalog.json");
const GEN_DIR = path.join(ROOT, "src", "tools", "epic");

interface Catalog {
  engineAssociation: string;
  recordedTools: number;
  toolsets: Array<{ name: string; tools?: Array<{ name: string }> }>;
}

const catalog: Catalog = JSON.parse(fs.readFileSync(CATALOG, "utf8"));
const catalogTools = catalog.toolsets.flatMap((ts) => (ts.tools ?? []).map((t) => t.name));

function epicActions() {
  return ALL_TOOLS.flatMap((t) =>
    Object.entries(t.actions)
      .filter(([k]) => k.startsWith("epic_"))
      .map(([action, spec]) => ({ tool: t.name, action, spec })),
  );
}

describe("the recorded catalog", () => {
  it("is a real recording, keyed by the engine it came from", () => {
    expect(catalog.engineAssociation).toMatch(/^\d+\.\d+/);
    expect(catalogTools.length).toBe(catalog.recordedTools);
    expect(catalogTools.length).toBeGreaterThan(700);
    expect(new Set(catalogTools).size, "the registry names a tool twice").toBe(catalogTools.length);
  });
});

describe("every tool has a reviewed effect", () => {
  it("covers the catalog exactly, with nothing left over", () => {
    const declared = new Set(Object.keys(EPIC_TOOL_EFFECTS));
    const missing = catalogTools.filter((n) => !declared.has(n));
    const stale = [...declared].filter((n) => !catalogTools.includes(n));
    expect(
      { missing: missing.slice(0, 10), stale: stale.slice(0, 10) },
      "src/tools/epic/effects.ts must name every tool in the catalog and no others. "
      + "`npm run epic:seed` adds the missing ones for review; a stale entry means the "
      + "catalog was re-recorded against an engine that dropped a tool.",
    ).toEqual({ missing: [], stale: [] });
  });

  it("states one of the three, every time", () => {
    const bad = Object.entries(EPIC_TOOL_EFFECTS).filter(
      ([, e]) => e !== "read" && e !== "mutate" && e !== "unknown",
    );
    expect(bad).toEqual([]);
  });
});

describe("the generated actions", () => {
  it("declares one action per catalog tool", () => {
    expect(epicActions().length).toBe(catalogTools.length);
  });

  it("carries a declared effect that matches the reviewed one", () => {
    const byQualified = new Map<string, string>();
    for (const [qualified, effect] of Object.entries(EPIC_TOOL_EFFECTS)) byQualified.set(qualified, effect);

    for (const { tool, action, spec } of epicActions()) {
      expect(["read", "mutate", "unknown"], `${tool}.${action}`).toContain(spec.effect);
      // Never inferred: these are declared now, which is the whole point.
      expect(spec.effectSource ?? "declared", `${tool}.${action}`).toBe("declared");
    }
    // Every reviewed effect actually reached an action.
    const used = new Set(epicActions().map(({ spec }) => spec.effect));
    expect(used.size).toBeGreaterThan(1);
  });

  it("dispatches through the registry's own bridge method", () => {
    for (const { tool, action, spec } of epicActions()) {
      expect(spec.kind, `${tool}.${action}`).toBe("bridge");
      if (spec.kind !== "bridge") continue;
      expect(spec.bridge, `${tool}.${action}`).toBe("epic_call_tool");
      expect(typeof spec.mapParams, `${tool}.${action}`).toBe("function");
    }
  });

  it("documents its parameters, like every other action on this surface", () => {
    for (const { tool, action, spec } of epicActions()) {
      expect(spec.description ?? "", `${tool}.${action}`).toMatch(/\bParams:/);
    }
  });

  it("declares the parameters it documents, so the MCP layer stops stripping them", () => {
    // The failure this closes: enrichment added only `input`/`inputJson` to a
    // category's schema, so a wrapped tool's own argument names were never
    // declared and were stripped before dispatch. The description promised
    // they were folded in for you, and that promise was false.
    const offenders: string[] = [];
    for (const tool of ALL_TOOLS) {
      const declared = new Set(Object.keys(tool.schema));
      for (const [action, spec] of Object.entries(tool.actions)) {
        if (!action.startsWith("epic_")) continue;
        const clause = (spec.description ?? "").split(/\bParams:\s*/)[1] ?? "";
        if (clause.trim() === "none") continue;
        for (const raw of clause.split(",")) {
          const name = raw.trim().replace(/\?$/, "");
          if (!name || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) continue;
          if (!declared.has(name)) offenders.push(`${tool.name}.${action}: ${name}`);
        }
      }
    }
    expect(offenders.slice(0, 20)).toEqual([]);
  });

  it("lands in the category the routing sends its toolset to", () => {
    for (const category of Object.keys(EPIC_CATEGORIES)) {
      const tool = ALL_TOOLS.find((t) => t.name === category);
      expect(tool, `${category} is generated into but not registered in ALL_TOOLS`).toBeDefined();
    }
  });
});

describe("the committed modules match their inputs", () => {
  it("regenerates byte for byte", () => {
    // The load-bearing one. A generated module edited by hand, or left behind
    // when the catalog or the effects file moved, is caught here and nowhere
    // else: every other test in this file would pass against a forgery.
    // Compared with line endings normalised. The generator writes LF and git
    // checks these files out as CRLF on Windows, so raw bytes differ on a
    // fresh clone for a reason that has nothing to do with whether the content
    // matches its inputs, which is the only thing this is asserting.
    const read = (f: string) => fs.readFileSync(path.join(GEN_DIR, f), "utf8").replace(/\r\n/g, "\n");
    const before = new Map<string, string>();
    for (const f of fs.readdirSync(GEN_DIR)) {
      if (f.endsWith(".generated.ts") || f === "index.ts") before.set(f, read(f));
    }

    execFileSync(process.execPath, [path.join(ROOT, "scripts", "generate-epic-actions.mjs")], {
      cwd: ROOT,
      stdio: "pipe",
    });

    const changed: string[] = [];
    for (const [name, contents] of before) {
      if (read(name) !== contents) changed.push(name);
    }
    expect(
      changed,
      "These generated modules do not match what the generator produces from the recorded "
      + "catalog and the reviewed effects. Run `npm run epic:generate` and commit the result; "
      + "do not hand-edit a generated file.",
    ).toEqual([]);
  }, 60_000);
});
