// The one scan of `Registry.RegisterHandler[WithTimeout](TEXT("x"), &Fn)` calls
// in the C++ plugin. Every script that counts or exercises handlers reads it
// here, so a registration split across lines is seen by all of them or none.

import { readFileSync, readdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

export const HANDLERS_DIR = join(
  dirname(fileURLToPath(import.meta.url)),
  "..", "..", "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private", "Handlers",
);

/** Groups: 1 method name, 2 explicit class qualifier (optional), 3 function name. */
export const REGISTRATION_RE =
  /Registry\.RegisterHandler(?:WithTimeout)?\(\s*TEXT\("([^"]+)"\)\s*,\s*&(?:(F\w+)::)?(\w+)/g;

/**
 * Every file under `dir` ending in one of `exts`, subfolders included, as
 * `{ name, path }` sorted by name. Handler file names are unique across the
 * tree, so callers key on the bare name; a duplicate throws.
 * @param {string} [dir]
 * @param {string[]} [exts]
 * @returns {Array<{ name: string, path: string }>}
 */
export function listHandlerFiles(dir = HANDLERS_DIR, exts = [".cpp"]) {
  /** @type {Array<{ name: string, path: string }>} */
  const out = [];
  /** @param {string} at */
  const walk = (at) => {
    for (const entry of readdirSync(at, { withFileTypes: true })) {
      const full = join(at, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (exts.some((ext) => entry.name.endsWith(ext))) out.push({ name: entry.name, path: full });
    }
  };
  walk(dir);
  out.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  for (let i = 1; i < out.length; i++) {
    if (out[i].name === out[i - 1].name) {
      throw new Error(`Two handler files are named ${out[i].name}: ${out[i - 1].path} and ${out[i].path}`);
    }
  }
  return out;
}

/** The absolute path of one handler file, found by name in whichever subfolder holds it.
 * @param {string} name
 * @param {string} [dir]
 * @returns {string} */
export function handlerFilePath(name, dir = HANDLERS_DIR) {
  const hit = listHandlerFiles(dir, [name.slice(name.lastIndexOf("."))]).find((f) => f.name === name);
  if (!hit) throw new Error(`No handler file named ${name} under ${dir}`);
  return hit.path;
}

/** The text of one handler file, found by name.
 * @param {string} name
 * @param {string} [dir]
 * @returns {string} */
export function readHandlerFile(name, dir = HANDLERS_DIR) {
  return readFileSync(handlerFilePath(name, dir), "utf8");
}

/** Every registration, in file order: `{ method, className, fn, file, index }`.
 *  `file` is the bare file name; `className` is only set when the registration
 *  qualifies the function. */
export function listRegistrations(dir = HANDLERS_DIR) {
  const out = [];
  for (const { name: file, path } of listHandlerFiles(dir)) {
    const body = readFileSync(path, "utf8");
    for (const m of body.matchAll(REGISTRATION_RE)) {
      out.push({ method: m[1], className: m[2] ?? null, fn: m[3], file, index: m.index });
    }
  }
  return out;
}
