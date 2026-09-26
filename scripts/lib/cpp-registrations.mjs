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

/** Every registration, in file order: `{ method, className, fn, file, index }`.
 *  `className` is only set when the registration qualifies the function. */
export function listRegistrations(dir = HANDLERS_DIR) {
  const out = [];
  for (const file of readdirSync(dir).filter((f) => f.endsWith(".cpp")).sort()) {
    const body = readFileSync(join(dir, file), "utf8");
    for (const m of body.matchAll(REGISTRATION_RE)) {
      out.push({ method: m[1], className: m[2] ?? null, fn: m[3], file, index: m.index });
    }
  }
  return out;
}
