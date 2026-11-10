// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog/
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../surface/category-tool.js";

const S_epic_add_assets_to_collection = {"properties":{"collection":{"type":"object","properties":{"name":{"type":"string"},"shareType":{"type":"string","enum":["Local","Private","Shared"]}}},"assetPaths":{"type":"array","items":{"type":"string"}}},"required":["collection","assetPaths"]} as const;
const S_epic_call_cheat = {"properties":{"cheatName":{"type":"string"},"args":{"type":"string"}},"required":["cheatName","args"]} as const;
const S_epic_capture = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"camera":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"projectionMode":{"type":"string","enum":["Perspective","Orthographic"]},"fOV":{"type":"number"},"orthoWidth":{"type":"number"},"perspectiveNearClipPlane":{"type":"number"},"orthographicClipPlanes":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}}},"mode":{"type":"string"},"options":{"type":"object","properties":{"width":{"type":"integer"},"height":{"type":"integer"},"screenPercentage":{"type":"number"},"visualizationSubMode":{"type":"string"}}}},"required":["world","camera","mode","options"]} as const;
const S_epic_capture_active_editor_view = {"properties":{"mode":{"type":"string"},"options":{"type":"object","properties":{"width":{"type":"integer"},"height":{"type":"integer"},"screenPercentage":{"type":"number"},"visualizationSubMode":{"type":"string"}}}},"required":["mode","options"]} as const;
const S_epic_capture_asset_image = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_capture_editor_image = {"properties":{}} as const;
const S_epic_capture_editor_viewport = {"properties":{"captureTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"annotations":{"type":"object","properties":{"gridSpacing":{"type":"number"},"gridExtent":{"type":"number"},"gridHeight":{"type":"number"},"maxLabelDistance":{"type":"number"},"classFilter":{"type":"object","properties":{"refPath":{}}},"maxLabels":{"type":"integer"}}},"bShowUI":{"type":"boolean"},"viewportId":{"type":"string"}}} as const;
const S_epic_capture_game_viewport = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"maxResolution":{"type":"integer"}},"required":["world","maxResolution"]} as const;
const S_epic_capture_viewport = {"properties":{"captureTransform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"annotations":{"type":"object","properties":{"gridSpacing":{"type":"number"},"gridExtent":{"type":"number"},"gridHeight":{"type":"number"},"maxLabelDistance":{"type":"number"},"classFilter":{"type":"object","properties":{"refPath":{}}},"maxLabels":{"type":"integer"}}},"bShowUI":{"type":"boolean"}}} as const;
const S_epic_center_view = {"properties":{"time":{"type":"number"}},"required":["time"]} as const;
const S_epic_close_editor_for_asset = {"properties":{"assetPath":{"type":"string"},"unsavedChanges":{"type":"string","enum":["Fail","Save","Discard"]}},"required":["assetPath","unsavedChanges"]} as const;
const S_epic_create_collection = {"properties":{"collectionName":{"type":"string"},"shareType":{"type":"string","enum":["Local","Private","Shared"]}},"required":["collectionName","shareType"]} as const;
const S_epic_describe_mode = {"properties":{"mode":{"type":"string"}},"required":["mode"]} as const;
const S_epic_destroy_collection = {"properties":{"collection":{"type":"object","properties":{"name":{"type":"string"},"shareType":{"type":"string","enum":["Local","Private","Shared"]}}}},"required":["collection"]} as const;
const S_epic_execute_python = {"properties":{"script":{"type":"object","properties":{"content":{"type":"string"},"language":{"type":"string","enum":["Unspecified","Cpp","Verse","Python","Markdown","HTML","CSS","JavaScript","TypeScript"]}}}},"required":["script"]} as const;
const S_epic_exit_editor = {"properties":{"bDiscardUnsavedChanges":{"type":"boolean"}}} as const;
const S_epic_focus_on_actors = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["actors"]} as const;
const S_epic_get_active_editor_modes = {"properties":{}} as const;
const S_epic_get_active_editor_view = {"properties":{"viewportId":{"type":"string"}}} as const;
const S_epic_get_asset_editor_selection = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}}},"required":["asset"]} as const;
const S_epic_get_asset_thumbnails = {"properties":{"assetPaths":{"type":"array","items":{"type":"string"}}},"required":["assetPaths"]} as const;
const S_epic_get_camera_transform = {"properties":{}} as const;
const S_epic_get_collection_assets = {"properties":{"collection":{"type":"object","properties":{"name":{"type":"string"},"shareType":{"type":"string","enum":["Local","Private","Shared"]}}}},"required":["collection"]} as const;
const S_epic_get_content_browser_path = {"properties":{}} as const;
const S_epic_get_csv_info = {"properties":{"path":{"type":"string"},"statNameFilter":{"type":"string"},"bIncludeAllMetadata":{"type":"boolean"},"groupDepth":{"type":"integer"}},"required":["path"]} as const;
const S_epic_get_cvar_value = {"properties":{"name":{"type":"string"}},"required":["name"]} as const;
const S_epic_get_frame_timings = {"properties":{}} as const;
const S_epic_get_frame_tree = {"properties":{"minInclusiveMs":{"type":"number"},"maxDepth":{"type":"integer"},"pathFilter":{"type":"string"}},"required":["minInclusiveMs","maxDepth","pathFilter"]} as const;
const S_epic_get_log_categories = {"properties":{"filter":{"type":"string"}},"required":["filter"]} as const;
const S_epic_get_log_categories__1 = {"properties":{"filter":{"type":"string"},"logFilePath":{"type":"string"}}} as const;
const S_epic_get_log_entries = {"properties":{"category":{"type":"string"},"pattern":{"type":"string"},"maxEntries":{"type":"integer"}},"required":["pattern"]} as const;
const S_epic_get_log_entries__1 = {"properties":{"logCategory":{"type":"string"},"pattern":{"type":"string"},"maxEntries":{"type":"integer"},"logFilePath":{"type":"string"},"maxCharsPerEntry":{"type":"integer"}}} as const;
const S_epic_get_object_events = {"properties":{"objectId":{"type":"integer"},"startTime":{"type":"number"},"endTime":{"type":"number"}},"required":["objectId","startTime","endTime"]} as const;
const S_epic_get_object_properties = {"properties":{"objectId":{"type":"integer"},"time":{"type":"number"}},"required":["objectId","time"]} as const;
const S_epic_get_open_assets = {"properties":{}} as const;
const S_epic_get_pivot_location = {"properties":{}} as const;
const S_epic_get_recent_transactions = {"properties":{"count":{"type":"integer"},"offset":{"type":"integer"}}} as const;
const S_epic_get_selected_actors = {"properties":{}} as const;
const S_epic_get_selected_assets = {"properties":{}} as const;
const S_epic_get_selected_object = {"properties":{}} as const;
const S_epic_get_selected_outliner_folders = {"properties":{}} as const;
const S_epic_get_session_info = {"properties":{}} as const;
const S_epic_get_show_flag = {"properties":{"flagName":{"type":"string"}},"required":["flagName"]} as const;
const S_epic_get_stat_counters = {"properties":{"groupName":{"type":"string"},"bIncludeZero":{"type":"boolean"}},"required":["groupName","bIncludeZero"]} as const;
const S_epic_get_symbol = {"properties":{"name":{"type":"string"}},"required":["name"]} as const;
const S_epic_get_trace_event_details = {"properties":{"source":{"type":"string"},"eventId":{"type":"string"}},"required":["source","eventId"]} as const;
const S_epic_get_transaction_details = {"properties":{"transactionId":{"type":"string"},"maxObjects":{"type":"integer"}},"required":["transactionId"]} as const;
const S_epic_get_transaction_recency_index = {"properties":{"transactionId":{"type":"string"}},"required":["transactionId"]} as const;
const S_epic_get_verbosity = {"properties":{"category":{"type":"string"}}} as const;
const S_epic_get_verbosity__1 = {"properties":{"logCategory":{"type":"string"}},"required":["logCategory"]} as const;
const S_epic_get_video_info = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_get_visible_actors = {"properties":{}} as const;
const S_epic_is_pierunning = {"properties":{}} as const;
const S_epic_list_cheats = {"properties":{"nameFilter":{"type":"string"}}} as const;
const S_epic_list_collections = {"properties":{}} as const;
const S_epic_list_editor_modes = {"properties":{}} as const;
const S_epic_list_editor_viewports = {"properties":{}} as const;
const S_epic_list_events = {"properties":{"path":{"type":"string"},"count":{"type":"integer"},"frameStart":{"type":"integer"},"frameEnd":{"type":"integer"}},"required":["path"]} as const;
const S_epic_list_modes = {"properties":{"bIncludeUnavailable":{"type":"boolean"}},"required":["bIncludeUnavailable"]} as const;
const S_epic_list_object_trace_events = {"properties":{"objectId":{"type":"integer"},"startTime":{"type":"number"},"endTime":{"type":"number"},"sourceFilter":{"type":"string"},"includedNameFilter":{"type":"string"},"excludedNameFilter":{"type":"string"},"limit":{"type":"integer"},"offset":{"type":"integer"}},"required":["objectId","startTime","endTime","sourceFilter","includedNameFilter","excludedNameFilter","limit","offset"]} as const;
const S_epic_list_objects = {"properties":{"className":{"type":"string"}}} as const;
const S_epic_list_show_flags = {"properties":{}} as const;
const S_epic_list_stat_groups = {"properties":{}} as const;
const S_epic_list_viewport_recordings = {"properties":{}} as const;
const S_epic_list_views = {"properties":{"world":{"type":"object","properties":{"refPath":{}}}},"required":["world"]} as const;
const S_epic_list_worlds = {"properties":{}} as const;
const S_epic_open_editor_for_asset = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_read_image_file = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_read_me = {"properties":{}} as const;
const S_epic_read_stat_values = {"properties":{"path":{"type":"string"},"statName":{"type":"string"},"frameStart":{"type":"integer"},"count":{"type":"integer"}},"required":["path","statName"]} as const;
const S_epic_record_viewport = {"properties":{"options":{"type":"object","properties":{"source":{"type":"string","enum":["GameViewport","EditorViewport"]},"world":{"type":"object","properties":{"refPath":{}}},"viewportId":{"type":"string"},"maxResolution":{"type":"integer"},"frameRate":{"type":"number"},"maxDurationSeconds":{"type":"number"}}},"durationSeconds":{"type":"number"}},"required":["options","durationSeconds"]} as const;
const S_epic_reduce_stat = {"properties":{"path":{"type":"string"},"statNames":{"type":"array","items":{"type":"string"}},"frameStart":{"type":"integer"},"frameEnd":{"type":"integer"}},"required":["path","statNames"]} as const;
const S_epic_remove_assets_from_collection = {"properties":{"collection":{"type":"object","properties":{"name":{"type":"string"},"shareType":{"type":"string","enum":["Local","Private","Shared"]}}},"assetPaths":{"type":"array","items":{"type":"string"}}},"required":["collection","assetPaths"]} as const;
const S_epic_run_console_command = {"properties":{"command":{"type":"string"}},"required":["command"]} as const;
const S_epic_sample_frames = {"properties":{"path":{"type":"string"},"startSeconds":{"type":"number"},"endSeconds":{"type":"number"},"maxFrames":{"type":"integer"},"maxEdge":{"type":"integer"}},"required":["path","startSeconds","endSeconds"]} as const;
const S_epic_screen_coords_to_world = {"properties":{"coords":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"traceDistance":{"type":"number"}},"required":["coords"]} as const;
const S_epic_search_console_commands = {"properties":{"name":{"type":"string"}},"required":["name"]} as const;
const S_epic_search_cvars = {"properties":{"name":{"type":"string"}},"required":["name"]} as const;
const S_epic_search_cvars__console_toolset = {"properties":{"name":{"type":"string"}},"required":["name"]} as const;
const S_epic_search_symbols = {"properties":{"pattern":{"type":"string"},"max_results":{"type":"integer"},"match":{"type":"string","enum":["NAME","SIGNATURE","DOCUMENTATION"]},"kinds":{"type":"array","items":{"type":"string","enum":["CLASS","STRUCT","ENUM","DELEGATE","FUNCTION","METHOD","PROPERTY","TOOL"]}},"accepts":{"type":"string"},"returns":{"type":"string"}}} as const;
const S_epic_search_transactions = {"properties":{"query":{"type":"string"},"count":{"type":"integer"},"offset":{"type":"integer"}},"required":["query"]} as const;
const S_epic_select_actors = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["actors"]} as const;
const S_epic_select_asset_editor_objects = {"properties":{"asset":{"type":"object","properties":{"refPath":{}}},"objects":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["asset","objects"]} as const;
const S_epic_select_assets = {"properties":{"assetPaths":{"type":"array","items":{"type":"string"}}},"required":["assetPaths"]} as const;
const S_epic_select_object = {"properties":{"objectId":{"type":"integer"}},"required":["objectId"]} as const;
const S_epic_select_outliner_folders = {"properties":{"folderPaths":{"type":"array","items":{"type":"string"}}},"required":["folderPaths"]} as const;
const S_epic_set_camera_transform = {"properties":{"transform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}}}} as const;
const S_epic_set_content_browser_path = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_set_cvar_value = {"properties":{"name":{"type":"string"},"value":{"type":"string"}},"required":["name","value"]} as const;
const S_epic_set_editor_mode = {"properties":{"modeId":{"type":"string"}},"required":["modeId"]} as const;
const S_epic_set_pivot_location = {"properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"bSnapToGrid":{"type":"boolean"}},"required":["location"]} as const;
const S_epic_set_scrub_time = {"properties":{"time":{"type":"number"}},"required":["time"]} as const;
const S_epic_set_show_flag = {"properties":{"flagName":{"type":"string"},"bEnabled":{"type":"boolean"}},"required":["flagName","bEnabled"]} as const;
const S_epic_set_verbosity = {"properties":{"category":{"type":"string"},"verbosity":{"type":"string"}},"required":["verbosity"]} as const;
const S_epic_set_verbosity__1 = {"properties":{"logCategory":{"type":"string"},"verbosity":{"type":"string"}},"required":["logCategory","verbosity"]} as const;
const S_epic_set_viewport_view_mode = {"properties":{"viewMode":{"type":"string","enum":["VMI_BrushWireframe","VMI_Wireframe","VMI_Unlit","VMI_Lit","VMI_Lit_DetailLighting","VMI_LightingOnly","VMI_LightComplexity","VMI_ShaderComplexity","VMI_LightmapDensity","VMI_LitLightmapDensity","VMI_ReflectionOverride","VMI_VisualizeBuffer","VMI_StationaryLightOverlap","VMI_CollisionPawn","VMI_CollisionVisibility","VMI_LODColoration","VMI_QuadOverdraw","VMI_PrimitiveDistanceAccuracy","VMI_MeshUVDensityAccuracy","VMI_ShaderComplexityWithQuadOverdraw","VMI_HLODColoration","VMI_GroupLODColoration","VMI_MaterialTextureScaleAccuracy","VMI_RequiredTextureResolution","VMI_PathTracing","VMI_RayTracingDebug","VMI_VisualizeNanite","VMI_VisualizeVirtualTexture","VMI_VisualizeLumen","VMI_VisualizeVirtualShadowMap","VMI_VisualizeGPUSkinCache","VMI_VisualizeSubstrate","VMI_VisualizeGroom","VMI_LWCComplexity","VMI_VisualizeActorColoration","VMI_ShadowCasters","VMI_Clay","VMI_Zebra","VMI_FrontBackFace","VMI_RandomColor","VMI_VisualizeMegaLights","VMI_StreamingTextureDeficit","VMI_StreamingTextureResidency","VMI_StreamingMeshLODDeficit","VMI_StreamingMeshLODResidency","VMI_FarShadowCasters","VMI_Unknown"]}},"required":["viewMode"]} as const;
const S_epic_show_notification = {"properties":{"message":{"type":"string"},"state":{"type":"string","enum":["None","Pending","Success","Fail"]},"expireDuration":{"type":"number"}},"required":["message"]} as const;
const S_epic_skim_video = {"properties":{"path":{"type":"string"},"maxFrames":{"type":"integer"},"maxEdge":{"type":"integer"}},"required":["path"]} as const;
const S_epic_start_pie = {"properties":{"options":{"type":"object","properties":{"bSimulate":{"type":"boolean"},"playMode":{"type":"string","enum":["PlayMode_InViewPort","PlayMode_InEditorFloating","PlayMode_InMobilePreview","PlayMode_InTargetedMobilePreview","PlayMode_InVulkanPreview","PlayMode_InNewProcess","PlayMode_InVR","PlayMode_Simulate","PlayMode_QuickLaunch","PlayMode_Count"]},"startTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"warmupSeconds":{"type":"number"}}}}} as const;
const S_epic_start_viewport_recording = {"properties":{"options":{"type":"object","properties":{"source":{"type":"string","enum":["GameViewport","EditorViewport"]},"world":{"type":"object","properties":{"refPath":{}}},"viewportId":{"type":"string"},"maxResolution":{"type":"integer"},"frameRate":{"type":"number"},"maxDurationSeconds":{"type":"number"}}}},"required":["options"]} as const;
const S_epic_stop_pie = {"properties":{}} as const;
const S_epic_stop_viewport_recording = {"properties":{"recordingId":{"type":"string"}},"required":["recordingId"]} as const;
const S_epic_top_stats = {"properties":{"path":{"type":"string"},"count":{"type":"integer"},"rankBy":{"type":"string","enum":["Average","Max","Total","P50","P90","P95","P99"]},"statNameFilter":{"type":"string"},"frameStart":{"type":"integer"},"frameEnd":{"type":"integer"}},"required":["path"]} as const;
const S_epic_trigger_gpuprofile = {"properties":{"minInclusiveMs":{"type":"number"}},"required":["minInclusiveMs"]} as const;
const S_epic_world_pos_to_screen_coords = {"properties":{"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"required":["position"]} as const;

/** 101 wrapped engine tools routed to the `editor` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_assets_to_collection: {
    epicSchema: S_epic_add_assets_to_collection,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.AddAssetsToCollection", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Adds assets to the specified collection. Available on UE 5.9; absent from UE 5.8. Params: collection, assetPaths", "epic_call_tool"),
  },
  epic_call_cheat: {
    epicSchema: S_epic_call_cheat,
    epicTool: { toolset: "CheatsToolset.CheatsToolset", name: "CheatsToolset.CheatsToolset.CallCheat", engines: ["5.9"] },
    ...bp("mutate", "[Epic CheatsToolset.CheatsToolset] Runs a cheat on the local player, exactly as if it were typed into the console. Available on UE 5.9; absent from UE 5.8. Params: cheatName, args", "epic_call_tool"),
  },
  epic_capture: {
    epicSchema: S_epic_capture,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.Capture", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Renders offscreen from the given world and camera using the specified mode. Use ListModes for names available in this build and session; DescribeMode supplies interpretation and sub-modes. Unknown or unavailable modes raise a script error. Mode names are case-sensitive. Availability does not validate scene content or every pass prerequisite. Uses world post-process volumes, with no temporal history or camera-specific post-process overrides. This is not a live viewport screenshot. PNG output is an opaque, display-encoded 8-bit visualization, not a raw or HDR buffer export. Capture timings are not representative game-frame timings. Available on UE 5.9; absent from UE 5.8. Params: world, camera, mode, options", "epic_call_tool"),
  },
  epic_capture_active_editor_view: {
    epicSchema: S_epic_capture_active_editor_view,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.CaptureActiveEditorView", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Captures the active editor camera offscreen using the requested mode, without changing the viewport. Repeated calls follow viewport movement; use GetActiveEditorView plus Capture for a fixed camera. Mode, Options, output format and rendering limits are the same as Capture. Does not reproduce temporal history, editor overlays or camera-specific post-processing. This is not a viewport screenshot. Use DescribeMode for diagnostic guidance. Raises a script error if no active editor level viewport is available. Available on UE 5.9; absent from UE 5.8. Params: mode, options", "epic_call_tool"),
  },
  epic_capture_asset_image: {
    epicSchema: S_epic_capture_asset_image,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.CaptureAssetImage", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Renders a thumbnail for the specified asset (e.g. static meshes, skeletal meshes, skeletons, animations, montages, materials, textures). Params: assetPath", "epic_call_tool"),
  },
  epic_capture_editor_image: {
    epicSchema: S_epic_capture_editor_image,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.CaptureEditorImage", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Captures an image of the entire editor application as the user sees it. Params: none", "epic_call_tool"),
  },
  epic_capture_editor_viewport: {
    epicSchema: S_epic_capture_editor_viewport,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.CaptureEditorViewport", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Redraws and captures an editor viewport with its view and post-process state: a level viewport or an asset editor viewport such as the Static Mesh or Skeletal Mesh Editor's. Restores camera and show flags on return; temporal history may advance or be affected by camera overrides. Asymmetric projections return the image with a warning and no camera snapshot or image FOV. Raises a script error for an unavailable/unreadable viewport or unsupported HDR encoding. Available on UE 5.9; absent from UE 5.8. Params: captureTransform?, annotations?, bShowUI?, viewportId?", "epic_call_tool"),
  },
  epic_capture_game_viewport: {
    epicSchema: S_epic_capture_game_viewport,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.CaptureGameViewport", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Captures the current Game or PIE viewport as Slate draws it, with UMG and any other UI over the viewport, such as the recording border. In PIE it redraws the window that holds the viewport; if that fails, the image shows the scene only, with a warning. Metadata records the world and request time, not the originating GPU frame, camera, mode or internal resolution. Output is an opaque 8-bit PNG, not a raw or HDR buffer export. PQ/scRGB HDR uses the engine screenshot decoder and sRGB8 image conversion. Conversion clips highlights and out-of-gamut channels without an additional tone curve. A warning identifies this conversion. Other HDR output encodings are rejected. Raises a script error if the host cannot render, or if no initialized, readable Game/PIE framebuffer is available. Available on UE 5.9; absent from UE 5.8. Params: world, maxResolution", "epic_call_tool"),
  },
  epic_capture_viewport: {
    epicSchema: S_epic_capture_viewport,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.CaptureViewport", engines: ["5.8"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Captures the level viewport with optional annotations. Annotations rendering overlays a projected 3D world-space grid plus name + position labels on visible actors. The grid is drawn at a configurable ground-plane Z and projected through the camera, with coordinate numbers at intersections (shown in meters). Each labeled actor gets a crosshair at its projected screen position with a leader-line callout placed to avoid overlap. This gives a vision-capable agent spatial awareness: it can reference grid coordinates to direct placement and identify scene contents by label. Available on UE 5.8; absent from UE 5.9. Params: captureTransform?, annotations?, bShowUI?", "epic_call_tool"),
  },
  epic_center_view: {
    epicSchema: S_epic_center_view,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.CenterView", engines: ["5.9"] },
    ...bp("mutate", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Re-centres the Rewind Debugger visible view range on Time, preserving the current view width. Independent of the scrub cursor: pair with SetScrubTime when you want to both move the cursor and bring it into the centre of the visible timeline. Available on UE 5.9; absent from UE 5.8. Params: time", "epic_call_tool"),
  },
  epic_close_editor_for_asset: {
    epicSchema: S_epic_close_editor_for_asset,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.CloseEditorForAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Closes every asset editor open for the specified asset. Available on UE 5.9; absent from UE 5.8. Params: assetPath, unsavedChanges", "epic_call_tool"),
  },
  epic_create_collection: {
    epicSchema: S_epic_create_collection,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.CreateCollection", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Creates a new static collection. Available on UE 5.9; absent from UE 5.8. Params: collectionName, shareType", "epic_call_tool"),
  },
  epic_describe_mode: {
    epicSchema: S_epic_describe_mode,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.DescribeMode", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Describes a mode's purpose, interpretation, limits, registered sub-modes and default. Accepts the same names and aliases as Capture. Unknown names raise a script error. Availability covers capture-path and session prerequisites, not per-world content or pass activation. Available on UE 5.9; absent from UE 5.8. Params: mode", "epic_call_tool"),
  },
  epic_destroy_collection: {
    epicSchema: S_epic_destroy_collection,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.DestroyCollection", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Deletes the specified collection. Available on UE 5.9; absent from UE 5.8. Params: collection", "epic_call_tool"),
  },
  epic_execute_python: {
    epicSchema: S_epic_execute_python,
    epicTool: { toolset: "editor_toolset.toolsets.python_scripting.PythonScriptingTools", name: "editor_toolset.toolsets.python_scripting.PythonScriptingTools.execute_python", engines: ["5.9"] },
    ...bp("unknown", "[Epic editor_toolset.toolsets.python_scripting.PythonScriptingTools] Runs a Python script in the editor's interpreter on the game thread. IMPORTANT: Call `read_me` once per conversation before using this tool, for full usage instructions -- including how to compose other tools and yield control during long-running work. Look up each `unreal` API with search_symbols or get_symbol before calling it. Available on UE 5.9; absent from UE 5.8. Params: script", "epic_call_tool"),
  },
  epic_exit_editor: {
    epicSchema: S_epic_exit_editor,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.ExitEditor", engines: ["5.9"] },
    ...bp("unknown", "[Epic EditorToolset.EditorAppToolset] Requests a shutdown of the editor application. The request is queued for the next editor tick so this call can return first; the process then exits and no further tool calls are possible. Errors if any level or content package has unsaved changes and bDiscardUnsavedChanges is false. Available on UE 5.9; absent from UE 5.8. Params: bDiscardUnsavedChanges?", "epic_call_tool"),
  },
  epic_focus_on_actors: {
    epicSchema: S_epic_focus_on_actors,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.FocusOnActors", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Repositions the level editor camera to focus on the specified actors. Cannot be called while PIE is active. Params: actors", "epic_call_tool"),
  },
  epic_get_active_editor_modes: {
    epicSchema: S_epic_get_active_editor_modes,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetActiveEditorModes", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Returns the IDs of the currently active editor modes. More than one mode can be active at once; the default placement mode (\"EM_Default\") is usually present. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_active_editor_view: {
    epicSchema: S_epic_get_active_editor_view,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.GetActiveEditorView", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Snapshots a level editor viewport's world, camera and clipping planes: ViewportId's, or the active one. Asset editor viewports are refused, since Capture cannot render their preview worlds. Pass World and Camera to Capture. Match SourceDimensions' aspect ratio to retain framing; letterboxing is excluded. The snapshot does not follow viewport movement. Requires an interactive editor session and an active level viewport. Raises a script error if the viewport is unavailable, has zero size or uses an unsupported projection. Available on UE 5.9; absent from UE 5.8. Params: viewportId?", "epic_call_tool"),
  },
  epic_get_asset_editor_selection: {
    epicSchema: S_epic_get_asset_editor_selection,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetAssetEditorSelection", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Gets what is selected inside the asset editor for the given asset (e.g. selected graph nodes in a Blueprint or Material editor, selected widgets in a UMG editor, and objects shown in the editor's details panels). This is distinct from level-actor selection (GetSelectedActors) and content-browser selection (GetSelectedAssets). Available on UE 5.9; absent from UE 5.8. Params: asset", "epic_call_tool"),
  },
  epic_get_asset_thumbnails: {
    epicSchema: S_epic_get_asset_thumbnails,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetAssetThumbnails", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Returns the assets' existing cached thumbnails (the images the Content Browser shows) without loading or rendering the assets. On success the result's value holds one image per requested path, in request order; an entry is an empty image when the asset has no saved thumbnail or its thumbnail could not be decoded. The call completes with an error instead if any requested path cannot be found. Available on UE 5.9; absent from UE 5.8. Params: assetPaths", "epic_call_tool"),
  },
  epic_get_camera_transform: {
    epicSchema: S_epic_get_camera_transform,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetCameraTransform", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Returns the position and rotation of the level viewport camera. Params: none", "epic_call_tool"),
  },
  epic_get_collection_assets: {
    epicSchema: S_epic_get_collection_assets,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetCollectionAssets", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Returns the assets in the specified collection. Available on UE 5.9; absent from UE 5.8. Params: collection", "epic_call_tool"),
  },
  epic_get_content_browser_path: {
    epicSchema: S_epic_get_content_browser_path,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetContentBrowserPath", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Gets the current path of the active content browser. Params: none", "epic_call_tool"),
  },
  epic_get_csv_info: {
    epicSchema: S_epic_get_csv_info,
    epicTool: { toolset: "PerfToolset.CsvProfilerToolset", name: "PerfToolset.CsvProfilerToolset.GetCsvInfo", engines: ["5.9"] },
    ...bp("unknown", "[Epic PerfToolset.CsvProfilerToolset] Surveys a capture without returning per-frame values. StatGroups (the group histogram) is always populated; individual stat names only when StatNameFilter is set. GroupDepth groups the histogram by the first N segments of the '/'-delimited stat tree: depth 1 buckets under \"Exclusive\", depth 2 under \"Exclusive/GameThread\". Available on UE 5.9; absent from UE 5.8. Params: path, statNameFilter?, bIncludeAllMetadata?, groupDepth?", "epic_call_tool"),
  },
  epic_get_cvar_value: {
    epicSchema: S_epic_get_cvar_value,
    epicTool: { toolset: "ConsoleToolset.ConsoleToolset", name: "ConsoleToolset.ConsoleToolset.GetCVarValue", engines: ["5.9"] },
    ...bp("read", "[Epic ConsoleToolset.ConsoleToolset] Reads the current value of a console variable as its canonical string representation (e.g. \"true\"/\"false\" for bools, \"1.5\" for floats). Available on UE 5.9; absent from UE 5.8. Params: name", "epic_call_tool"),
  },
  epic_get_frame_timings: {
    epicSchema: S_epic_get_frame_timings,
    epicTool: { toolset: "PerfToolset.StatsToolset", name: "PerfToolset.StatsToolset.GetFrameTimings", engines: ["5.9"] },
    ...bp("read", "[Epic PerfToolset.StatsToolset] Reads the engine's per-frame timing globals once (no smoothing - usually the previous frame). Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_frame_tree: {
    epicSchema: S_epic_get_frame_tree,
    epicTool: { toolset: "PerfToolset.StatsToolset", name: "PerfToolset.StatsToolset.GetFrameTree", engines: ["5.9"] },
    ...bp("read", "[Epic PerfToolset.StatsToolset] Captures the latest stat frame as a nested tree. Siblings sorted most-expensive first. Top-level entries are typically thread roots (GameThread, RenderThread). On leaves, ExclusiveMs equals InclusiveMs. Available on UE 5.9; absent from UE 5.8. Params: minInclusiveMs, maxDepth, pathFilter", "epic_call_tool"),
  },
  epic_get_log_categories: {
    epicSchema: S_epic_get_log_categories,
    epicTool: { toolset: "EditorToolset.LogsToolset", name: "EditorToolset.LogsToolset.GetLogCategories", engines: ["5.8"], successors: [{ toolset: "ConsoleToolset.LogsToolset", name: "ConsoleToolset.LogsToolset.GetLogCategories", engines: ["5.9"], schema: S_epic_get_log_categories__1 }] },
    ...bp("read", "[Epic EditorToolset.LogsToolset] Returns a sorted list of registered log categories. Params: filter. On UE 5.9 it calls ConsoleToolset.LogsToolset.GetLogCategories (Params: filter?, logFilePath?).", "epic_call_tool"),
  },
  epic_get_log_entries: {
    epicSchema: S_epic_get_log_entries,
    epicTool: { toolset: "EditorToolset.LogsToolset", name: "EditorToolset.LogsToolset.GetLogEntries", engines: ["5.8"], successors: [{ toolset: "ConsoleToolset.LogsToolset", name: "ConsoleToolset.LogsToolset.GetLogEntries", engines: ["5.9"], schema: S_epic_get_log_entries__1 }] },
    ...bp("read", "[Epic EditorToolset.LogsToolset] Returns log entries from the current session's log file. Params: category?, pattern, maxEntries?. On UE 5.9 it calls ConsoleToolset.LogsToolset.GetLogEntries (Params: logCategory?, pattern?, maxEntries?, logFilePath?, maxCharsPerEntry?).", "epic_call_tool"),
  },
  epic_get_object_events: {
    epicSchema: S_epic_get_object_events,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.GetObjectEvents", engines: ["5.9"] },
    ...bp("read", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Returns all events that fired on a recorded object within a time range. Events represent discrete occurrences such as Linguine prompt executions. Returns {\"Events\": []} when no session is active or no events exist in the range. Available on UE 5.9; absent from UE 5.8. Params: objectId, startTime, endTime", "epic_call_tool"),
  },
  epic_get_object_properties: {
    epicSchema: S_epic_get_object_properties,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.GetObjectProperties", engines: ["5.9"] },
    ...bp("read", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Returns the recorded property state of an object at or before a given time. Uses closest-before semantics: returns the most recent snapshot at or before Time. Returns {} when no session is active or no snapshot exists before Time. Available on UE 5.9; absent from UE 5.8. Params: objectId, time", "epic_call_tool"),
  },
  epic_get_open_assets: {
    epicSchema: S_epic_get_open_assets,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetOpenAssets", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Gets the list of assets currently open in asset editors. Params: none", "epic_call_tool"),
  },
  epic_get_pivot_location: {
    epicSchema: S_epic_get_pivot_location,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetPivotLocation", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Gets the current location of the editor's transform-gizmo pivot. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_recent_transactions: {
    epicSchema: S_epic_get_recent_transactions,
    epicTool: { toolset: "EditorToolset.UndoHistoryToolset", name: "EditorToolset.UndoHistoryToolset.GetRecentTransactions", engines: ["5.9"] },
    ...bp("unknown", "[Epic EditorToolset.UndoHistoryToolset] Returns transactions from the undo history, newest first. Available on UE 5.9; absent from UE 5.8. Params: count?, offset?", "epic_call_tool"),
  },
  epic_get_selected_actors: {
    epicSchema: S_epic_get_selected_actors,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetSelectedActors", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Gets the currently selected actors in the level editor. Params: none", "epic_call_tool"),
  },
  epic_get_selected_assets: {
    epicSchema: S_epic_get_selected_assets,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetSelectedAssets", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Gets the list of assets selected in the content browser. Params: none", "epic_call_tool"),
  },
  epic_get_selected_object: {
    epicSchema: S_epic_get_selected_object,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.GetSelectedObject", engines: ["5.9"] },
    ...bp("read", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Returns the object currently selected in the Rewind Debugger outliner, if any. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selected_outliner_folders: {
    epicSchema: S_epic_get_selected_outliner_folders,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetSelectedOutlinerFolders", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Gets the folder paths selected across all open scene outliners. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_session_info: {
    epicSchema: S_epic_get_session_info,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.GetSessionInfo", engines: ["5.9"] },
    ...bp("read", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Returns the current Rewind Debugger session state. Call this first to confirm a session is active before querying object data. Always returns all fields; IsActive is false when no session is loaded. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_show_flag: {
    epicSchema: S_epic_get_show_flag,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetShowFlag", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Reads whether a named engine show flag is currently enabled on the active level editor viewport. Available on UE 5.9; absent from UE 5.8. Params: flagName", "epic_call_tool"),
  },
  epic_get_stat_counters: {
    epicSchema: S_epic_get_stat_counters,
    epicTool: { toolset: "PerfToolset.StatsToolset", name: "PerfToolset.StatsToolset.GetStatCounters", engines: ["5.9"] },
    ...bp("read", "[Epic PerfToolset.StatsToolset] Samples every non-cycle counter in a stat group for a single frame: integer/float counters and memory pool sizes (draw calls, primitive counts, LLM tags, etc). Available on UE 5.9; absent from UE 5.8. Params: groupName, bIncludeZero", "epic_call_tool"),
  },
  epic_get_symbol: {
    epicSchema: S_epic_get_symbol,
    epicTool: { toolset: "editor_toolset.toolsets.python_scripting.PythonScriptingTools", name: "editor_toolset.toolsets.python_scripting.PythonScriptingTools.get_symbol", engines: ["5.9"] },
    ...bp("read", "[Epic editor_toolset.toolsets.python_scripting.PythonScriptingTools] Returns the documentation of a registered tool, or of a type, function, method, or property in the `unreal` Python API. Pass a toolset or full tool name, a top-level name ('EditorActorSubsystem', 'log'), or a qualified 'Class.member'. A toolset returns its description and the signature of each of its tools. A tool returns its signature, description, and parameters. A function or member returns its signature and full documentation. A type returns its description, then one line for each editor property (inherited ones included), enum value, and method it declares, and how many methods it inherits from each base; get_symbol on 'Type.member' or on a base gives the details. A struct only tools use returns its fields and the tools that use it. Available on UE 5.9; absent from UE 5.8. Params: name", "epic_call_tool"),
  },
  epic_get_trace_event_details: {
    epicSchema: S_epic_get_trace_event_details,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.GetTraceEventDetails", engines: ["5.9"] },
    ...bp("read", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Returns the full decoded JSON for a single trace event from a named source. Available on UE 5.9; absent from UE 5.8. Params: source, eventId", "epic_call_tool"),
  },
  epic_get_transaction_details: {
    epicSchema: S_epic_get_transaction_details,
    epicTool: { toolset: "EditorToolset.UndoHistoryToolset", name: "EditorToolset.UndoHistoryToolset.GetTransactionDetails", engines: ["5.9"] },
    ...bp("unknown", "[Epic EditorToolset.UndoHistoryToolset] Returns detailed information about a single transaction Available on UE 5.9; absent from UE 5.8. Params: transactionId, maxObjects?", "epic_call_tool"),
  },
  epic_get_transaction_recency_index: {
    epicSchema: S_epic_get_transaction_recency_index,
    epicTool: { toolset: "EditorToolset.UndoHistoryToolset", name: "EditorToolset.UndoHistoryToolset.GetTransactionRecencyIndex", engines: ["5.9"] },
    ...bp("unknown", "[Epic EditorToolset.UndoHistoryToolset] Returns how far back a transaction is in the undo history, counted newest-first: 0 is the most recent transaction, 1 the next, and so on. Available on UE 5.9; absent from UE 5.8. Params: transactionId", "epic_call_tool"),
  },
  epic_get_verbosity: {
    epicSchema: S_epic_get_verbosity,
    epicTool: { toolset: "EditorToolset.LogsToolset", name: "EditorToolset.LogsToolset.GetVerbosity", engines: ["5.8"], successors: [{ toolset: "ConsoleToolset.LogsToolset", name: "ConsoleToolset.LogsToolset.GetVerbosity", engines: ["5.9"], schema: S_epic_get_verbosity__1 }] },
    ...bp("read", "[Epic EditorToolset.LogsToolset] Returns the current verbosity level for a log category. Params: category?. On UE 5.9 it calls ConsoleToolset.LogsToolset.GetVerbosity (Params: logCategory).", "epic_call_tool"),
  },
  epic_get_video_info: {
    epicSchema: S_epic_get_video_info,
    epicTool: { toolset: "MediaToolset.MediaToolset", name: "MediaToolset.MediaToolset.GetVideoInfo", engines: ["5.9"] },
    ...bp("read", "[Epic MediaToolset.MediaToolset] Reads container metadata. Windows Media Foundation is required. Available on UE 5.9; absent from UE 5.8. Params: path", "epic_call_tool"),
  },
  epic_get_visible_actors: {
    epicSchema: S_epic_get_visible_actors,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.GetVisibleActors", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Returns all actors in the current level whose bounds intersect the viewport frustum. Params: none", "epic_call_tool"),
  },
  epic_is_pierunning: {
    epicSchema: S_epic_is_pierunning,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.IsPIERunning", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] True for active PIE or simulation, including online login before world creation. A queued start alone returns false. Params: none", "epic_call_tool"),
  },
  epic_list_cheats: {
    epicSchema: S_epic_list_cheats,
    epicTool: { toolset: "CheatsToolset.CheatsToolset", name: "CheatsToolset.CheatsToolset.ListCheats", engines: ["5.9"] },
    ...bp("read", "[Epic CheatsToolset.CheatsToolset] Lists the cheat commands available on the local player. Available on UE 5.9; absent from UE 5.8. Params: nameFilter?", "epic_call_tool"),
  },
  epic_list_collections: {
    epicSchema: S_epic_list_collections,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.ListCollections", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Lists all collections in the content browser. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_editor_modes: {
    epicSchema: S_epic_list_editor_modes,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.ListEditorModes", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Lists the IDs of all registered editor modes (e.g. \"EM_Default\", \"EM_Landscape\", \"EM_Foliage\"), ordered by mode priority. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_editor_viewports: {
    epicSchema: S_epic_list_editor_viewports,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.ListEditorViewports", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Lists the open level and asset editor viewports, with ids to pass as Options.ViewportId or to CaptureEditorViewport. Only a viewport with bIsVisible set presents frames, so ask the user to bring any other to the front first. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_events: {
    epicSchema: S_epic_list_events,
    epicTool: { toolset: "PerfToolset.CsvProfilerToolset", name: "PerfToolset.CsvProfilerToolset.ListEvents", engines: ["5.9"] },
    ...bp("unknown", "[Epic PerfToolset.CsvProfilerToolset] Returns the capture's events with their frame indices. Repeats of the same event name on one frame collapse into a single entry carrying a Count. FrameEnd -1 runs to the end; the defaults return the whole run. At most Count events (the first in frame order) are returned. Available on UE 5.9; absent from UE 5.8. Params: path, count?, frameStart?, frameEnd?", "epic_call_tool"),
  },
  epic_list_modes: {
    epicSchema: S_epic_list_modes,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.ListModes", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Lists canonical names of modes that can capture in this build and session. Aliases are not listed separately. Availability reflects capture-path, build, session and project prerequisites, not scene content. Buffer-visualization targets incompatible with the capture output format are omitted. Available on UE 5.9; absent from UE 5.8. Params: bIncludeUnavailable", "epic_call_tool"),
  },
  epic_list_object_trace_events: {
    epicSchema: S_epic_list_object_trace_events,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.ListObjectTraceEvents", engines: ["5.9"] },
    ...bp("read", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Lists trace events attributed to ObjectId within [StartTime, EndTime], aggregated across every registered IRewindDebuggerToolsetEventDecoder. Returns lightweight rows; call GetTraceEventDetails to decode a specific event in full. Available on UE 5.9; absent from UE 5.8. Params: objectId, startTime, endTime, sourceFilter, includedNameFilter, excludedNameFilter, limit, offset", "epic_call_tool"),
  },
  epic_list_objects: {
    epicSchema: S_epic_list_objects,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.ListObjects", engines: ["5.9"] },
    ...bp("read", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Lists all objects recorded in the current session, optionally filtered by class name. Each entry includes an ObjectId for use with GetObjectEvents and GetObjectProperties. Returns {\"Objects\": []} when no session is active or no objects match the filter. Available on UE 5.9; absent from UE 5.8. Params: className?", "epic_call_tool"),
  },
  epic_list_show_flags: {
    epicSchema: S_epic_list_show_flags,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.ListShowFlags", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Lists every engine show flag with its display name and editor group. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_stat_groups: {
    epicSchema: S_epic_list_stat_groups,
    epicTool: { toolset: "PerfToolset.StatsToolset", name: "PerfToolset.StatsToolset.ListStatGroups", engines: ["5.9"] },
    ...bp("read", "[Epic PerfToolset.StatsToolset] Enumerates registered stat groups. Names are STATGROUP_-prefixed and match the argument accepted by GetStatCounters. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_viewport_recordings: {
    epicSchema: S_epic_list_viewport_recordings,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.ListViewportRecordings", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Lists this session's viewport recordings, including the active one. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_views: {
    epicSchema: S_epic_list_views,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.ListViews", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Snapshots actors with active camera components and player camera managers in the given world. Preserves custom perspective near planes and explicitly configured orthographic clip planes. A player view whose transform, active lens and clipping match a listed view marks that entry as a player view instead of adding one. Lists player views first, then the remaining actor cameras. Returns empty when no views exist; an invalid world raises a script error. Available on UE 5.9; absent from UE 5.8. Params: world", "epic_call_tool"),
  },
  epic_list_worlds: {
    epicSchema: S_epic_list_worlds,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.ListWorlds", engines: ["5.9"] },
    ...bp("read", "[Epic SceneCaptureToolset.SceneCaptureToolset] Lists Editor, PIE and Game worlds in the engine's world contexts, including non-renderable worlds. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_open_editor_for_asset: {
    epicSchema: S_epic_open_editor_for_asset,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.OpenEditorForAsset", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Opens an asset editor for the specified asset. Params: assetPath", "epic_call_tool"),
  },
  epic_read_image_file: {
    epicSchema: S_epic_read_image_file,
    epicTool: { toolset: "MediaToolset.MediaToolset", name: "MediaToolset.MediaToolset.ReadImageFile", engines: ["5.9"] },
    ...bp("read", "[Epic MediaToolset.MediaToolset] Reads up to 64 MiB of encoded PNG, JPEG, GIF, BMP, or WebP data without re-encoding. Available on UE 5.9; absent from UE 5.8. Params: path", "epic_call_tool"),
  },
  epic_read_me: {
    epicSchema: S_epic_read_me,
    epicTool: { toolset: "editor_toolset.toolsets.python_scripting.PythonScriptingTools", name: "editor_toolset.toolsets.python_scripting.PythonScriptingTools.read_me", engines: ["5.9"] },
    ...bp("read", "[Epic editor_toolset.toolsets.python_scripting.PythonScriptingTools] Usage instructions for execute_python. Call this once per conversation before using execute_python. Explains the execution environment (namespace, async/await, composing other tools) and, only when relevant, the spatial coordinate-conversion helpers available in the current editor session. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_read_stat_values: {
    epicSchema: S_epic_read_stat_values,
    epicTool: { toolset: "PerfToolset.CsvProfilerToolset", name: "PerfToolset.CsvProfilerToolset.ReadStatValues", engines: ["5.9"] },
    ...bp("unknown", "[Epic PerfToolset.CsvProfilerToolset] Returns up to Count per-frame values of one stat, starting at FrameStart. An over-large Count clamps to the stat's own length rather than erroring; Count = -1 reads to the end of the column. Available on UE 5.9; absent from UE 5.8. Params: path, statName, frameStart?, count?", "epic_call_tool"),
  },
  epic_record_viewport: {
    epicSchema: S_epic_record_viewport,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.RecordViewport", engines: ["5.9"] },
    ...bp("unknown", "[Epic SceneCaptureToolset.SceneCaptureToolset] Records a viewport for DurationSeconds, then completes with the finalized file. Canceling the call stops the recording early and keeps the file. Available on UE 5.9; absent from UE 5.8. Params: options, durationSeconds", "epic_call_tool"),
  },
  epic_reduce_stat: {
    epicSchema: S_epic_reduce_stat,
    epicTool: { toolset: "PerfToolset.CsvProfilerToolset", name: "PerfToolset.CsvProfilerToolset.ReduceStat", engines: ["5.9"] },
    ...bp("unknown", "[Epic PerfToolset.CsvProfilerToolset] Reduces each requested stat to average, min, max, total, and the P50/P90/P95/P99 percentiles over a frame-index range. FrameEnd -1 runs to the end; the defaults reduce over the whole run. Stats not present in the capture are omitted from the result. Available on UE 5.9; absent from UE 5.8. Params: path, statNames, frameStart?, frameEnd?", "epic_call_tool"),
  },
  epic_remove_assets_from_collection: {
    epicSchema: S_epic_remove_assets_from_collection,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.RemoveAssetsFromCollection", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Removes assets from the specified collection. Available on UE 5.9; absent from UE 5.8. Params: collection, assetPaths", "epic_call_tool"),
  },
  epic_run_console_command: {
    epicSchema: S_epic_run_console_command,
    epicTool: { toolset: "ConsoleToolset.ConsoleToolset", name: "ConsoleToolset.ConsoleToolset.RunConsoleCommand", engines: ["5.9"] },
    ...bp("unknown", "[Epic ConsoleToolset.ConsoleToolset] Executes an arbitrary UE exec command. Output is written to the session log. Python is refused: any command containing \"py\" as a whole word (the py command, including when wrapped by defer, redirecttofile, etc.) is rejected. Run Python with the Python scripting tool instead. Available on UE 5.9; absent from UE 5.8. Params: command", "epic_call_tool"),
  },
  epic_sample_frames: {
    epicSchema: S_epic_sample_frames,
    epicTool: { toolset: "MediaToolset.MediaToolset", name: "MediaToolset.MediaToolset.SampleFrames", engines: ["5.9"] },
    ...bp("read", "[Epic MediaToolset.MediaToolset] Samples midpoint-spaced frames in a time range. Windows only. Available on UE 5.9; absent from UE 5.8. Params: path, startSeconds, endSeconds, maxFrames?, maxEdge?", "epic_call_tool"),
  },
  epic_screen_coords_to_world: {
    epicSchema: S_epic_screen_coords_to_world,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.ScreenCoordsToWorld", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Finds the world position of the nearest solid object at a given set of normalized view space coords. Params: coords, traceDistance?", "epic_call_tool"),
  },
  epic_search_console_commands: {
    epicSchema: S_epic_search_console_commands,
    epicTool: { toolset: "ConsoleToolset.ConsoleToolset", name: "ConsoleToolset.ConsoleToolset.SearchConsoleCommands", engines: ["5.9"] },
    ...bp("read", "[Epic ConsoleToolset.ConsoleToolset] Returns exec commands whose name contains the search string (case-insensitive). Available on UE 5.9; absent from UE 5.8. Params: name", "epic_call_tool"),
  },
  epic_search_cvars: {
    epicSchema: S_epic_search_cvars,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SearchCVars", engines: ["5.8"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Finds all console variables that contain a given name. Available on UE 5.8; absent from UE 5.9. Params: name", "epic_call_tool"),
  },
  epic_search_cvars__console_toolset: {
    epicSchema: S_epic_search_cvars__console_toolset,
    epicTool: { toolset: "ConsoleToolset.ConsoleToolset", name: "ConsoleToolset.ConsoleToolset.SearchCVars", engines: ["5.9"] },
    ...bp("read", "[Epic ConsoleToolset.ConsoleToolset] Finds all console variables that contain a given name. Available on UE 5.9; absent from UE 5.8. Params: name", "epic_call_tool"),
  },
  epic_search_symbols: {
    epicSchema: S_epic_search_symbols,
    epicTool: { toolset: "editor_toolset.toolsets.python_scripting.PythonScriptingTools", name: "editor_toolset.toolsets.python_scripting.PythonScriptingTools.search_symbols", engines: ["5.9"] },
    ...bp("read", "[Epic editor_toolset.toolsets.python_scripting.PythonScriptingTools] Searches the registered tools and the `unreal` Python API for types, functions, methods, and properties. Searches what this editor session provides, so every result exists here. Feed a result's qualified_name to get_symbol to read it. Available on UE 5.9; absent from UE 5.8. Params: pattern?, max_results?, match?, kinds?, accepts?, returns?", "epic_call_tool"),
  },
  epic_search_transactions: {
    epicSchema: S_epic_search_transactions,
    epicTool: { toolset: "EditorToolset.UndoHistoryToolset", name: "EditorToolset.UndoHistoryToolset.SearchTransactions", engines: ["5.9"] },
    ...bp("unknown", "[Epic EditorToolset.UndoHistoryToolset] Searches the undo history by transaction label using a fuzzy (edit-distance) match, closest match first. When two transactions match a query equally well the more recent one is returned first. Available on UE 5.9; absent from UE 5.8. Params: query, count?, offset?", "epic_call_tool"),
  },
  epic_select_actors: {
    epicSchema: S_epic_select_actors,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SelectActors", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Selects the specified actors in the current scene. Params: actors", "epic_call_tool"),
  },
  epic_select_asset_editor_objects: {
    epicSchema: S_epic_select_asset_editor_objects,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SelectAssetEditorObjects", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Sets what is selected inside the asset editor for the given asset, replacing the current selection. Supported for Blueprint editors (graph nodes) and Widget editors (widgets and graph nodes); raises for material editors and other asset types whose selection cannot be set. Available on UE 5.9; absent from UE 5.8. Params: asset, objects", "epic_call_tool"),
  },
  epic_select_assets: {
    epicSchema: S_epic_select_assets,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SelectAssets", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Selects the specified assets in the content browser. Completes once the content browser has applied the selection. Params: assetPaths", "epic_call_tool"),
  },
  epic_select_object: {
    epicSchema: S_epic_select_object,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.SelectObject", engines: ["5.9"] },
    ...bp("mutate", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Selects a recorded object in the Rewind Debugger panel and opens the details panel so its state and timelines are visible to the user. Use after ListObjects to surface a particular object when reporting findings. Available on UE 5.9; absent from UE 5.8. Params: objectId", "epic_call_tool"),
  },
  epic_select_outliner_folders: {
    epicSchema: S_epic_select_outliner_folders,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SelectOutlinerFolders", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Selects the specified folders in every open scene outliner. Replaces the existing folder selection; non-folder selections (e.g. selected actors) are preserved. Available on UE 5.9; absent from UE 5.8. Params: folderPaths", "epic_call_tool"),
  },
  epic_set_camera_transform: {
    epicSchema: S_epic_set_camera_transform,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SetCameraTransform", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Sets the position and/or rotation of the level viewport camera. Anything not provided keeps its current value. Params: transform?, translation?, rotation?", "epic_call_tool"),
  },
  epic_set_content_browser_path: {
    epicSchema: S_epic_set_content_browser_path,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SetContentBrowserPath", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Navigates the active content browser to the specified folder path. Params: path", "epic_call_tool"),
  },
  epic_set_cvar_value: {
    epicSchema: S_epic_set_cvar_value,
    epicTool: { toolset: "ConsoleToolset.ConsoleToolset", name: "ConsoleToolset.ConsoleToolset.SetCVarValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic ConsoleToolset.ConsoleToolset] Sets the value of a console variable. The value is parsed according to the variable's declared type. Available on UE 5.9; absent from UE 5.8. Params: name, value", "epic_call_tool"),
  },
  epic_set_editor_mode: {
    epicSchema: S_epic_set_editor_mode,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SetEditorMode", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Activates the specified editor mode. Pass \"EM_Default\" (or an empty string) to return to the default placement/select mode. Cannot be used during PIE. Available on UE 5.9; absent from UE 5.8. Params: modeId", "epic_call_tool"),
  },
  epic_set_pivot_location: {
    epicSchema: S_epic_set_pivot_location,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SetPivotLocation", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Sets the location of the editor's transform-gizmo pivot. Available on UE 5.9; absent from UE 5.8. Params: location, bSnapToGrid?", "epic_call_tool"),
  },
  epic_set_scrub_time: {
    epicSchema: S_epic_set_scrub_time,
    epicTool: { toolset: "RewindDebuggerToolset.RewindDebuggerToolset", name: "RewindDebuggerToolset.RewindDebuggerToolset.SetScrubTime", engines: ["5.9"] },
    ...bp("mutate", "[Epic RewindDebuggerToolset.RewindDebuggerToolset] Moves the Rewind Debugger scrub cursor to a specific recording time. The engine clamps to the recording's range. Disables auto-scroll-with-recording so the timeline stays put. Does not re-centre the visible view range; pair with CenterView to do that. Available on UE 5.9; absent from UE 5.8. Params: time", "epic_call_tool"),
  },
  epic_set_show_flag: {
    epicSchema: S_epic_set_show_flag,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SetShowFlag", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Toggles a named engine show flag on the active level editor viewport (e.g. \"Navigation\", \"Collision\", \"Grid\", \"Bounds\", \"Fog\", \"Landscape\", \"Volumes\"). Available on UE 5.9; absent from UE 5.8. Params: flagName, bEnabled", "epic_call_tool"),
  },
  epic_set_verbosity: {
    epicSchema: S_epic_set_verbosity,
    epicTool: { toolset: "EditorToolset.LogsToolset", name: "EditorToolset.LogsToolset.SetVerbosity", engines: ["5.8"], successors: [{ toolset: "ConsoleToolset.LogsToolset", name: "ConsoleToolset.LogsToolset.SetVerbosity", engines: ["5.9"], schema: S_epic_set_verbosity__1 }] },
    ...bp("mutate", "[Epic EditorToolset.LogsToolset] Sets the verbosity level for a log category. Params: category?, verbosity. On UE 5.9 it calls ConsoleToolset.LogsToolset.SetVerbosity (Params: logCategory, verbosity).", "epic_call_tool"),
  },
  epic_set_viewport_view_mode: {
    epicSchema: S_epic_set_viewport_view_mode,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.SetViewportViewMode", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Sets the view mode of the active level editor viewport (e.g. Lit, Unlit, Wireframe, Lighting-only, complexity visualizations). Available on UE 5.9; absent from UE 5.8. Params: viewMode", "epic_call_tool"),
  },
  epic_show_notification: {
    epicSchema: S_epic_show_notification,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.ShowNotification", engines: ["5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Shows a transient editor notification (Slate toast, bottom-right). Available on UE 5.9; absent from UE 5.8. Params: message, state?, expireDuration?", "epic_call_tool"),
  },
  epic_skim_video: {
    epicSchema: S_epic_skim_video,
    epicTool: { toolset: "MediaToolset.MediaToolset", name: "MediaToolset.MediaToolset.SkimVideo", engines: ["5.9"] },
    ...bp("read", "[Epic MediaToolset.MediaToolset] Samples midpoint-spaced frames over the video's known duration. Windows only. Available on UE 5.9; absent from UE 5.8. Params: path, maxFrames?, maxEdge?", "epic_call_tool"),
  },
  epic_start_pie: {
    epicSchema: S_epic_start_pie,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.StartPIE", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Starts PIE or simulation in the current level, then waits for warmup. Local single-player worlds usually exist at PostPIEStarted; deferred online login delays warmup until a world exists. The whole request has a 30-second timeout. Errors if a session is active or queued. Params: options?", "epic_call_tool"),
  },
  epic_start_viewport_recording: {
    epicSchema: S_epic_start_viewport_recording,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.StartViewportRecording", engines: ["5.9"] },
    ...bp("unknown", "[Epic SceneCaptureToolset.SceneCaptureToolset] Starts recording a viewport to an MP4 as the user sees it and returns at once; StopViewportRecording finishes it. One recording runs at a time, and it also ends on its own at MaxDurationSeconds or when the viewport goes away. The red border at the frame edges is the recording indicator, not scene content. Available on UE 5.9; absent from UE 5.8. Params: options", "epic_call_tool"),
  },
  epic_stop_pie: {
    epicSchema: S_epic_stop_pie,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.StopPIE", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic EditorToolset.EditorAppToolset] Stops PIE or simulation and waits for shutdown. Normally handled on an editor tick; pending online login defers shutdown until a world exists. Errors if no session is active. Params: none", "epic_call_tool"),
  },
  epic_stop_viewport_recording: {
    epicSchema: S_epic_stop_viewport_recording,
    epicTool: { toolset: "SceneCaptureToolset.SceneCaptureToolset", name: "SceneCaptureToolset.SceneCaptureToolset.StopViewportRecording", engines: ["5.9"] },
    ...bp("unknown", "[Epic SceneCaptureToolset.SceneCaptureToolset] Stops a recording and completes once its file is finalized. A recording that already ended returns its result. Available on UE 5.9; absent from UE 5.8. Params: recordingId", "epic_call_tool"),
  },
  epic_top_stats: {
    epicSchema: S_epic_top_stats,
    epicTool: { toolset: "PerfToolset.CsvProfilerToolset", name: "PerfToolset.CsvProfilerToolset.TopStats", engines: ["5.9"] },
    ...bp("unknown", "[Epic PerfToolset.CsvProfilerToolset] Ranks the capture's stats by a reduced aggregate (average, max, total, or a P50/P90/P95/P99 percentile) over a frame range and returns the top Count, each already reduced. A StatNameFilter wildcard restricts the ranking to a group (e.g. \"GPU*\"); empty ranks all stats. FrameEnd -1 runs to the end; the defaults rank the whole run. Ranking is by raw aggregate value with no unit normalisation, so an unfiltered ranking mixes unit domains (byte counters rank alongside millisecond timings). Available on UE 5.9; absent from UE 5.8. Params: path, count?, rankBy?, statNameFilter?, frameStart?, frameEnd?", "epic_call_tool"),
  },
  epic_trigger_gpuprofile: {
    epicSchema: S_epic_trigger_gpuprofile,
    epicTool: { toolset: "PerfToolset.GpuProfilerToolset", name: "PerfToolset.GpuProfilerToolset.TriggerGPUProfile", engines: ["5.9"] },
    ...bp("read", "[Epic PerfToolset.GpuProfilerToolset] Captures the next rendered frame's GPU work as a nested tree (2-3 frame latency). Top-level entries are GPU queues (Graphics, Async Compute); each contains its passes as a recursive subtree. Available on UE 5.9; absent from UE 5.8. Params: minInclusiveMs", "epic_call_tool"),
  },
  epic_world_pos_to_screen_coords: {
    epicSchema: S_epic_world_pos_to_screen_coords,
    epicTool: { toolset: "EditorToolset.EditorAppToolset", name: "EditorToolset.EditorAppToolset.WorldPosToScreenCoords", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic EditorToolset.EditorAppToolset] Converts a world-space position into normalized screen space based on the editor viewport camera. Params: position", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  accepts: z.string().optional().describe("Only return functions, methods, and tools with a parameter of this type, and writable properties of it, alone or inside a container such as Array[...]."),
  actors: z.array(z.unknown()).optional().describe("The actors to focus the level camera on."),
  annotations: z.record(z.string(), z.unknown()).optional().describe("Optional annotation overlay configuration. Only use this when you need the information in order to perform spatial actions."),
  args: z.string().optional().describe("The arguments as a single space-separated string matching the cheat's Args hint from ListCheats. Empty for a cheat that takes no arguments."),
  asset: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The asset whose editor selection to query. Open the editor first with OpenEditorForAsset."),
  assetPath: z.string().optional().describe("The path to the asset, e.g. '/Game/Meshes/SM_Cube'."),
  assetPaths: z.array(z.unknown()).optional().describe("The object paths of the assets to select. Bare package paths are also accepted."),
  bDiscardUnsavedChanges: z.boolean().optional().describe("Exit even if packages have unsaved changes, losing them."),
  bEnabled: z.boolean().optional().describe("Whether to enable or disable the flag."),
  bIncludeAllMetadata: z.boolean().optional().describe("Return every metadata field instead of the curated subset."),
  bIncludeUnavailable: z.boolean().optional().describe("Also list modes this build or session cannot capture, with bAvailable false and UnavailableReason set."),
  bIncludeZero: z.boolean().optional().describe("Include counters whose sampled value is 0."),
  bShowUI: z.boolean().optional().describe("If false (default), editor UI overlays such as transform gizmos and selection outlines are hidden in the captured image. Set true to capture exactly what's on…"),
  bSnapToGrid: z.boolean().optional().describe("Snaps the pivot to the active editor grid after moving it."),
  camera: z.record(z.string(), z.unknown()).optional().describe("Snapshot from ListViews or a caller-constructed view."),
  captureTransform: z.record(z.string(), z.unknown()).optional().describe("Optional pose to capture from. If unset, uses the viewport's current camera."),
  category: z.string().optional().describe("If non-empty, only returns entries from this log category (e.g. \"LogTemp\")."),
  cheatName: z.string().optional().describe("The cheat command name (case-insensitive), as returned by ListCheats."),
  className: z.string().optional().describe("Case-insensitive substring filter on class name. Empty returns all objects."),
  collection: z.record(z.string(), z.unknown()).optional().describe("The collection to add to."),
  collectionName: z.string().optional().describe("The name of the new collection."),
  command: z.string().optional().describe("The exec command line (as if typed at the console)."),
  coords: z.record(z.string(), z.unknown()).optional().describe("The normalized screen-space coordinates to trace from."),
  count: z.number().optional().describe("Maximum number of transactions to return; must be positive."),
  durationSeconds: z.number().optional().describe("How long to record."),
  endSeconds: z.number().optional().describe("Exclusive end, clamped to a known duration."),
  endTime: z.number().optional().describe("End of the time range in seconds (trace time)."),
  eventId: z.string().optional().describe("Event identifier from ListObjectTraceEvents (string-encoded uint64)."),
  excludedNameFilter: z.string().optional().describe("Case-insensitive substring exclusion on EventName. Empty excludes nothing."),
  expireDuration: z.number().optional().describe("Seconds the toast remains on screen before it fades out. Defaults to 1 (FNotificationInfo's own default)."),
  filter: z.string().optional().describe("If non-empty, only returns categories whose name contains this substring."),
  flagName: z.string().optional().describe("The show flag name (see ListShowFlags)."),
  folderPaths: z.array(z.unknown()).optional().describe("The folder paths to select."),
  frameEnd: z.number().optional().describe("Exclusive last frame, or -1 to run to the end."),
  frameStart: z.number().optional().describe("Inclusive first frame."),
  groupDepth: z.number().optional().describe("Stat-tree levels to group by (>= 1); values below 1 are treated as 1."),
  groupName: z.string().optional().describe("STATGROUP_-prefixed group name, e.g. \"STATGROUP_SceneRendering\"."),
  includedNameFilter: z.string().optional().describe("Case-insensitive substring match on EventName. Empty matches everything."),
  kinds: z.array(z.unknown()).optional().describe("Only return symbols of these kinds. Empty returns all."),
  limit: z.number().optional().describe("Maximum number of rows after sorting. <= 0 means no cap. Recommended default: 100."),
  location: z.record(z.string(), z.unknown()).optional().describe("The world-space location to move the pivot to."),
  logCategory: z.string().optional().describe("If non-empty, only returns entries from this log category (e.g. \"LogTemp\")."),
  logFilePath: z.string().optional().describe("Path to a log file to parse. When empty, the current session's registered categories are used."),
  match: z.string().optional().describe("What to match the pattern against: the symbol's own NAME, its SIGNATURE, or also its DOCUMENTATION (a type's includes its editor properties). A tool's signatur…"),
  max_results: z.number().optional().describe("Maximum number of matches to return."),
  maxCharsPerEntry: z.number().optional().describe("Maximum length of a single entry, so that one very long line (such as a full plugin listing) cannot dominate the result. Longer entries are cut to this length…"),
  maxDepth: z.number().optional().describe("Maximum tree depth to emit - value clamped to [1,120] and counts from the tree root down"),
  maxEdge: z.number().optional().describe("Longest output edge in pixels, limited to 64-1568 without upscaling."),
  maxEntries: z.number().optional().describe("Maximum number of entries to return, taken from the end of the log. Pass 0 for no limit. Defaults to 1000."),
  maxFrames: z.number().optional().describe("Maximum images; runtime limit is 30."),
  maxObjects: z.number().optional().describe("Maximum number of modified objects to include in ModifiedObjects, to keep the response compact for transactions that touch many objects (e.g. bulk deletes). Pa…"),
  maxResolution: z.number().optional().describe("Longest edge of the returned image, in pixels. Larger viewports are scaled down to fit, keeping their aspect ratio. Must be greater than zero, and is capped by…"),
  message: z.string().optional().describe("The text to display."),
  minInclusiveMs: z.number().optional().describe("Drop passes (and subtrees) below this. 0 = full tree."),
  mode: z.string().optional().describe("Which mode to capture; use ListModes to enumerate valid values."),
  modeId: z.string().optional().describe("The editor mode ID to activate (see ListEditorModes)."),
  name: z.string().optional().describe("The partial or full name to search for."),
  nameFilter: z.string().optional().describe("Case-insensitive substring matched against cheat names. Empty lists everything."),
  objectId: z.number().optional().describe("Object identifier returned by ListObjects."),
  objects: z.array(z.unknown()).optional().describe("The objects to select: graph nodes (UEdGraphNode), plus widgets (UWidget) for Widget editors. An empty array clears the selection."),
  offset: z.number().optional().describe("Number of newest transactions to skip before collecting, for paging back through the history; must not be negative. 0 starts at the most recent."),
  options: z.record(z.string(), z.unknown()).optional().describe("Optional session configuration: PIE vs Simulate, play mode, optional spawn transform override, warmup duration. See FPIESessionOptions. When unset, the default…"),
  path: z.string().optional().describe("The internal path to navigate to, e.g. '/Game/Meshes'."),
  pathFilter: z.string().optional().describe("Case-insensitive substring. Only subtrees rooted at a matching node are emitted; ancestors are kept as context. Empty = no filter."),
  pattern: z.string().optional().describe("If non-empty, only returns entries whose text matches this regular expression."),
  position: z.record(z.string(), z.unknown()).optional().describe("The world space position to convert."),
  query: z.string().optional().describe("Text to match against transaction labels. Case-insensitive. An empty query returns the most recent transactions."),
  rankBy: z.string().optional().describe("Which aggregate to rank by: Average, Max, Total, or a P50/P90/P95/P99 percentile."),
  recordingId: z.string().optional().describe("An id from StartViewportRecording or ListViewportRecordings."),
  returns: z.string().optional().describe("Only return functions, methods, and tools that return this type, and properties of it, alone or inside a container."),
  rotation: z.record(z.string(), z.unknown()).optional().describe("The new camera rotation, or unset to keep the current one."),
  script: z.record(z.string(), z.unknown()).optional().describe("Code block whose content is the Python source to execute. May contain top-level `await`."),
  shareType: z.string().optional().describe("How the collection is shared."),
  source: z.string().optional().describe("Source identifier from ListObjectTraceEvents (e.g. \"ChatCompletion\")."),
  sourceFilter: z.string().optional().describe("Only events whose source name (case-insensitive) equals this are returned. Empty includes every registered source."),
  startSeconds: z.number().optional().describe("Inclusive start, clamped to a known duration."),
  startTime: z.number().optional().describe("Start of the time range in seconds (trace time)."),
  state: z.string().optional().describe("The notification's status, which drives its icon. Defaults to None (neutral info, no icon)."),
  statName: z.string().optional().describe("Single stat to read."),
  statNameFilter: z.string().optional().describe("Wildcard ('*'/'?'); matching stat names are returned. Empty returns no names; \"*\" returns all."),
  statNames: z.array(z.unknown()).optional().describe("Stat names to reduce."),
  time: z.number().optional().describe("Recording time in seconds to centre the view on. Must be finite."),
  traceDistance: z.number().optional().describe("The maximum distance to trace within the scene."),
  transactionId: z.string().optional().describe("The id of the transaction."),
  transform: z.record(z.string(), z.unknown()).optional().describe("The transform to apply to the viewport camera."),
  translation: z.record(z.string(), z.unknown()).optional().describe("The new camera position, or unset to keep the current one."),
  unsavedChanges: z.string().optional().describe("What to do if the asset has unsaved changes."),
  value: z.string().optional().describe("The new value as a string."),
  verbosity: z.string().optional().describe("The verbosity level: one of \"NoLogging\", \"Fatal\", \"Error\", \"Warning\", \"Display\", \"Log\", \"Verbose\", or \"VeryVerbose\"."),
  viewMode: z.string().optional().describe("The view mode to apply. Value display names: VMI_BrushWireframe = \"Wireframe\", VMI_Wireframe = \"CSG Wireframe\", VMI_Unlit = \"Unlit\", VMI_Lit = \"Lit\", VMI_Lit_D…"),
  viewportId: z.string().optional().describe("Optional id from ListEditorViewports. Unset captures the active level viewport."),
  world: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A world from ListWorlds."),
};
