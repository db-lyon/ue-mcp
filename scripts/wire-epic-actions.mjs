#!/usr/bin/env node
/**
 * One-shot: fold the generated Epic action modules into their category tools.
 *
 * Run once to perform the edit; the result is committed source. Kept in the
 * tree because the edit it makes is precise and worth being able to re-read:
 * for each category that Unreal contributes tools to, it adds the import and
 * two spreads that make those actions part of the category's own declaration.
 *
 *   actions      the Epic actions come AFTER the native ones, so the action
 *                enum keeps its authored order and the additions are grouped.
 *   extraSchema  the Epic parameters come FIRST, so a native declaration wins
 *                any name they share. 78 names collide (`assetPath`, `name`,
 *                `recursive`...), and the native type is the one the wire has
 *                always accepted; `resolveEpicToolInput` coerces a flat string
 *                into the engine's `{refPath}` shape anyway.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { maskLiterals, topLevelArgs } from "./lib/tool-source.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const TOOLS = path.join(ROOT, "src", "tools");
const GEN = path.join(TOOLS, "epic");

const generated = fs.readdirSync(GEN)
  .filter((f) => f.endsWith(".generated.ts"))
  .map((f) => f.replace(".generated.ts", ""));

let wired = 0;
const missing = [];

for (const category of generated) {
  const file = path.join(TOOLS, `${category}.ts`);
  if (!fs.existsSync(file)) { missing.push(category); continue; }

  const raw = fs.readFileSync(file, "utf8");
  const crlf = raw.includes("\r\n");
  let src = raw.replace(/\r\n/g, "\n");

  if (src.includes(`./epic/${category}.generated.js`)) {
    console.log(`${category}: already wired`);
    continue;
  }

  const masked = maskLiterals(src);
  const call = masked.indexOf("categoryTool(");
  if (call === -1) throw new Error(`${category}: no categoryTool call`);
  const args = topLevelArgs(masked, masked.indexOf("(", call));
  if (!args || args.length < 3) throw new Error(`${category}: could not read categoryTool arguments`);

  // Splice from the back so earlier offsets stay valid.
  const edits = [];

  // extraSchema is the fifth argument. Absent on a category that declares no
  // parameters of its own, in which case one is added.
  if (args.length >= 5) {
    const brace = masked.indexOf("{", args[4][0]);
    if (brace === -1 || brace > args[4][1]) throw new Error(`${category}: fifth argument is not an object literal`);
    edits.push({ at: brace + 1, text: `\n    ...epicSchema,` });
  } else {
    const closeParen = args[args.length - 1][1];
    const pad = args.length === 3 ? ",\n  undefined,\n  { ...epicSchema },\n" : ",\n  { ...epicSchema },\n";
    edits.push({ at: closeParen, text: pad });
  }

  // The actions record is the third argument.
  const actionsEnd = lastBraceBefore(masked, args[2][1]);
  if (actionsEnd === -1) throw new Error(`${category}: could not find the end of the actions record`);
  edits.push({ at: actionsEnd, text: `    ...epicActions,\n` });

  edits.sort((a, b) => b.at - a.at);
  for (const e of edits) src = src.slice(0, e.at) + e.text + src.slice(e.at);

  // The import goes after the last existing import line.
  const lines = src.split("\n");
  let lastImport = -1;
  for (let i = 0; i < lines.length; i++) if (/^import .*;$/.test(lines[i])) lastImport = i;
  lines.splice(
    lastImport + 1,
    0,
    `import { actions as epicActions, schema as epicSchema } from "./epic/${category}.generated.js";`,
  );
  src = lines.join("\n");

  fs.writeFileSync(file, crlf ? src.replace(/\n/g, "\r\n") : src);
  console.log(`${category}: wired`);
  wired++;
}

/** Offset of the line start of the `}` closing an object literal ending at `end`. */
function lastBraceBefore(masked, end) {
  for (let i = end; i >= 0; i--) {
    if (masked[i] === "}") {
      let j = i - 1;
      while (j >= 0 && (masked[j] === " " || masked[j] === "\t")) j--;
      return j + 1;
    }
  }
  return -1;
}

console.log(`\nwired ${wired} categor${wired === 1 ? "y" : "ies"}`);
if (missing.length > 0) {
  console.log(`no native category file for: ${missing.join(", ")} (these need a category module of their own)`);
}
