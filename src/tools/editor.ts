import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const editorTool: ToolDef = categoryTool(
  "editor",
  "Editor commands, Python execution, PIE, undo/redo, hot reload, viewport, performance, sequencer, build pipeline, logs.",
  {
    execute_command:     bp("execute_command"),
    execute_python:      bp("execute_python"),
    set_property:        bp("set_property"),
    play_in_editor:      bp("pie_control", (p) => ({ action: p.pieAction ?? "status" })),
    get_runtime_value:   bp("get_runtime_value"),
    hot_reload:          bp("hot_reload"),
    undo:                bp("undo"),
    redo:                bp("redo"),
    get_perf_stats:      bp("get_editor_performance_stats"),
    run_stat:            bp("run_stat_command"),
    set_scalability:     bp("set_scalability"),
    capture_screenshot:  bp("capture_screenshot"),
    get_viewport:        bp("get_viewport_info"),
    set_viewport:        bp("set_viewport_camera"),
    focus_on_actor:      bp("focus_viewport_on_actor"),
    create_sequence:     bp("create_level_sequence"),
    get_sequence_info:   bp("get_sequence_info"),
    add_sequence_track:  bp("add_sequence_track"),
    play_sequence:       bp("play_sequence", (p) => ({ assetPath: p.assetPath, action: p.sequenceAction ?? "play" })),
    // Build Pipeline
    build_all:           bp("build_all"),
    build_geometry:      bp("build_geometry"),
    build_hlod:          bp("build_hlod"),
    validate_assets:     bp("validate_assets"),
    get_build_status:    bp("get_build_status"),
    cook_content:        bp("cook_content"),
    // Logs
    get_log:             bp("get_output_log"),
    search_log:          bp("search_log"),
    get_message_log:     bp("get_message_log"),
  },
  `- execute_command: Run console command. Params: command
- execute_python: Run Python in editor. Params: code
- set_property: Set UObject property. Params: objectPath, propertyName, value
- play_in_editor: PIE control. Params: pieAction ('start'|'stop'|'status')
- get_runtime_value: Read PIE actor value. Params: actorLabel, propertyName
- hot_reload: Hot reload C++
- undo/redo: Undo/redo last transaction
- get_perf_stats: Editor performance stats
- run_stat: Run stat command. Params: command
- set_scalability: Set quality. Params: level
- capture_screenshot: Screenshot. Params: filename?, resolution?
- get_viewport: Get viewport camera
- set_viewport: Set viewport camera. Params: location?, rotation?
- focus_on_actor: Focus on actor. Params: actorLabel
- create_sequence: Create Level Sequence. Params: name, packagePath?
- get_sequence_info: Read sequence. Params: assetPath
- add_sequence_track: Add track. Params: assetPath, trackType, actorLabel?
- play_sequence: Play/stop/pause. Params: assetPath, sequenceAction
- build_all: Build all (geometry, lighting, paths, HLOD)
- build_geometry: Rebuild BSP geometry
- build_hlod: Build HLODs
- validate_assets: Run data validation. Params: directory?
- get_build_status: Get build/map status
- cook_content: Cook content. Params: platform?
- get_log: Read output log. Params: maxLines?, filter?, category?
- search_log: Search log. Params: query
- get_message_log: Read message log. Params: logName?`,
  {
    command: z.string().optional(),
    code: z.string().optional(),
    objectPath: z.string().optional(),
    propertyName: z.string().optional(),
    value: z.unknown().optional(),
    pieAction: z.enum(["start", "stop", "status"]).optional(),
    actorLabel: z.string().optional(),
    level: z.string().optional(),
    filename: z.string().optional(),
    resolution: z.number().optional(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    assetPath: z.string().optional(),
    trackType: z.string().optional(),
    sequenceAction: z.enum(["play", "stop", "pause"]).optional(),
    directory: z.string().optional(),
    platform: z.string().optional(),
    maxLines: z.number().optional(),
    filter: z.string().optional(),
    category: z.string().optional(),
    query: z.string().optional(),
    logName: z.string().optional(),
  },
);
