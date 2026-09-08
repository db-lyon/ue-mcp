#!/usr/bin/env node
/**
 * Seed `src/tools/epic/effects.ts` with an entry for every tool in the
 * recorded catalog that does not already have one.
 *
 *     npm run epic:seed
 *
 * ADDITIVE ONLY. An entry that already exists is never rewritten, because it
 * is somebody's reviewed judgement and this script's opinion is worth less
 * than that. Re-recording a newer engine's catalog therefore appends the tools
 * it added and leaves every existing decision alone.
 *
 * The seed reads the tool's DESCRIPTION rather than its name. Epic writes
 * "Returns the blackboard asset for this behavior tree" and "Creates a new
 * Niagara emitter", which say what a tool does; a name says what somebody
 * called it. The name is consulted only when the description decides nothing.
 *
 * A seed is still a guess. Every entry it writes is marked with a trailing
 * `// SEEDED` comment, and `npm run epic:unreviewed` lists what still carries
 * one, so the review has a worklist and finishing it is observable.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const CATALOG = path.join(ROOT, "tests", "golden", "epic-catalog.json");
const EFFECTS = path.join(ROOT, "src", "tools", "epic", "effects.ts");

// Leading verbs of Epic's own descriptions. These are what the sentence starts
// with, not fragments matched anywhere in it, so "Returns the list of actors
// that were deleted" reads as a return and not as a delete.
const READ_OPENERS = [
  "returns", "return", "gets", "get", "lists", "list", "finds", "find", "queries", "query",
  "checks", "check", "reads", "read", "retrieves", "retrieve", "fetches", "fetch",
  "reports", "report", "describes", "describe", "inspects", "inspect", "searches", "search",
  "enumerates", "enumerate", "counts", "count", "resolves", "resolve", "looks", "look",
  "is", "are", "does", "has", "have", "whether", "collects", "collect", "gathers", "gather",
  "captures", "capture", "computes", "compute", "calculates", "calculate", "converts", "convert",
  "provides", "provide", "yields", "yield", "obtains", "obtain", "extracts", "extract",
];
const MUTATE_OPENERS = [
  "creates", "create", "adds", "add", "sets", "set", "removes", "remove", "deletes", "delete",
  "updates", "update", "applies", "apply", "renames", "rename", "moves", "move",
  "imports", "import", "exports", "export", "enables", "enable", "disables", "disable",
  "compiles", "compile", "bakes", "bake", "spawns", "spawn", "assigns", "assign",
  "inserts", "insert", "replaces", "replace", "clears", "clear", "resets", "reset",
  "modifies", "modify", "writes", "write", "saves", "save", "opens", "open", "closes", "close",
  "starts", "start", "stops", "stop", "runs", "run", "executes", "execute", "triggers", "trigger",
  "connects", "connect", "disconnects", "disconnect", "duplicates", "duplicate",
  "attaches", "attach", "detaches", "detach", "selects", "select", "focuses", "focus",
  "generates", "generate", "builds", "build", "installs", "install", "registers", "register",
  "unregisters", "unregister", "binds", "bind", "links", "link", "marks", "mark",
  "toggles", "toggle", "activates", "activate", "deactivates", "deactivate", "destroys", "destroy",
  "pastes", "paste", "copies", "copy", "sorts", "sort", "arranges", "arrange", "positions", "position",
  "configures", "configure", "initializes", "initialize", "loads", "load", "reloads", "reload",
  "refreshes", "refresh", "rebuilds", "rebuild", "recompiles", "recompile", "waits", "wait",
  "observes", "observe", "unobserves", "unobserve", "snapshots", "snapshot", "traces", "trace",
];

function firstWord(text) {
  const m = String(text ?? "").trim().match(/^[A-Za-z_]+/);
  return m ? m[0].toLowerCase() : "";
}

function seedEffect(tool) {
  const opener = firstWord(tool.description);
  if (READ_OPENERS.includes(opener)) return "read";
  if (MUTATE_OPENERS.includes(opener)) return "mutate";

  // The description said nothing usable. Fall back to the name's own leading
  // segment, which is the same signal, just weaker.
  const bare = tool.name.slice(tool.name.lastIndexOf(".") + 1);
  const segs = bare.replace(/([a-z0-9])([A-Z])/g, "$1_$2").toLowerCase().split(/[^a-z0-9]+/);
  for (const s of segs) {
    if (READ_OPENERS.includes(s)) return "read";
    if (MUTATE_OPENERS.includes(s)) return "mutate";
  }
  // Nothing said. `unknown` gates as a change, which is the safe direction for
  // a tool nobody has read yet.
  return "unknown";
}

function existingEntries(src) {
  const map = new Map();
  for (const m of src.matchAll(/^\s*"([^"]+)":\s*"(read|mutate|unknown)",(\s*\/\/ SEEDED)?/gm)) {
    map.set(m[1], { effect: m[2], seeded: Boolean(m[3]) });
  }
  return map;
}

const HEADER = `/**
 * What each wrapped engine tool does to the editor it is addressed to.
 *
 * This file is the reviewed half of the Epic action surface. The catalog in
 * \`tests/golden/epic-catalog.json\` says what tools exist and what arguments
 * they take; Epic ships no annotation saying whether one reads or writes, so
 * that judgement is made here, by a person, once, and lives in a diff.
 *
 * It is hand-edited. \`npm run epic:seed\` only ADDS entries for tools that have
 * none, so re-recording a newer engine appends its new tools and never
 * overwrites a decision already made. \`npm run epic:generate\` refuses to run
 * while any tool in the catalog is missing from here.
 *
 * An entry marked \`// SEEDED\` was written by the seeder from the tool's own
 * description and has not been read by a person yet. \`npm run epic:unreviewed\`
 * lists them. Removing that comment is what "I have read this one" means.
 *
 *   read     observes. Landing it in the wrong editor returns the wrong answer
 *            and changes nothing.
 *   mutate   may change the editor, its project on disk, or its process.
 *   unknown  decided by an argument rather than by the tool. Gated as mutate.
 */
import type { ActionEffect } from "../../types.js";

/** Keyed by the tool's fully qualified registry name. */
export const EPIC_TOOL_EFFECTS: Record<string, ActionEffect> = {
`;

function main() {
  const catalog = JSON.parse(fs.readFileSync(CATALOG, "utf8"));
  const previous = fs.existsSync(EFFECTS) ? existingEntries(fs.readFileSync(EFFECTS, "utf8")) : new Map();

  const rows = [];
  let added = 0;
  let kept = 0;
  for (const ts of catalog.toolsets ?? []) {
    const group = [];
    for (const tool of ts.tools ?? []) {
      if (!tool?.name) continue;
      const prior = previous.get(tool.name);
      const effect = prior ? prior.effect : seedEffect(tool);
      const seeded = prior ? prior.seeded : true;
      if (prior) kept++; else added++;
      // Epic's prose uses em dashes freely and these lines land in a tracked
      // source file, where this repository's prose rule applies. Sanitised the
      // same way the generator sanitises a description it emits.
      const summary = String(tool.description ?? "")
        .split(/\n\s*(?:Args|Arguments|Returns|Raises|Note):/)[0]
        .replace(/\s*—\s*/g, " - ") // em-dash-allowed: the literal is the character being stripped
        .replace(/\s+/g, " ")
        .trim()
        .slice(0, 110);
      group.push(`  // ${summary}\n  ${JSON.stringify(tool.name)}: ${JSON.stringify(effect)},${seeded ? " // SEEDED" : ""}`);
    }
    if (group.length > 0) rows.push(`  /* ── ${ts.name} ${"─".repeat(Math.max(0, 60 - ts.name.length))} */\n${group.join("\n")}`);
  }

  fs.writeFileSync(EFFECTS, `${HEADER}${rows.join("\n\n")}\n};\n`);
  console.log(`effects.ts: ${added} added, ${kept} kept`);
}

try {
  main();
} catch (e) {
  console.error(String(e?.message ?? e));
  process.exit(1);
}
