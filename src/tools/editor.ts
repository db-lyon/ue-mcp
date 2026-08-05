import { z } from "zod";
import { categoryTool, bp, directive, type ToolDef, type ToolContext } from "../types.js";
import { startEditor, stopEditor, restartEditor, buildProject } from "../editor-control.js";
import { readEngineState } from "../engine-observer.js";
import { progressRenderingNote } from "../client-quirks.js";
import { pushWorkaround, workaroundCount } from "../workaround-tracker.js";
import { searchTools } from "../tool-search.js";
import { Vec3, Rotator } from "../schemas.js";
import { FunctionArgs, normalizeFunctionArgs, normalizePythonArgs } from "../function-args.js";

/** Where a caller declares a standing opt-in to the Blueprint-error bypass.
 *  Rides the normal global < project < env < local config cascade, so a
 *  developer can enable it in the untracked ue-mcp.local.yml without
 *  committing the relaxed behavior for the whole team. */
const IGNORE_BLUEPRINT_ERRORS_CONFIG_KEY = "ue-mcp.pie.allowIgnoreBlueprintErrors";

export const editorTool: ToolDef = categoryTool(
  "editor",
  "Editor commands, Python execution, PIE, undo/redo, hot reload, viewport, performance, sequencer, build pipeline, logs, editor control.",
  {
    start_editor: {
      description: "Launch Unreal Editor and BLOCK until it is fully ready (not merely until the socket answers), rendering a startup progress bar in the terminal. Returns the phase timeline it waited through. Do NOT poll get_engine_state or get_status afterwards: this call already waited, and a ready editor is the only way it returns success. Params: timeout? (seconds, default 300)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const timeout = typeof p?.timeout === "number" && p.timeout > 0 ? p.timeout : 300;
        const result = await startEditor(ctx.project, timeout, ctx.onProgress);

        // The call blocks for as long as the editor takes, so when its progress
        // is not visible the user is left to conclude the tool hung. Say which
        // of the two possible reasons applied, in the result, once.
        const note = progressRenderingNote(ctx.client);
        if (note) return { ...result, progressDisplayNote: note };
        if (!ctx.onProgress) {
          // Progress is opt-in per request: no token, no stream. This is the
          // client declining it, not the server withholding it.
          return {
            ...result,
            progressDisplayNote:
              `Note: ${ctx.client?.name ?? "this client"}${ctx.client?.version ? ` ${ctx.client.version}` : ""} ` +
              "did not send a progressToken with this call, so no live progress could be streamed - " +
              "MCP progress is opt-in per request. The phase timeline above is what the live view would have shown. " +
              "Clients that request progress (the reference SDK client, MCP Inspector) render it throughout the wait.",
          };
        }
        if (result.success) {
          try { await ctx.bridge.connect(5000); } catch { /* reconnect timer handles it */ }
        }
        return result;
      },
    },
    get_engine_state: {
      description: "What the engine is REALLY doing, read from outside the game thread: startup phase from the editor's own log, process table (PID, command line, responding), the plugin's status snapshot (slow-task name and percent, active modal dialog, game-thread stall), and native dialog windows. Call this ONCE when something is already wrong (handlers timing out, an editor that will not come up). Never call it in a wait loop: start_editor blocks until ready on its own, and polling this during startup burns tokens re-reading state that is already tracked. Params: probeWindows? (default true; scans native windows, costs ~2s)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const probeWindows = p?.probeWindows !== false;
        const state = await readEngineState(ctx.project.projectPath ?? null, { probeWindows });

        // The bridge answers this one on its socket thread without scheduling
        // any game-thread work, so it stays reachable while every other handler
        // is timing out. Prefer it over the on-disk snapshot when it replies,
        // and never let it block the rest of the report.
        let live: unknown = null;
        if (ctx.bridge.isConnected) {
          try {
            live = await ctx.bridge.call("get_engine_state", {});
          } catch {
            live = null;
          }
        }
        return live ? { ...state, snapshot: live, snapshotSource: "bridge" } : { ...state, snapshotSource: "status.json" };
      },
    },
    stop_editor: {
      description: "Close Unreal Editor gracefully (asks the editor to quit itself via the bridge; never an OS kill). Acts only on the editor for the loaded project, resolved from the port lockfile that editor published at <project>/Saved/UE_MCP_Bridge/port.json. With no lockfile there is no port to aim at and the call refuses, naming the file it checked, rather than probing a default port that another project's editor could answer on (#819).",
      handler: async (ctx: ToolContext) => {
        return stopEditor(false, ctx.project.projectDir ?? undefined);
      },
    },
    restart_editor: {
      description: "Stop then start the editor for the loaded project. Editors for other projects are left alone: the stop is aimed by this project's port lockfile, and the decision to start is made from the process holding this project's .uproject open, never from whether some editor is running (#819).",
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
      description: "GATED LAST RESORT. execute_python is unreachable until a semantic tool search over your taskSummary has been run AND every candidate it returns is EXPLICITLY ruled out with a stated reason. Flow: (1) call with taskSummary (+code) - it returns the candidate actions; (2) re-call with the same taskSummary/code PLUS ruledOut=[{action, reason}] giving a specific reason each candidate does not fit. Python runs only once every candidate is ruled out. Params: code, taskSummary (required), ruledOut?, resultVariable? (name of a top-level variable to return as `result`, separate from print()/log; #732) (#704)",
      handler: async (ctx: ToolContext, params: Record<string, unknown>) => {
        const code = (params.code as string) ?? "";
        const taskSummary = ((params.taskSummary as string) ?? "").trim();

        // #704: hard gate. Require an intent statement, run the semantic search,
        // and refuse to run Python until EVERY candidate action is explicitly
        // ruled out with a stated reason.
        if (!taskSummary) {
          return {
            blocked: true,
            reason: "missing_task_summary",
            message: "execute_python requires a 'taskSummary' (plain-words intent). It is searched against the tool registry and gated behind ruling out every candidate. Re-call with taskSummary.",
          };
        }

        // Candidates = meaningful matches (a name/phrase hit), capped at 5.
        const candidates = (await searchTools(taskSummary, 5)).filter((h) => h.score >= 4);
        if (candidates.length > 0) {
          const ruledRaw = Array.isArray(params.ruledOut) ? (params.ruledOut as Array<Record<string, unknown>>) : [];
          const ruled = new Map<string, string>();
          for (const r of ruledRaw) {
            const action = String(r?.action ?? "").trim();
            const reason = String(r?.reason ?? "").trim();
            if (action && reason.length >= 12) ruled.set(action, reason); // non-trivial reason required
          }
          const unresolved = candidates.filter((c) => !ruled.has(c.action));
          if (unresolved.length > 0) {
            pushWorkaround({ code, timestamp: new Date().toISOString(), taskSummary, suggestedTool: candidates.map((c) => `${c.tool}(${c.action})`).join(", ") });
            return {
              blocked: true,
              reason: "candidates_not_ruled_out",
              taskSummary,
              candidates,
              needReasonFor: unresolved.map((c) => `${c.tool}(${c.action})`),
              message: `execute_python is GATED. A tool search for "${taskSummary}" returned ${candidates.length} candidate action(s). Rule out EACH with a specific reason (>=12 chars) via ruledOut:[{action, reason}], then re-call. Still need a reason for: ${unresolved.map((c) => c.action).join(", ")}. If one of these actually does the task, call it instead of Python.`,
            };
          }
        }

        // Gate passed (no candidates, or every candidate ruled out) - run Python.
        // #732: forward an optional resultVariable so scripts can return a value
        // through a first-class `result` channel instead of print()/log.
        const result = await ctx.bridge.call("execute_python", { code, resultVariable: params.resultVariable });

        // Track this workaround in memory, and side-channel to a tmp log so
        // the record survives even if the agent ignores the directive.
        const snippet = typeof result === "object" && result !== null
          ? JSON.stringify(result).slice(0, 200)
          : String(result).slice(0, 200);
        const entry = { code, timestamp: new Date().toISOString(), resultSnippet: snippet, taskSummary };
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
            `[AGENT DIRECTIVE - MANDATORY]`,
            `execute_python was used as a workaround (${n} time(s) this session).`,
            `This means a native ue-mcp tool could not handle the task.`,
            ``,
            `YOUR NEXT MESSAGE TO THE USER must include:`,
            `"I had to use execute_python to <describe what you did and why>.`,
            ` Would you like to submit feedback so this can become a native tool?"`,
            ``,
            `If the user agrees, call feedback(action="submit") with:`,
            `  title  - short description of the gap`,
            `  summary - what was attempted and why the native tool fell short`,
            `  pythonWorkaround - the Python code above`,
            `  idealTool - what tool/action should handle this natively`,
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
    run_python_file: bp("Run a Python file from disk with __file__/__name__ populated (#142). Params: filePath, args?, resultVariable? (name of a top-level variable to return as `result`, separate from logs; #732)", "run_python_file", (p) => ({ filePath: p.filePath, args: normalizePythonArgs(p.args), resultVariable: p.resultVariable })),
    purge_python_modules: bp("Purge cached embedded-Python modules whose name starts with a prefix, so the editor drops stale code after you edit a Python tool on disk. Returns the purged module names + count. Params: prefix (required, non-empty) (#719)", "purge_python_modules", (p) => ({ prefix: p.prefix })),
    close_sequence: bp("Close the currently open Level Sequence editor (Sequencer). Do this before bulk-deleting actors a sequence may possess - open sequences re-resolve possessables by name during destruction and can mis-bind. Returns wasOpen + closedSequence (#718)", "close_sequence"),
    open_tab: bp("Open a registered editor tab by ID so its UI can be screenshotted as evidence (e.g. 'ProjectSettings', 'OutputLog', 'ContentBrowserTab1'). Params: tabId (#727)", "open_tab", (p) => ({ tabId: p.tabId })),
    open_settings: bp("Open (and navigate) a settings viewer for visual settings evidence. Params: container? (Project|Editor; default Project), category? (e.g. 'Engine'), section? (e.g. 'Physics', or a combined 'Engine.Physics') (#727)", "open_settings", (p) => ({ container: p.container, category: p.category, section: p.section })),
    set_property: bp("Set UObject property. Saves the package to disk by default; pass save=false to leave it dirty in-memory (batch many writes, then editor(save_dirty)/asset(save)) (#674). TMap values take either { \"Key\": value } or, for struct keys, [{ key: {...}, value: ... }] - exactly what get_property/describe_object return under `value`. A write that cannot store every entry it was given fails and leaves the old value in place; containers report elementCount on success (#820). Params: objectPath, propertyName, value, save? (default true)", "set_property"),
    get_property: bp("Read UObject property. `value` is structured JSON and is always safe to write straight back with set_property; `valueText` is UE export text and for a struct-keyed TMap does not read back, so valueTextRoundTrips reports whether it can be reused (#820). Params: objectPath, propertyName", "get_property"),
    describe_object: bp("Describe a UObject and optionally list/read properties. Per property, `value` is the round-trippable structured form and valueTextRoundTrips flags export text that is not (#820). Params: objectPath, includeProperties?, includeValues?, propertyNames?", "describe_object"),
    play_in_editor: bp("PIE control. Params: pieAction (start|stop|status), waitForAssetRegistry? (start only; default true - block until the AssetRegistry initial scan completes before requesting PIE, otherwise PIE silently no-ops on cold editor starts), assetRegistryTimeoutSeconds? (default 180) (#406)", "pie_control", (p) => ({ action: p.pieAction ?? "status", waitForAssetRegistry: p.waitForAssetRegistry, assetRegistryTimeoutSeconds: p.assetRegistryTimeoutSeconds })),
    play_in_editor_ignore_blueprint_errors: {
      description: `Start PIE for one launch with the editor's unresolved-Blueprint-error prompt suppressed. PIE then runs whatever bytecode those Blueprints last compiled to, so the launch is authorized per call: set ${IGNORE_BLUEPRINT_ERRORS_CONFIG_KEY} to true in your ue-mcp config to pre-authorize it, otherwise the user answers an MCP approval prompt. The bridge refuses the launch when a Blueprint would have to be recompiled first (dirty non-data Blueprints, errored Level Blueprints) and lists every errored Blueprint it suppressed in loadedErroredBlueprints. Params: waitForAssetRegistry? (default true), assetRegistryTimeoutSeconds? (default 180).`,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const preauthorized = ctx.project.config.pie?.allowIgnoreBlueprintErrors === true;
        let authorizationSource = "config";

        if (!preauthorized) {
          authorizationSource = "user_approval";
          if (!ctx.elicit) {
            return {
              success: false,
              blocked: true,
              code: "approval_required",
              message: `This action needs a user approval prompt, and the connected MCP client did not advertise the elicitation capability. Set ${IGNORE_BLUEPRINT_ERRORS_CONFIG_KEY} to true in ue-mcp.local.yml to pre-authorize it instead.`,
            };
          }

          let approval;
          try {
            approval = await ctx.elicit({
              message: [
                "Start Play In Editor while bypassing unresolved Blueprint compiler-error dialogs?",
                "",
                "PIE may run stale or invalid Blueprint bytecode. Runtime behavior and validation results may be unreliable until those Blueprint errors are fixed.",
                "",
                "Approve only for this PIE launch.",
              ].join("\n"),
              requestedSchema: {
                type: "object",
                properties: {},
              },
            });
          } catch (error) {
            return {
              success: false,
              blocked: true,
              code: "approval_prompt_failed",
              message: error instanceof Error ? error.message : String(error),
            };
          }

          if (approval.action !== "accept") {
            return {
              success: false,
              blocked: true,
              code: approval.action === "decline" ? "user_declined" : "user_cancelled",
              message: "PIE was not started because bypass approval was not granted.",
            };
          }
        }

        return ctx.bridge.call("pie_control", {
          action: "start",
          ignoreBlueprintErrors: true,
          authorizationSource,
          waitForAssetRegistry: p.waitForAssetRegistry,
          assetRegistryTimeoutSeconds: p.assetRegistryTimeoutSeconds,
        });
      },
    },
    get_runtime_value: bp("Read PIE actor property. Params: actorLabel, propertyName (supports dotted paths: component.field or component.struct.field for nested reads on component subobjects, #344/#381)", "get_runtime_value"),
    get_pie_pawn: bp("Resolve the controlled pawn in the active PIE world. Params: playerIndex? (default 0). Returns actorLabel/class/location/rotation (#228/#229)", "get_pie_pawn", (p) => ({ playerIndex: p.playerIndex })),
    list_pie_instances: bp("List the running PIE worlds with their instance id, net mode (standalone|listenServer|dedicatedServer|client), player count and whether they own a game viewport. In a multiplayer PIE session every other runtime action resolves the primary world (the server) unless you pass pieInstance, so this is how you discover that a client exists and what id addresses it. Params: none (#778)", "list_pie_instances"),
    invoke_object_function: bp("Call a UFUNCTION on any UObject, not just a placed actor. Target it with objectPath, or target=gameinstance|gamemode|gamestate|playercontroller|playerpawn|subsystem (subsystem also needs subsystemClass; playercontroller/playerpawn accept playerIndex). The GameInstance, GameMode and subsystems have no actor label, so invoke_function could never reach them. Returns output and return params under returnValues; an unknown function name lists the available ones. Params: functionName, objectPath? | target?, subsystemClass?, playerIndex?, args?, world? (editor|pie|auto), pieInstance? (#739)", "invoke_object_function", (p) => ({ functionName: p.functionName, objectPath: p.objectPath, target: p.target, subsystemClass: p.subsystemClass, playerIndex: p.playerIndex, args: normalizeFunctionArgs(p.args), world: p.world, pieInstance: p.pieInstance })),
    invoke_object_functions: {
      description: "Call 1-64 UFUNCTIONs in order without yielding to the editor tick loop. Each call independently targets a UObject using the same fields as invoke_object_function, so one sequence can span an actor and its components. Calls stop at the first failure; earlier calls are not rolled back. Returns results[] in call order plus completedCalls/requestedCalls, and failedIndex when it stops early, so a retry can resume instead of replaying mutations. Params: calls[] ({functionName, objectPath? | target?, subsystemClass?, playerIndex?, args?}), world? (editor|pie|auto), pieInstance?",
      bridge: "invoke_object_functions",
      timeoutMs: 300_000,
      mapParams: (p) => ({
        calls: Array.isArray(p.calls)
          ? (p.calls as Record<string, unknown>[]).map((c, i) => ({ ...c, args: normalizeFunctionArgs(c.args, `calls[${i}].args`) }))
          : p.calls,
        world: p.world,
        pieInstance: p.pieInstance,
      }),
    },
    read_bone_transforms: bp("Read live skeletal bone and socket transforms off an actor, once. This is a point-in-time read, NOT a time series - for per-frame capture over a window use the pie category's observe actions. Pass bones (bone OR socket names) or omit for every bone up to limit. space=world (default) or component; component space is independent of where the actor is standing. Pass relativeTo (bone OR socket name) to express every sample in that live reference frame; relativeTo supersedes space and is calculated in component space. Also reports the AnimInstance class/path. Params: actorLabel, componentName?, bones?, relativeTo?, space?, limit?, world? (editor|pie|auto), pieInstance? (#756/#757/#761/#764)", "read_bone_transforms", (p) => ({ actorLabel: p.actorLabel, componentName: p.componentName, bones: p.bones, relativeTo: p.relativeTo, space: p.space, limit: p.limit, world: p.world, pieInstance: p.pieInstance })),
    get_object_properties: bp("Read reflected properties off any UObject, with the same targeting as invoke_object_function. Blueprint-declared variables are reflected properties, so they read the same way as native ones. Pass propertyNames to filter; entries may use the Details-panel spelling ('World Context Object' finds WorldContextObject, 'Is Active' finds bIsActive). Names that do not exist come back under missingProperties instead of silently returning nothing. Properties holding a TMap are also reported under `values` in the structured form set_property accepts, because export text cannot carry a struct-keyed map back (#820). Params: objectPath? | target?, subsystemClass?, playerIndex?, propertyNames?, world? (editor|pie|auto), pieInstance? (#739/#802)", "get_object_properties", (p) => ({ objectPath: p.objectPath, target: p.target, subsystemClass: p.subsystemClass, playerIndex: p.playerIndex, propertyNames: p.propertyNames, world: p.world, pieInstance: p.pieInstance, limit: p.limit, maxValueLength: p.maxValueLength })),
    set_movement_mode: bp("Set a live PIE character's movement mode and/or velocity on its CharacterMovementComponent. Modes are named (none|walking|navwalking|falling|swimming|flying|custom) rather than raw enum numbers, because a wrong number reads as success and then behaves as None. Reports previousMode/previousVelocity and reads the mode back afterwards, since SetMovementMode can refuse a mode the character cannot enter (flying with bCanFly off, swimming outside a volume). Params: actorLabel, mode?, customMode? (only with mode='custom'), velocity? {x,y,z}, world? (default pie), pieInstance? (#757)", "set_movement_mode", (p) => ({ actorLabel: p.actorLabel, mode: p.mode, customMode: p.customMode, velocity: p.velocity, world: p.world, pieInstance: p.pieInstance })),
    set_object_property: bp("Write a reflected property on a live UObject instance, with the same targeting as invoke_object_function. Use this for a PIE actor, a spawned widget or any runtime instance: editor(set_property) is the asset path and marks the package dirty and saves it, which a live instance has no business doing. propertyName accepts dotted/indexed paths and the Details-panel spelling. Reports previousValue plus the value read back after the write, so a coerced or clamped write is visible. Nothing is saved; pass postEditChange=true to fire PostEditChangeProperty. Params: propertyName, value, objectPath? | target?, subsystemClass?, playerIndex?, postEditChange?, world? (editor|pie|auto), pieInstance? (#802)", "set_object_property", (p) => ({ propertyName: p.propertyName, value: p.value, objectPath: p.objectPath, target: p.target, subsystemClass: p.subsystemClass, playerIndex: p.playerIndex, postEditChange: p.postEditChange, world: p.world, pieInstance: p.pieInstance })),
    find_object: bp("Resolve or search for a live UObject instance and report the objectPath that addresses it, which is what invoke_object_function / get_object_properties / set_object_property need. Pass objectPath to check one path (returns found/isValid rather than failing when it is gone), or className and/or nameContains to search every loaded object. className takes a short name (StaticMeshActor), a /Script path, a generated class name (WBP_Hud_C) or a Blueprint asset path. This is how you get the path of something spawned at runtime, an editor utility widget or a UMG widget, which no naming convention predicts. Params: objectPath? | className?, nameContains?, outerPath?, exactClass?, includeDefaults?, world? (any (default)|editor|pie), pieInstance?, limit? (default 50) (#802)", "find_object", (p) => ({ objectPath: p.objectPath, className: p.className, nameContains: p.nameContains, outerPath: p.outerPath, exactClass: p.exactClass, includeDefaults: p.includeDefaults, world: p.world, pieInstance: p.pieInstance, limit: p.limit })),
    teleport_runtime_actor: bp("Move a live PIE actor and have it STAY moved. A plain SetActorLocation on a Character is undone by CharacterMovement on the next tick, so this stops the movement component, teleports, and stops it again. Reports actualLocation read back from the actor rather than what was requested. Params: actorLabel, location?, rotation?, stopMovement? (default true), sweep? (default false), world? (default pie), pieInstance? (#770/#777)", "teleport_runtime_actor", (p) => ({ actorLabel: p.actorLabel, location: p.location, rotation: p.rotation, stopMovement: p.stopMovement, sweep: p.sweep, world: p.world, pieInstance: p.pieInstance })),
    invoke_static_function: bp("Call a static UFUNCTION on a UBlueprintFunctionLibrary (no actor instance). invoke_function needs an actor/component target; this targets the library class CDO instead, so it reaches static *_BlueprintOnly libraries (Voxel sculpt/query/stamp), GeometryScript, Kismet math, any function library. Params: className (short name or /Script/Module.Class path), functionName, args? (name -> JSON value, same marshalling as invoke_function), actorArgs? (name -> actor label for UObject* params that are actors, e.g. the sculpt actor), worldContextParam? (name of a UObject* param to fill with the editor/PIE world; auto-detected for params named WorldContextObject), world? (editor|pie). Returns return/out params under returnValues. Discover libraries + functions with list_function_libraries.", "invoke_static_function", (p) => ({ className: p.className, functionName: p.functionName, args: normalizeFunctionArgs(p.args), actorArgs: p.actorArgs, worldContextParam: p.worldContextParam, world: p.world, pieInstance: p.pieInstance })),
    invoke_function: bp("Call a BlueprintCallable / Exec UFUNCTION on a target actor or one of its components. world=editor (the default) runs the function on the actor placed in the level, no PIE session needed, and reports which instance ran it as resolvedActorLabel/resolvedActorPath. actorLabel is matched against placed actors by editor label first, then internal object name, then full object path; a miss is an error naming what was searched (#806). Params: actorLabel, functionName, component? (component subobject name; redirects target from the actor to that component, #382), args? (object; struct values accept a JSON object such as {X,Y,Z} as well as an export-text string), actorArgs? (object mapping UObject* parameter name to actor label, resolved against live actors in the active world; #383), world? (editor|pie). Returns out/return params (#228/#229)", "invoke_function", (p) => ({ actorLabel: p.actorLabel, functionName: p.functionName, component: p.component, args: normalizeFunctionArgs(p.args), actorArgs: p.actorArgs, world: p.world, pieInstance: p.pieInstance })),
    list_function_libraries: bp("Enumerate UBlueprintFunctionLibrary subclasses on this build. Filter by name (case-insensitive substring, e.g. 'GeometryScript' / 'Kismet' / 'Animation'). Returns name, module, and (by default) every static BlueprintCallable function on the library with its tooltip. Use to discover what's available for editor.invoke_function (#455). Params: pattern?, includeFunctions?", "list_function_libraries", (p) => ({ pattern: p.pattern, includeFunctions: p.includeFunctions })),
    set_pie_time_scale: bp("Fast-forward PIE game time. Params: factor (>0). Raises WorldSettings caps and calls SetGlobalTimeDilation.", "set_pie_time_scale"),
    hot_reload: bp("Hot reload C++", "hot_reload"),
    undo: bp("Undo last transaction", "undo"),
    redo: bp("Redo last transaction", "redo"),
    get_perf_stats: bp("Editor performance stats", "get_editor_performance_stats"),
    run_stat: bp("Run a stat overlay. Params: name (bare stat name, e.g. 'unit','fps','game','gpu') OR command (full console command). A bare name is prefixed with 'stat ' (#722).", "run_stat_command", (p) => ({ command: p.command, name: p.name })),
    set_scalability: bp("Set rendering quality via the Scalability system (actually applies + persists, not just sg.* cvars). Params: level (Low|Medium|High|Epic|Cinematic). Returns appliedLevels (#591)", "set_scalability"),
    set_cvars: bp("Bulk-set console variables. Params: cvars ({name: value} object OR [{name, value}] array). Returns per-cvar old/new values and any notFound names (#591)", "set_cvars", (p) => ({ cvars: p.cvars })),
    capture_screenshot: bp("Screenshot. target=pie synchronously captures the selected PIE client game viewport with UMG/Slate UI; target=editor captures the level viewport; target=window synchronously captures a whole Slate window via FSlateApplication::TakeScreenshot - pixel-true for ALL Slate/UMG UI, returns after the PNG is written, and works while the window is unfocused or off-screen. Multi-instance PIE automatically prefers a world with a game viewport; pass pieInstance or worldPath to select explicitly, which for target=window is what picks the PIE client window instead of the active editor window. Every mode captures at the source viewport/window size - use capture_scene_png for a chosen output size. Params: filename?, target? (auto|pie|editor|window), pieInstance?, worldPath?. Returns the resolved PIE instance/world and image dimensions (#226/#724).", "capture_screenshot", (p) => ({ filename: p.filename, target: p.target, pieInstance: p.pieInstance, worldPath: p.worldPath })),
    capture_scene_png: bp("Headless PNG screenshot via SceneCapture2D (works unfocused, guaranteed RGBA8 LDR). focusActorLabel auto-frames the camera on an actor's bounds; world:pie captures the running game world (#599). Params: outputPath, location?, rotation?, focusActorLabel?, focusDirection?, focusMargin?, world? (editor|pie), width? (default 1280), height? (default 720), fov? (default 90) (#148/#599)", "capture_scene_png", (p) => ({ pieInstance: p.pieInstance, outputPath: p.outputPath, location: p.location, rotation: p.rotation, focusActorLabel: p.focusActorLabel, focusDirection: p.focusDirection, focusMargin: p.focusMargin, world: p.world, width: p.width, height: p.height, fov: p.fov, fullyLoadTextures: p.fullyLoadTextures })),
    set_realtime: bp("Toggle realtime update on the level editor viewports so the editor-world sim (Niagara, anims) ticks - otherwise capture_scene_png renders an unticked, empty sim. Params: enabled (default true) (#537)", "set_realtime", (p) => ({ enabled: p.enabled })),
    get_viewport: bp("Get viewport camera", "get_viewport_info"),
    hit_test_viewport_pixel: bp("Ray-cast from a screen pixel through the active editor viewport and return the first hit. Builds the ray from the live viewport's projection matrix (no FOV/aspect guessing). Returns hit + actorLabel/actorClass/componentName/componentClass/materialPath/location/impactPoint/normal/distance/faceIndex/boneName/physicalMaterial. Params: x, y (pixel coords), width? height? (override viewport size when picking from a different-resolution screenshot), maxDistance? (default 200000), ignoreActors? (array of actor labels) (#418)", "hit_test_viewport_pixel", (p) => ({ x: p.x, y: p.y, width: p.width, height: p.height, maxDistance: p.maxDistance, ignoreActors: p.ignoreActors })),
    get_runtime_values: bp("Bulk runtime read across the active world. For each actor/component matching classFilter, resolves every path against the (actor|component) root and returns rows of {actorLabel, actorClass, componentName?, componentClass?, values, errors?}. Paths support property hops, sub-object hops, and zero-arg BlueprintCallable getter calls at any segment (e.g. 'PowerConnector.GetRequired' reaches a UFUNCTION on a UObject sub-object). classFilter matches actor class OR component class - omit to match everything. World defaults to PIE if running, else editor. Params: classFilter?, paths[], world? (editor|pie) (#414)", "get_runtime_values", (p) => ({ classFilter: p.classFilter, paths: p.paths, world: p.world, pieInstance: p.pieInstance })),
    set_viewport: bp("Set viewport camera. Params: location?, rotation?", "set_viewport_camera"),
    focus_on_actor: bp("Focus on actor. Params: actorLabel", "focus_viewport_on_actor"),
    create_sequence: bp("Create Level Sequence. Params: name, packagePath?", "create_level_sequence"),
    get_sequence_info: bp("Read sequence: bindings (possessable/spawnable) with their Sequencer tags (#556), tracks, and optional section detail. Params: assetPath, includeSectionDetails? (attach sockets, first transform key values per track)", "get_sequence_info"),
    add_sequence_track: bp("Add an empty track. Params: assetPath, trackType, actorLabel?", "add_sequence_track"),
    add_sequence_section: bp("Add a section to a track (creating the track if needed), set its start/end in seconds, and for a CameraCut track bind it to a camera. Returns the section index + channel names to key. Params: sequencePath, trackType (Transform|Float|Fade|CameraCut|Audio|Event|SkeletalAnimation), actorLabel? (binding scope), startSeconds?, endSeconds?, cameraActorLabel? (#548)", "add_sequence_section", (p) => ({ sequencePath: p.sequencePath ?? p.assetPath, trackType: p.trackType, actorLabel: p.actorLabel, startSeconds: p.startSeconds, endSeconds: p.endSeconds, cameraActorLabel: p.cameraActorLabel })),
    set_sequence_keyframes: bp("Add keyframes to a section channel. Transform channels: Location.X/Y/Z, Rotation.X/Y/Z (or friendly x/y/z, yaw/pitch/roll); Fade/Float: the float channel. Params: sequencePath, trackType, actorLabel?, sectionIndex? (default 0), channel, keyframes ([{seconds, value}]), interpolation? (cubic|linear) (#548)", "set_sequence_keyframes", (p) => ({ sequencePath: p.sequencePath ?? p.assetPath, trackType: p.trackType, actorLabel: p.actorLabel, sectionIndex: p.sectionIndex, channel: p.channel, keyframes: p.keyframes, interpolation: p.interpolation })),
    set_sequence_playback_range: bp("Set a Level Sequence's playback range in seconds. Params: sequencePath, startSeconds, endSeconds (#548)", "set_sequence_playback_range", (p) => ({ sequencePath: p.sequencePath ?? p.assetPath, startSeconds: p.startSeconds, endSeconds: p.endSeconds })),
    play_sequence: bp("Play/stop/pause a Level Sequence in Sequencer. Pass sequencePath (or assetPath) to target a specific sequence - it is opened first, because the underlying Sequencer commands act on whatever is currently open. Omit it and the call applies to the open sequence and says so. Params: sequencePath? (or assetPath), sequenceAction? (play|pause|stop, default play)", "play_sequence", (p) => ({ sequencePath: p.sequencePath ?? p.assetPath, assetPath: p.assetPath, action: p.sequenceAction ?? "play" })),
    build_all: bp("Build all (geometry, lighting, paths, HLOD)", "build_all"),
    build_geometry: bp("Rebuild BSP geometry", "build_geometry"),
    build_hlod: bp("Build HLODs", "build_hlod"),
    validate_assets: bp("Run data validation. Params: directory?", "validate_assets"),
    get_build_status: bp("Get build/map status", "get_build_status"),
    cook_content: bp("Cook content. Params: platform?", "cook_content"),
    get_log: bp("Read output log. Params: maxLines?, filter?, category?", "get_output_log"),
    search_log: bp("Search log. Params: query", "search_log"),
    get_message_log: bp("Read a Message Log listing (MapCheck, AssetCheck, PIE, LoadErrors, LightingResults...). Call with NO logName to list the registered listings with their error/warning counts, then read one. Counts come from the listing itself; message bodies come from the current page and honour the Message Log tab's severity checkboxes, so when fewer are readable than exist the response says so instead of reading clean. An unknown logName is an error, not an empty log. Blueprint COMPILE results are not here - the compiler makes a listing per Blueprint; use blueprint(compile). Params: logName?, maxLines? (default 200), severity? (severity-name substring)", "get_message_log", (p) => ({ logName: p.logName, maxLines: p.maxLines, severity: p.severity })),
    list_crashes: bp("List crash reports", "list_crashes"),
    get_crash_info: bp("Get crash details. Params: crashFolder", "get_crash_info"),
    check_for_crashes: bp("Check for recent crashes", "check_for_crashes"),
    set_dialog_policy: bp("Auto-respond to dialogs matching a pattern. Params: pattern, response", "set_dialog_policy"),
    clear_dialog_policy: bp("Clear dialog policies. Params: pattern?", "clear_dialog_policy"),
    get_dialog_policy: bp("Get current dialog policies", "get_dialog_policy"),
    list_dialogs: bp("List active modal dialogs, with title, message text and button labels. Runs even while a dialog is blocking the editor, when every other handler times out", "list_dialogs"),
    respond_to_dialog: bp("Click a button on the active modal dialog, releasing the game thread. Runs even while the dialog is blocking the editor. Pass action='close' to destroy the dialog window when no button label fits. Params: buttonIndex?, buttonLabel?, action? (escape or close)", "respond_to_dialog"),
    open_asset: bp("Open asset in its editor. Params: assetPath", "open_asset"),
    reload_bridge: bp("Hot-reload Python bridge handlers from disk", "reload_handlers"),
    save_dirty: bp("Flush every dirty package and return a per-package saved/failed map. Use after multi-step CDO/component edits when set_class_default leaves the asset dirty without persisting (#378). Params: includeMaps? (default true), includeContent? (default true)", "save_dirty", (p) => ({ includeMaps: p.includeMaps, includeContent: p.includeContent })),
    configure_pie: bp("Set ULevelEditorPlaySettings - multi-client PIE, net mode, single-process flag, Play-in-New-Window resolution. Params: numClients?, netMode? (standalone|listen|client), runUnderOneProcess?, launchSeparateServer?, newWindowWidth?, newWindowHeight? (#384/#671)", "configure_pie", (p) => ({ numClients: p.numClients, netMode: p.netMode, runUnderOneProcess: p.runUnderOneProcess, launchSeparateServer: p.launchSeparateServer, newWindowWidth: p.newWindowWidth, newWindowHeight: p.newWindowHeight })),
    get_pie_config: bp("Read current ULevelEditorPlaySettings (numClients, netMode, single-process, separate-server) (#384)", "get_pie_config"),
    pie_set_player_view: bp("Point the running PIE player's view (control rotation) at a pitch/yaw/roll so a capture frames the intended direction. Requires PIE. Params: pitch?, yaw?, roll? (#671)", "pie_set_player_view", (p) => ({ pitch: p.pitch, yaw: p.yaw, roll: p.roll })),
    stage_game_input: bp("Stage input for the running game: set input mode (gameOnly|gameAndUI|uiOnly) and mouse cursor so injected/simulated input reaches the pawn. This only sets the mode - the injection itself lives in the pie category (pie(inject_input*)), not here. Requires PIE. Params: inputMode? (default gameOnly), showMouseCursor? (#671)", "stage_game_input", (p) => ({ inputMode: p.inputMode, showMouseCursor: p.showMouseCursor })),
    run_automation_tests: bp("Run registered Automation tests matching a filter and return per-test pass/fail plus error lines. Runs them synchronously through the test framework rather than the console queue, and suspends the editor's unfocused-CPU throttle for the duration - otherwise an unfocused editor drops to a few FPS and the framework's interactive-frame-rate gate never opens, leaving tests queued forever (#765). Params: filter?, maxTests? (default 50) (#693)", "run_automation_tests", (p) => ({ filter: p.filter, maxTests: p.maxTests })),
    list_dirty_packages: bp("Enumerate currently-dirty content + map packages, read from the editor's own dirty-package lists (the same ones Save All uses). Includes a never-saved /Temp world, because an unsaved new map is exactly the unsaved work a caller needs to see before closing or reloading (#340)", "list_dirty_packages"),
    request_editor_shutdown: bp("Ask the editor to close itself from inside the engine, after it has checked that closing is safe. Refuses by default when any content or map package is dirty (including an unsaved /Temp world) and reports which ones, so nothing is lost to a silent discard. Ends an active PIE/SIE session first and closes only once play has actually stopped. The response is returned before the process exits. Use editor(stop_editor) for the full stop-and-confirm flow; this action is the in-engine half of it. Params: requireClean? (default true), endPIE? (default true)", "request_editor_shutdown", (p) => ({ requireClean: p.requireClean, endPIE: p.endPIE })),
  },
  undefined,
  {
    command: z.string().optional(),
    code: z.string().optional(),
    resultVariable: z.string().optional().describe("execute_python/run_python_file: name of a top-level Python variable to return as `result`, separate from print()/log output (#732)"),
    prefix: z.string().optional().describe("purge_python_modules: purge sys.modules entries starting with this prefix (#719)"),
    tabId: z.string().optional().describe("open_tab: registered editor tab ID, e.g. 'ProjectSettings' (#727)"),
    container: z.string().optional().describe("open_settings: settings container - Project | Editor (#727)"),
    section: z.string().optional().describe("open_settings: settings section, e.g. 'Physics' or 'Engine.Physics' (#727)"),
    taskSummary: z.string().optional().describe("execute_python: plain-words intent, searched against the tool registry to gate the call (#704)"),
    ruledOut: z.array(z.object({ action: z.string(), reason: z.string() })).optional().describe("execute_python: reason each searched candidate action does not fit; every candidate must be ruled out before Python runs (#704)"),
    filePath: z.string().optional().describe("Absolute path to a .py file for run_python_file"),
    args: FunctionArgs.optional().describe('run_python_file: array of positional args. invoke_function / invoke_object_function / invoke_static_function: object mapping parameter name to value, e.g. {"bEnabled": true}. An entry list ([{"name","value"}]) or a JSON string of either is accepted and normalized (#811)'),
    calls: z.array(z.object({
      functionName: z.string().min(1),
      objectPath: z.string().optional(),
      target: z.string().optional(),
      subsystemClass: z.string().optional(),
      playerIndex: z.number().int().optional(),
      args: FunctionArgs.optional(),
    })).min(1).max(64).optional().describe("invoke_object_functions: ordered UObject calls executed in one game-thread dispatch"),
    objectPath: z.string().optional(),
    target: z.string().optional().describe("capture_screenshot: auto (default) | pie | editor | window. invoke_object_function/get_object_properties: gameinstance | gamemode | gamestate | playercontroller | playerpawn | subsystem (#739)"),
    worldPath: z.string().optional().describe("capture_screenshot: select an exact PIE UWorld path or name"),
    playerIndex: z.number().optional().describe("get_pie_pawn: 0-based player index (default 0)"),
    functionName: z.string().optional(),
    timeout: z.number().optional().describe("start_editor: seconds to wait for the bridge (default 120) (#758)"),
    probeWindows: z.boolean().optional().describe("get_engine_state: also enumerate native windows to catch pre-Slate dialogs (default true, costs ~2s)"),
    requireClean: z.boolean().optional().describe("request_editor_shutdown: refuse to close while any content or map package is dirty (default true)"),
    endPIE: z.boolean().optional().describe("request_editor_shutdown: end an active PIE/SIE session before closing (default true); false refuses to close while play is running"),
    pieInstance: z.number().optional().describe("Select which PIE world to target: 0 = server/primary, 1..N = clients. See list_pie_instances (#778)"),
    subsystemClass: z.string().optional().describe("invoke_object_function/get_object_properties: subsystem class name or /Script path (#739)"),
    bones: z.array(z.string()).optional().describe("read_bone_transforms: bone OR socket names; omit for every bone (#756)"),
    componentName: z.string().optional().describe("read_bone_transforms: which SkeletalMeshComponent to read; omit for the first one (#756)"),
    relativeTo: z.string().optional().describe("read_bone_transforms: express every sample relative to this live bone OR socket; supersedes space"),
    limit: z.number().optional().describe("read_bone_transforms: max bones when 'bones' is omitted (default 200). get_object_properties: max properties returned (default 200). find_object: max matches returned (default 50) (#756/#739/#802)"),
    maxValueLength: z.number().optional().describe("get_object_properties: truncate each exported value past this many characters (default 2000) (#739)"),
    postEditChange: z.boolean().optional().describe("set_object_property: fire PostEditChangeProperty after the write (default false) (#802)"),
    nameContains: z.string().optional().describe("find_object: case-insensitive substring of the object name (#802)"),
    outerPath: z.string().optional().describe("find_object: only objects somewhere under this outer, e.g. one level or world (#802)"),
    exactClass: z.boolean().optional().describe("find_object: match className exactly instead of including subclasses (default false) (#802)"),
    includeDefaults: z.boolean().optional().describe("find_object: include class default objects and archetypes (default false) (#802)"),
    space: z.string().optional().describe("read_bone_transforms: world (default) | component (#756)"),
    stopMovement: z.boolean().optional().describe("teleport_runtime_actor: stop the movement component so the move is not undone (default true) (#777)"),
    mode: z.string().optional().describe("set_movement_mode: none|walking|navwalking|falling|swimming|flying|custom (#757)"),
    customMode: z.number().int().optional().describe("set_movement_mode: 0-255, only with mode='custom' (#757)"),
    velocity: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("set_movement_mode: velocity written to the movement component (#757)"),
    sweep: z.boolean().optional().describe("teleport_runtime_actor: collide on the way (default false) (#777)"),
    component: z.string().optional().describe("invoke_function: optional component subobject name to call the function on instead of the actor (#382)"),
    actorArgs: z.record(z.string()).optional().describe("invoke_function: map of UObject* parameter name to actor label, resolved against live actors in the active world (#383)"),
    className: z.string().optional().describe("invoke_static_function: UBlueprintFunctionLibrary class - short name or /Script/Module.Class path. find_object: class to search for, including Blueprint generated classes (#802)"),
    worldContextParam: z.string().optional().describe("invoke_static_function: name of a UObject* param to fill with the editor/PIE world (auto-detected for params named WorldContextObject)"),
    world: z.string().optional().describe("invoke_function world scope: editor (default) | pie"),
    propertyName: z.string().optional(),
    propertyNames: z.array(z.string()).optional().describe("describe_object: dotted/indexed property paths. get_object_properties: only these properties (#739)"),
    includeProperties: z.boolean().optional().describe("describe_object: include reflected property metadata (default true)"),
    includeValues: z.boolean().optional().describe("describe_object: include current property values (default false)"),
    value: z.unknown().optional(),
    save: z.boolean().optional().describe("set_property: save package to disk after the write (default true; false leaves it dirty) (#674)"),
    pieAction: z.enum(["start", "stop", "status"]).optional(),
    waitForAssetRegistry: z.boolean().optional().describe("play_in_editor start: block until AssetRegistry finishes the initial scan (default true)"),
    assetRegistryTimeoutSeconds: z.number().optional().describe("play_in_editor start: wait budget for the AssetRegistry scan (default 180s)"),
    actorLabel: z.string().optional(),
    level: z.string().optional(),
    filename: z.string().optional(),
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
    maxTests: z.number().optional().describe("run_automation_tests: cap on tests to run (default 50) (#693)"),
    category: z.string().optional(),
    query: z.string().optional(),
    logName: z.string().optional().describe("get_message_log: listing name; omit to enumerate the registered listings"),
    severity: z.string().optional().describe("get_message_log: severity-name substring (Error|Warning|PerformanceWarning|Info)"),
    crashFolder: z.string().optional(),
    pattern: z.string().optional().describe("Substring filter - dialog title/message, or library name for list_function_libraries (#455)"),
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
    focusActorLabel: z.string().optional().describe("capture_scene_png: auto-frame the camera on this actor's bounds (#599)"),
    focusDirection: Vec3.optional().describe("capture_scene_png: framing direction from the actor (default front/above) (#599)"),
    focusMargin: z.number().optional().describe("capture_scene_png: bounds fill margin, higher pulls back (default 1.5) (#599)"),
    fullyLoadTextures: z.boolean().optional().describe("capture_scene_png: force-stream textures + flush render thread before capture to avoid the checker/stale frame (default true) (#662)"),
    x: z.number().optional().describe("hit_test_viewport_pixel: viewport pixel X"),
    y: z.number().optional().describe("hit_test_viewport_pixel: viewport pixel Y"),
    maxDistance: z.number().optional().describe("hit_test_viewport_pixel: max ray length in cm (default 200000)"),
    ignoreActors: z.array(z.string()).optional().describe("hit_test_viewport_pixel: actor labels to skip"),
    classFilter: z.string().optional().describe("get_runtime_values: actor or component class name (omit for all)"),
    paths: z.array(z.string()).optional().describe("get_runtime_values: dotted property/function paths to evaluate per match"),
    includeMaps: z.boolean().optional().describe("save_dirty: include map packages (default true)"),
    includeContent: z.boolean().optional().describe("save_dirty: include content packages (default true)"),
    cvars: z.union([z.record(z.unknown()), z.array(z.object({ name: z.string(), value: z.unknown() }))]).optional().describe("set_cvars: {name: value} object or [{name, value}] array of console variables (#591)"),
    numClients: z.number().optional().describe("configure_pie: number of PIE clients"),
    netMode: z.string().optional().describe("configure_pie: standalone | listen | client"),
    runUnderOneProcess: z.boolean().optional().describe("configure_pie: single-process flag"),
    launchSeparateServer: z.boolean().optional().describe("configure_pie: separate dedicated server"),
    newWindowWidth: z.number().optional().describe("configure_pie: Play-in-New-Window width (#671)"),
    newWindowHeight: z.number().optional().describe("configure_pie: Play-in-New-Window height (#671)"),
    pitch: z.number().optional().describe("pie_set_player_view: control-rotation pitch (#671)"),
    yaw: z.number().optional().describe("pie_set_player_view: control-rotation yaw (#671)"),
    roll: z.number().optional().describe("pie_set_player_view: control-rotation roll (#671)"),
    inputMode: z.string().optional().describe("stage_game_input: gameOnly|gameAndUI|uiOnly (#671)"),
    showMouseCursor: z.boolean().optional().describe("stage_game_input: show mouse cursor (#671)"),
  },
);
