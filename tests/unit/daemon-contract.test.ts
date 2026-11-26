/**
 * The daemon contract (src/contract/daemon.ts) is what `ue-mcp/daemon`
 * publishes and what DAEMON_API_VERSION and EXTENSION_API_VERSION version.
 * Within one version it changes only additively.
 *
 * tests/golden/daemon-api.d.ts.txt records the declarations the export hands
 * a client: the contract's own .d.ts, which is all of it because the contract
 * imports nothing, and the signature of every helper exported beside it. A
 * change to any public type fails here until it is re-recorded with
 * `npm run golden:record`. Read the diff: a removed or narrowed line is a
 * breaking change, which bumps the version it falls under rather than shipping
 * as a patch to the same one.
 */
import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import ts from "typescript";
import {
  DAEMON_FEATURES,
  DAEMON_FEATURES_BEYOND_ROUTES,
  DAEMON_ROUTES,
  EXTENSION_FEATURES,
  EXTENSION_FEATURE_MEMBERS,
  apiVersionsOverlap,
  describeRoute,
  routePath,
  type DaemonRoute,
} from "../../src/contract/daemon.js";
import { findRoute, matchRoutePath } from "../../src/daemon/router.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.join(here, "..", "..");
const contractFile = path.join(repo, "src", "contract", "daemon.ts");
/** The Playtest Hub's contract files, beside daemon.ts: each imports only its siblings. */
const hubContractFiles = ["hub-qa.ts", "hub.ts", "hub-attention.ts", "hub-ports.ts"].map((f) => path.join(repo, "src", "contract", f));
const contractFiles = [contractFile, ...hubContractFiles];
const entryFile = path.join(repo, "src", "daemon-api.ts");
const recordingFile = path.join(repo, "tests", "golden", "daemon-api.d.ts.txt");
const RECORDING = process.env.UE_MCP_RECORD_GOLDEN === "1";

const same = (a: string, b: string): boolean => path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();

/** The declarations the `ue-mcp/daemon` export hands a client, comments left out. */
function publicDeclarations(): string {
  const program = ts.createProgram([entryFile], {
    target: ts.ScriptTarget.ES2022,
    module: ts.ModuleKind.Node16,
    moduleResolution: ts.ModuleResolutionKind.Node16,
    strict: true,
    skipLibCheck: true,
    declaration: true,
    emitDeclarationOnly: true,
    removeComments: true,
    newLine: ts.NewLineKind.LineFeed,
  });
  const emit = (file: string): string => {
    const source = program.getSourceFile(file);
    if (!source) throw new Error(`${path.relative(repo, file)} is not part of the ue-mcp/daemon program`);
    let out = "";
    const emitted = program.emit(source, (name, text) => {
      if (name.endsWith(".d.ts")) out = text;
    }, undefined, true);
    if (emitted.emitSkipped || !out) throw new Error(`${path.relative(repo, file)}'s declarations did not emit`);
    return out.trim();
  };
  const dts = emit(contractFile);
  const hubDts = hubContractFiles.map((f) => `// ── contract/${path.basename(f)} ──\n${emit(f)}`);

  const checker = program.getTypeChecker();
  const entry = program.getSourceFile(entryFile)!;
  const helpers: string[] = [];
  for (const exp of checker.getExportsOfModule(checker.getSymbolAtLocation(entry)!)) {
    const target = exp.flags & ts.SymbolFlags.Alias ? checker.getAliasedSymbol(exp) : exp;
    const decl = target.declarations?.[0];
    if (!decl || contractFiles.some((f) => same(decl.getSourceFile().fileName, f))) continue;
    const type = checker.typeToString(checker.getTypeOfSymbolAtLocation(target, decl), undefined, ts.TypeFormatFlags.NoTruncation);
    helpers.push(`export declare const ${exp.name}: ${type};`);
  }
  return [
    "// Recorded by tests/unit/daemon-contract.test.ts (npm run golden:record). Do not edit by hand.",
    "",
    "// ── contract/daemon.ts ──",
    dts,
    "",
    ...hubDts.flatMap((d) => [d, ""]),
    "// ── helpers exported beside it ──",
    ...helpers.sort(),
    "",
  ].join("\n");
}

describe("the daemon contract", () => {
  it("imports nothing, so its declarations are the whole public type surface", () => {
    const sf = ts.createSourceFile(contractFile, fs.readFileSync(contractFile, "utf8"), ts.ScriptTarget.Latest);
    const imports = sf.statements.filter((s) => ts.isImportDeclaration(s) || ts.isImportEqualsDeclaration(s) || (ts.isExportDeclaration(s) && s.moduleSpecifier));
    expect(imports.map((s) => s.getText(sf))).toEqual([]);
  });

  it("keeps the hub's contract files self-contained: each imports only its siblings", () => {
    for (const file of hubContractFiles) {
      const sf = ts.createSourceFile(file, fs.readFileSync(file, "utf8"), ts.ScriptTarget.Latest);
      const from = sf.statements.flatMap((s) => (ts.isImportDeclaration(s) || (ts.isExportDeclaration(s) && s.moduleSpecifier) ? [(s.moduleSpecifier as ts.StringLiteral).text] : []));
      expect(from.filter((m) => !/^\.\/(daemon|hub|hub-qa|hub-attention|hub-arrangement|hub-compose|hub-set|hub-performers|hub-ports)\.js$/.test(m)), path.basename(file)).toEqual([]);
    }
  });

  it("keeps ue-mcp/hub to the hub's contract files, so a browser or phone bundle can take it", () => {
    const entry = path.join(repo, "src", "hub-api.ts");
    const sf = ts.createSourceFile(entry, fs.readFileSync(entry, "utf8"), ts.ScriptTarget.Latest);
    const from = sf.statements.flatMap((s) => (ts.isImportDeclaration(s) || (ts.isExportDeclaration(s) && s.moduleSpecifier) ? [(s.moduleSpecifier as ts.StringLiteral).text] : []));
    expect(from.filter((m) => !/^\.\/contract\/(hub|hub-qa|hub-attention|hub-arrangement|hub-compose|hub-set|hub-performers|hub-ports)\.js$/.test(m))).toEqual([]);
    const pkg = JSON.parse(fs.readFileSync(path.join(repo, "package.json"), "utf8")) as { exports: Record<string, unknown> };
    expect(pkg.exports["./hub"]).toEqual({ types: "./dist/hub-api.d.ts", default: "./dist/hub-api.js" });
  });

  it("names a route or a described change for every daemon feature", () => {
    const routed = new Set(Object.values(DAEMON_ROUTES as Record<string, DaemonRoute>).map((r) => r.feature).filter(Boolean));
    const unexplained = DAEMON_FEATURES.filter((f) => !routed.has(f) && !DAEMON_FEATURES_BEYOND_ROUTES[f]);
    expect(unexplained).toEqual([]);
    for (const f of Object.keys(DAEMON_FEATURES_BEYOND_ROUTES)) expect(DAEMON_FEATURES).toContain(f);
  });

  it("ties every extension feature to an API member", () => {
    expect(Object.keys(EXTENSION_FEATURE_MEMBERS).sort()).toEqual([...EXTENSION_FEATURES].sort());
  });

  it("declares each method and path once", () => {
    const seen = Object.values(DAEMON_ROUTES as Record<string, DaemonRoute>).map((r) => `${r.method} ${r.path}`);
    expect(seen.length).toBe(new Set(seen).size);
  });

  it("names routes in messages and fills their paths", () => {
    expect(describeRoute("checkpoints.setEnabled")).toBe("PUT /v1/checkpoints/enabled");
    expect(describeRoute("sessions.access", "PUT")).toBe("PUT /v1/sessions/<id>/access");
    expect(routePath("checkpoints.thumbnail", { file: "ab.png" })).toBe("/v1/checkpoints/thumbnails/ab.png");
    expect(routePath("health")).toBe("/v1/health");
  });

  it("holds both API versions to one rule: the ranges two sides speak overlap", () => {
    expect(apiVersionsOverlap({ min: 1, max: 1 }, { min: 1, max: 1 })).toBe(true);
    expect(apiVersionsOverlap({ min: 2, max: 3 }, { min: 1, max: 1 })).toBe(false);
    expect(apiVersionsOverlap({ min: 1, max: 3 }, { min: 3, max: 3 })).toBe(true);
  });

  it("publishes the declarations recorded in tests/golden/daemon-api.d.ts.txt", () => {
    const now = publicDeclarations();
    if (RECORDING) {
      const before = fs.existsSync(recordingFile) ? fs.readFileSync(recordingFile, "utf8") : "";
      const kept = new Set(now.split("\n"));
      const removed = before.split("\n").filter((l) => l.trim() && !kept.has(l));
      if (removed.length > 0) {
        console.warn(
          `[golden] ${removed.length} line(s) of the ue-mcp/daemon declarations changed or went away. `
          + "Unless each is additive, bump DAEMON_API_VERSION or EXTENSION_API_VERSION:\n"
          + removed.map((l) => `  - ${l.trim()}`).join("\n"),
        );
      }
      fs.writeFileSync(recordingFile, now);
      return;
    }
    expect(
      now,
      "The ue-mcp/daemon declarations changed. Re-record with `npm run golden:record`, read the diff, "
      + "and bump DAEMON_API_VERSION or EXTENSION_API_VERSION unless every change is additive.",
    ).toBe(fs.readFileSync(recordingFile, "utf8").replace(/\r\n/g, "\n"));
  }, 60_000);
});

describe("the daemon router", () => {
  const cases: Array<[string, string, string | null]> = [
    ["GET", "/v1/health", "health"],
    ["POST", "/v1/health", null],
    ["GET", "/v1/events", "events"],
    ["POST", "/v1/update/apply", "update.apply"],
    ["GET", "/v1/sessions", "sessions.list"],
    ["DELETE", "/v1/sessions", "sessions.unknown"],
    ["PUT", "/v1/sessions/abc/access", "sessions.access"],
    ["PATCH", "/v1/sessions/abc/access", "sessions.access"],
    ["GET", "/v1/sessions/abc", "sessions.unknown"],
    ["GET", "/v1/sessionsX", null],
    ["POST", "/v1/editor/rebuild", "editor.rebuild"],
    ["POST", "/v1/editor/other", null],
    ["GET", "/v1/checkpoints", "checkpoints.state"],
    ["POST", "/v1/checkpoints", "checkpoints.create"],
    ["DELETE", "/v1/checkpoints", "checkpoints.clear"],
    ["GET", "/v1/checkpoints/thumbnails/x.png", "checkpoints.thumbnail"],
    ["GET", "/v1/checkpoints/nope", null],
    ["GET", "/v1/journal", "journal.state"],
    ["POST", "/v1/journal/revert-after", "journal.revertAfter"],
    ["GET", "/v1/tools", "tools.list"],
    ["GET", "/v1/tools/categories", "tools.unsupported"],
    ["DELETE", "/v1/tools", "tools.unsupported"],
    ["GET", "/v1/leases", "leases.list"],
    ["POST", "/v1/leases", "leases.unsupported"],
    ["GET", "/v1/plugins", "plugins.list"],
    ["POST", "/v1/plugins/install", "plugins.install"],
    ["GET", "/v1/plugins/other", null],
    ["POST", "/v1/flows/demo/run", "flows"],
    ["GET", "/v1/flows", "flows"],
    ["DELETE", "/v1/ext/studio/x", "ext"],
    ["GET", "/ui", "ui"],
    ["GET", "/ui/ext/studio/index.html", "ui.extension"],
    ["POST", "/ui/index.html", null],
    ["GET", "/v1/mcp", null],
    ["GET", "/", null],
  ];
  it.each(cases)("%s %s goes to %s", (method, p, name) => {
    expect(findRoute(method, p)?.name ?? null).toBe(name);
  });

  it("captures and decodes :params, and refuses a segment that does not decode", () => {
    expect(matchRoutePath("/v1/sessions/:id/access", "/v1/sessions/a%20b/access")).toEqual({ id: "a b" });
    expect(matchRoutePath("/v1/sessions/:id/access", "/v1/sessions/%E0%A4%A/access")).toBeNull();
    expect(matchRoutePath("/v1/ext/*", "/v1/ext")).toEqual({});
  });
});
