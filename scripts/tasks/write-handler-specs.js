/**
 * Write the handler specs the running plugin published (#1057) to
 * tests/golden/handler-specs.json. The write step of the repo's specs_record
 * flow (tests/ue_mcp/ue-mcp.yml); `specs` is what get_bridge_capabilities
 * answered under handlerSpecs. Plain ESM so the runner can import it as is.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { BaseTask } from "@db-lyon/flowkit";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
export const HANDLER_SPECS_OUT = path.join(ROOT, "tests", "golden", "handler-specs.json");

export default class WriteHandlerSpecs extends BaseTask {
  get taskName() {
    return "write_handler_specs";
  }

  async execute() {
    const specs = this.options.specs;
    if (!specs || typeof specs !== "object" || Object.keys(specs).length === 0) {
      throw new Error(
        "The connected plugin published no handler specs. Recording that would delete every generated "
        + "action, so nothing was written. Rebuild the plugin from this checkout and restart the editor.",
      );
    }
    // Sorted on the way in, so a registration order change is not a diff.
    const handlers = Object.fromEntries(Object.keys(specs).sort().map((m) => [m, specs[m]]));
    const snapshot = { handlerCount: Object.keys(handlers).length, handlers };
    const out = this.options.out ?? HANDLER_SPECS_OUT;
    fs.writeFileSync(out, `${JSON.stringify(snapshot, null, 2)}\n`);
    return {
      success: true,
      data: { handlerCount: snapshot.handlerCount, wrote: path.relative(ROOT, out), next: "npm run specs:generate" },
    };
  }
}
