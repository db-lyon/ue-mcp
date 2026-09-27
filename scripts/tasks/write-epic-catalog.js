/**
 * Write Unreal's AI Toolset Registry catalog to tests/golden/epic-catalog.json,
 * keyed by the engine the project declares. The write step of the repo's
 * epic_record flow (tests/ue_mcp/ue-mcp.yml); `toolsets` is what
 * epic_list_toolsets answered. Plain ESM so the runner can import it as is.
 *
 * A snapshot rather than the live catalog at startup: an action generated from
 * a file is declared, its effect reviewed and in a diff. The drift test in
 * tests/live compares a live editor against it.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { BaseTask } from "@db-lyon/flowkit";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
export const EPIC_CATALOG_OUT = path.join(ROOT, "tests", "golden", "epic-catalog.json");

/** The EngineAssociation of the single .uproject in a project directory. */
export function engineAssociationOf(projectDir) {
  const dir = projectDir.replace(/[\\/]+$/, "");
  const uproject = fs.readdirSync(dir).find((f) => f.endsWith(".uproject"));
  if (!uproject) throw new Error(`no .uproject in ${dir}, so the engine version cannot be read`);
  const descriptor = JSON.parse(fs.readFileSync(path.join(dir, uproject), "utf8"));
  const engine = descriptor.EngineAssociation;
  if (typeof engine !== "string" || engine.trim() === "") {
    throw new Error(
      `${uproject} declares no EngineAssociation. The snapshot is keyed by engine version, so `
      + "recording one without it would make the drift check meaningless.",
    );
  }
  return engine;
}

export default class WriteEpicCatalog extends BaseTask {
  get taskName() {
    return "write_epic_catalog";
  }

  async execute() {
    // Off the .uproject the identified editor has open: the descriptor decides which engine it is.
    const projectDir = this.ctx.project?.projectDir;
    if (!projectDir) throw new Error("no project is loaded, so the engine version cannot be read");
    const engine = engineAssociationOf(projectDir);

    const toolsets = Array.isArray(this.options.toolsets) ? this.options.toolsets : [];
    if (toolsets.length === 0) {
      throw new Error(
        "The connected editor registered no toolsets. Recording an empty catalog would delete "
        + "every generated action, so nothing was written. This needs UE 5.8+ with the "
        + "ToolsetRegistry plugin enabled.",
      );
    }

    // Sorted on the way in: the registry promises the set of toolsets, not an order.
    const sorted = [...toolsets]
      .map((ts) => ({ ...ts, tools: [...(ts.tools ?? [])].sort((a, b) => a.name.localeCompare(b.name)) }))
      .sort((a, b) => a.name.localeCompare(b.name));
    const toolCount = sorted.reduce((n, ts) => n + (ts.tools?.length ?? 0), 0);
    const snapshot = {
      engineAssociation: engine,
      recordedToolsets: sorted.length,
      recordedTools: toolCount,
      toolsets: sorted,
    };

    const out = this.options.out ?? EPIC_CATALOG_OUT;
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, `${JSON.stringify(snapshot, null, 2)}\n`);
    return {
      success: true,
      data: { recordedTools: toolCount, recordedToolsets: sorted.length, engine, wrote: path.relative(ROOT, out) },
    };
  }
}
