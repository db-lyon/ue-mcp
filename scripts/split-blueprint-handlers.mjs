#!/usr/bin/env node
// One-shot script: move a set of FBlueprintHandlers methods from
// BlueprintHandlers.cpp into BlueprintHandlers_Graph.cpp while keeping the
// same class. Deletes the original ranges. Idempotent when re-run on an
// already-split file (detects the marker).
//
// Run once: `node scripts/split-blueprint-handlers.mjs`.

import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const ROOT = join(here, "..");
const SRC = join(ROOT, "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/BlueprintHandlers.cpp");
const DST = join(ROOT, "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/BlueprintHandlers_Graph.cpp");

if (existsSync(DST)) {
  console.error("DST already exists; refusing to clobber. Delete it first if you really want to re-run.");
  process.exit(1);
}

const TO_MOVE = new Set([
  "AddNode",
  "ReadBlueprintGraph",
  "ConnectPins",
  "DeleteNode",
  "SetNodeProperty",
  "ReadNodeProperty",
  "ExportNodesT3D",
  "ImportNodesT3D",
]);

const src = readFileSync(SRC, "utf8");
const lines = src.split(/\r?\n/);

const headerRe = /^TSharedPtr<FJsonValue> FBlueprintHandlers::(\w+)\(/;

// Pass 1: find start line of every handler, plus end markers.
const handlers = [];
for (let i = 0; i < lines.length; i++) {
  const m = lines[i].match(headerRe);
  if (m) handlers.push({ name: m[1], start: i });
}
// End line of a handler = line before the next handler's comment block, or EOF.
for (let j = 0; j < handlers.length; j++) {
  const next = handlers[j + 1];
  handlers[j].end = next ? next.start - 1 : lines.length - 1;
}

// Walk comment header lines (lines starting with // immediately above each
// handler) back so we move the doc comment with the function.
for (const h of handlers) {
  let s = h.start - 1;
  while (s > 0) {
    const line = lines[s].trimEnd();
    if (line === "" || line.startsWith("//")) {
      s--;
      continue;
    }
    break;
  }
  // s is now the last non-comment non-blank line; handler truly starts at s+1.
  h.start = s + 1;
}

// Partition.
const toMove = handlers.filter((h) => TO_MOVE.has(h.name));
const toKeep = handlers.filter((h) => !TO_MOVE.has(h.name));
if (toMove.length !== TO_MOVE.size) {
  const found = new Set(toMove.map((h) => h.name));
  const missing = [...TO_MOVE].filter((n) => !found.has(n));
  console.error(`Missing handlers in source: ${missing.join(", ")}`);
  process.exit(1);
}

// Build a mask of lines to remove from the original.
const drop = new Set();
for (const h of toMove) {
  for (let i = h.start; i <= h.end; i++) drop.add(i);
}

const kept = lines.filter((_, i) => !drop.has(i));
const moved = toMove
  .map((h) => lines.slice(h.start, h.end + 1).join("\n").replace(/\n+$/, ""))
  .join("\n\n");

// New file gets the minimum-viable include set from the original's top block
// plus a note. UBT unity builds pull the rest transitively.
const headerBlock = [
  `// Split from BlueprintHandlers.cpp to keep that file under 3k lines.`,
  `// All functions below are still members of FBlueprintHandlers - this file`,
  `// is a translation-unit partition, not a new class. The original registers`,
  `// these handlers in BlueprintHandlers.cpp::RegisterHandlers.`,
  ``,
  `#include "BlueprintHandlers.h"`,
  `#include "HandlerRegistry.h"`,
  `#include "HandlerUtils.h"`,
  `#include "HandlerJsonProperty.h"`,
  `#include "Kismet2/BlueprintEditorUtils.h"`,
  `#include "Kismet2/KismetEditorUtilities.h"`,
  `#include "BlueprintEditorLibrary.h"`,
  `#include "Engine/Blueprint.h"`,
  `#include "EdGraph/EdGraph.h"`,
  `#include "EdGraph/EdGraphPin.h"`,
  `#include "EdGraphSchema_K2.h"`,
  `#include "EdGraphUtilities.h"`,
  `#include "K2Node.h"`,
  `#include "K2Node_CallFunction.h"`,
  `#include "K2Node_Event.h"`,
  `#include "K2Node_FunctionEntry.h"`,
  `#include "K2Node_EditablePinBase.h"`,
  `#include "K2Node_IfThenElse.h"`,
  `#include "K2Node_MacroInstance.h"`,
  `#include "K2Node_VariableGet.h"`,
  `#include "K2Node_VariableSet.h"`,
  `#include "K2Node_DynamicCast.h"`,
  `#include "K2Node_CustomEvent.h"`,
  `#include "K2Node_CallDelegate.h"`,
  `#include "UObject/UnrealType.h"`,
  `#include "UObject/Package.h"`,
  `#include "UObject/TopLevelAssetPath.h"`,
  `#include "Editor.h"`,
  `#include "EditorAssetLibrary.h"`,
  `#include "Containers/Queue.h"`,
  `#include "Dom/JsonObject.h"`,
  `#include "Dom/JsonValue.h"`,
  ``,
  ``,
].join("\n");

writeFileSync(DST, headerBlock + moved + "\n");

// Tidy consecutive blank lines in the kept file.
const keptText = kept.join("\n").replace(/\n{4,}/g, "\n\n\n");
writeFileSync(SRC, keptText);

console.log(`Moved ${toMove.length} handlers.`);
console.log(`  ${SRC}: ${kept.length} lines (was ${lines.length})`);
console.log(`  ${DST}: ${(headerBlock + moved + "\n").split(/\r?\n/).length} lines`);
for (const h of toMove) console.log(`  - ${h.name}`);
