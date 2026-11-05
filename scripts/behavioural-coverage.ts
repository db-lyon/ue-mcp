#!/usr/bin/env tsx
/**
 * The behavioural-coverage inventory (#1223): every bridge action the plugin
 * registers, and the release-gated tests that dispatch it and assert something
 * about what came back.
 *
 *   npx tsx scripts/behavioural-coverage.ts                   rewrite the inventory
 *   npx tsx scripts/behavioural-coverage.ts --check           fail when it is stale or an action is unexplained
 *   npx tsx scripts/behavioural-coverage.ts --shrink-ratchet  drop ratchet entries that are now covered or gone
 *
 * Sources, both run by the release process: tests/live (`npm run test:live`)
 * and the plugin's UE.MCP.* automation tests (`npm run test:automation`).
 * An action counts as covered when a test body (an `it`/`test` block, or an
 * automation test's RunTest) dispatches it, directly or through a helper in
 * the same file, and that body asserts something beyond the call answering.
 * Calls made only from beforeAll/afterAll are fixtures and do not count.
 *
 * An action with no such test is either exempt, with a reason, in
 * tests/coverage/behavioural-exemptions.json, or a grandfathered gap in
 * tests/coverage/behavioural-gaps.json. That list may only shrink.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import { listRegistrations } from "./lib/cpp-registrations.mjs";
import { maskLiterals } from "./lib/tool-source.mjs";
import {
  calledHelpers,
  helperSpans,
  matching,
  tsAsserts,
  tsTitledBlocks,
  walk,
  type FileHelpers,
  type Span,
} from "./lib/test-scan.js";
import {
  EXEMPTIONS_FILE,
  INVENTORY_FILE,
  MIN_EXEMPTION_REASON,
  RATCHET_FILE,
  readExemptions,
  readRatchet,
} from "./lib/behavioural-ratchet.mjs";
import { ALL_TOOLS } from "../src/tools.js";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const LIVE_DIR = "tests/live";
const PLUGIN_SOURCE = "plugin/ue_mcp_bridge/Source";

export interface CoverageRef {
  suite: "live" | "automation";
  /** Repo-relative, forward slashes. */
  file: string;
  /** The `it` title, or the automation test's full name. */
  test: string;
}

export type CoverageStatus = "covered" | "exempt" | "gap" | "unexplained";

export interface InventoryRow {
  method: string;
  category: string;
  registeredIn: string;
  status: CoverageStatus;
  tests: CoverageRef[];
  exemption?: string;
}

export interface Inventory {
  note: string;
  summary: {
    actions: number;
    covered: number;
    exempt: number;
    gaps: number;
    unexplained: number;
    /** covered / (actions - exempt), as a percentage with one decimal. */
    coveredShare: number;
  };
  actions: InventoryRow[];
}

// ---------------------------------------------------------------------------
// Source helpers
// ---------------------------------------------------------------------------

const rel = (abs: string): string => path.relative(REPO, abs).split(path.sep).join("/");

// ---------------------------------------------------------------------------
// tests/live
// ---------------------------------------------------------------------------

/** category -> action -> bridge method, for the actions that dispatch to the bridge. */
export function bridgeActionMap(): Map<string, Map<string, string>> {
  const out = new Map<string, Map<string, string>>();
  for (const tool of ALL_TOOLS) {
    const actions = new Map<string, string>();
    for (const [name, spec] of Object.entries(tool.actions)) {
      const bridge = (spec as { bridge?: string }).bridge;
      if (bridge) actions.set(name, bridge);
    }
    out.set(tool.name, actions);
  }
  return out;
}

/** Bridge methods a TS span dispatches, by method name or by category action. */
export function tsDispatches(
  source: string,
  masked: string,
  spans: Span[],
  methods: Set<string>,
  actions: Map<string, Map<string, string>>,
  forwarders: Set<string>,
): Set<string> {
  const found = new Set<string>();
  const resolve =(category: string, action: string) => {
    const bridge = actions.get(category)?.get(action);
    if (bridge && methods.has(bridge)) found.add(bridge);
  };
  for (const span of spans) {
    const text = source.slice(span.start, span.end);
    const maskedText = masked.slice(span.start, span.end);
    // callBridge(bridge, "method") and anything.call("method").
    for (const m of text.matchAll(/\b(?:callBridge\s*\(\s*[\w.]+\s*,|\bcall\s*\()\s*(["'`])(\w+)\1/g)) {
      if (methods.has(m[2])) found.add(m[2]);
    }
    // server.call("category", { action: "x" }) and the micro gateway's category/method pair.
    for (const m of maskedText.matchAll(/\bcall\s*\(/g)) {
      const open = span.start + m.index! + m[0].length - 1;
      const close = matching(masked, open);
      if (close === -1) continue;
      const args = source.slice(open + 1, close);
      const head = /^\s*(["'`])(\w+)\1\s*,/.exec(args);
      if (!head) continue;
      const action = /\b(?:action|method)\s*:\s*(["'`])(\w+)\1/.exec(args)?.[2];
      const category = /\bcategory\s*:\s*(["'`])(\w+)\1/.exec(args)?.[2];
      const nested = /\bmethod\s*:\s*(["'`])(\w+)\1/.exec(args)?.[2];
      if (category && nested) resolve(category, nested);
      if (action) resolve(head[2], action);
    }
    for (const m of text.matchAll(/\b(\w+)\s*\(\s*(["'`])(\w+)\2/g)) {
      // A wrapper named after its category: niagara("add_emitter", {...}).
      if (actions.has(m[1])) resolve(m[1], m[3]);
      // A same-file helper that forwards its first argument to the bridge: survive("open_asset", {...}).
      else if (forwarders.has(m[1]) && methods.has(m[3])) found.add(m[3]);
    }
  }
  return found;
}

/** Same-file helpers whose body dispatches a bridge call, so a method literal handed to one is a dispatch. */
export function forwardingHelpers(masked: string, helpers: FileHelpers): Set<string> {
  const out = new Set<string>();
  for (const [name, span] of helpers.spans) {
    if (/\b(?:callBridge|call)\s*\(/.test(masked.slice(span.start, span.end))) out.add(name);
  }
  return out;
}

export function scanLiveTests(
  methods: Set<string>,
  actions = bridgeActionMap(),
  dir = path.join(REPO, LIVE_DIR),
): Map<string, CoverageRef[]> {
  const out = new Map<string, CoverageRef[]>();
  for (const file of walk(dir, (n) => n.endsWith(".test.ts"))) {
    const source = fs.readFileSync(file, "utf8");
    const masked = maskLiterals(source);
    const helpers = helperSpans(masked, "ts");
    const forwarders = forwardingHelpers(masked, helpers);
    const credit = (method: string, test: string) => {
      const refs = out.get(method) ?? [];
      refs.push({ suite: "live", file: rel(file), test });
      out.set(method, refs);
    };
    const asserting: Span[] = [];
    for (const block of tsTitledBlocks(source, masked, /\b(?:it|test)\b/g)) {
      const spans = [block.span, ...calledHelpers(masked, block.span, helpers)];
      if (!tsAsserts(source, masked, spans)) continue;
      asserting.push(block.span);
      for (const method of tsDispatches(source, masked, spans, methods, actions, forwarders)) credit(method, block.title);
    }
    // A hook inside a describe sets up what that describe's tests assert on, so
    // what it dispatches is credited to the describe. A file-level hook is a fixture.
    const describes = tsTitledBlocks(source, masked, /\bdescribe\b/g);
    for (const m of masked.matchAll(/\b(?:beforeAll|beforeEach)\s*\(/g)) {
      const open = m.index! + m[0].length - 1;
      const hook = { start: open, end: matching(masked, open) };
      if (hook.end === -1) continue;
      const owner = describes
        .filter((d) => d.span.start < hook.start && d.span.end > hook.end)
        .sort((a, b) => b.span.start - a.span.start)[0];
      if (!owner) continue;
      if (!asserting.some((t) => t.start > owner.span.start && t.end < owner.span.end)) continue;
      const spans = [hook, ...calledHelpers(masked, hook, helpers)];
      for (const method of tsDispatches(source, masked, spans, methods, actions, forwarders)) credit(method, owner.title);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// UE.MCP.* automation tests
// ---------------------------------------------------------------------------

/** Registration, preflight and refusal suites assert the parameter contract, which smoke already proves. */
const VALIDATION_ONLY = /\.\w*(?:Registration\w*|Refusals)$/;

const CPP_ASSERT = /\b(Test(True|False|Equal|NotEqual|Null|NotNull|Valid|Invalid|Same|NotSame)|AddError)\s*\(/g;

function cppAsserts(masked: string, spans: Span[]): boolean {
  for (const span of spans) {
    const text = masked.slice(span.start, span.end);
    for (const m of text.matchAll(CPP_ASSERT)) {
      const open = span.start + m.index! + m[0].length - 1;
      const close = matching(masked, open);
      if (close === -1) continue;
      if (!/\bHasHandler\s*\(/.test(masked.slice(open, close))) return true;
    }
  }
  return false;
}

export function cppDispatches(
  source: string,
  masked: string,
  spans: Span[],
  methods: Set<string>,
  byFunction: Map<string, string>,
): Set<string> {
  const found = new Set<string>();
  // Named constants: `const TCHAR* Method = TEXT("action");` used by name in the test.
  const constants = new Map<string, string>();
  for (const m of source.matchAll(/\b(\w+)\s*=\s*TEXT\(\s*"(\w+)"\s*\)/g)) {
    if (methods.has(m[2])) constants.set(m[1], m[2]);
  }
  for (const span of spans) {
    // A HasHandler argument names the action without running it.
    const hasHandlerArgs: Span[] = [];
    for (const m of masked.slice(span.start, span.end).matchAll(/\bHasHandler\s*\(/g)) {
      const open = span.start + m.index! + m[0].length - 1;
      hasHandlerArgs.push({ start: open, end: matching(masked, open) });
    }
    const text = source.slice(span.start, span.end);
    for (const m of text.matchAll(/\bTEXT\(\s*"(\w+)"\s*\)/g)) {
      const at = span.start + m.index!;
      if (!methods.has(m[1])) continue;
      if (hasHandlerArgs.some((h) => at > h.start && at < h.end)) continue;
      found.add(m[1]);
    }
    for (const m of masked.slice(span.start, span.end).matchAll(/\b\w+\b/g)) {
      const method = constants.get(m[0]);
      const at = span.start + m.index!;
      if (method && !hasHandlerArgs.some((h) => at > h.start && at < h.end)) found.add(method);
    }
    for (const m of masked.slice(span.start, span.end).matchAll(/\bF\w+::(\w+)\s*\(/g)) {
      const method = byFunction.get(m[1]);
      if (method) found.add(method);
    }
  }
  return found;
}

export function scanAutomationTests(
  methods: Set<string>,
  byFunction: Map<string, string>,
  dir = path.join(REPO, PLUGIN_SOURCE),
): Map<string, CoverageRef[]> {
  const out = new Map<string, CoverageRef[]>();
  for (const file of walk(dir, (n) => n.endsWith(".cpp"))) {
    const source = fs.readFileSync(file, "utf8");
    if (!source.includes("_AUTOMATION_TEST")) continue;
    const masked = maskLiterals(source);
    const names = new Map<string, string>();
    for (const m of source.matchAll(/IMPLEMENT_\w*AUTOMATION_TEST\w*\(\s*(\w+)\s*,\s*"([^"]+)"/g)) {
      names.set(m[1], m[2]);
    }
    const helpers = helperSpans(masked, "cpp");
    for (const m of masked.matchAll(/\bbool\s+(\w+)::RunTest\s*\([^)]*\)\s*\{/g)) {
      const testName = names.get(m[1]);
      if (!testName || !testName.startsWith("UE.MCP.") || VALIDATION_ONLY.test(testName)) continue;
      const open = m.index! + m[0].length - 1;
      const span = { start: open, end: matching(masked, open) };
      if (span.end === -1) continue;
      const spans = [span, ...calledHelpers(masked, span, helpers, "RunTest")];
      if (!cppAsserts(masked, spans)) continue;
      for (const method of cppDispatches(source, masked, spans, methods, byFunction)) {
        const refs = out.get(method) ?? [];
        refs.push({ suite: "automation", file: rel(file), test: testName });
        out.set(method, refs);
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// The inventory
// ---------------------------------------------------------------------------

/** bridge method -> the category it belongs to. A moved action's old address never names it. */
function categoryOf(): Map<string, string> {
  const out = new Map<string, string>();
  for (const tool of ALL_TOOLS) {
    for (const spec of Object.values(tool.actions)) {
      const { bridge, movedTo } = spec as { bridge?: string; movedTo?: unknown };
      if (bridge && !movedTo && !out.has(bridge)) out.set(bridge, tool.name);
    }
  }
  return out;
}

export interface InventoryInputs {
  exemptions: Record<string, string>;
  ratchet: string[];
  /** Bridge method -> whether any action dispatching it is declared mutate. Defaults to ALL_TOOLS. */
  mutating?: Set<string>;
}

/** Bridge methods some category action declares with effect mutate. */
export function mutatingBridgeMethods(): Set<string> {
  const out = new Set<string>();
  for (const tool of ALL_TOOLS) {
    for (const spec of Object.values(tool.actions)) {
      const { bridge, effect } = spec as { bridge?: string; effect?: string };
      if (bridge && effect === "mutate") out.add(bridge);
    }
  }
  return out;
}

export function buildInventory(inputs: InventoryInputs = { exemptions: readExemptions(), ratchet: readRatchet() }): Inventory {
  const registrations = listRegistrations() as Array<{ method: string; fn: string; file: string }>;
  const methods = new Set(registrations.map((r) => r.method));

  // A static handler called directly from a test counts as its action, when the
  // function name identifies one action and only one.
  const fnCounts = new Map<string, Set<string>>();
  for (const r of registrations) {
    const set = fnCounts.get(r.fn) ?? new Set<string>();
    set.add(r.method);
    fnCounts.set(r.fn, set);
  }
  const byFunction = new Map<string, string>();
  for (const [fn, set] of fnCounts) if (set.size === 1) byFunction.set(fn, [...set][0]);

  const live = scanLiveTests(methods);
  const automation = scanAutomationTests(methods, byFunction);
  const categories = categoryOf();
  const ratchet = new Set(inputs.ratchet);

  const firstFile = new Map<string, string>();
  for (const r of registrations) if (!firstFile.has(r.method)) firstFile.set(r.method, r.file);

  const rows: InventoryRow[] = [...methods].sort().map((method) => {
    const tests = [...(live.get(method) ?? []), ...(automation.get(method) ?? [])];
    const exemption = inputs.exemptions[method];
    let status: CoverageStatus;
    if (tests.length > 0) status = "covered";
    else if (exemption) status = "exempt";
    else if (ratchet.has(method)) status = "gap";
    else status = "unexplained";
    const file = firstFile.get(method)!;
    return {
      method,
      category: categories.get(method) ?? `(${file.replace(/Handlers.*$/, "").toLowerCase() || "unrouted"})`,
      registeredIn: file,
      status,
      tests: dedupe(tests),
      ...(exemption && status === "exempt" ? { exemption } : {}),
    };
  });

  const count = (s: CoverageStatus) => rows.filter((r) => r.status === s).length;
  const exempt = count("exempt");
  const covered = count("covered");
  const denominator = rows.length - exempt;
  return {
    note:
      "Generated by scripts/behavioural-coverage.ts; do not edit. Each action lists the release-gated tests " +
      "(tests/live, UE.MCP.* automation) whose body dispatches it and asserts more than that the call answered.",
    summary: {
      actions: rows.length,
      covered,
      exempt,
      gaps: count("gap"),
      unexplained: count("unexplained"),
      coveredShare: denominator === 0 ? 100 : Math.round((covered / denominator) * 1000) / 10,
    },
    actions: rows,
  };
}

function dedupe(refs: CoverageRef[]): CoverageRef[] {
  const seen = new Set<string>();
  return refs
    .filter((r) => {
      const key = `${r.suite}\u0000${r.file}\u0000${r.test}`;
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    })
    .sort((a, b) => (a.file + a.test < b.file + b.test ? -1 : a.file + a.test > b.file + b.test ? 1 : 0));
}

export function serializeInventory(inventory: Inventory): string {
  return JSON.stringify(inventory, null, 2) + "\n";
}

/**
 * Everything wrong with the inventory against its two hand-kept inputs, as
 * sentences. Empty means the gate passes.
 */
export function inventoryProblems(inventory: Inventory, inputs: InventoryInputs): string[] {
  const problems: string[] = [];
  const byMethod = new Map(inventory.actions.map((r) => [r.method, r]));

  const unexplained = inventory.actions.filter((r) => r.status === "unexplained").map((r) => r.method);
  if (unexplained.length > 0) {
    problems.push(
      `${unexplained.length} action(s) have no behavioural test and no exemption: ${unexplained.join(", ")}. ` +
        "Add a test in tests/live or a UE.MCP.* automation test that dispatches the action and asserts its effect, " +
        `or add it to ${EXEMPTIONS_FILE} with the reason no test can observe it. The gap list (${RATCHET_FILE}) ` +
        "only shrinks; new actions do not go there.",
    );
  }

  // A mutating action changes something a test can read back, so it may not sit in
  // the gap list: it gets a behavioural test, or an exemption saying why none can see it.
  const mutating = inputs.mutating ?? mutatingBridgeMethods();
  const mutatingGaps = inputs.ratchet.filter((m) => mutating.has(m));
  if (mutatingGaps.length > 0) {
    problems.push(
      `${RATCHET_FILE} lists ${mutatingGaps.length} mutating action(s): ${mutatingGaps.join(", ")}. ` +
        "Every action declared with effect mutate needs a behavioural test that asserts what it changed.",
    );
  }

  const ratchetSeen = new Set<string>();
  for (const method of inputs.ratchet) {
    if (ratchetSeen.has(method)) problems.push(`${RATCHET_FILE} lists ${method} twice.`);
    ratchetSeen.add(method);
    const row = byMethod.get(method);
    if (!row) {
      problems.push(`${RATCHET_FILE} lists ${method}, which is no longer registered. Remove it (npm run coverage:behavioural -- --shrink-ratchet).`);
    } else if (row.status === "covered") {
      problems.push(`${RATCHET_FILE} lists ${method}, which is now covered by ${row.tests[0].file}. Remove it (npm run coverage:behavioural -- --shrink-ratchet).`);
    } else if (row.status === "exempt") {
      problems.push(`${method} is both exempt and in ${RATCHET_FILE}. A gap is one or the other.`);
    }
  }
  const sorted = [...inputs.ratchet].sort();
  if (sorted.some((m, i) => m !== inputs.ratchet[i])) problems.push(`${RATCHET_FILE} is not sorted.`);

  for (const [method, reason] of Object.entries(inputs.exemptions)) {
    const row = byMethod.get(method);
    if (!row) problems.push(`${EXEMPTIONS_FILE} exempts ${method}, which is not registered.`);
    else if (row.status === "covered") problems.push(`${EXEMPTIONS_FILE} exempts ${method}, which is covered by ${row.tests[0].file}. Drop the exemption.`);
    if (reason.trim().length < MIN_EXEMPTION_REASON) {
      problems.push(`${EXEMPTIONS_FILE}: the reason for ${method} is ${reason.trim().length} characters. Say why no test can observe it.`);
    }
  }
  return problems;
}

function main(argv: string[]): number {
  const inputs = { exemptions: readExemptions(), ratchet: readRatchet() };
  const inventory = buildInventory(inputs);
  const target = path.join(REPO, INVENTORY_FILE);

  if (argv.includes("--shrink-ratchet")) {
    const keep = inputs.ratchet.filter((m) => inventory.actions.find((r) => r.method === m)?.status === "gap");
    const dropped = inputs.ratchet.length - keep.length;
    const file = path.join(REPO, RATCHET_FILE);
    const parsed = JSON.parse(fs.readFileSync(file, "utf8")) as { note: string; gaps: string[] };
    parsed.gaps = [...new Set(keep)].sort();
    fs.writeFileSync(file, JSON.stringify(parsed, null, 2) + "\n");
    console.log(`behavioural-coverage: dropped ${dropped} ratchet entr${dropped === 1 ? "y" : "ies"}, ${parsed.gaps.length} left.`);
    const fresh = buildInventory({ ...inputs, ratchet: parsed.gaps });
    fs.writeFileSync(target, serializeInventory(fresh));
    return 0;
  }

  const summary = inventory.summary;
  const line =
    `${summary.actions} actions: ${summary.covered} covered (${summary.coveredShare}% of non-exempt), ` +
    `${summary.exempt} exempt, ${summary.gaps} grandfathered gaps, ${summary.unexplained} unexplained`;

  if (argv.includes("--check")) {
    const problems = inventoryProblems(inventory, inputs);
    const committed = fs.existsSync(target) ? fs.readFileSync(target, "utf8") : "";
    if (committed !== serializeInventory(inventory)) {
      problems.push(`${INVENTORY_FILE} is stale. Run npm run coverage:behavioural and commit what it changes.`);
    }
    if (problems.length > 0) {
      for (const p of problems) console.error(`behavioural-coverage: ${p}`);
      return 1;
    }
    console.log(`behavioural-coverage: ${line}`);
    return 0;
  }

  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, serializeInventory(inventory));
  console.log(`behavioural-coverage: wrote ${INVENTORY_FILE}. ${line}`);
  return 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(main(process.argv.slice(2)));
}
