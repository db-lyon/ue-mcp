// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog.json
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
import { z } from "zod";
import { bp, type ActionSpec } from "../../types.js";
import { epicToolCall } from "../../epic-input.js";

const S_epic_capture_asset_image = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_capture_editor_image = {"properties":{}} as const;
const S_epic_capture_viewport = {"properties":{"captureTransform":{"type":"object"},"annotations":{"type":"object"},"bShowUI":{"type":"boolean"}}} as const;
const S_epic_focus_on_actors = {"properties":{"actors":{"type":"array"}},"required":["actors"]} as const;
const S_epic_get_camera_transform = {"properties":{}} as const;
const S_epic_get_content_browser_path = {"properties":{}} as const;
const S_epic_get_log_categories = {"properties":{"filter":{"type":"string"}},"required":["filter"]} as const;
const S_epic_get_log_entries = {"properties":{"category":{"type":"string"},"pattern":{"type":"string"},"maxEntries":{"type":"integer"}},"required":["pattern"]} as const;
const S_epic_get_open_assets = {"properties":{}} as const;
const S_epic_get_selected_actors = {"properties":{}} as const;
const S_epic_get_selected_assets = {"properties":{}} as const;
const S_epic_get_verbosity = {"properties":{"category":{"type":"string"}}} as const;
const S_epic_get_visible_actors = {"properties":{}} as const;
const S_epic_is_pierunning = {"properties":{}} as const;
const S_epic_open_editor_for_asset = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_screen_coords_to_world = {"properties":{"coords":{"type":"object"},"traceDistance":{"type":"number"}},"required":["coords"]} as const;
const S_epic_search_cvars = {"properties":{"name":{"type":"string"}},"required":["name"]} as const;
const S_epic_select_actors = {"properties":{"actors":{"type":"array"}},"required":["actors"]} as const;
const S_epic_select_assets = {"properties":{"assetPaths":{"type":"array"}},"required":["assetPaths"]} as const;
const S_epic_set_camera_transform = {"properties":{"transform":{"type":"object"}},"required":["transform"]} as const;
const S_epic_set_content_browser_path = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_set_verbosity = {"properties":{"category":{"type":"string"},"verbosity":{"type":"string"}},"required":["verbosity"]} as const;
const S_epic_start_pie = {"properties":{"options":{"type":"object"}},"required":["options"]} as const;
const S_epic_stop_pie = {"properties":{}} as const;
const S_epic_world_pos_to_screen_coords = {"properties":{"position":{"type":"object"}},"required":["position"]} as const;

/** 25 wrapped engine tools routed to the `editor` category. */
export const actions: Record<string, ActionSpec> = {
  epic_capture_asset_image: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Renders a thumbnail for the specified asset (e.g. static meshes, skeletal meshes, skeletons, animations, montages, materials, textures). Params: assetPath",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.CaptureAssetImage", S_epic_capture_asset_image, p),
  ),
  epic_capture_editor_image: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Captures an image of the entire editor application as the user sees it. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.CaptureEditorImage", S_epic_capture_editor_image, p),
  ),
  epic_capture_viewport: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Captures the level viewport with optional annotations. Annotations rendering overlays a projected 3D world-space grid plus name + position labels on visible actors. The grid is drawn at a configurable ground-plane Z and projected through the camera, with coordinate numbers at intersections (shown in meters). Each labeled actor gets a crosshair at its projected screen position with a leader-line callout placed to avoid overlap. This gives a vision-capable agent spatial awareness: it can reference grid coordinates to direct placement and identify scene contents by label. Params: captureTransform?, annotations?, bShowUI?",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.CaptureViewport", S_epic_capture_viewport, p),
  ),
  epic_focus_on_actors: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Repositions the level editor camera to focus on the specified actors. Cannot be called while PIE is active. Params: actors",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.FocusOnActors", S_epic_focus_on_actors, p),
  ),
  epic_get_camera_transform: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Returns the position and rotation of the level viewport camera. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.GetCameraTransform", S_epic_get_camera_transform, p),
  ),
  epic_get_content_browser_path: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Gets the current path of the active content browser. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.GetContentBrowserPath", S_epic_get_content_browser_path, p),
  ),
  epic_get_log_categories: bp(
    "read",
    "[Epic EditorToolset.LogsToolset] Returns a sorted list of registered log categories. Params: filter",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.LogsToolset", "EditorToolset.LogsToolset.GetLogCategories", S_epic_get_log_categories, p),
  ),
  epic_get_log_entries: bp(
    "read",
    "[Epic EditorToolset.LogsToolset] Returns log entries from the current session's log file. Params: category?, pattern, maxEntries?",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.LogsToolset", "EditorToolset.LogsToolset.GetLogEntries", S_epic_get_log_entries, p),
  ),
  epic_get_open_assets: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Gets the list of assets currently open in asset editors. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.GetOpenAssets", S_epic_get_open_assets, p),
  ),
  epic_get_selected_actors: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Gets the currently selected actors in the level editor. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.GetSelectedActors", S_epic_get_selected_actors, p),
  ),
  epic_get_selected_assets: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Gets the list of assets selected in the content browser. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.GetSelectedAssets", S_epic_get_selected_assets, p),
  ),
  epic_get_verbosity: bp(
    "read",
    "[Epic EditorToolset.LogsToolset] Returns the current verbosity level for a log category. Params: category?",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.LogsToolset", "EditorToolset.LogsToolset.GetVerbosity", S_epic_get_verbosity, p),
  ),
  epic_get_visible_actors: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Returns all actors in the current level whose bounds intersect the viewport frustum. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.GetVisibleActors", S_epic_get_visible_actors, p),
  ),
  epic_is_pierunning: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Returns whether a Play In Editor session is currently running. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.IsPIERunning", S_epic_is_pierunning, p),
  ),
  epic_open_editor_for_asset: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Opens an asset editor for the specified asset. Params: assetPath",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.OpenEditorForAsset", S_epic_open_editor_for_asset, p),
  ),
  epic_screen_coords_to_world: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Finds the world position of the nearest solid object at a given set of normalized view space coords. Params: coords, traceDistance?",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.ScreenCoordsToWorld", S_epic_screen_coords_to_world, p),
  ),
  epic_search_cvars: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Finds all console variables that contain a given name. Params: name",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.SearchCVars", S_epic_search_cvars, p),
  ),
  epic_select_actors: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Selects the specified actors in the current scene. Params: actors",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.SelectActors", S_epic_select_actors, p),
  ),
  epic_select_assets: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Selects the specified assets in the content browser. Completes once the content browser has applied the selection. Params: assetPaths",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.SelectAssets", S_epic_select_assets, p),
  ),
  epic_set_camera_transform: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Sets the position and rotation of the level viewport camera. Params: transform",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.SetCameraTransform", S_epic_set_camera_transform, p),
  ),
  epic_set_content_browser_path: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Navigates the active content browser to the specified folder path. Params: path",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.SetContentBrowserPath", S_epic_set_content_browser_path, p),
  ),
  epic_set_verbosity: bp(
    "mutate",
    "[Epic EditorToolset.LogsToolset] Sets the verbosity level for a log category. Params: category?, verbosity",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.LogsToolset", "EditorToolset.LogsToolset.SetVerbosity", S_epic_set_verbosity, p),
  ),
  epic_start_pie: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Starts a Play-In-Editor or Simulate-In-Editor session using the current level. Completes after the engine fires PostPIEStarted (session fully started, BeginPlay called) and Options.WarmupSeconds have elapsed, giving project- specific initialization (services, authentication, plugin warmup) time to settle before the agent inspects state or logs. Raises an error if a play session is already running. Params: options",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.StartPIE", S_epic_start_pie, p),
  ),
  epic_stop_pie: bp(
    "mutate",
    "[Epic EditorToolset.EditorAppToolset] Stops the currently running play session (PIE or Simulate). Raises an error if no play session is running. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.StopPIE", S_epic_stop_pie, p),
  ),
  epic_world_pos_to_screen_coords: bp(
    "read",
    "[Epic EditorToolset.EditorAppToolset] Converts a world-space position into normalized screen space based on the editor viewport camera. Params: position",
    "epic_call_tool",
    (p) => epicToolCall("EditorToolset.EditorAppToolset", "EditorToolset.EditorAppToolset.WorldPosToScreenCoords", S_epic_world_pos_to_screen_coords, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  actors: z.array(z.unknown()).optional().describe("The actors to focus the level camera on."),
  annotations: z.record(z.unknown()).optional().describe("Optional annotation overlay configuration. Only use this when you need the information in order to perform spatial actions."),
  assetPath: z.string().optional().describe("The path to the asset, e.g. '/Game/Meshes/SM_Cube'."),
  assetPaths: z.array(z.unknown()).optional().describe("The package paths of the assets to select."),
  bShowUI: z.boolean().optional().describe("If false (default), editor UI overlays such as transform gizmos and selection outlines are hidden in the captured image. Set true to capture exactly what's on…"),
  captureTransform: z.record(z.unknown()).optional().describe("Optional pose to capture from. If unset, uses the viewport's current camera."),
  category: z.string().optional().describe("If non-empty, only returns entries from this log category (e.g. \"LogTemp\")."),
  coords: z.record(z.unknown()).optional().describe("The normalized screen-space coordinates to trace from."),
  filter: z.string().optional().describe("If non-empty, only returns categories whose name contains this substring."),
  maxEntries: z.number().optional().describe("Maximum number of entries to return, taken from the end of the log. Pass 0 for no limit. Defaults to 1000."),
  name: z.string().optional().describe("The partial or full name to search for."),
  options: z.record(z.unknown()).optional().describe("Session configuration: PIE vs Simulate, play mode, optional spawn transform override, warmup duration. See FPIESessionOptions."),
  path: z.string().optional().describe("The internal path to navigate to, e.g. '/Game/Meshes'."),
  pattern: z.string().optional().describe("If non-empty, only returns entries whose text matches this regular expression."),
  position: z.record(z.unknown()).optional().describe("The world space position to convert."),
  traceDistance: z.number().optional().describe("The maximum distance to trace within the scene."),
  transform: z.record(z.unknown()).optional().describe("The transform to apply to the viewport camera."),
  verbosity: z.string().optional().describe("The verbosity level: one of \"NoLogging\", \"Fatal\", \"Error\", \"Warning\", \"Display\", \"Log\", \"Verbose\", or \"VeryVerbose\"."),
};
