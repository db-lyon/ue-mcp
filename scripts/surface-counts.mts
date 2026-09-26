// Emit a single-line summary of the bridge surface for CI to post as a
// commit status. Derived from the source of truth (TS schema + C++
// RegisterHandler calls), not copy in package.json / .uplugin.
//
// Output (single line, no trailing newline):
//   "<tools> tools · <actions> actions · <bridge> bridge methods · <cpp> cpp handlers"
import { ALL_TOOLS, enumerateBridgeActions } from "../src/tools.js";
import { listRegistrations } from "./lib/cpp-registrations.mjs";

let totalActions = 0;
for (const t of ALL_TOOLS) totalActions += Object.keys(t.actions).length;

const bridgeActions = enumerateBridgeActions().length;
const handlerNames = new Set(listRegistrations().map((r: { method: string }) => r.method));

const line = `${ALL_TOOLS.length} tools · ${totalActions} actions · ${bridgeActions} bridge methods · ${handlerNames.size} cpp handlers`;
process.stdout.write(line);
