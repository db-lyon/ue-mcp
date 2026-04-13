import { z } from "zod";
import { categoryTool, bp, directive, type ToolDef, type ToolContext } from "../types.js";
import { startEditor, stopEditor, restartEditor } from "../editor-control.js";
import { pushWorkaround, workaroundCount } from "../workaround-tracker.js";
import { Vec3, Rotator } from "../schemas.js";

export const editorTool: ToolDef = categoryTool(
  "editor",
  "Editor commands, Python execution, PIE, undo/redo, hot reload, viewport, performance, sequencer, build pipeline, logs, editor control.",
  {
    start_editor: {
      description: "Launch Unreal Editor with the current project and reconnect bridge",
      handler: async (ctx: ToolContext) => {
        const result = await startEditor(ctx.project);
        if (result.success) {
          try { await ctx.bridge.connect(5000); } catch { /* reconnect timer handles it */ }
        }
        return result;
      },
    },
    stop_editor: {
      description: "Close Unreal Editor gracefully",
      handler: async () => {
        return stopEditor();
      },
    },
    restart_editor: {
      description: "Stop then start the editor",
      handler: async (ctx: ToolContext) => {
        return restartEditor(ctx.project, ctx.bridge);
      },
    },
    execute_command: bp("Run console command. Params: command", "execute_command"),
    execute_python: {
      description: "Run Python in editor. Params: code",
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
    set_property: bp("Set UObject property. Params: objectPath, propertyName, value", "set_property"),
    play_in_editor: bp("PIE control. Params: pieAction (start|stop|status)", "pie_control", (p) => ({ action: p.pieAction ?? "status" })),
    get_runtime_value: bp("Read PIE actor value. Params: actorLabel, propertyName", "get_runtime_value"),
    hot_reload: bp("Hot reload C++", "hot_reload"),
    undo: bp("Undo last transaction", "undo"),
    redo: bp("Redo last transaction", "redo"),
    get_perf_stats: bp("Editor performance stats", "get_editor_performance_stats"),
    run_stat: bp("Run stat command. Params: command", "run_stat_command"),
    set_scalability: bp("Set quality. Params: level", "set_scalability"),
    capture_screenshot: bp("Screenshot. Params: filename?, resolution?", "capture_screenshot"),
    get_viewport: bp("Get viewport camera", "get_viewport_info"),
    set_viewport: bp("Set viewport camera. Params: location?, rotation?", "set_viewport_camera"),
    focus_on_actor: bp("Focus on actor. Params: actorLabel", "focus_viewport_on_actor"),
    create_sequence: bp("Create Level Sequence. Params: name, packagePath?", "create_level_sequence"),
    get_sequence_info: bp("Read sequence. Params: assetPath", "get_sequence_info"),
    add_sequence_track: bp("Add track. Params: assetPath, trackType, actorLabel?", "add_sequence_track"),
    play_sequence: bp("Play/stop/pause sequence. Params: assetPath, sequenceAction", "play_sequence", (p) => ({ assetPath: p.assetPath, action: p.sequenceAction ?? "play" })),
    build_all: bp("Build all (geometry, lighting, paths, HLOD)", "build_all"),
    build_geometry: bp("Rebuild BSP geometry", "build_geometry"),
    build_hlod: bp("Build HLODs", "build_hlod"),
    validate_assets: bp("Run data validation. Params: directory?", "validate_assets"),
    get_build_status: bp("Get build/map status", "get_build_status"),
    cook_content: bp("Cook content. Params: platform?", "cook_content"),
    get_log: bp("Read output log. Params: maxLines?, filter?, category?", "get_output_log"),
    search_log: bp("Search log. Params: query", "search_log"),
    get_message_log: bp("Read message log. Params: logName?", "get_message_log"),
    list_crashes: bp("List crash reports", "list_crashes"),
    get_crash_info: bp("Get crash details. Params: crashFolder", "get_crash_info"),
    check_for_crashes: bp("Check for recent crashes", "check_for_crashes"),
    set_dialog_policy: bp("Auto-respond to dialogs matching a pattern. Params: pattern, response", "set_dialog_policy"),
    clear_dialog_policy: bp("Clear dialog policies. Params: pattern?", "clear_dialog_policy"),
    get_dialog_policy: bp("Get current dialog policies", "get_dialog_policy"),
    list_dialogs: bp("List active modal dialogs", "list_dialogs"),
    respond_to_dialog: bp("Click a button on the active modal dialog. Params: buttonIndex?, buttonLabel?", "respond_to_dialog"),
    open_asset: bp("Open asset in its editor. Params: assetPath", "open_asset"),
    reload_bridge: bp("Hot-reload Python bridge handlers from disk", "reload_handlers"),
  },
  undefined,
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
