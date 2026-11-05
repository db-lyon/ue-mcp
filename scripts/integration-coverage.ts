#!/usr/bin/env tsx
/**
 * The integration-coverage inventory: every user-facing surface that is not a
 * bridge action (CLI commands, server startup per strategy and mode, the
 * gateway, flows, plugins, the daemon, feedback, auth, config), and the tests
 * that drive the real thing and assert on what it did. Bridge actions have
 * their own inventory, scripts/behavioural-coverage.ts.
 *
 *   npx tsx scripts/integration-coverage.ts                   rewrite the inventory
 *   npx tsx scripts/integration-coverage.ts --check           fail when it is stale or a surface is unexplained
 *   npx tsx scripts/integration-coverage.ts --shrink-ratchet  drop ratchet entries that are now covered or gone
 *
 * The surfaces are a hand-kept manifest, tests/coverage/integration-surfaces.json.
 * A test claims a surface with `covers("id")` (tests/helpers/covers.ts), in its
 * own body or in an enclosing describe. It is credited only when its body
 * asserts more than that a call answered, the same rule the behavioural
 * inventory applies. Sources: tests/integration (CI), tests/live (release),
 * and the spawned-process suites already under tests/unit and tests/multi-editor.
 *
 * A surface with no such test is either exempt, with a reason, or a
 * grandfathered gap in tests/coverage/integration-gaps.json, which may only shrink.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { maskLiterals } from "./lib/tool-source.mjs";
import { calledHelpers, helperSpans, matching, tsAsserts, tsTitledBlocks, walk, type Span } from "./lib/test-scan.js";
import {
  INTEGRATION_EXEMPTIONS_FILE as EXEMPTIONS_FILE,
  INTEGRATION_INVENTORY_FILE as INVENTORY_FILE,
  INTEGRATION_MANIFEST_FILE as MANIFEST_FILE,
  INTEGRATION_RATCHET_FILE as RATCHET_FILE,
  MIN_EXEMPTION_REASON,
  readExemptions,
  readRatchet,
} from "./lib/behavioural-ratchet.mjs";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/** Where a crediting test lives, and so which run proves it. */
export type Suite = "integration" | "live" | "unit" | "multi-editor";

const SUITE_DIRS: Record<Suite, string> = {
  integration: "tests/integration",
  live: "tests/live",
  unit: "tests/unit",
  "multi-editor": "tests/multi-editor",
};

export interface SurfaceRef {
  suite: Suite;
  file: string;
  test: string;
}

export interface Surface {
  id: string;
  group: string;
  summary: string;
}

export type SurfaceStatus = "covered" | "exempt" | "gap" | "unexplained";

export interface SurfaceRow extends Surface {
  status: SurfaceStatus;
  tests: SurfaceRef[];
  exemption?: string;
}

export interface SurfaceInventory {
  note: string;
  summary: {
    surfaces: number;
    covered: number;
    exempt: number;
    gaps: number;
    unexplained: number;
    coveredShare: number;
  };
  /** Ids a test claims that the manifest does not list. Always a problem. */
  unknownClaims: Array<{ id: string; file: string }>;
  surfaces: SurfaceRow[];
}

export function readManifest(root = REPO): Surface[] {
  const parsed = JSON.parse(fs.readFileSync(path.join(root, MANIFEST_FILE), "utf8")) as {
    surfaces?: Array<Partial<Surface>>;
  };
  if (!Array.isArray(parsed.surfaces)) throw new Error(`${MANIFEST_FILE} has no "surfaces" array`);
  return parsed.surfaces.map((s) => ({ id: String(s.id), group: String(s.group ?? ""), summary: String(s.summary ?? "") }));
}

const rel = (abs: string, root: string): string => path.relative(root, abs).split(path.sep).join("/");

/** Every `covers("a", "b")` call in a span of a file, as [position, ids]. */
function coversCalls(source: string, masked: string): Array<{ at: number; ids: string[] }> {
  const out: Array<{ at: number; ids: string[] }> = [];
  for (const m of masked.matchAll(/\bcovers\s*\(/g)) {
    const open = m.index! + m[0].length - 1;
    const close = matching(masked, open);
    if (close === -1) continue;
    const args = source.slice(open + 1, close);
    const ids = [...args.matchAll(/(["'`])([\w.:-]+)\1/g)].map((x) => x[2]);
    if (ids.length > 0) out.push({ at: m.index!, ids });
  }
  return out;
}

const inside = (at: number, span: Span): boolean => at > span.start && at < span.end;

/**
 * Surface ids credited per test file. A test credits the ids claimed in its
 * own body, and the ids claimed directly in any describe that encloses it
 * (outside every test body), when its body asserts something.
 */
export function scanSurfaceClaims(
  dirs: Array<{ suite: Suite; dir: string }> = (Object.keys(SUITE_DIRS) as Suite[]).map((suite) => ({
    suite,
    dir: path.join(REPO, SUITE_DIRS[suite]),
  })),
  root = REPO,
): { credits: Map<string, SurfaceRef[]>; claims: Array<{ id: string; file: string }> } {
  const credits = new Map<string, SurfaceRef[]>();
  const claims: Array<{ id: string; file: string }> = [];
  for (const { suite, dir } of dirs) {
    for (const file of walk(dir, (n) => n.endsWith(".test.ts"))) {
      const source = fs.readFileSync(file, "utf8");
      if (!/\bcovers\s*\(/.test(source)) continue;
      const masked = maskLiterals(source);
      const calls = coversCalls(source, masked);
      if (calls.length === 0) continue;
      const relFile = rel(file, root);
      for (const c of calls) for (const id of c.ids) claims.push({ id, file: relFile });

      const helpers = helperSpans(masked, "ts");
      const tests = tsTitledBlocks(source, masked, /\b(?:it|test)\b/g);
      const describes = tsTitledBlocks(source, masked, /\bdescribe\b/g);
      const inAnyTest = (at: number) => tests.some((t) => inside(at, t.span));
      for (const t of tests) {
        const spans = [t.span, ...calledHelpers(masked, t.span, helpers)];
        if (!tsAsserts(source, masked, spans)) continue;
        const ids = new Set<string>();
        for (const c of calls) {
          if (inside(c.at, t.span)) c.ids.forEach((id) => ids.add(id));
          else if (!inAnyTest(c.at) && describes.some((d) => inside(c.at, d.span) && inside(t.span.start, d.span))) {
            c.ids.forEach((id) => ids.add(id));
          }
        }
        for (const id of ids) {
          const refs = credits.get(id) ?? [];
          refs.push({ suite, file: relFile, test: t.title });
          credits.set(id, refs);
        }
      }
    }
  }
  return { credits, claims };
}

export interface SurfaceInputs {
  manifest: Surface[];
  exemptions: Record<string, string>;
  ratchet: string[];
}

export function readInputs(root = REPO): SurfaceInputs {
  return {
    manifest: readManifest(root),
    exemptions: readExemptions(root, EXEMPTIONS_FILE),
    ratchet: readRatchet(root, RATCHET_FILE),
  };
}

export function buildSurfaceInventory(
  inputs: SurfaceInputs = readInputs(),
  scanned = scanSurfaceClaims(),
): SurfaceInventory {
  const ratchet = new Set(inputs.ratchet);
  const known = new Set(inputs.manifest.map((s) => s.id));
  const rows: SurfaceRow[] = inputs.manifest.map((s) => {
    const tests = dedupe(scanned.credits.get(s.id) ?? []);
    const exemption = inputs.exemptions[s.id];
    let status: SurfaceStatus;
    if (tests.length > 0) status = "covered";
    else if (exemption) status = "exempt";
    else if (ratchet.has(s.id)) status = "gap";
    else status = "unexplained";
    return { ...s, status, tests, ...(exemption && status === "exempt" ? { exemption } : {}) };
  });
  const unknownSeen = new Set<string>();
  const unknownClaims = scanned.claims
    .filter((c) => !known.has(c.id))
    .filter((c) => {
      const key = `${c.id}\u0000${c.file}`;
      if (unknownSeen.has(key)) return false;
      unknownSeen.add(key);
      return true;
    })
    .sort((a, b) => (a.id + a.file < b.id + b.file ? -1 : 1));
  const count = (st: SurfaceStatus) => rows.filter((r) => r.status === st).length;
  const exempt = count("exempt");
  const covered = count("covered");
  const denominator = rows.length - exempt;
  return {
    note:
      "Generated by scripts/integration-coverage.ts; do not edit. Each surface from integration-surfaces.json lists " +
      "the tests that claim it with covers() and assert on what the real process did.",
    summary: {
      surfaces: rows.length,
      covered,
      exempt,
      gaps: count("gap"),
      unexplained: count("unexplained"),
      coveredShare: denominator === 0 ? 100 : Math.round((covered / denominator) * 1000) / 10,
    },
    unknownClaims,
    surfaces: rows,
  };
}

function dedupe(refs: SurfaceRef[]): SurfaceRef[] {
  const seen = new Set<string>();
  return refs
    .filter((r) => {
      const key = `${r.file}\u0000${r.test}`;
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    })
    .sort((a, b) => (a.file + a.test < b.file + b.test ? -1 : a.file + a.test > b.file + b.test ? 1 : 0));
}

export function serializeSurfaceInventory(inventory: SurfaceInventory): string {
  return JSON.stringify(inventory, null, 2) + "\n";
}

/** Everything wrong with the inventory against its hand-kept inputs, as sentences. Empty passes. */
export function surfaceProblems(inventory: SurfaceInventory, inputs: SurfaceInputs): string[] {
  const problems: string[] = [];
  const byId = new Map(inventory.surfaces.map((r) => [r.id, r]));

  const ids = inputs.manifest.map((s) => s.id);
  const dupes = ids.filter((id, i) => ids.indexOf(id) !== i);
  if (dupes.length > 0) problems.push(`${MANIFEST_FILE} lists ${[...new Set(dupes)].join(", ")} more than once.`);
  for (const s of inputs.manifest) {
    if (!/^[a-z][\w-]*(\.[\w-]+)+$/.test(s.id)) problems.push(`${MANIFEST_FILE}: "${s.id}" is not a dotted surface id.`);
    if (s.summary.trim().length < 10) problems.push(`${MANIFEST_FILE}: ${s.id} needs a summary of what a user drives.`);
  }

  const unexplained = inventory.surfaces.filter((r) => r.status === "unexplained").map((r) => r.id);
  if (unexplained.length > 0) {
    problems.push(
      `${unexplained.length} surface(s) have no integration test and no exemption: ${unexplained.join(", ")}. ` +
        "Add a test under tests/integration (or tests/live, for one that needs an editor) that drives the real " +
        `process, calls covers("<id>") and asserts on the effect, or exempt it with a reason in ${EXEMPTIONS_FILE}. ` +
        `The gap list (${RATCHET_FILE}) only shrinks; new surfaces do not go there.`,
    );
  }

  for (const c of inventory.unknownClaims) {
    problems.push(`${c.file} claims covers("${c.id}"), which ${MANIFEST_FILE} does not list.`);
  }

  const seen = new Set<string>();
  for (const id of inputs.ratchet) {
    if (seen.has(id)) problems.push(`${RATCHET_FILE} lists ${id} twice.`);
    seen.add(id);
    const row = byId.get(id);
    if (!row) problems.push(`${RATCHET_FILE} lists ${id}, which the manifest no longer has. Remove it (npm run coverage:integration -- --shrink-ratchet).`);
    else if (row.status === "covered") problems.push(`${RATCHET_FILE} lists ${id}, which is now covered by ${row.tests[0].file}. Remove it (npm run coverage:integration -- --shrink-ratchet).`);
    else if (row.status === "exempt") problems.push(`${id} is both exempt and in ${RATCHET_FILE}. A gap is one or the other.`);
  }
  const sorted = [...inputs.ratchet].sort();
  if (sorted.some((m, i) => m !== inputs.ratchet[i])) problems.push(`${RATCHET_FILE} is not sorted.`);

  for (const [id, reason] of Object.entries(inputs.exemptions)) {
    const row = byId.get(id);
    if (!row) problems.push(`${EXEMPTIONS_FILE} exempts ${id}, which the manifest does not list.`);
    else if (row.status === "covered") problems.push(`${EXEMPTIONS_FILE} exempts ${id}, which is covered by ${row.tests[0].file}. Drop the exemption.`);
    if (reason.trim().length < MIN_EXEMPTION_REASON) {
      problems.push(`${EXEMPTIONS_FILE}: the reason for ${id} is ${reason.trim().length} characters. Say why no test can drive it.`);
    }
  }
  return problems;
}

function main(argv: string[]): number {
  const inputs = readInputs();
  const inventory = buildSurfaceInventory(inputs);
  const target = path.join(REPO, INVENTORY_FILE);

  if (argv.includes("--shrink-ratchet")) {
    const keep = inputs.ratchet.filter((id) => inventory.surfaces.find((r) => r.id === id)?.status === "gap");
    const dropped = inputs.ratchet.length - keep.length;
    const file = path.join(REPO, RATCHET_FILE);
    const parsed = JSON.parse(fs.readFileSync(file, "utf8")) as { note: string; gaps: string[] };
    parsed.gaps = [...new Set(keep)].sort();
    fs.writeFileSync(file, JSON.stringify(parsed, null, 2) + "\n");
    console.log(`integration-coverage: dropped ${dropped} ratchet entr${dropped === 1 ? "y" : "ies"}, ${parsed.gaps.length} left.`);
    fs.writeFileSync(target, serializeSurfaceInventory(buildSurfaceInventory({ ...inputs, ratchet: parsed.gaps })));
    return 0;
  }

  const s = inventory.summary;
  const line =
    `${s.surfaces} surfaces: ${s.covered} covered (${s.coveredShare}% of non-exempt), ` +
    `${s.exempt} exempt, ${s.gaps} grandfathered gaps, ${s.unexplained} unexplained`;

  if (argv.includes("--check")) {
    const problems = surfaceProblems(inventory, inputs);
    const committed = fs.existsSync(target) ? fs.readFileSync(target, "utf8") : "";
    if (committed !== serializeSurfaceInventory(inventory)) {
      problems.push(`${INVENTORY_FILE} is stale. Run npm run coverage:integration and commit what it changes.`);
    }
    if (problems.length > 0) {
      for (const p of problems) console.error(`integration-coverage: ${p}`);
      return 1;
    }
    console.log(`integration-coverage: ${line}`);
    return 0;
  }

  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, serializeSurfaceInventory(inventory));
  console.log(`integration-coverage: wrote ${INVENTORY_FILE}. ${line}`);
  return 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(main(process.argv.slice(2)));
}
