import { z } from "zod";
import { categoryTool, type ToolDef, type ToolContext } from "../types.js";
import { directive } from "../directive.js";
import { startEditor, stopEditor, restartEditor, buildProject, resolveOwnedEditor, connectedEditorOf, clientAdvertisesElicitation } from "../editor-control.js";
import { readEngineState, withBridgeSnapshot, type EngineSnapshot } from "../engine-observer.js";
import { progressRenderingNote } from "../client-quirks.js";
import { pushWorkaround, workaroundCount } from "../workaround-tracker.js";
import { searchTools } from "../tool-search.js";
import { evaluateGate, gateRefusalMessage, type GateCandidate } from "../python-gate.js";
import { checkBridgeParity } from "../bridge-parity.js";
import { PLUGIN_UPGRADE_POINTER } from "../bridge.js";
import { actions as epicActions, schema as epicSchema } from "./epic/editor.generated.js";
import { specBp, schema as specSchema } from "./specs/editor.generated.js";

/**
 * Which gate candidates dispatch to a bridge method the connected plugin does
 * not register, read from the parity check. Nothing is excluded when the
 * plugin published no action list, since then nothing is known.
 */
async function notInRunningPlugin(ctx: ToolContext): Promise<(c: GateCandidate) => boolean> {
  const graph = ctx.getToolGraph?.() ?? (await import("../tools.js")).getLiveToolGraph();
  const missing = new Set(checkBridgeParity(graph, ctx.bridge.capabilities).missing);
  if (missing.size === 0) return () => false;
  return (c) => {
    const spec = graph.find((t) => t.name === c.tool)?.actions[c.action];
    return spec?.kind === "bridge" && missing.has(spec.bridge);
  };
}

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
      kind: "handler",
      effect: "mutate",
      description: "Launch Unreal Editor and BLOCK until it is fully ready (not merely until the socket answers), rendering a startup progress bar in the terminal. Returns the phase timeline it waited through. Do NOT poll get_engine_state or get_status afterwards: this call already waited, and a ready editor is the only way it returns success. An editor already running for this project is reported as a failure, because this call launched nothing, with alreadyRunning=true, bridgeReady, and the port it published, so a caller can tell \"there was nothing to do\" from \"the launch broke\" without parsing the sentence. A flow step that expects that outcome sets ignore_failure: true on itself rather than asking this action to call a non-launch a launch. dialogPolicy answers startup prompts before they can wedge the game thread, which is how the post-crash \"Restore Packages\" modal used to stall a launch (#968). Params: timeout? (seconds, default 300), dialogPolicy? (\"pattern=response;pattern=response\", responses as set_dialog_policy takes them)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const timeout = typeof p?.timeout === "number" && p.timeout > 0 ? p.timeout : 300;
        const dialogPolicy = typeof p?.dialogPolicy === "string" && p.dialogPolicy.trim() !== "" ? p.dialogPolicy.trim() : undefined;
        const paramEcho = p?.paramEcho === true;
        const result = await startEditor(ctx.project, timeout, ctx.onProgress, {
          dialogPolicy,
          paramEcho,
        });

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
      kind: "handler",
      effect: "read",
      description: "What the engine is REALLY doing, read from outside the game thread: startup phase from the editor's own log, every process holding this project's .uproject open (PID, command line, responding), the plugin's status snapshot (slow-task name and percent, active modal dialog, game-thread stall), and native dialog windows. `running` follows the strongest evidence: an editor that answered over the bridge is running whatever the process table saw, and a probe that could not run is reported as processProbeFailed rather than as an absent editor (#965). Call this ONCE when something is already wrong (handlers timing out, an editor that will not come up). Never call it in a wait loop: start_editor blocks until ready on its own, and polling this during startup burns tokens re-reading state that is already tracked. Params: probeWindows? (default true; scans native windows, costs ~2s)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const probeWindows = p?.probeWindows !== false;
        const state = await readEngineState(ctx.project.projectPath ?? null, { probeWindows });

        // The bridge answers this one on its socket thread without scheduling
        // any game-thread work, so it stays reachable while every other handler
        // is timing out. Prefer it over the on-disk snapshot when it replies,
        // and never let it block the rest of the report.
        let live: EngineSnapshot | null = null;
        if (ctx.bridge.isConnected) {
          try {
            const answered = await ctx.bridge.call("get_engine_state", {});
            live = answered && typeof answered === "object" ? (answered as EngineSnapshot) : null;
          } catch {
            live = null;
          }
        }
        // An editor served that snapshot, so it exists. Reporting running:false
        // alongside it is the report contradicting itself (#965).
        return live ? withBridgeSnapshot(state, live) : state;
      },
    },
    stop_editor: {
      kind: "handler",
      effect: "mutate",
      description: "Close Unreal Editor gracefully (asks the editor to quit itself via the bridge; never an OS kill). Acts only on the editor for the loaded project, resolved from the port lockfile that editor published at <project>/Saved/UE_MCP_Bridge/port.json. With no lockfile there is no port to aim at and the call refuses, naming the file it checked, rather than probing a default port that another project's editor could answer on (#819). With no editor of this project running there is nothing to quit, and the call fails saying so, with alreadyStopped=true marking that reason apart from a running editor that cannot be reached or refuses on unsaved work. A flow that stops the editor before building sets ignore_failure: true on the stop step. With more than one editor of this project open it closes the one the lockfile names and reports the rest under remainingInstances, so plain success never has to be read as 'no editor of this project is running' (#1072). Unsaved work: the quit is sent and the EDITOR decides. It refuses inside the engine and names every dirty package without scheduling a close, so nothing is lost and no quit is left pending; save them with editor(save_dirty), or close the editor yourself and answer its save prompt by hand. There is deliberately no flag that discards. This action has no dialog behaviour of its own: a modal blocks it exactly as it blocks every other action, refused by the same gate with the same fields. Read the dialog with editor(list_dialogs) and answer it with editor(respond_to_dialog). Params: none",
      handler: async (ctx: ToolContext) => {
        return stopEditor(ctx.project.projectDir ?? undefined, { connected: connectedEditorOf(ctx.bridge) });
      },
    },
    restart_editor: {
      kind: "handler",
      effect: "mutate",
      description: "Stop then start the editor for the loaded project. Editors for other projects are left alone: the stop is aimed by this project's port lockfile, and the decision to start is made from the process holding this project's .uproject open, never from whether some editor is running (#819). The stop half is editor(stop_editor) exactly as it behaves on its own, so a restart refuses on unsaved packages and reports them rather than acting on them, and an editor that was already down is not a reason to refuse the start. Like the stop half it has no dialog behaviour of its own: a modal blocks it through the same gate as every other action. Params: none",
      handler: async (ctx: ToolContext) => {
        return restartEditor(ctx.project, ctx.bridge);
      },
    },
    build_project: {
      kind: "handler",
      effect: "mutate",
      description: "Build the project's C++ code using Unreal Build Tool. Editor should be stopped first. Params: none",
      handler: async (ctx: ToolContext) => {
        ctx.project.ensureLoaded();
        const lines: string[] = [];
        const result = await buildProject(ctx.project.projectPath!, {
          onOutput: (text) => lines.push(text),
        });
        return { ...result, output: lines.join("") };
      },
    },
    execute_command: specBp("unknown", "Run console command.", "execute_command"),
    execute_python: {
      kind: "handler",
      effect: "unknown",
      description: "GATED LAST RESORT. execute_python is unreachable until a semantic tool search over your taskSummary has been run AND every candidate it returns is EXPLICITLY ruled out with a stated reason. Flow: (1) call with taskSummary (+code) - it returns the candidate actions AND the exact ruledOut array to send back; (2) re-call with the same taskSummary/code PLUS that ruledOut=[{action, reason}], each reason at least 12 characters saying why that candidate does not fit. The action field accepts the bare name, tool(action) or tool.action, and rulings are remembered for the session so rewording the taskSummary never asks you to justify the same action twice. A candidate the running plugin does not register cannot do the task, so it needs no ruling: it is listed under notInRunningPlugin with the upgrade command instead. Python runs only once every candidate is ruled out. Params: code, taskSummary (required), ruledOut?, resultVariable? (name of a top-level variable to return as `result`, separate from print()/log; #732) (#704, #938, #960, #1167)",
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
          // #938 / #960: matching is spelling-insensitive and rulings persist
          // for the session, so the strings this refusal prints are exactly the
          // strings that satisfy it, and a reworded summary cannot reset the
          // work already done. See src/python-gate.ts.
          // #1167: a candidate whose bridge method the running plugin does not
          // register cannot do the task, so it is owed no ruling.
          const verdict = evaluateGate(candidates, params.ruledOut, ctx, await notInRunningPlugin(ctx));
          if (verdict.unresolved.length > 0) {
            pushWorkaround({ code, timestamp: new Date().toISOString(), taskSummary, suggestedTool: candidates.map((c) => `${c.tool}(${c.action})`).join(", ") }, ctx);
            return {
              blocked: true,
              reason: "candidates_not_ruled_out",
              taskSummary,
              candidates,
              needReasonFor: verdict.unresolved.map((c) => `${c.tool}(${c.action})`),
              // The array to send back, ready to fill in. #960 asked for this:
              // describing the shape was not enough to make the gate passable.
              sendThisBack: { ruledOut: verdict.ruledOutTemplate },
              alreadyRuledOut: verdict.satisfied,
              ...(verdict.notInPlugin.length > 0
                ? {
                    notInRunningPlugin: verdict.notInPlugin.map((c) => `${c.tool}(${c.action})`),
                    upgrade: PLUGIN_UPGRADE_POINTER,
                  }
                : {}),
              ignoredEntries: verdict.rejected,
              message: gateRefusalMessage(taskSummary, candidates, verdict),
            };
          }
        }

        // Gate passed (no candidates, or every candidate ruled out) - run Python.
        // #732: forward an optional resultVariable so scripts can return a value
        // through a first-class `result` channel instead of print()/log.
        const result = await ctx.bridge.call("execute_python", {
          code,
          resultVariable: params.resultVariable,
          captureLog: params.captureLog,
          maxLogChars: params.maxLogChars,
        });

        // Track this workaround in memory, and side-channel to a tmp log so
        // the record survives even if the agent ignores the directive.
        const snippet = typeof result === "object" && result !== null
          ? JSON.stringify(result).slice(0, 200)
          : String(result).slice(0, 200);
        const entry = { code, timestamp: new Date().toISOString(), resultSnippet: snippet, taskSummary };
        pushWorkaround(entry, ctx);
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

        const n = workaroundCount(ctx);
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
    run_python_file: specBp("mutate", "Run a Python file from disk with __file__/__name__ populated (#142). Pass entryPoint to load the file WITHOUT firing its `if __name__ == \"__main__\"` guard and then call one named function in it, which is how a Tools/ script holding several stages behind a main() is driven a stage at a time; `args` are then that call's positional arguments rather than sys.argv, `kwargs` its keyword arguments, and its return value comes back as `result` with no resultVariable needed. captureLog=false drops everything the script logged except its errors, and maxLogChars keeps only the tail: a script that prints a few hundred lines otherwise returns tens of KB to a caller that wanted one value. (#142/#732/#995)", "run_python_file"),
    purge_python_modules: specBp("mutate", "Purge cached embedded-Python modules whose name starts with a prefix, so the editor drops stale code after you edit a Python tool on disk. Returns the purged module names + count (#719).", "purge_python_modules"),
    close_sequence: specBp("mutate", "Close the currently open Level Sequence editor (Sequencer). Do this before bulk-deleting actors a sequence may possess - open sequences re-resolve possessables by name during destruction and can mis-bind. Returns wasOpen + closedSequence (#718).", "close_sequence"),
    open_tab: specBp("mutate", "Open a registered editor tab by ID so its UI can be screenshotted as evidence (e.g. 'ProjectSettings', 'OutputLog', 'ContentBrowserTab1') (#727).", "open_tab"),
    open_settings: specBp("mutate", "Open (and navigate) a settings viewer for visual settings evidence. section takes a bare name with category, or a combined 'Engine.Physics' (#727).", "open_settings"),
    set_property: specBp("mutate", "Set UObject property. Saves the package to disk by default; pass save=false to leave it dirty in-memory (batch many writes, then editor(save_dirty)/asset(save)) (#674). TMap values take either { \"Key\": value } or, for struct keys, [{ key: {...}, value: ... }] - exactly what get_property/describe_object return under `value`. A write that cannot store every entry it was given fails and leaves the old value in place; containers report elementCount on success (#820). SkeletalMeshAsset/SkinnedAsset on a skinned mesh component go through SetSkinnedAssetAndUpdate, since a raw write crashes the next pose evaluation; for a placed actor use level(set_component_skeletal_mesh) (#1099).", "set_property"),
    get_property: specBp("read", "Read UObject property. `value` is structured JSON and is always safe to write straight back with set_property; `valueText` is UE export text and for a struct-keyed TMap does not read back, so valueTextRoundTrips reports whether it can be reused (#820).", "get_property"),
    describe_object: specBp("read", "Describe a UObject and optionally list/read properties. Per property, `value` is the round-trippable structured form and valueTextRoundTrips flags export text that is not (#820).", "describe_object"),
    play_in_editor: specBp("mutate", "PIE control. A start with a session already active fails with alreadyRunning, and a stop with none active fails with alreadyStopped: neither call changed anything, and the marker names the reason. A flow step that expects either outcome carries ignore_failure: true. pieAction defaults to status. A start waits for the AssetRegistry initial scan by default, since PIE silently no-ops during it on a cold editor (#406).", "pie_control"),
    play_in_editor_ignore_blueprint_errors: {
      kind: "handler",
      effect: "mutate",
      description: `Start PIE for one launch with the editor's unresolved-Blueprint-error prompt suppressed. PIE then runs whatever bytecode those Blueprints last compiled to, so the launch is authorized per call: set ${IGNORE_BLUEPRINT_ERRORS_CONFIG_KEY} to true in your ue-mcp config to pre-authorize it, otherwise the user answers an MCP approval prompt. The bridge refuses the launch when a Blueprint would have to be recompiled first (dirty non-data Blueprints, errored Level Blueprints) and lists every errored Blueprint it suppressed in loadedErroredBlueprints. Params: waitForAssetRegistry? (default true), assetRegistryTimeoutSeconds? (default 180).`,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const preauthorized = ctx.project.config.pie?.allowIgnoreBlueprintErrors === true;
        let authorizationSource = "config";

        if (!preauthorized) {
          authorizationSource = "user_approval";
          // Asked as a capability, not as "is there a function". The server
          // builds the gate at startup, before any client has connected, so
          // ctx.elicit is defined for every client and testing it for undefined
          // made this branch unreachable: a client that advertised nothing fell
          // through to the call below, which throws, and the refusal it got was
          // approval_prompt_failed with the raw error rather than the
          // approval_required one that names the config key to set instead.
          if (!ctx.elicit || !clientAdvertisesElicitation(ctx.elicit)) {
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

        // A method of its own: play_in_editor's pie_control never reads an
        // authorization, so the bypass is reachable only through this gate.
        return ctx.bridge.call("pie_start_ignoring_blueprint_errors", {
          authorizationSource,
          waitForAssetRegistry: p.waitForAssetRegistry,
          assetRegistryTimeoutSeconds: p.assetRegistryTimeoutSeconds,
        });
      },
    },
    get_runtime_value: specBp("read", "Read PIE actor property. Pass actorLabel or actorPath. propertyName supports dotted paths (component.field or component.struct.field) for nested reads on component subobjects (#344/#381).", "get_runtime_value"),
    get_pie_pawn: specBp("read", "Resolve the controlled pawn in the active PIE world. Returns actorLabel/class/location/rotation (#228/#229).", "get_pie_pawn"),
    list_pie_instances: specBp("read", "List the running PIE worlds with their instance id, net mode (standalone|listenServer|dedicatedServer|client), player count and whether they own a game viewport. In a multiplayer PIE session every other runtime action resolves the primary world (the server) unless you pass pieInstance, so this is how you discover that a client exists and what id addresses it (#778).", "list_pie_instances"),
    invoke_object_function: specBp("unknown", "Call a UFUNCTION on any UObject, not just a placed actor. Target it with objectPath, or target=gameinstance|gamemode|gamestate|playercontroller|playerpawn|subsystem (subsystem also needs subsystemClass; playercontroller/playerpawn accept playerIndex). The GameInstance, GameMode and subsystems have no actor label, so invoke_function could never reach them. Returns output and return params under returnValues, with a TArray/TSet/TMap return as real JSON and everything else as export text (#885); an unknown function name lists the available ones. A scripted call runs under the editor script-execution guard, which forces every actor callspace to Local, so a UFUNCTION(Server) executes locally instead of being sent; the result warns when that happened, and deferToNextTick=true queues the call for the next engine tick where it routes normally, at the cost of returning before it runs (#973). To click a UMG button in PIE, use widget(invoke_runtime_function) (#1140). (#739)", "invoke_object_function"),
    invoke_object_functions: { ...specBp("unknown", "Call 1-64 UFUNCTIONs in order without yielding to the editor tick loop. Each call independently targets a UObject using the same fields as invoke_object_function, so one sequence can span an actor and its components. Calls stop at the first failure; earlier calls are not rolled back. Returns results[] in call order plus completedCalls/requestedCalls, and failedIndex when it stops early, so a retry can resume instead of replaying mutations.", "invoke_object_functions"), timeoutMs: 300_000 },
    read_bone_transforms: specBp("read", "Read live skeletal bone and socket transforms off an actor, once. This is a point-in-time read, NOT a time series - for per-frame capture over a window use the pie category's observe actions. Pass actorLabel or actorPath. Pass bones (bone OR socket names) or omit for every bone up to limit. space=world (default) or component; component space is independent of where the actor is standing. Pass relativeTo (bone OR socket name) to express every sample in that live reference frame; relativeTo supersedes space and is calculated in component space. world defaults to auto. Also reports the AnimInstance class/path (#756/#757/#761/#764).", "read_bone_transforms"),
    get_object_properties: specBp("read", "Read reflected properties off any UObject, with the same targeting as invoke_object_function: objectPath, or target (plus subsystemClass or playerIndex). Blueprint-declared variables are reflected properties, so they read the same way as native ones. Pass propertyNames to filter; entries may use the Details-panel spelling ('World Context Object' finds WorldContextObject, 'Is Active' finds bIsActive). Names that do not exist come back under missingProperties instead of silently returning nothing. Properties holding a TMap are also reported under `values` in the structured form set_property accepts, because export text cannot carry a struct-keyed map back (#820). world defaults to auto (#739/#802).", "get_object_properties"),
    set_movement_mode: specBp("mutate", "Set a live PIE character's movement mode and/or velocity on its CharacterMovementComponent. Pass actorLabel or actorPath. Modes are named (none|walking|navwalking|falling|swimming|flying|custom) rather than raw enum numbers, because a wrong number reads as success and then behaves as None. Reports previousMode/previousVelocity and reads the mode back afterwards, since SetMovementMode can refuse a mode the character cannot enter (flying with bCanFly off, swimming outside a volume). world defaults to pie (#757).", "set_movement_mode"),
    set_object_property: specBp("mutate", "Write a reflected property on a live UObject instance, with the same targeting as invoke_object_function: objectPath, or target (plus subsystemClass or playerIndex). Use this for a PIE actor, a spawned widget or any runtime instance: editor(set_property) is the asset path and marks the package dirty and saves it, which a live instance has no business doing. propertyName accepts dotted/indexed paths and the Details-panel spelling. Reports previousValue plus the value read back after the write, so a coerced or clamped write is visible. Nothing is saved; pass postEditChange=true to fire PostEditChangeProperty. world defaults to auto (#802).", "set_object_property"),
    find_object: specBp("read", "Resolve or search for a live UObject instance and report the objectPath that addresses it, which is what invoke_object_function / get_object_properties / set_object_property need. Pass objectPath to check one path (returns found/isValid rather than failing when it is gone), or className and/or nameContains to search every loaded object. className takes a short name (StaticMeshActor), a /Script path, a generated class name (WBP_Hud_C) or a Blueprint asset path. This is how you get the path of something spawned at runtime, an editor utility widget or a UMG widget, which no naming convention predicts. The search is paged: pass back nextCursor as cursor to read the next page (#802).", "find_object"),
    teleport_runtime_actor: specBp("mutate", "Move a live PIE actor and have it STAY moved. A plain SetActorLocation on a Character is undone by CharacterMovement on the next tick, so this stops the movement component, teleports, and stops it again. Pass actorLabel or actorPath. Reports actualLocation read back from the actor rather than what was requested. world defaults to pie (#770/#777).", "teleport_runtime_actor"),
    set_runtime_visibility: specBp("mutate", "Hide or show live PIE actors and their scene components, capturing an exact rollback snapshot. PIE-only: world must be 'pie' (the default) and pieInstance picks the world when several are running (see list_pie_instances). Provide exactly ONE actor selector - actorLabels[], actorPaths[] (the unambiguous one) or actorClass; a label matching several actors is refused rather than resolved at random. hidden=true hides, hidden=false shows. componentNames[]/componentClasses[] narrow to matching SceneComponents and imply affectComponents; with no component filter the actor itself is the target. affectActor/affectComponents override that split, propagateToChildren (default true) also takes each matched component's descendants, matchSubclasses (default true) widens class matching, and maxTargets bounds how far the expansion may go. dryRun defaults to TRUE: the call reports what it would change and mutates nothing until dryRun=false. Returns hidden, dryRun, mutationPerformed, matchedActors, targetCount, changed, alreadyDesired, worldPath, pieInstance, netMode, targets[], and on a real mutation a rollbackToken to hand to restore_runtime_visibility.", "set_runtime_visibility"),
    restore_runtime_visibility: specBp("mutate", "Put back the exact visibility state set_runtime_visibility captured, addressed by the rollbackToken from its response. Run set_runtime_visibility with dryRun=false first; a dry run issues no token. The token belongs to one PIE session and expires with it, so restore before play ends. world, if passed, must be 'pie', and pieInstance must match the token's session. Returns restored, rollbackToken, targetCount, changed, alreadyRestored, worldPath, pieInstance, netMode.", "restore_runtime_visibility"),
    invoke_static_function: specBp("unknown", "Call a static UFUNCTION on a UBlueprintFunctionLibrary (no actor instance). invoke_function needs an actor/component target; this targets the library class CDO instead, so it reaches static *_BlueprintOnly libraries (Voxel sculpt/query/stamp), GeometryScript, Kismet math, any function library. world/pieInstance pick WHICH world fills the context param, so a static that looks a GameInstance subsystem up off its context can be exercised against a live PIE session (#971); the result names the world it ran against and the parameter that carried it. Returns return/out params under returnValues, with a TArray/TSet/TMap return as real JSON and everything else as export text (#885). Discover libraries + functions with list_function_libraries.", "invoke_static_function"),
    invoke_function: specBp("unknown", "Call a BlueprintCallable / Exec UFUNCTION on a target actor or one of its components. world=editor (the default) runs the function on the actor placed in the level, no PIE session needed, and reports which instance ran it as resolvedActorLabel/resolvedActorPath. actorLabel is matched against placed actors by editor label first, then internal object name, then full object path; a miss is an error naming what was searched (#806). Returns out/return params under returnValues; a TArray/TSet/TMap return comes back as real JSON and everything else as export text (#885). A scripted call runs under the editor script-execution guard, which forces every actor callspace to Local, so a UFUNCTION(Server) executes locally instead of being sent; the result warns when that happened, and deferToNextTick=true queues the call for the next engine tick where it routes normally, at the cost of returning before it runs (#973). A component redirects the call to that subobject (#382), and actorArgs fills UObject* parameters from actor labels (#383). See #228/#229.", "invoke_function"),
    list_function_libraries: specBp("read", "Enumerate UBlueprintFunctionLibrary subclasses on this build. Filter by name with pattern (case-insensitive substring, e.g. 'GeometryScript' / 'Kismet' / 'Animation'). Returns name, module, and (by default) every static BlueprintCallable function on the library with its tooltip. Use to discover what's available for editor.invoke_function (#455).", "list_function_libraries"),
    set_pie_time_scale: specBp("mutate", "Fast-forward PIE game time. Raises WorldSettings caps and calls SetGlobalTimeDilation.", "set_pie_time_scale"),
    hot_reload: specBp("mutate", "Hot reload C++.", "hot_reload"),
    undo: specBp("mutate", "Undo last transaction.", "undo"),
    redo: specBp("mutate", "Redo last transaction.", "redo"),
    get_perf_stats: specBp("read", "Editor performance stats.", "get_editor_performance_stats"),
    run_stat: specBp("mutate", "Run a stat overlay. name is a bare stat name (e.g. 'unit','fps','game','gpu') prefixed with 'stat '; command is a full console command and wins over name (#722).", "run_stat_command"),
    set_scalability: specBp("mutate", "Set rendering quality via the Scalability system (actually applies + persists, not just sg.* cvars). Returns appliedLevels (#591).", "set_scalability"),
    get_cvars: specBp("read", "Read console variables from the running editor by name, names, or pattern (substring match over every registered variable). Each row carries value, defaultValue, isDefault, type, whether it is a cheat, and `setBy`: the priority the current value was written at, which is what separates a variable sitting at its default from one a scalability group or device profile drove there. A variable set at a higher priority ignores later writes from lower ones. Names that do not resolve come back under notFound rather than as an error (#1006).", "get_cvars"),
    set_cvars: specBp("mutate", "Bulk-set console variables, from a {name: value} object or a [{name, value}] array. Returns per-cvar old/new values and any notFound names. Use get_cvars to read one back, including which priority its value was set at (#591).", "set_cvars"),
    capture_screenshot: specBp("mutate", "Screenshot. target=pie synchronously captures the selected PIE client game viewport with UMG/Slate UI; target=editor captures the level viewport; target=window synchronously captures a whole Slate window via FSlateApplication::TakeScreenshot - pixel-true for ALL Slate/UMG UI, returns after the PNG is written, and works while the window is unfocused or off-screen. Multi-instance PIE automatically prefers a world with a game viewport; pass pieInstance or worldPath to select explicitly, which for target=window is what picks the PIE client window instead of the active editor window. Every mode captures at the source viewport/window size - use capture_scene_png for a chosen output size. outputPath is accepted for filename, so the two capture actions take the same name (#966). Returns the resolved PIE instance/world and image dimensions (#226/#724).", "capture_screenshot"),
    capture_scene_png: specBp("mutate", "Headless PNG via a transient SceneCapture2D (RGBA8 LDR). Returns captureMetadata with the actual camera transform/basis, FOV, resolution, world/PIE identity and resolved focus actor/bounds. Pair this data with this image; get_viewport_state and hit_test_viewport_pixel refer to a different, live editor camera. The capture actor is destroyed before return. Old stray capture actors are swept and their removal reported as strayCaptureActorsRemoved. focusActorLabel or focusActorPath auto-frames the camera on that actor's bounds (#599/#662).", "capture_scene_png"),
    get_viewport_state: specBp("read", "Full readout of a level viewport: viewMode, viewportType, fov, nearClip, farClipOverride, exposure (fixed or auto, with the EV100), cameraSpeed, gameView, realtime, location and rotation, plus the view modes this engine build supports. get_viewport reports location, rotation and fov only. Call this before a capture to record the conditions it was taken under, so two captures can be compared honestly.", "get_viewport_state"),
    set_view_mode: specBp("mutate", "Pin the viewport's shading mode (Lit, Unlit, Wireframe, LightingOnly, DetailLighting, ShaderComplexity and the rest this build supports). The single biggest determinism lever for screenshot comparison: Unlit takes lighting out of the picture, Wireframe takes shading out. An unknown name is refused with the full list this engine supports rather than silently ignored. Idempotent: setting the mode it already has reports unchanged, and the previous mode comes back as a rollback.", "set_view_mode"),
    set_viewport_exposure: specBp("mutate", "Pin the editor viewport to a fixed EV100 instead of auto eye-adaptation. Targets the viewport CLIENT, so it is transient, editor-only and does not dirty the level. This does not pin capture_scene_png's separate SceneCapture2D exposure. Post-process volumes remain asset(set_property) territory. Pass ev100 for a fixed value, or mode='auto' to return to eye adaptation.", "set_viewport_exposure"),
    set_viewport_view: specBp("mutate", "Set fov, nearClip, farClip, viewportType and cameraSpeed in one call. set_viewport writes only location and rotation and does not write the fov it reads back, which is the gap this fills. Reports a per-field changed flag and the previous values, and rolls back to them. nearClip's rollback is marked lossy because the engine reports the effective plane rather than the override.", "set_viewport_view"),
    set_game_view: specBp("mutate", "Toggle game view, which hides editor-only overlays (grid, gizmos, actor icons, volume wireframes) so a viewport capture shows what the game shows rather than what the editor shows. Idempotent: setting the state it already has reports unchanged.", "set_game_view"),
    redraw_viewport: specBp("read", "Force the viewport to repaint. A bridge write marks the viewport dirty but does not repaint it, so a capture taken immediately afterwards can show the state from before the write. Use set_realtime instead when a ticking simulation also has to advance.", "redraw_viewport"),
    begin_transaction: specBp("mutate", "Open an undo transaction so a run of writes collapses into ONE undo step. General-purpose, unlike material(begin_transaction) which is material-scoped. Nesting is reported rather than refused. Pair with end_transaction to commit or cancel_transaction to discard.", "begin_editor_transaction"),
    end_transaction: specBp("mutate", "Commit the open undo transaction and return its index in the undo buffer. Ending with nothing open reports that rather than erroring, so a flow that already closed one is safe to replay.", "end_editor_transaction"),
    cancel_transaction: specBp("mutate", "Discard the open transaction and restore every object it touched. This is what makes 'do several writes, detect a failure partway, abort, leave the editor unchanged' possible at all; material's begin/end pair had no cancel, so an aborted flow could only ever commit. Cancelling with nothing open reports that rather than erroring.", "cancel_editor_transaction"),
    get_undo_state: specBp("read", "Report canUndo and canRedo plus the DESCRIPTION strings of what an undo or redo would actually apply, the queue length, the undo count and the current index. Look before you undo, instead of undoing and reading back a bare boolean.", "get_undo_state"),
    undo_redo_steps: specBp("mutate", "Undo or redo several steps at once, returning appliedDescriptions: the titles of the transactions actually reversed or reapplied, which is how you confirm you undid what you meant. Stops early with a stated reason rather than silently doing fewer steps, and refuses while a transaction is open.", "undo_redo_steps"),
    get_transaction_history: specBp("read", "Read the undo buffer itself, newest first: per entry the index, title, id, record count, byte size and primary object, and whether it is applied or undone. currentIndex splits the applied entries from the undone ones. Use it to find the transaction a later cancel or undo should target.", "get_transaction_history"),
    // V4 Insights profiling. The trace half drives FTraceAuxiliary, which is a
    // static C++ API with no UObject in front of it, so none of this is
    // reachable through set_property. Reading a .utrace back is NOT offered:
    // every action that produces one reports its absolute path, its size and
    // the UnrealInsights command line that opens it.
    start_trace: specBp("mutate", "Start an Unreal Insights trace. Writes a .utrace and REPORTS WHERE IT LANDED (traceFile, plus the exact UnrealInsights command that opens it) - the bridge records traces, it does not read them back, because trace analysis lives in the engine's TraceServices/TraceAnalysis Developer modules that this plugin does not link. Defaults to a timestamped file under <Project>/Saved/Profiling so the path is deterministic rather than invented by the engine. Idempotent in the way that matters here: a trace that is ALREADY running is reported as alreadyTracing rather than quietly starting a second one, because only one connection exists per process. A channels list where nothing resolves is refused with the closest channel names. Rollback: stop_trace.", "start_insights_trace"),
    stop_trace: specBp("mutate", "Stop the running trace and report the finished file: absolute path, byte size, and the UnrealInsights command line that opens it. The destination is only readable while connected, so it is captured before the stop rather than lost by it. Stopping when nothing is running reports wasTracing=false rather than erroring, so a replayed flow is safe. Warns when profiling regions were still open, since those have no end event in the file. The rollback is LOSSY and says so: restarting writes a NEW .utrace and cannot reopen this one.", "stop_insights_trace"),
    pause_trace: specBp("mutate", "Pause or resume the running trace by muting every active channel, without closing the file. Idempotent: pausing an already-paused trace reports changed=false. Rollback restores the previous state.", "pause_insights_trace"),
    get_trace_status: specBp("read", "Read the whole trace system: tracing, paused, systemStatus, connectionType, destination, activeChannels, byte and memory statistics, the channel presets start_trace accepts, whether UnrealInsights is on disk, every profiling region still open, and whether a bridge-launched standalone run is alive.", "get_insights_trace_status"),
    list_trace_channels: specBp("read", "Every trace channel this build registers, with its enabled state and, on UE 5.7+, its description, id and read-only flag. This is what makes start_trace's channels parameter discoverable instead of guesswork.", "list_trace_channels"),
    set_trace_channels: specBp("mutate", "Turn named trace channels on and off, including mid-trace. Validates the WHOLE request before applying any of it, so a typo cannot leave a half-configured trace recording something other than what was asked for, and an unknown name comes back with the closest real ones. Each channel reports wasEnabled, enabled and changed read back from the trace system rather than assumed, so a read-only channel refusing at runtime is visible instead of silent. Rollback restores exactly the channels that moved.", "set_trace_channels"),
    begin_profile_region: specBp("mutate", "Open a named bracket around an operation so it can be measured. Times wall clock unconditionally and, on UE 5.7+ with a trace running, also emits an Insights timing region; when it cannot emit one it says so in tracedReason rather than pretending. The name is the key: opening the same name twice reports the existing region rather than nesting two begins under one end. Rollback: end_profile_region.", "begin_profile_region"),
    end_profile_region: specBp("mutate", "Close a named bracket and return what it measured: durationMs and the number of rendered frames it spanned. A region spanning zero frames says so, because a CPU-versus-GPU verdict cannot describe work that ran inside one tick. Ending a region that is not open reports wasOpen=false and lists the ones that are, rather than erroring. No inverse exists and the response says why.", "end_profile_region"),
    add_trace_bookmark: specBp("mutate", "Drop a named marker on the Insights timeline. Reports recorded=false with the reason when no trace is running or the Bookmark channel is off, instead of returning a success for an event that was dropped. Deliberately NOT idempotent: a bookmark is a point event, so two calls write two markers and the response says so.", "add_trace_bookmark"),
    get_frame_timing: specBp("read", "Frame timings WITH A VERDICT: gameThreadMs, renderThreadMs, rhiThreadMs, swapBufferMs, per-thread wait time, frameMs and fps, and GPU min/avg/max drained from the RHI's own history, then bound = gpu | cpu-game | cpu-render | cpu-rhi | balanced | unknown with the arithmetic that produced it spelled out in verdict. Says unknown when the RHI published no GPU timing rather than guessing. warnings[] names the conditions that make the numbers meaningless and the exact call that fixes each, including the unfocused-editor CPU throttle, which is a plain UPROPERTY and is therefore REPORTED here with its objectPath for editor(set_property) instead of getting a typed setter that would duplicate a working path. sampleWindow states what was actually measured: a handler runs inside one tick and cannot advance frames to build a window.", "get_frame_timing"),
    trigger_hitch: specBp("mutate", "Stall the game thread for a known number of milliseconds, so hitch-detection logic can be tested against a hitch whose size is known in advance. Brackets the stall with a trace region and a bookmark so it is findable in the capture. Capped at 5000ms because this blocks the same thread the bridge answers on. Sleeping consumes no CPU, so it reads as a long frame rather than as game-thread work. No inverse: time does not come back.", "trigger_hitch"),
    launch_standalone: specBp("mutate", "Launch the project as a separate -game process, optionally tracing, so frame times come from a real game process rather than from the editor. Passing channels (or traceFile) adds -trace and -tracefile, and the response carries the .utrace path plus the UnrealInsights command that opens it. Idempotent: a bridge-launched run that is still alive is reported as alreadyRunning rather than joined by a second. The process is detached and its output is not read; poll get_standalone_status. Rollback: stop_standalone.", "launch_standalone_game"),
    get_standalone_status: specBp("read", "Is the bridge-launched standalone run still alive: running, processId, commandLine, uptimeSeconds, the exit code once it has ended, and its .utrace path with the command that opens it. Only reports runs this bridge started; one launched another way is not tracked.", "get_standalone_status"),
    stop_standalone: specBp("mutate", "Terminate the standalone run this bridge launched, killing its process tree so nothing is left holding the .utrace open. Stopping when nothing is running reports wasRunning=false rather than erroring. Termination is not a graceful quit and the response says what that costs. No inverse: relaunching is a new run.", "stop_standalone_game"),
    set_realtime: specBp("mutate", "Toggle realtime update on the level editor viewports so the editor-world sim (Niagara, anims) ticks - otherwise capture_scene_png renders an unticked, empty sim (#537).", "set_realtime"),
    get_viewport: specBp("read", "Get viewport camera.", "get_viewport_info"),
    hit_test_viewport_pixel: specBp("read", "Ray-cast from a screen pixel through the active editor viewport and return the first hit. Builds the ray from the live viewport's projection matrix (no FOV/aspect guessing). width and height override the viewport size when picking from a different-resolution screenshot. Returns hit + actorLabel/actorClass/componentName/componentClass/materialPath/location/impactPoint/normal/distance/faceIndex/boneName/physicalMaterial (#418).", "hit_test_viewport_pixel"),
    get_runtime_values: specBp("read", "Bulk runtime read across the active world. For each actor/component matching classFilter, resolves every path against the (actor|component) root and returns rows of {actorLabel, actorClass, componentName?, componentClass?, values, errors?}. Paths support property hops, sub-object hops, and BlueprintCallable getter calls at any segment (e.g. 'PowerConnector.GetRequired' reaches a UFUNCTION on a UObject sub-object). A getter that takes arguments is written with them inline, 'GetMirroredTallyWeight(overclock)' or 'GetBalance(gold, 2)', which is what makes a keyed accessor readable across every matched instance in one call; the literals are coerced by the same rules invoke_object_function's args use, so FName/FString/int/float/bool/enum all read (#969). classFilter matches actor class OR component class - omit to match everything. componentName roots every path at the component with that instance name (case-insensitive) and skips actors without one, so two components of the same class are addressable apart (#1113). A path whose result is a TArray/TSet/TMap comes back as real JSON rather than one string (#885). World defaults to PIE if running, else editor (#414).", "get_runtime_values"),
    set_viewport: specBp("mutate", "Set the viewport camera, its projection and its orthographic zoom. The projection is switched BEFORE the requested pose is applied, because the viewport keeps a separate transform cache per projection and writing the pose first would hand back the camera from the mode you just left. Every value is validated before anything is written, and the rollback restores the projection and zoom as well as the location and rotation. Acts on the active level viewport (#1029).", "set_viewport_camera"),
    focus_on_actor: specBp("mutate", "Focus on actor. Pass actorLabel or actorPath (#983).", "focus_viewport_on_actor"),
    create_sequence: specBp("mutate", "Create Level Sequence.", "create_level_sequence"),
    get_sequence_info: specBp("read", "Read sequence: bindings (possessable/spawnable) with their Sequencer tags (#556), tracks, and optional section detail. UNITS: playbackRange is reported in TICKS (tick resolution, commonly 24000/s), while MovieSceneScripting*Channel.add_key defaults its time_unit to DISPLAY_RATE. Keys authored with a tick number under the default unit land roughly 800x past the range and the track evaluates to its first key, which presents as transforms that do not work on a structurally perfect sequence (#881).", "get_sequence_info"),
    add_sequence_track: specBp("mutate", "Add an empty track. actorLabel or actorPath binds it to an actor; omit both for a master track (#983).", "add_sequence_track"),
    add_sequence_section: specBp("mutate", "Add a section to a track (creating the track if needed), set its start/end in seconds, and for a CameraCut track bind it to a camera. actorLabel or actorPath scopes the track to an actor binding. Returns the section index + channel names to key (#548/#983).", "add_sequence_section"),
    set_sequence_keyframes: specBp("mutate", "Add keyframes to a section channel. Transform channels: Location.X/Y/Z, Rotation.X/Y/Z (or friendly x/y/z, yaw/pitch/roll); Fade/Float: the float channel (#548).", "set_sequence_keyframes"),
    set_sequence_playback_range: specBp("mutate", "Set a Level Sequence's playback range in seconds (#548).", "set_sequence_playback_range"),
    play_sequence: specBp("mutate", "Play/stop/pause a Level Sequence in Sequencer. Pass sequencePath (or assetPath) to target a specific sequence - it is opened first, because the underlying Sequencer commands act on whatever is currently open. Omit it and the call applies to the open sequence and says so. sequenceAction defaults to play.", "play_sequence"),
    scrub_sequence: specBp("mutate", "Park the Sequencer playhead on an exact time and evaluate there, then return. This is what makes scrub-then-capture_scene_png deterministic: play_sequence only offers play/pause/stop and realtime playback races the capture. Pauses first, scrubs, and forces the evaluation before answering, because the playhead move alone does not write possessed-actor transforms. Pass exactly one of seconds or frame. frame is read in timeUnit: display (default, the frame numbers Sequencer shows) or tick (the units get_sequence_info's playbackRange reports). Returns the evaluated time in all three units plus displayRate/tickResolution/playbackRange, and warns when the time is outside the playback range (#881).", "scrub_sequence"),
    render_sequence_frames: {
      ...specBp("mutate", "Render a Level Sequence to a PNG image sequence in ONE call: for each frame it scrubs, forces the evaluation, and captures through a transient SceneCapture2D (RGBA8 LDR, real PNG), instead of scrub_sequence + capture_scene_png per frame. Camera: the sequence's camera cuts by default (frames with no active cut are skipped and reported), or cameraActorLabel/cameraActorPath for one camera actor, or location/rotation/fov for a fixed camera. The camera's own FOV and post process are used. Range is in display frames: startFrame and endFrame (inclusive), or startSeconds and endSeconds (end exclusive); default is the playback range. frameStep skips frames. More frames than maxFrames is refused. Files are written to outputDir (absolute or project-relative, default Saved/SequenceFrames/<sequence>) as <sequence>.<frame>.png. Refused while PIE runs. The playhead is restored afterwards and playback is left paused. A run that nears the 600s timeout stops, returns truncated=true and nextStartFrame. Returns files[], frames[] with per-frame evaluateMs/captureMs/exportMs/totalMs, and timing totals (#1098).", "render_sequence_frames"),
      timeoutMs: 600_000,
    },
    build_all: specBp("mutate", "Build all (geometry, lighting, paths, HLOD).", "build_all"),
    build_geometry: specBp("mutate", "Rebuild BSP geometry.", "build_geometry"),
    build_hlod: specBp("mutate", "Build HLODs.", "build_hlod"),
    validate_assets: specBp("read", "Run data validation over a directory (default /Game/) or over exact assets named by assetPath or assetPaths, one selector per call.", "validate_assets"),
    get_build_status: specBp("read", "Get build/map status.", "get_build_status"),
    cook_content: specBp("mutate", "Cook content.", "cook_content"),
    get_log: specBp("read", "Read output log. maxLines selects how far back into the ring buffer to read (default 100); limit pages the lines that match filter/category within that window, and each line carries the sequence number that anchors a cursor.", "get_output_log"),
    search_log: specBp("read", "Search the captured log. Every match in the 4096-line ring buffer is collected and paged, so a busy log reports how many matched instead of stopping at the first hundred. maxResults caps the search itself and reports cappedAtMaxResults when it was what ended the collection.", "search_log"),
    get_message_log: specBp("read", "Read a Message Log listing (MapCheck, AssetCheck, PIE, LoadErrors, LightingResults...). Call with NO logName to list the registered listings with their error/warning counts, then read one. Counts come from the listing itself; message bodies come from the current page and honour the Message Log tab's severity checkboxes, so when fewer are readable than exist the response says so instead of reading clean. An unknown logName is an error, not an empty log. Blueprint COMPILE results are not here - the compiler makes a listing per Blueprint; use blueprint(compile).", "get_message_log"),
    list_crashes: specBp("read", "List crash reports, sorted by folder name, which is chronological.", "list_crashes"),
    get_crash_info: specBp("read", "Get crash details.", "get_crash_info"),
    check_for_crashes: specBp("read", "Check for recent crashes.", "check_for_crashes"),
    set_dialog_policy: specBp("mutate", "Arm an answer, in advance, for dialogs whose title or message contains a pattern. READ THIS BEFORE USING IT: an armed policy presses the button for you, so from then on a matching prompt is answered and dismissed and the user never sees the question. On a save prompt that means unsaved work can be discarded without anyone reading the warning. Nothing arms a policy on your behalf - the plugin ships none and no other action arms one - so every policy in effect is one somebody typed here deliberately, and this is the only way the bridge ever answers a dialog by itself. Covers the Slate modal windows the editor raises itself (the shutdown \"Save Content\" prompt among them, whose buttons are Save Selected / Don't Save / Cancel). A response keyword (yes/no/ok/cancel/retry/continue/yesall/noall) resolves to whichever of the dialog's buttons carries that meaning, so response='no' presses \"Don't Save\"; pass buttonLabel to name a button literally instead. A policy set here answers a matching dialog whoever raised it, and answers one that is already on screen. To read a dialog instead of pre-answering it, use editor(list_dialogs) and then editor(respond_to_dialog).", "set_dialog_policy"),
    clear_dialog_policy: specBp("mutate", "Clear dialog policies.", "clear_dialog_policy"),
    get_dialog_policy: specBp("read", "Get the dialog policies currently armed, each with its response and literal buttonLabel. Every one was armed by a caller through set_dialog_policy: the plugin arms none of its own, so an empty list means nothing will answer any dialog on its own.", "get_dialog_policy"),
    list_dialogs: specBp("read", "Read the modal dialog blocking the editor, in full: its exact title, its COMPLETE message text (never truncated - messageTruncated is always false), every button label in the order the dialog lays them out, and a `choices` array pairing each button with the exact editor(respond_to_dialog) call that presses it. When the dialog asks a question per row (the \"Save Content\" prompt is a checkbox per unsaved package), an `items` array reports each tickable row: its index, its label, its cells (asset name, package path, class path) and whether it is currently ticked. No button is marked recommended and none is reordered; choosing is yours. Also reports which armed policy matches and which button that policy would press, if any. `blocksGameThread` says whether the editor is actually held: true only for a window on Slate's modal stack, which is what parks the game thread and refuses every other action. A prompt the editor raised without AddModalWindow comes back with `blocksGameThread` false and `detectedBy` parentedWindow - it is reported, because an unanswered question must not be quit out from under, and nothing is refused for it (#1118). `notTreatedAsDialogs` names any window the walk considered and rejected, with the reason: a standalone Message Log or an undocked Output Log is a regular parented window like a dialog is, and used to block every action for the life of the session with no button that could answer it (#1078). A window in that list is NOT blocking anything. Runs even while a dialog is blocking the editor, when every other handler times out, so this is the way to see what the editor is asking.", "list_dialogs"),
    respond_to_dialog: specBp("mutate", "Press one named button on the active modal dialog, releasing the game thread. This is the deliberate way to answer a dialog: read it with editor(list_dialogs) first, then name the button you chose. Runs even while the dialog is blocking the editor. ONLY IN auto MODE. The dialog handling mode decides who answers, and it is enforced here: under interactive the question is put to the person in an elicitation form (raised on the call AFTER the one that hands the dialog back, so its text is read somewhere nothing truncates it) and only their button is pressed, under defer they answer it in the Unreal Editor window, and in both this call is refused with the dialog named. editor(list_dialogs) stays available in every mode, so the dialog can always be READ. Pass dialogAction='close' (or 'escape') to destroy the dialog window when no button label fits, which ends the modal without answering the question. It is NOT called 'action': that name is this tool's own dispatch field, so a call writing action='close' selects a nonexistent editor action and never reaches the dialog (#1078). Pass items to tick or untick the dialog's own rows before the button is pressed, which is what makes \"Save Selected\" mean something: read them from editor(list_dialogs) and send [{index, checked}] for the ones you want changed. The ticks and the press happen in this one call, so a modal is never left holding a selection nobody pressed anything on.", "respond_to_dialog"),
    open_asset: specBp("mutate", "Open asset in its editor.", "open_asset"),
    get_open_asset_editors: specBp("read", "What the user currently has open in the editor, and which of it holds focus. Returns every open asset editor with its asset path/class, its window and major tab label, and its own tabs - so an open PCG Graph, the Blueprint graph documents someone has open, and the Details panel they are looking at all come back by name. Unreal publishes no single answer to this: the asset editor subsystem knows what is open, the global tab manager knows the active tab, and each tab knows when it was last activated, and nothing joins them. focusedAssetEditor is the editor holding Slate's active tab, activeTab.context says whether that tab is in an asset editor, the Level Editor or elsewhere, and mostRecentlyActiveAssetEditor is where the user last was. Read editorApplicationFocused before trusting the word focused: an agent driven from a terminal has taken OS focus away from Unreal, so Slate's active tab is a record of where the user was rather than where their cursor is, and focusNote says which of the two this answer is. Reads only - it never focuses or opens anything, which editor(open_asset) is for (#1112).", "get_open_asset_editors"),
    reload_bridge: specBp("mutate", "Hot-reload Python bridge handlers from disk.", "reload_handlers"),
    save_dirty: specBp("mutate", "Flush every dirty package and return a per-package saved/failed map. Use after multi-step CDO/component edits when set_class_default leaves the asset dirty without persisting (#378). With commitDeletes=true it uses the editor's own dirty-package save instead, which deletes the external package of a deleted World Partition actor rather than failing with 'SavePackage returned false', and reports written, deleted and stillDirty per package (#1156).", "save_dirty"),
    configure_pie: specBp("mutate", "Set ULevelEditorPlaySettings - multi-client PIE, net mode, single-process flag, Play-in-New-Window resolution (#384/#671).", "configure_pie"),
    get_pie_config: specBp("read", "Read current ULevelEditorPlaySettings (numClients, netMode, single-process, separate-server) (#384).", "get_pie_config"),
    pie_set_player_view: specBp("mutate", "Point the running PIE player's view (control rotation) at a pitch/yaw/roll so a capture frames the intended direction. Requires PIE (#671).", "pie_set_player_view"),
    stage_game_input: specBp("mutate", "Stage input for the running game: set input mode (gameOnly|gameAndUI|uiOnly) and mouse cursor so injected/simulated input reaches the pawn. This only sets the mode - the injection itself lives in the pie category (pie(inject_input*)), not here. Requires PIE (#671).", "stage_game_input"),
    run_automation_tests: specBp("mutate", "Run registered Automation tests matching a filter and return per-test pass/fail plus error lines. Runs them synchronously through the test framework rather than the console queue, and suspends the editor's unfocused-CPU throttle for the duration - otherwise an unfocused editor drops to a few FPS and the framework's interactive-frame-rate gate never opens, leaving tests queued forever (#765). A test whose latent commands are still queued when latentTimeoutSeconds runs out is reported as abandoned, with the reason: latent work needs engine frames, and this runs on the game thread, so a test that starts PIE (CQTest multi-client network tests, for instance) belongs in the editor's Automation window or -ExecCmds=\"Automation RunTests <name>\" at launch. Such a test used to terminate the editor outright (#993) (#693).", "run_automation_tests"),
    list_dirty_packages: specBp("read", "Enumerate currently-dirty content + map packages, read from the editor's own dirty-package lists (the same ones Save All uses). Includes a never-saved /Temp world, because an unsaved new map is exactly the unsaved work a caller needs to see before closing or reloading (#340).", "list_dirty_packages"),
    get_world_state: specBp("read", "One atomic read of which world is open and what is unsaved. Returns editorWorldName, editorWorldPackage, persistentLevelPackage, worldPackageDirty, a sorted dirtyPackages list with counts, and the editor/play/simulate mode. level(get_current) plus editor(list_dirty_packages) is two calls, so the editor can change between them and neither result proves which world the other described; this answers both in one game-thread dispatch. Read-only, and fails closed rather than reporting an empty world as a clean one (#920/#921).", "get_world_state"),
    request_editor_shutdown: {
      kind: "handler",
      effect: "mutate",
      description: "Ask the editor to close itself from inside the engine, after it has checked that closing is safe. Refuses by default when any content or map package is dirty (including an unsaved /Temp world) and reports which ones, so nothing is lost to a silent discard. Ends an active PIE/SIE session first and closes only once play has actually stopped. The response is returned before the process exits. Aimed at the same editor stop_editor aims at, through the same ownership check, so the two can never disagree about which editor belongs to the loaded project (#967), including what happens when none is running: both fail with alreadyStopped=true rather than one succeeding and the other refusing. This IS what stop_editor sends: stop_editor calls it with requireClean=false, so the editor schedules its own close and raises its own save prompt for anything dirty rather than the server refusing in its place. Called directly it defaults to requireClean=true, which refuses and names the dirty packages without scheduling anything. Use editor(stop_editor) for the full stop-and-confirm flow; this action is the in-engine half of it. Params: requireClean? (default true), endPIE? (default true)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        // #967/#970: stop_editor refused on an ownership check this action did
        // not perform at all, so the two actions gave opposite answers about
        // one editor. They now ask the same question. A refusal here means the
        // same thing it means there, in the same words.
        const ownership = await resolveOwnedEditor(
          ctx.project.projectDir ?? null,
          ctx.project.projectPath ?? null,
          connectedEditorOf(ctx.bridge),
        );
        if (!ownership.owned) {
          // The description promises this action and stop_editor can never
          // disagree about one editor, and that covers the verdict as well as
          // the target: an editor that is already gone is a failure on both,
          // carrying the same marker for the same reason, rather than a
          // success on one and a refusal on the other.
          return {
            success: false,
            ...(ownership.alreadyStopped ? { alreadyStopped: true } : {}),
            error: ownership.message,
          };
        }
        const result = await ctx.bridge.call("request_editor_shutdown", {
          requireClean: p.requireClean,
          endPIE: p.endPIE,
        });
        return ownership.healed && result && typeof result === "object"
          ? { ...(result as Record<string, unknown>), lockfileNote: ownership.healed }
          : result;
      },
    },
    ...epicActions,
  },
  undefined,
  {
    ...epicSchema,
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration. A key listed again below is shared with hand-written
    // actions, and holds the same type as the generated one.
    ...specSchema,
    code: z.string().optional(),
    captureLog: z.boolean().optional().describe("execute_python/run_python_file: false drops everything the script logged except its error entries, which are always kept. logChars and logEntryCount still report what was there (#995)"),
    maxLogChars: z.number().optional().describe("execute_python/run_python_file: keep only the last N characters of logged output, dropping whole entries from the front. Reported as logTruncated (#995)"),
    resultVariable: z.string().optional().describe("execute_python/run_python_file: name of a top-level Python variable to return as `result`, separate from print()/log output (#732)"),
    taskSummary: z.string().optional().describe("execute_python: plain-words intent, searched against the tool registry to gate the call (#704)"),
    ruledOut: z.array(z.object({ action: z.string(), reason: z.string() })).optional().describe("execute_python: reason each searched candidate action does not fit; every candidate must be ruled out before Python runs. 'action' accepts the bare action name, tool(action) or tool.action; 'reason' must be at least 12 characters. Send back the array the previous refusal printed under 'sendThisBack' (#704, #938, #960)"),
    timeout: z.number().optional().describe("start_editor: seconds to wait for the bridge (default 120) (#758)"),
    probeWindows: z.boolean().optional().describe("get_engine_state: also enumerate native windows to catch pre-Slate dialogs (default true, costs ~2s)"),
    dialogPolicy: z.string().optional().describe("start_editor: semicolon-separated pattern=response pairs armed before the bridge is listening, so a prompt raised during startup is answered from the first frame (e.g. \"Restore=no\"). Same effect as set_dialog_policy and the same warning: an armed pattern presses the button, so the user never sees that prompt. Responses are the ones set_dialog_policy takes (#968)"),
    paramEcho: z.boolean().optional().describe("start_editor: arm the bridge parameter echo for the launched editor. It is read at startup, so it cannot be turned on over the socket afterwards. The live tests' leak assertions skip without it"),
    requireClean: z.boolean().optional().describe("request_editor_shutdown: refuse to close while any content or map package is dirty (default true)"),
    endPIE: z.boolean().optional().describe("request_editor_shutdown: end an active PIE/SIE session before closing (default true); false refuses to close while play is running"),
    waitForAssetRegistry: z.boolean().optional().describe("play_in_editor / play_in_editor_ignore_blueprint_errors start: block until AssetRegistry finishes the initial scan (default true)"),
    assetRegistryTimeoutSeconds: z.number().optional().describe("play_in_editor / play_in_editor_ignore_blueprint_errors start: wait budget for the AssetRegistry scan (default 180s)"),
    className: z.string().optional().describe("invoke_static_function: UBlueprintFunctionLibrary class - short name or /Script/Module.Class path. find_object: class to search for, including Blueprint generated classes (#802)"),
  },
);
