/**
 * What a built-in flow may and may not do.
 *
 * Both of these were rules somebody had to remember while writing a new flow,
 * and both are visible in the flow definitions themselves.
 *
 * Usage:
 *   node scripts/check-flows.mjs
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const LOADER = path.join(REPO, "src", "flow", "loader.ts");

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
 * The namespaced paths the demos already use, listed exactly.
 *
 * Allowing "anything on a line that mentions a demo" would exempt every future
 * one too, so the exemption is the two literal paths that exist.
 */
export const DEMO_PATHS = new Set(["/Game/Flows/Beacon", "/Game/MCP_Home"]);

/** Every flow name declared in the loader, in source order. */
export function flowNames(source) {
  const names = [];
  // A flow is a key in the returned record whose value opens with `description:`.
  const re = /^\s{4}([a-z0-9_]+):\s*\{\s*$/gm;
  let m;
  while ((m = re.exec(source)) !== null) {
    const after = source.slice(m.index, m.index + 400);
    if (/\n\s{6}description:/.test(after)) names.push(m[1]);
  }
  return names;
}

/** Tool-namespaced content paths, with the line each appears on. */
export function namespacedPaths(source) {
  const out = [];
  source.split(/\r?\n/).forEach((line, i) => {
    if (line.includes("lint-prose-allow") || /^\s*\/\//.test(line)) return;
    for (const ns of TOOL_NAMESPACES) {
      // Any string form, not only a double or single quoted literal: a
      // template literal is the natural way to write a parameterised default
      // and was the one that escaped. Case-insensitive, since /game and /Game
      // resolve to the same content root.
      const m = new RegExp(`["'\`](${ns}[A-Za-z0-9_/$\\{\\}]*)`, "i").exec(line);
      if (m) {
        out.push({ line: i + 1, text: line.trim(), namespace: ns, path: m[1] });
      }
    }
  });
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
  const source = fs.readFileSync(LOADER, "utf8");
  const names = flowNames(source);
  let bad = 0;

  if (names.length === 0) {
    console.error("check:flows - found no flows to check, which means the parser is wrong");
    return 1;
  }

  for (const stray of strayCleanupFlows(names)) {
    bad++;
    console.error(`check:flows - '${stray}' is a cleanup twin for a flow that is not a demo.`);
    console.error("    Flows create content; reverting is the user's, through version control.");
    console.error("    A cleanup twin doubles the surface and signals that flows are unsafe to run.");
  }

  for (const hit of namespacedPaths(source)) {
    if (DEMO_PATHS.has(hit.path)) continue;
    bad++;
    console.error(`check:flows - ${LOADER}:${hit.line} defaults into ${hit.namespace}.`);
    console.error(`    ${hit.text}`);
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
