// @session per-session: Every category tool is cloned per session and dispatches against the addressed editor.
import type { ToolDef } from "../surface/tool-types.js";
import { categoryTool } from "../surface/category-tool.js";
import { specBp, schema as specSchema } from "./specs/datasmith.generated.js";

// Datasmith scene import (#1383). The Datasmith plugins are optional: the
// bridge reaches them through reflection, so every action answers "Datasmith
// plugin not available" with the enable step when DatasmithImporter is off.
export const datasmithTool: ToolDef = categoryTool(
  "datasmith",
  "Datasmith scenes (Revit, SketchUp, CAD exports): inspect a .udatasmith file's options and element counts, import it into a content folder and the level, reimport a scene asset with actor sync, and query level actors by tag with their Datasmith metadata and bounds.",
  {
    status: specBp("read", "Report whether the DatasmithImporter and DatasmithContent plugins are enabled and loaded, and list the DatasmithSceneActors in the editor level with their scene asset and related actor count. Call this first.", "datasmith_status"),
    inspect: specBp("read", "Open a .udatasmith file without importing it and return the exporter header (host, vendor, product, version), element counts (meshes, mesh actors, lights, cameras, materials, textures) and every import options object with its current values. The option names it returns are what import and reimport take in options.", "datasmith_inspect"),
    import: specBp("mutate", "Import a .udatasmith file into destinationPath and the current level. Returns the DatasmithScene asset path, the spawned DatasmithSceneActor, the scene's asset counts (static meshes, materials, textures) and actor count, and the source element counts. options sets DatasmithImportOptions fields by dotted path, e.g. {\"BaseOptions.bIncludeLight\": false}. A failed import is an error that points at LogDatasmithImport. No inverse: it creates the asset set and the actors.", "datasmith_import"),
    reimport: specBp("mutate", "Reimport an existing DatasmithScene asset from its source (or from filePath when the source moved), updating its actors in the level (syncActors, default true) and optionally respawning deleted ones (respawnDeleted). Returns asset counts before and after and the per-actor changes keyed by Datasmith id: added, removed, moved and kept. No inverse.", "datasmith_reimport"),
    query_actors: specBp("read", "Find level actors by tag prefix (e.g. Revit.Element.Id.), exact tags, or a Datasmith metadata key/value, and return each one's matched tags, UDatasmithAssetUserData metadata and world bounds, plus the union bounds. Tags and bounds work without the Datasmith plugin; metadata needs it.", "datasmith_query_actors"),
  },
  {
    ...specSchema,
  },
);
