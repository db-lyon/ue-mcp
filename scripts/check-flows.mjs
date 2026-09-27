/**
 * What a built-in flow may and may not do.
 *
 * Both of these were rules somebody had to remember while writing a new flow,
 * and both are visible in the flow definitions themselves: the universal
 * layer, universal/ue-mcp.universal.yml.
 *
 * Usage:
 *   node scripts/check-flows.mjs
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import yaml from "js-yaml";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const UNIVERSAL = path.join(REPO, "universal", "ue-mcp.universal.yml");

/**
 * Flows that exist to be run, looked at and removed.
 *
 * A demo may use a tool-namespaced path and may have a cleanup twin, because
 * removing what it made is part of what it is for. Nothing else may.
 */
export const DEMO_FLOWS = new Set(["beacon", "neon_shrine", "neon_shrine_cleanup"]);

/** Content paths that name the tool rather than the asset's real domain. */
const TOOL_NAMESPACES = ["/Game/Flows", "/Game/MCP", "/Game/UEMCP", "/Game/Bridge"];

/**
 * The namespaced roots the demos already use, listed exactly. A path is exempt
 * when it is one of these or lies under one.
 */
export const DEMO_PATHS = new Set(["/Game/Flows/Beacon", "/Game/MCP_Home"]);

export function isDemoPath(p) {
  return [...DEMO_PATHS].some((root) => p === root || p.startsWith(`${root}/`));
}

/** Every flow name a config document declares, in order. */
export function flowNames(doc) {
  return Object.keys(doc?.flows ?? {});
}

/** Tool-namespaced content paths in one string. Case-insensitive: /game and /Game are one root. */
export function namespacedPaths(text) {
  const out = [];
  for (const ns of TOOL_NAMESPACES) {
    const re = new RegExp(`${ns}[A-Za-z0-9_/$\\{\\}.]*`, "gi");
    for (const m of text.matchAll(re)) out.push({ namespace: ns, path: m[0].replace(/\.$/, "") });
  }
  return out;
}

/** Every tool-namespaced path in a flow's values, with where it sits. */
export function namespacedFlowPaths(flows) {
  const out = [];
  const walk = (flow, at, value) => {
    if (typeof value === "string") {
      for (const hit of namespacedPaths(value)) out.push({ flow, at, ...hit });
    } else if (value && typeof value === "object") {
      for (const [k, v] of Object.entries(value)) walk(flow, at ? `${at}.${k}` : k, v);
    }
  };
  for (const [name, def] of Object.entries(flows ?? {})) walk(name, "", def);
  return out;
}

/**
 * Cleanup flows that are not part of a demo.
 *
 * Matched by what the name means rather than by one suffix. A twin called
 * `cleanup_water` or `water_teardown` is the same thing as `water_cleanup`,
 * and checking only the suffix invited the rename rather than the rethink.
 */
export function strayCleanupFlows(names, demos = DEMO_FLOWS) {
  const undoish = /(^|[_-])(cleanup|teardown|wipe|destroy|remove|revert|undo)([_-]|$)|[a-z0-9](Cleanup|Teardown|Wipe|Revert|Undo)/;
  return names.filter((n) => undoish.test(n) && !demos.has(n));
}

function main() {
  const doc = yaml.load(fs.readFileSync(UNIVERSAL, "utf8"));
  const names = flowNames(doc);
  let bad = 0;

  if (names.length === 0) {
    console.error(`check:flows - ${UNIVERSAL} declares no flows, which means it did not generate`);
    return 1;
  }

  for (const stray of strayCleanupFlows(names)) {
    bad++;
    console.error(`check:flows - '${stray}' is a cleanup twin for a flow that is not a demo.`);
    console.error("    Flows create content; reverting is the user's, through version control.");
    console.error("    A cleanup twin doubles the surface and signals that flows are unsafe to run.");
  }

  for (const hit of namespacedFlowPaths(doc.flows)) {
    if (isDemoPath(hit.path)) continue;
    bad++;
    console.error(`check:flows - flow '${hit.flow}' (${hit.at}) defaults into ${hit.namespace}: ${hit.path}`);
    console.error("    Name the asset's real domain instead (/Game/Materials/PBR, /Game/VFX/Fire).");
    console.error("    A tool-namespaced path says the content is throwaway. If no domain fits,");
    console.error("    make the path a required parameter and fail without it.");
  }

  if (bad > 0) {
    console.error(`\n${bad} flow problem${bad === 1 ? "" : "s"}.`);
    return 1;
  }
  console.log(`check:flows - ${names.length} flows, no tool-namespaced defaults, no stray cleanup twins`);
  return 0;
}

if (process.argv[1]?.endsWith("check-flows.mjs")) {
  process.exit(main());
}
