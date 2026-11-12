#!/usr/bin/env node
/**
 * Record Unreal's live AI Toolset Registry catalog to a checked-in snapshot.
 *
 *     npm run epic:record
 *
 * The catalog is the input every Epic action is generated from: its qualified
 * name, its description, and its full input JSON Schema. Reading it from a live
 * editor is the only way to obtain it, so it is recorded once and committed,
 * the same way `tests/golden/editor-connected.json` is.
 *
 * Why a snapshot rather than the live catalog at startup: an action generated
 * from a file is DECLARED. Its effect was reviewed by a person, its parameters
 * are in the category's schema, and both are in a diff somebody read. An action
 * built from whatever the editor happened to answer with is none of those
 * things, and the 830 of them were carrying an effect nobody had ever looked at.
 *
 * One snapshot per engine version, `tests/golden/epic-catalog/<major.minor>.json`,
 * because the catalog is Epic's surface and moves on Epic's cadence. A run
 * writes the attached editor's engine and leaves every other snapshot alone.
 * `tests/live/epic-catalog-drift.test.ts` compares a live editor against the
 * snapshot for its own engine, so a snapshot cannot rot quietly.
 *
 * Talks to the bridge directly rather than through the server: the server's own
 * enrichment is what this file feeds, so reading it back through that would be
 * circular.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { connectTestBridge } from "./bridge-target.mjs";
import { engineKey, snapshotPath } from "./lib/epic-catalog.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/** The EngineAssociation of the single .uproject in a project directory. */
function engineAssociationOf(projectDir) {
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

async function main() {
  // The catalog is read-only, but nothing in this repo talks to an editor it
  // has not identified.
  const bridge = await connectTestBridge({ log: console.log });
  const projectDir = bridge.projectDir;
  console.log(`target confirmed: ${projectDir}`);

  // The engine the editor reports it is running, which a source build names
  // where its .uproject holds a GUID; the descriptor is the fallback.
  const caps = await bridge.call("get_bridge_capabilities", {}).catch(() => null);
  const engine = engineKey(caps?.engineVersion) ?? engineKey(engineAssociationOf(projectDir));
  if (!engine) throw new Error("The editor reported no engine version and its .uproject names none, so there is no snapshot to write.");
  const OUT = snapshotPath(engine);

  const answered = await bridge.call("epic_list_toolsets", { includeSchemas: true });
  const toolsets = answered?.toolsets ?? [];
  if (toolsets.length === 0) {
    throw new Error(
      "The connected editor registered no toolsets. Recording an empty catalog would delete "
      + "every generated action, so nothing was written. This needs UE 5.8+ with the "
      + "ToolsetRegistry plugin enabled.",
    );
  }

  // Sorted on the way in. Unreal's registry promises the SET of toolsets, not
  // an order, so an unsorted snapshot would show a restart as a diff. The same
  // reasoning the connected golden baseline records.
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

  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, `${JSON.stringify(snapshot, null, 2)}\n`);
  console.log(`recorded ${toolCount} tools across ${sorted.length} toolsets (engine ${engine})`);
  console.log(`wrote ${path.relative(ROOT, OUT)}`);
  bridge.close();
}

main().catch((e) => {
  console.error(String(e?.message ?? e));
  process.exit(1);
});
