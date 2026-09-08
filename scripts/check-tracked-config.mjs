/**
 * Personal preferences must not live in a tracked config file.
 *
 * `ue-mcp.yml` is checked in and shared by everybody on a project. A setting
 * that differs per person, per machine, or per session belongs in one of the
 * untracked layers instead:
 *
 *     ~/.ue-mcp/config.yml   user-wide taste, every project
 *     ue-mcp.local.yml       one person's override for one project
 *
 * Putting one in the tracked file guarantees merge noise and one person
 * silently clobbering another's setup. One of these was already migrated out
 * once, from `ue-mcp.yml` into per-user state, which is why the migration code
 * exists at all.
 *
 * A related trap worth knowing when layering anything per user: the merge
 * REPLACES arrays and MERGES maps. Anything meant to be layered has to be a map
 * keyed by name, never a list, or the higher layer silently discards the lower
 * one instead of adding to it.
 *
 * Usage:
 *   node scripts/check-tracked-config.mjs [file...]   defaults to every tracked ue-mcp.yml
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import yaml from "js-yaml";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/**
 * Settings that describe a person or a machine rather than a project.
 *
 * Each says where it belongs, because "this is not allowed here" without a
 * destination just moves the problem.
 */
export const PERSONAL_KEYS = [
  {
    key: "feedback.mode",
    why: "Whether somebody is at the keyboard to approve a submission is a property of their session.",
  },
  {
    key: "dialog.mode",
    why: "Whether somebody is there to answer a modal is a property of their machine, not project policy.",
  },
  {
    key: "contextStrategy",
    why: "How much surface a client can hold is a property of that client.",
  },
  {
    key: "bridge.port",
    why: "A pinned port is per machine. Ports are derived from the project path so each project already has its own.",
  },
];

/** Every `a.b.c` path present in a parsed object, as strings. */
export function flattenKeys(obj, prefix = "") {
  if (obj === null || typeof obj !== "object" || Array.isArray(obj)) return [];
  const out = [];
  for (const [k, v] of Object.entries(obj)) {
    const full = prefix ? `${prefix}.${k}` : k;
    out.push(full);
    out.push(...flattenKeys(v, full));
  }
  return out;
}

/**
 * Every `a.b.c` path present in a parsed config.
 *
 * Parsed with js-yaml, which is what the product itself uses to load these
 * files, so what this sees is what the server sees. A hand-rolled reader that
 * only understood indentation missed an inline map, a quoted key, and any
 * nesting it did not anticipate, all of which the real loader honours.
 */
export function keysInYaml(text) {
  let doc;
  try {
    doc = yaml.load(text);
  } catch {
    // A file the loader cannot parse is not a file with settings hidden in it.
    return [];
  }
  return flattenKeys(doc);
}

/** Personal settings found in one tracked config, with where each belongs. */
export function personalKeysIn(text, personal = PERSONAL_KEYS) {
  const present = keysInYaml(text);
  // A suffix match, not two fixed prefixes. The block is usually nested under
  // `ue-mcp:`, but it can be nested under anything a project invents, and a
  // per-user setting is exactly as personal at `projects.vale.feedback.mode`
  // as it is at the top level.
  const has = (key) => present.some((p) => p === key || p.endsWith(`.${key}`));
  return personal.filter((p) => has(p.key));
}

function trackedConfigs() {
  return execFileSync("git", ["ls-files", "*ue-mcp.yml"], { cwd: REPO, encoding: "utf8" })
    .split("\n")
    .filter(Boolean);
}

function main(argv) {
  const files = argv.length > 0 ? argv : trackedConfigs();
  let bad = 0;
  for (const file of files) {
    const full = path.isAbsolute(file) ? file : path.join(REPO, file);
    if (!fs.existsSync(full)) continue;
    const found = personalKeysIn(fs.readFileSync(full, "utf8"));
    for (const f of found) {
      bad++;
      console.error(`${file}  personal setting in a tracked config: ${f.key}`);
      console.error(`    ${f.why}`);
      console.error("    Move it to ~/.ue-mcp/config.yml (user-wide) or ue-mcp.local.yml (this project).");
    }
  }
  if (bad > 0) {
    console.error(`\n${bad} personal setting${bad === 1 ? "" : "s"} in tracked config.`);
    return 1;
  }
  console.log("check:tracked-config - no personal settings in tracked config");
  return 0;
}

if (process.argv[1]?.endsWith("check-tracked-config.mjs")) {
  process.exit(main(process.argv.slice(2)));
}
