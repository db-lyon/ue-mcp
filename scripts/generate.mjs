/**
 * Every generated file in the repo, regenerated in dependency order.
 *
 *     npm run generate                    every step that needs no editor
 *     npm run generate -- --editor        also the steps that record from a live
 *                                         editor on tests/ue_mcp
 *     npm run generate -- --check         the editor-free steps, then fail if any
 *                                         committed file changed (the CI drift gate)
 *     npm run generate -- --only a,b      just those steps, still in order
 *     npm run generate -- --list          the steps, and the npm script for each
 *
 * A source change ripples into committed outputs (the spec'd and Epic action
 * modules, the tool reference, the stamped counts, the golden baseline, the
 * coverage inventories), and each used to have its own command somebody had to
 * remember, in the right order. Each step below is still runnable alone by the
 * npm script it names.
 *
 * The golden baseline is a review surface: a step that re-records it is not a
 * licence to commit it unread. Read the diff of tests/golden/ before committing.
 */
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const node = process.execPath;
const tsx = (script, ...args) => [node, "--import", "tsx", script, ...args];

const EDITOR_NEEDED =
  "needs a running editor on tests/ue_mcp with the plugin built from this checkout "
  + "(node scripts/deploy.mjs, npm run build, npm run up)";

/**
 * In dependency order: the recordings, then the modules generated from them,
 * then everything read off the assembled surface.
 *
 * `npm` is the script that runs the step alone. `editor` steps run only with
 * --editor. `outputs` are the committed paths a step writes, for --list.
 */
export const STEPS = [
  {
    name: "specs:record",
    npm: "specs:record",
    editor: true,
    cmd: [node, "scripts/record-handler-specs.mjs"],
    outputs: ["tests/golden/handler-specs.json", "tests/golden/handler-specs.source.json"],
  },
  {
    name: "specs:generate",
    npm: "specs:generate",
    cmd: tsx("scripts/generate-handler-specs.mjs"),
    outputs: ["src/tools/specs/"],
  },
  {
    name: "epic:record",
    npm: "epic:record",
    editor: true,
    cmd: [node, "scripts/record-epic-catalog.mjs"],
    outputs: ["tests/golden/epic-catalog/"],
  },
  {
    name: "epic:generate",
    npm: "epic:generate",
    cmd: tsx("scripts/generate-epic-actions.mjs"),
    outputs: ["src/tools/epic/"],
  },
  {
    name: "epic:wire",
    npm: "epic:generate",
    cmd: [node, "scripts/wire-epic-actions.mjs"],
    outputs: ["src/tools/<category>.ts"],
  },
  {
    name: "epic:docs",
    npm: "gen:epic-docs",
    cmd: tsx("scripts/gen-epic-docs.mjs"),
    outputs: ["docs/native-tools.md"],
  },
  {
    name: "metadata",
    npm: "generate:metadata",
    cmd: tsx("scripts/generate-tool-metadata.ts"),
    outputs: ["docs/tool-reference.md", "<!-- count:* --> markers in tracked Markdown", "dist/tool-counts.json"],
  },
  {
    name: "stamp:package",
    npm: "stamp:package",
    cmd: tsx("scripts/stamp-package.ts"),
    outputs: ["plugin/ue_mcp_bridge/*.uplugin", "package.json description", "docs/assets/ue-mcp-overview.png"],
  },
  {
    name: "config",
    npm: "generate:config",
    cmd: tsx("scripts/generate-default-config.ts"),
    outputs: ["dist/ue-mcp.default.yml"],
  },
  {
    name: "golden",
    npm: "golden:record",
    cmd: [node, "scripts/record-golden.mjs"],
    outputs: ["tests/golden/editor-down.json"],
  },
  {
    name: "golden:connected",
    npm: "golden:record -- --connected",
    editor: true,
    cmd: [node, "scripts/record-golden.mjs", "--connected"],
    outputs: ["tests/golden/editor-connected.json"],
  },
  {
    name: "coverage:behavioural",
    npm: "coverage:behavioural",
    cmd: tsx("scripts/behavioural-coverage.ts"),
    outputs: ["tests/coverage/behavioural-coverage.json"],
  },
  {
    name: "coverage:integration",
    npm: "coverage:integration",
    cmd: tsx("scripts/integration-coverage.ts"),
    outputs: ["tests/coverage/integration-coverage.json"],
  },
];

/**
 * The steps a run executes and the ones it skips for want of an editor.
 * @param {{ editor?: boolean, only?: string[] | null }} [opts]
 */
export function planSteps({ editor = false, only = null } = {}) {
  const unknown = (only ?? []).filter((n) => !STEPS.some((s) => s.name === n));
  if (unknown.length > 0) {
    throw new Error(`No such step: ${unknown.join(", ")}. Steps: ${STEPS.map((s) => s.name).join(", ")}`);
  }
  const wanted = STEPS.filter((s) => only === null || only.includes(s.name));
  return {
    run: wanted.filter((s) => editor || !s.editor),
    skipped: wanted.filter((s) => s.editor && !editor),
  };
}

function git(args) {
  const r = spawnSync("git", args, { cwd: ROOT, encoding: "utf8" });
  if (r.status !== 0) throw new Error(`git ${args.join(" ")} failed: ${r.stderr}`);
  return r.stdout;
}

/** Every path git reports as changed or untracked, with a hash of its content. */
function dirtyState() {
  const state = new Map();
  const entries = git(["status", "--porcelain", "-z", "--untracked-files=all"]).split("\0");
  for (let i = 0; i < entries.length; i++) {
    const entry = entries[i];
    if (entry.length < 4) continue;
    // A rename or copy is followed by its source path as a separate entry.
    if (entry[0] === "R" || entry[0] === "C") i++;
    const rel = entry.slice(3);
    const abs = path.join(ROOT, rel);
    const content = fs.existsSync(abs) && fs.statSync(abs).isFile() ? fs.readFileSync(abs) : Buffer.from("<missing>");
    state.set(rel, createHash("sha256").update(content).digest("hex"));
  }
  return state;
}

/** Paths whose content differs between two dirty-state snapshots. */
export function changedBetween(before, after) {
  return [...after.keys()].filter((rel) => before.get(rel) !== after.get(rel)).sort();
}

function runStep(step, { quiet }) {
  const r = spawnSync(step.cmd[0], step.cmd.slice(1), {
    cwd: ROOT,
    stdio: quiet ? ["ignore", "pipe", "pipe"] : "inherit",
    encoding: "utf8",
    env: process.env,
  });
  if (r.status === 0) return true;
  if (quiet) process.stderr.write(`${r.stdout ?? ""}${r.stderr ?? ""}`);
  if (r.error) console.error(r.error.message);
  return false;
}

async function specSourceWarning() {
  const { readSpecSource, specSourceDrift, specSourceFingerprint } = await import("./lib/spec-source.mjs");
  const drift = specSourceDrift(readSpecSource(), specSourceFingerprint());
  if (drift.length === 0) return null;
  return (
    `The C++ parameter specs changed since tests/golden/handler-specs.json was recorded (${drift.length} `
    + `difference${drift.length === 1 ? "" : "s"}, first: ${drift[0]}), so the spec'd actions were generated `
    + `from the old recording. Rebuild the plugin and run npm run generate -- --editor.`
  );
}

async function main(argv) {
  const editor = argv.includes("--editor");
  const check = argv.includes("--check");
  const onlyAt = argv.indexOf("--only");
  const only = onlyAt >= 0 ? (argv[onlyAt + 1] ?? "").split(",").filter(Boolean) : null;

  if (argv.includes("--list")) {
    for (const s of STEPS) {
      console.log(`${s.name.padEnd(22)} npm run ${s.npm}${s.editor ? "   (needs an editor)" : ""}`);
      console.log(`${"".padEnd(22)} ${s.outputs.join(", ")}`);
    }
    return 0;
  }
  if (check && editor) {
    console.error("--check runs only the steps CI can run; drop --editor.");
    return 2;
  }

  let plan;
  try {
    plan = planSteps({ editor, only });
  } catch (e) {
    console.error(e.message);
    return 2;
  }

  const before = dirtyState();
  for (const step of plan.run) {
    const started = Date.now();
    if (!check) console.log(`\n[generate] ${step.name}`);
    if (!runStep(step, { quiet: check })) {
      console.error(
        `[generate] ${step.name} failed, so the steps after it did not run. `
        + `Fix it, then rerun npm run generate (or npm run ${step.npm} alone).`,
      );
      return 1;
    }
    if (check) console.log(`[generate] ${step.name} (${((Date.now() - started) / 1000).toFixed(1)}s)`);
  }
  const changed = changedBetween(before, dirtyState());

  for (const step of plan.skipped) {
    console.log(`[generate] skipped ${step.name}: ${EDITOR_NEEDED}. Run npm run generate -- --editor to include it, or npm run ${step.npm} alone.`);
  }
  const specWarning = editor ? null : await specSourceWarning();
  if (specWarning) console.warn(`[generate] ${specWarning}`);

  if (check) {
    if (changed.length > 0) {
      console.error(
        "[generate] these committed files are stale (regenerating changed them):\n"
          + changed.map((rel) => `    ${rel}`).join("\n")
          + "\n  Run npm run generate and commit what it changes.",
      );
      return 1;
    }
    console.log("[generate] every generated file is current");
    return 0;
  }

  console.log(changed.length > 0 ? `\n[generate] changed:\n${changed.map((rel) => `    ${rel}`).join("\n")}` : "\n[generate] nothing changed");
  if (changed.some((rel) => rel.startsWith("tests/golden/editor-"))) {
    console.log("[generate] the golden baseline moved: read its diff before committing it, it is the contract every client sees.");
  }
  return 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(await main(process.argv.slice(2)));
}
