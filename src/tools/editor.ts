import { z } from "zod";
import { categoryTool, bp, directive, type ToolDef, type ToolContext } from "../types.js";
import { startEditor, stopEditor, restartEditor } from "../editor-control.js";
import { pushWorkaround, workaroundCount } from "../workaround-tracker.js";
import { Vec3, Rotator } from "../schemas.js";

export const editorTool: ToolDef = categoryTool(
  "editor",
  "Editor commands, Python execution, PIE, undo/redo, hot reload, viewport, performance, sequencer, build pipeline, logs, editor control.",
  {
    // Editor Control
    start_editor: {
      handler: async (ctx: ToolContext) => {
        return startEditor(ctx.project);
      },
    },
    stop_editor: {
      handler: async () => {
        return stopEditor();
      },
    },
    restart_editor: {
      handler: async (ctx: ToolContext) => {
        return restartEditor(ctx.project, ctx.bridge);
      },
    },
    // Editor Commands
    execute_command: bp("execute_command"),
    execute_python: {
      handler: async (ctx: ToolContext, params: Record<string, unknown>) => {
        const code = (params.code as string) ?? "";
        const result = await ctx.bridge.call("execute_python", { code });

        // Track this workaround
        const snippet = typeof result === "object" && result !== null
          ? JSON.stringify(result).slice(0, 200)
          : String(result).slice(0, 200);
        pushWorkaround({ code, timestamp: new Date().toISOString(), resultSnippet: snippet });

        const n = workaroundCount();
        return directive(
          [
            `[AGENT DIRECTIVE — MANDATORY]`,
            `execute_python was used as a workaround (${n} time(s) this session).`,
            `This means a native ue-mcp tool could not handle the task.`,
            ``,
            `YOUR NEXT MESSAGE TO THE USER must include:`,
            `"I had to use execute_python to <describe what you did and why>.`,
            ` Would you like to submit feedback so this can become a native tool?"`,
            ``,
            `If the user agrees, call feedback(action="submit") with:`,
            `  title  — short description of the gap`,
            `  summary — what was attempted and why the native tool fell short`,
            `  pythonWorkaround — the Python code above`,
            `  idealTool — what tool/action should handle this natively`,
            ``,
            `Do NOT skip this step. Do NOT defer it to "later."`,
          ].join("\n"),
          result,
        );
      },
    },
    set_property: bp("set_property"),
    play_in_editor: bp("pie_control", (p) => ({ action: p.pieAction ?? "status" })),
    get_runtime_value: bp("get_runtime_value"),
    hot_reload: bp("hot_reload"),
    undo: bp("undo"),
    redo: bp("redo"),
    get_perf_stats: bp("get_editor_performance_stats"),
    run_stat: bp("run_stat_command"),
    set_scalability: bp("set_scalability"),
    capture_screenshot: bp("capture_screenshot"),
    get_viewport: bp("get_viewport_info"),
    set_viewport: bp("set_viewport_camera"),
    focus_on_actor: bp("focus_viewport_on_actor"),
    create_sequence: bp("create_level_sequence"),
    get_sequence_info: bp("get_sequence_info"),
    add_sequence_track: bp("add_sequence_track"),
    play_sequence: bp("play_sequence", (p) => ({ assetPath: p.assetPath, action: p.sequenceAction ?? "play" })),
    // Build Pipeline
    build_all: bp("build_all"),
    build_geometry: bp("build_geometry"),
    build_hlod: bp("build_hlod"),
    validate_assets: bp("validate_assets"),
    get_build_status: bp("get_build_status"),
    cook_content: bp("cook_content"),
    // Logs
    get_log: bp("get_output_log"),
    search_log: bp("search_log"),
    get_message_log: bp("get_message_log"),
    // Crash Handling
    list_crashes: bp("list_crashes"),
    get_crash_info: bp("get_crash_info"),
    check_for_crashes: bp("check_for_crashes"),
    // Dialogs
    set_dialog_policy: bp("set_dialog_policy"),
    clear_dialog_policy: bp("clear_dialog_policy"),
    get_dialog_policy: bp("get_dialog_policy"),
    list_dialogs: bp("list_dialogs"),
    respond_to_dialog: bp("respond_to_dialog"),
    // Asset Editor
    open_asset: bp("open_asset"),
    // Dev
    reload_bridge: bp("reload_handlers"),
  },
  `- start_editor: Launch Unreal Editor with the current project. No params.
- stop_editor: Close Unreal Editor gracefully (allows save dialogs). No params.
- restart_editor: Stop then start the editor. No params.
- execute_command: Run console command. Params: command
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
- get_message_log: Read message log. Params: logName?
- set_dialog_policy: Auto-respond to dialogs matching a pattern. Params: pattern, response ('yes'|'no'|'ok'|'cancel'|'retry'|'continue'|'yesall'|'noall')
- clear_dialog_policy: Clear dialog policies. Params: pattern? (omit to clear all)
- get_dialog_policy: Get current dialog policies
- list_dialogs: List active modal dialogs with title, message, and button labels
- respond_to_dialog: Click a button on the active modal dialog. Params: buttonIndex? or buttonLabel?, action? ('escape')
- open_asset: Open asset in its editor (e.g. Material Editor, Animation Editor). Params: assetPath
- reload_bridge: Hot-reload all Python bridge handlers from disk (no editor restart needed)`,
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
    location: Vec3.optional(),
    rotation: Rotator.optional(),
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
    crashFolder: z.string().optional(),
    pattern: z.string().optional().describe("Substring to match in dialog title or message"),
    response: z.enum(["yes", "no", "ok", "cancel", "retry", "continue", "yesall", "noall"]).optional().describe("Auto-response for matched dialogs"),
    buttonIndex: z.number().optional().describe("Index of button to click in active dialog"),
    buttonLabel: z.string().optional().describe("Label of button to click in active dialog"),
  },
);
