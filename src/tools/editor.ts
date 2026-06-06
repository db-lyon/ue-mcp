import { z } from "zod";
import { categoryTool, bp, directive, type ToolDef, type ToolContext } from "../types.js";
import { startEditor, stopEditor, restartEditor, buildProject } from "../editor-control.js";
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
    build_project: {
      description: "Build the project's C++ code using Unreal Build Tool. Editor should be stopped first.",
      handler: async (ctx: ToolContext) => {
        ctx.project.ensureLoaded();
        const lines: string[] = [];
        const result = await buildProject(ctx.project.projectPath!, {
          onOutput: (text) => lines.push(text),
        });
        return { ...result, output: lines.join("") };
      },
    },
    execute_command: bp("Run console command. Params: command", "execute_command"),
    execute_python: {
      description: "Run Python in editor. Params: code",
      handler: async (ctx: ToolContext, params: Record<string, unknown>) => {
        const code = (params.code as string) ?? "";
        const result = await ctx.bridge.call("execute_python", { code });

        // Track this workaround in memory, and side-channel to a tmp log so
        // the record survives even if the agent ignores the directive.
        const snippet = typeof result === "object" && result !== null
          ? JSON.stringify(result).slice(0, 200)
          : String(result).slice(0, 200);
        const entry = { code, timestamp: new Date().toISOString(), resultSnippet: snippet };
        pushWorkaround(entry);
        try {
          const os = await import("node:os");
          const fs = await import("node:fs");
          const path = await import("node:path");
          fs.appendFileSync(
            path.join(os.tmpdir(), "ue-mcp-workarounds.log"),
            JSON.stringify(entry) + "\n",
          );
        } catch {
          // side-channel is best-effort; primary tracking is the in-memory stack
        }

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
          {
            kind: "workaround.feedback",
            requiredActions: [
              "surface_workaround_to_user",
              "ask_if_user_wants_to_submit_feedback",
              "on_agreement_call_feedback_submit",
            ],
            context: {
              workaroundCount: n,
              feedbackTool: "feedback",
              feedbackAction: "submit",
              expectedFields: ["title", "summary", "pythonWorkaround", "idealTool"],
            },
          },
        );
      },
    },
    run_python_file: bp("Run a Python file from disk with __file__/__name__ populated (#142). Params: filePath, args?", "run_python_file", (p) => ({ filePath: p.filePath, args: p.args })),
    set_property: bp("Set UObject property. Params: objectPath, propertyName, value", "set_property"),
    play_in_editor: bp("PIE control. Params: pieAction (start|stop|status), waitForAssetRegistry? (start only; default true - block until the AssetRegistry initial scan completes before requesting PIE, otherwise PIE silently no-ops on cold editor starts), assetRegistryTimeoutSeconds? (default 180) (#406)", "pie_control", (p) => ({ action: p.pieAction ?? "status", waitForAssetRegistry: p.waitForAssetRegistry, assetRegistryTimeoutSeconds: p.assetRegistryTimeoutSeconds })),
    get_runtime_value: bp("Read PIE actor property. Params: actorLabel, propertyName (supports dotted paths: component.field or component.struct.field for nested reads on component subobjects, #344/#381)", "get_runtime_value"),
    get_pie_pawn: bp("Resolve the controlled pawn in the active PIE world. Params: playerIndex? (default 0). Returns actorLabel/class/location/rotation (#228/#229)", "get_pie_pawn", (p) => ({ playerIndex: p.playerIndex })),
    invoke_function: bp("Call a BlueprintCallable / Exec UFUNCTION on a target actor or one of its components. Params: actorLabel, functionName, component? (component subobject name; redirects target from the actor to that component, #382), args? (object), actorArgs? (object mapping UObject* parameter name to actor label, resolved against live actors in the active world; #383), world? (editor|pie). Returns out/return params (#228/#229)", "invoke_function", (p) => ({ actorLabel: p.actorLabel, functionName: p.functionName, component: p.component, args: p.args, actorArgs: p.actorArgs, world: p.world })),
    invoke_static_function: bp("Call a static UFUNCTION on a UBlueprintFunctionLibrary (no actor instance). invoke_function needs an actor/component target; this targets the library class CDO instead, so it reaches static *_BlueprintOnly libraries (Voxel sculpt/query/stamp), GeometryScript, Kismet math, any function library. Params: className (short name or /Script/Module.Class path), functionName, args? (name -> JSON value, same marshalling as invoke_function), actorArgs? (name -> actor label for UObject* params that are actors, e.g. the sculpt actor), worldContextParam? (name of a UObject* param to fill with the editor/PIE world; auto-detected for params named WorldContextObject), world? (editor|pie). Returns return/out params under returnValues. Discover libraries + functions with list_function_libraries.", "invoke_static_function", (p) => ({ className: p.className, functionName: p.functionName, args: p.args, actorArgs: p.actorArgs, worldContextParam: p.worldContextParam, world: p.world })),
    list_function_libraries: bp("Enumerate UBlueprintFunctionLibrary subclasses on this build. Filter by name (case-insensitive substring, e.g. 'GeometryScript' / 'Kismet' / 'Animation'). Returns name, module, and (by default) every static BlueprintCallable function on the library with its tooltip. Use to discover what's available for editor.invoke_function (#455). Params: pattern?, includeFunctions?", "list_function_libraries", (p) => ({ pattern: p.pattern, includeFunctions: p.includeFunctions })),
    set_pie_time_scale: bp("Fast-forward PIE game time. Params: factor (>0). Raises WorldSettings caps and calls SetGlobalTimeDilation.", "set_pie_time_scale"),
    hot_reload: bp("Hot reload C++", "hot_reload"),
    undo: bp("Undo last transaction", "undo"),
    redo: bp("Redo last transaction", "redo"),
    get_perf_stats: bp("Editor performance stats", "get_editor_performance_stats"),
    run_stat: bp("Run stat command. Params: command", "run_stat_command"),
    set_scalability: bp("Set quality. Params: level", "set_scalability"),
    capture_screenshot: bp("Screenshot. Params: filename?, resolution?, target? (auto|pie|editor; auto routes to PIE viewport when PIE is running) (#226)", "capture_screenshot"),
    capture_scene_png: bp("Headless PNG screenshot via SceneCapture2D (works unfocused, guaranteed RGBA8 LDR). Params: outputPath, location?, rotation?, width? (default 1280), height? (default 720), fov? (default 90) (#148)", "capture_scene_png", (p) => ({ outputPath: p.outputPath, location: p.location, rotation: p.rotation, width: p.width, height: p.height, fov: p.fov })),
    set_realtime: bp("Toggle realtime update on the level editor viewports so the editor-world sim (Niagara, anims) ticks - otherwise capture_scene_png renders an unticked, empty sim. Params: enabled (default true) (#537)", "set_realtime", (p) => ({ enabled: p.enabled })),
    get_viewport: bp("Get viewport camera", "get_viewport_info"),
    hit_test_viewport_pixel: bp("Ray-cast from a screen pixel through the active editor viewport and return the first hit. Builds the ray from the live viewport's projection matrix (no FOV/aspect guessing). Returns hit + actorLabel/actorClass/componentName/componentClass/materialPath/location/impactPoint/normal/distance/faceIndex/boneName/physicalMaterial. Params: x, y (pixel coords), width? height? (override viewport size when picking from a different-resolution screenshot), maxDistance? (default 200000), ignoreActors? (array of actor labels) (#418)", "hit_test_viewport_pixel", (p) => ({ x: p.x, y: p.y, width: p.width, height: p.height, maxDistance: p.maxDistance, ignoreActors: p.ignoreActors })),
    get_runtime_values: bp("Bulk runtime read across the active world. For each actor/component matching classFilter, resolves every path against the (actor|component) root and returns rows of {actorLabel, actorClass, componentName?, componentClass?, values, errors?}. Paths support property hops, sub-object hops, and zero-arg BlueprintCallable getter calls at any segment (e.g. 'PowerConnector.GetRequired' reaches a UFUNCTION on a UObject sub-object). classFilter matches actor class OR component class - omit to match everything. World defaults to PIE if running, else editor. Params: classFilter?, paths[], world? (editor|pie) (#414)", "get_runtime_values", (p) => ({ classFilter: p.classFilter, paths: p.paths, world: p.world })),
    set_viewport: bp("Set viewport camera. Params: location?, rotation?", "set_viewport_camera"),
    focus_on_actor: bp("Focus on actor. Params: actorLabel", "focus_viewport_on_actor"),
    create_sequence: bp("Create Level Sequence. Params: name, packagePath?", "create_level_sequence"),
    get_sequence_info: bp("Read sequence. Params: assetPath, includeSectionDetails? (attach sockets, first transform key values per track)", "get_sequence_info"),
    add_sequence_track: bp("Add an empty track. Params: assetPath, trackType, actorLabel?", "add_sequence_track"),
    add_sequence_section: bp("Add a section to a track (creating the track if needed), set its start/end in seconds, and for a CameraCut track bind it to a camera. Returns the section index + channel names to key. Params: sequencePath, trackType (Transform|Float|Fade|CameraCut|Audio|Event|SkeletalAnimation), actorLabel? (binding scope), startSeconds?, endSeconds?, cameraActorLabel? (#548)", "add_sequence_section", (p) => ({ sequencePath: p.sequencePath ?? p.assetPath, trackType: p.trackType, actorLabel: p.actorLabel, startSeconds: p.startSeconds, endSeconds: p.endSeconds, cameraActorLabel: p.cameraActorLabel })),
    set_sequence_keyframes: bp("Add keyframes to a section channel. Transform channels: Location.X/Y/Z, Rotation.X/Y/Z (or friendly x/y/z, yaw/pitch/roll); Fade/Float: the float channel. Params: sequencePath, trackType, actorLabel?, sectionIndex? (default 0), channel, keyframes ([{seconds, value}]), interpolation? (cubic|linear) (#548)", "set_sequence_keyframes", (p) => ({ sequencePath: p.sequencePath ?? p.assetPath, trackType: p.trackType, actorLabel: p.actorLabel, sectionIndex: p.sectionIndex, channel: p.channel, keyframes: p.keyframes, interpolation: p.interpolation })),
    set_sequence_playback_range: bp("Set a Level Sequence's playback range in seconds. Params: sequencePath, startSeconds, endSeconds (#548)", "set_sequence_playback_range", (p) => ({ sequencePath: p.sequencePath ?? p.assetPath, startSeconds: p.startSeconds, endSeconds: p.endSeconds })),
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
    save_dirty: bp("Flush every dirty package and return a per-package saved/failed map. Use after multi-step CDO/component edits when set_class_default leaves the asset dirty without persisting (#378). Params: includeMaps? (default true), includeContent? (default true)", "save_dirty", (p) => ({ includeMaps: p.includeMaps, includeContent: p.includeContent })),
    configure_pie: bp("Set ULevelEditorPlaySettings - multi-client PIE, net mode, single-process flag. Params: numClients?, netMode? (standalone|listen|client), runUnderOneProcess?, launchSeparateServer? (#384)", "configure_pie", (p) => ({ numClients: p.numClients, netMode: p.netMode, runUnderOneProcess: p.runUnderOneProcess, launchSeparateServer: p.launchSeparateServer })),
    get_pie_config: bp("Read current ULevelEditorPlaySettings (numClients, netMode, single-process, separate-server) (#384)", "get_pie_config"),
    list_dirty_packages: bp("Enumerate currently-dirty content + map packages (#340)", "list_dirty_packages"),
  },
  undefined,
  {
    command: z.string().optional(),
    code: z.string().optional(),
    filePath: z.string().optional().describe("Absolute path to a .py file for run_python_file"),
    args: z.union([
      z.array(z.string()),
      z.record(z.unknown()),
    ]).optional().describe("run_python_file: array of positional args. invoke_function: object mapping parameter name to value"),
    objectPath: z.string().optional(),
    target: z.string().optional().describe("capture_screenshot target: auto (default) | pie | editor"),
    playerIndex: z.number().optional().describe("get_pie_pawn: 0-based player index (default 0)"),
    functionName: z.string().optional(),
    component: z.string().optional().describe("invoke_function: optional component subobject name to call the function on instead of the actor (#382)"),
    actorArgs: z.record(z.string()).optional().describe("invoke_function: map of UObject* parameter name to actor label, resolved against live actors in the active world (#383)"),
    className: z.string().optional().describe("invoke_static_function: UBlueprintFunctionLibrary class - short name or /Script/Module.Class path"),
    worldContextParam: z.string().optional().describe("invoke_static_function: name of a UObject* param to fill with the editor/PIE world (auto-detected for params named WorldContextObject)"),
    world: z.string().optional().describe("invoke_function world scope: editor (default) | pie"),
    propertyName: z.string().optional(),
    value: z.unknown().optional(),
    pieAction: z.enum(["start", "stop", "status"]).optional(),
    waitForAssetRegistry: z.boolean().optional().describe("play_in_editor start: block until AssetRegistry finishes the initial scan (default true)"),
    assetRegistryTimeoutSeconds: z.number().optional().describe("play_in_editor start: wait budget for the AssetRegistry scan (default 180s)"),
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
    sequencePath: z.string().optional().describe("Level Sequence asset path for sequencer authoring (#548)"),
    startSeconds: z.number().optional().describe("Section/playback range start in seconds (#548)"),
    endSeconds: z.number().optional().describe("Section/playback range end in seconds (#548)"),
    cameraActorLabel: z.string().optional().describe("add_sequence_section CameraCut: camera actor to bind (#548)"),
    sectionIndex: z.number().optional().describe("set_sequence_keyframes: target section index (default 0) (#548)"),
    channel: z.string().optional().describe("set_sequence_keyframes: channel name (Location.X, Rotation.Z, yaw, fade...) (#548)"),
    keyframes: z.array(z.object({ seconds: z.number(), value: z.number() })).optional().describe("set_sequence_keyframes: [{seconds, value}] (#548)"),
    interpolation: z.string().optional().describe("set_sequence_keyframes: cubic (default) or linear (#548)"),
    sequenceAction: z.enum(["play", "stop", "pause"]).optional(),
    directory: z.string().optional(),
    platform: z.string().optional(),
    maxLines: z.number().optional(),
    filter: z.string().optional(),
    category: z.string().optional(),
    query: z.string().optional(),
    logName: z.string().optional(),
    crashFolder: z.string().optional(),
    pattern: z.string().optional().describe("Substring filter — dialog title/message, or library name for list_function_libraries (#455)"),
    includeFunctions: z.boolean().optional().describe("list_function_libraries: include each library's function listing (default true) (#455)"),
    response: z.enum(["yes", "no", "ok", "cancel", "retry", "continue", "yesall", "noall"]).optional().describe("Auto-response for matched dialogs"),
    buttonIndex: z.number().optional().describe("Index of button to click in active dialog"),
    buttonLabel: z.string().optional().describe("Label of button to click in active dialog"),
    factor: z.number().optional().describe("Time-scale factor for set_pie_time_scale (e.g. 500)"),
    includeSectionDetails: z.boolean().optional().describe("Include attach sockets + first-key transform values in get_sequence_info"),
    outputPath: z.string().optional().describe("Absolute or project-relative output path for capture_scene_png (e.g. \"Saved/Screenshots/cap.png\")"),
    enabled: z.boolean().optional().describe("set_realtime: enable/disable viewport realtime update (#537)"),
    width: z.number().optional().describe("Capture width in pixels"),
    height: z.number().optional().describe("Capture height in pixels"),
    fov: z.number().optional().describe("Capture FOV in degrees"),
    x: z.number().optional().describe("hit_test_viewport_pixel: viewport pixel X"),
    y: z.number().optional().describe("hit_test_viewport_pixel: viewport pixel Y"),
    maxDistance: z.number().optional().describe("hit_test_viewport_pixel: max ray length in cm (default 200000)"),
    ignoreActors: z.array(z.string()).optional().describe("hit_test_viewport_pixel: actor labels to skip"),
    classFilter: z.string().optional().describe("get_runtime_values: actor or component class name (omit for all)"),
    paths: z.array(z.string()).optional().describe("get_runtime_values: dotted property/function paths to evaluate per match"),
    includeMaps: z.boolean().optional().describe("save_dirty: include map packages (default true)"),
    includeContent: z.boolean().optional().describe("save_dirty: include content packages (default true)"),
    numClients: z.number().optional().describe("configure_pie: number of PIE clients"),
    netMode: z.string().optional().describe("configure_pie: standalone | listen | client"),
    runUnderOneProcess: z.boolean().optional().describe("configure_pie: single-process flag"),
    launchSeparateServer: z.boolean().optional().describe("configure_pie: separate dedicated server"),
  },
);
