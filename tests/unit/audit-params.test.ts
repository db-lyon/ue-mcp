/**
 * The reader the doc audits are built on.
 *
 * Both `audit:docs` and `audit:params` used to parse src/tools/*.ts with their
 * own regex, and both were blind in ways that made them report zero while
 * exiting 0. That is the worst failure an audit has: not a wrong answer, an
 * answer that looks clean because nothing was read.
 *
 *   * `categoryTool\(\s*"[^"]+",\s*"[^"]*",\s*\{` failed outright on the one
 *     category whose description contains an escaped quote. foliage reported
 *     src=0 and its fifteen documented actions came back as EXTRA.
 *   * A brace counter that does not know what a regex literal is read the two
 *     escaped closing braces in project.ts's `/(\n\s*\}\s*\n\s*\})\s*$/` as
 *     scope, closed the actions object early, and dropped every action after
 *     `add_module_dependency`.
 *   * `description:\s*"((?:[^"\\]|\\.)*)"` captured only the FIRST literal of a
 *     concatenated description. The `Params:` clause sits at the END, so every
 *     multi-line description lost exactly the part the param audit exists to
 *     read - fifteen actions reported as drifting when none were.
 *
 * Each of those is pinned below against a fixture, so the fix cannot be undone
 * by the next person who reaches for a regex.
 */
import { describe, it, expect } from "vitest";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import * as path from "node:path";
import { maskLiterals, readCategory, readCategories } from "../../scripts/lib/tool-source.mjs";
import { PAGINATION_PARAM_NAMES } from "../../src/pagination.js";
import { auditParams } from "../../scripts/audit-params.mjs";
import { auditDocs } from "../../scripts/audit-docs.mjs";

function fixture(body: string): string {
  const dir = mkdtempSync(path.join(tmpdir(), "tool-source-"));
  mkdirSync(dir, { recursive: true });
  const file = path.join(dir, "fixture.ts");
  writeFileSync(file, body);
  return file;
}

describe("maskLiterals", () => {
  it("keeps every character position, so a span in the mask is a span in the source", () => {
    const src = 'const a = "hello";\nconst b = 1;\n';
    expect(maskLiterals(src)).toHaveLength(src.length);
  });

  it("blanks an escaped quote instead of ending the literal on it", () => {
    const masked = maskLiterals('const a = "say \\"hi\\" now"; const b = {};');
    // The braces after the literal are the only ones left standing.
    expect(masked.split("{")).toHaveLength(2);
    expect(masked).not.toContain("hi");
  });

  it("blanks a regex literal, whose escaped braces are not scope", () => {
    const masked = maskLiterals("const re = /(\\n\\s*\\}\\s*\\n\\s*\\})\\s*$/;\nconst o = { a: 1 };");
    // One `{` and one `}` survive: the object's own.
    expect(masked.split("{")).toHaveLength(2);
    expect(masked.split("}")).toHaveLength(2);
  });

  it("leaves a division alone, which is not a regex", () => {
    const masked = maskLiterals("const half = total / 2; const o = { a: 1 };");
    expect(masked).toContain("total / 2");
  });
});

describe("readCategory", () => {
  it("reads a category whose description contains an escaped quote", () => {
    // The exact shape that made foliage report src=0.
    const file = fixture(
      'export const t = categoryTool(\n'
      + '  "foliage",\n'
      + '  "Grass is authored elsewhere: material(add_expression, expressionType=\\"LandscapeGrassOutput\\").",\n'
      + "  {\n"
      + '    list_types: bp("read", "List foliage types. Params: cursor?", "list_foliage_types"),\n'
      + '    get_settings: bp("read", "Read settings. Params: foliageTypeName", "get_foliage_type_settings"),\n'
      + "  },\n"
      + ");\n",
    );
    const parsed = readCategory(file);
    expect(parsed?.name).toBe("foliage");
    expect(parsed?.actions.map((a) => a.name)).toEqual(["list_types", "get_settings"]);
  });

  it("does not lose the actions declared after a regex literal", () => {
    const file = fixture(
      'export const t = categoryTool(\n'
      + '  "project",\n'
      + '  "Project.",\n'
      + "  {\n"
      + "    add_module_dependency: {\n"
      + '      description: "Add a module. Params: moduleName",\n'
      + "      handler: async () => {\n"
      + "        const ctorCloseRe = /(\\n\\s*\\}\\s*\\n\\s*\\})\\s*$/;\n"
      + "        return ctorCloseRe;\n"
      + "      },\n"
      + "    },\n"
      + "    add_cpp_member: {\n"
      + '      description: "Append a declaration. Params: headerPath",\n'
      + "      handler: async () => ({}),\n"
      + "    },\n"
      + "  },\n"
      + ");\n",
    );
    expect(readCategory(file)?.actions.map((a) => a.name)).toEqual([
      "add_module_dependency",
      "add_cpp_member",
    ]);
  });

  it("reads a concatenated description whole, Params clause included", () => {
    const file = fixture(
      'export const t = categoryTool(\n'
      + '  "asset",\n'
      + '  "Assets.",\n'
      + "  {\n"
      + '    duplicate: bp("read",' + "\n"
      + '      "Duplicate an asset, keeping every reference the original carried "\n'
      + '      + "and reporting what it renamed. "\n'
      + '      + "Params: assetPath, destinationPath, onConflict?",\n'
      + '      "duplicate_asset",\n'
      + "    ),\n"
      + "  },\n"
      + ");\n",
    );
    const action = readCategory(file)?.actions[0];
    expect(action?.description).toContain("Params: assetPath, destinationPath, onConflict?");
  });

  it("reads the description out of a bp() spread into an object", () => {
    // `{ ...bp("read", ...), timeoutMs: N }`. Reading only a leading `bp(` left every
    // action with a timeout override describing nothing, and landscape alone
    // then reported fourteen documented parameters as invented.
    const file = fixture(
      'export const t = categoryTool(\n'
      + '  "landscape",\n'
      + '  "Landscape.",\n'
      + "  {\n"
      + "    get_height_region: {\n"
      + '      ...bp("read", "Read heights. Params: actorLabel?, region?", "get_landscape_height_region"),\n'
      + "      timeoutMs: 120_000,\n"
      + "    },\n"
      + "  },\n"
      + ");\n",
    );
    const action = readCategory(file)?.actions[0];
    expect(action?.name).toBe("get_height_region");
    expect(action?.description).toContain("Params: actorLabel?, region?");
  });

  it("marks an action wrapped in paged()", () => {
    const file = fixture(
      'export const t = categoryTool(\n'
      + '  "asset",\n'
      + '  "Assets.",\n'
      + "  {\n"
      + '    list: bp("read", paged("List assets. Params: directory?"), "list_assets"),\n'
      + '    create: bp("read", "Create an asset. Params: name", "create_asset"),\n'
      + "  },\n"
      + ");\n",
    );
    const actions = readCategory(file)?.actions ?? [];
    expect(actions.find((a) => a.name === "list")?.paged).toBe(true);
    expect(actions.find((a) => a.name === "create")?.paged).toBe(false);
  });
});

describe("the audits over the real tree", () => {
  it("reads every category tool the source declares", () => {
    // A hard-coded list of twenty had stopped growing with the surface, so
    // chooser, plugins, epic and fab were never checked at all.
    const { categories, blind } = readCategories();
    expect(blind).toEqual([]);
    expect(categories.length).toBeGreaterThanOrEqual(24);
    for (const name of ["chooser", "plugins", "epic", "fab", "foliage"]) {
      expect(categories.map((c) => c.name)).toContain(name);
    }
  });

  it("finds a description for every action", () => {
    // An empty description is the silent form of blindness: the param audit
    // has nothing to compare and reports the docs as inventing everything.
    const { categories } = readCategories();
    const empty = categories.flatMap((c) =>
      c.actions.filter((a) => a.description.trim() === "").map((a) => `${c.name}.${a.name}`),
    );
    expect(empty, `Actions whose description could not be read:\n  ${empty.join("\n  ")}`).toEqual([]);
  });

  it("documents every advertised action", () => {
    const { rows, blind } = auditDocs();
    expect(blind).toEqual([]);
    const drift = rows.flatMap((r) => [
      ...r.missing.map((a: string) => `${r.category}.${a} missing from docs`),
      ...r.extra.map((a: string) => `${r.category}.${a} documented but not advertised`),
    ]);
    expect(drift.join("\n")).toBe("");
  });

  it("agrees with the docs on every action's parameter names", () => {
    const { drifts, blind, compared } = auditParams();
    expect(blind).toEqual([]);
    // The old audit compared a fraction of the surface and called it clean.
    expect(compared).toBeGreaterThanOrEqual(1000);
    expect(
      drifts.map((d: { category: string; action: string }) => `${d.category}.${d.action}`).join("\n"),
    ).toBe("");
  });

  it("keeps the audit's pagination parameter names in step with pagination.ts", () => {
    // audit-params.mjs is plain node and cannot import the TS module, so it
    // carries a copy. This is the assertion that makes the copy safe.
    expect([...PAGINATION_PARAM_NAMES]).toEqual(["cursor", "limit"]);
  });
});

describe("blindness is a failure, not an empty result", () => {
  it("returns null for a file the parser cannot read", () => {
    // The caller turns null into a BLIND row and a non-zero exit. Returning an
    // empty action list instead is what let `--- foliage (src=0, doc=15) ---`
    // print alongside "Total: 0 missing" and exit 0.
    const file = fixture('export const t = categoryTool("asset", "Assets.");\n');
    expect(readCategory(file)).toBeNull();
  });

  it("returns null when the name argument is not a plain literal", () => {
    const file = fixture('export const t = categoryTool(NAME, "Assets.", { list: bp("read", "x", "y") });\n');
    expect(readCategory(file)).toBeNull();
  });
});
