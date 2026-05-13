// Emit a single-line summary of the bridge surface for CI to post as a
// commit status. Stays accurate at every push because the numbers are
// derived from the source of truth (TS schema + C++ RegisterHandler calls),
// not copy in package.json / .uplugin.
//
// Output line (single line, no trailing newline):
//   "<tools> tools · <total> actions · <bridge> bridge methods"
//
// Examples:
//   20 tools · 502 actions · 477 bridge methods
import { readFileSync, readdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { ALL_TOOLS, enumerateBridgeActions } from "../src/tools.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(__dirname, "..");

let totalActions = 0;
for (const t of ALL_TOOLS) totalActions += Object.keys(t.actions).length;

const bridgeActions = enumerateBridgeActions().length;

// Count unique RegisterHandler(TEXT("...")) names across every Handlers cpp.
const handlersDir = join(
  repoRoot,
  "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers",
);
const handlerRe = /RegisterHandler\(TEXT\("([^"]+)"\)/g;
const handlerNames = new Set<string>();
for (const file of readdirSync(handlersDir)) {
  if (!file.endsWith(".cpp")) continue;
  const body = readFileSync(join(handlersDir, file), "utf8");
  for (const m of body.matchAll(handlerRe)) handlerNames.add(m[1]);
}

const line = `${ALL_TOOLS.length} tools · ${totalActions} actions · ${bridgeActions} bridge methods · ${handlerNames.size} cpp handlers`;
process.stdout.write(line);
