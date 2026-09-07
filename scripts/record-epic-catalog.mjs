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
 * The snapshot is keyed by engine version, because the catalog is Epic's
 * surface and moves on Epic's cadence. `tests/live/epic-catalog-drift.test.ts`
 * compares a live editor against the recorded file and fails when they differ,
 * so the snapshot cannot rot quietly.
 *
 * Talks to the bridge directly rather than through the server: the server's own
 * enrichment is what this file feeds, so reading it back through that would be
 * circular.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import WebSocket from "ws";
import {
  assertLoopbackHost,
  assertTestProjectDir,
  bridgePortCandidates,
  describeMissingBridge,
  extractReportedProjectDir,
  PROJECT_IDENTITY_PYTHON,
} from "./bridge-target.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const OUT = path.join(ROOT, "tests", "golden", "epic-catalog.json");
const HOST = "127.0.0.1";
const TIMEOUT_MS = 60_000;

function connect(url, timeoutMs) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const timer = setTimeout(() => {
      ws.terminate();
      reject(new Error(`Connection to ${url} timed out after ${timeoutMs}ms`));
    }, timeoutMs);
    ws.on("open", () => { clearTimeout(timer); resolve(ws); });
    ws.on("error", (err) => { clearTimeout(timer); reject(err); });
  });
}

let nextId = 1;
function rpc(ws, method, params) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), TIMEOUT_MS);
    const onMessage = (raw) => {
      let msg;
      try { msg = JSON.parse(raw.toString()); } catch { return; }
      if (msg.id !== id) return;
      clearTimeout(timer);
      ws.off("message", onMessage);
      resolve(msg);
    };
    ws.on("message", onMessage);
    ws.send(JSON.stringify({ id, method, params }));
  });
}

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
  assertLoopbackHost(HOST);
  const { candidates, lockfile } = bridgePortCandidates({});
  let ws = null;
  let lastError = null;
  for (const c of candidates) {
    try {
      ws = await connect(`ws://${HOST}:${c.port}`, 3000);
      console.log(`connected to ws://${HOST}:${c.port} (${c.source})`);
      break;
    } catch (e) {
      lastError = e.message;
    }
  }
  if (!ws) throw new Error(describeMissingBridge({ host: HOST, candidates, lockfile, lastError }));

  // The catalog is read-only, but confirming the target keeps this consistent
  // with every other script that attaches to an editor: nothing in this repo
  // talks to an editor it has not identified.
  const ident = await rpc(ws, "execute_python", { code: PROJECT_IDENTITY_PYTHON });
  if (ident.error) throw new Error(`could not identify the connected editor: ${ident.error.message}`);
  const projectDir = assertTestProjectDir(extractReportedProjectDir(ident.result));
  console.log(`target confirmed: ${projectDir}`);

  // Off the .uproject the identified editor has open, rather than over the
  // bridge: the snapshot is keyed by engine version, and the descriptor is the
  // thing that decides which engine this editor IS.
  const engine = engineAssociationOf(projectDir);

  const answered = await rpc(ws, "epic_list_toolsets", { includeSchemas: true });
  if (answered.error) throw new Error(`epic_list_toolsets failed: ${answered.error.message}`);
  const toolsets = answered.result?.toolsets ?? [];
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
  ws.close();
}

main().catch((e) => {
  console.error(String(e?.message ?? e));
  process.exit(1);
});
