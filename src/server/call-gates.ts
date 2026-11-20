// @session per-session: Runs one call through the gates against the editor it was routed to.
/**
 * The gates of dispatch/call.ts, against this server's sessions, loads,
 * guards, journal and locks. Every route that runs an action ends up in
 * `runCall`; what differs per route is only how its boundary built the call.
 */
import type { TaskResult } from "@db-lyon/flowkit";
import type { EditorSession, SessionRegistry } from "../sessions/session.js";
import type { SessionLoad, SessionLoads } from "../sessions/session-load.js";
import type { ToolContext, ToolDef } from "../surface/tool-types.js";
import type { FlowContext } from "../flow/context.js";
import { McpError, ErrorCode } from "../core/errors.js";
import { withCallAsker } from "../flow/call-elicit.js";
import { readOnlyRefusal } from "../flow/session-access.js";
import { actionSpecOf, taskEffect } from "../surface/action-effects.js";
import { journalFor } from "../journal/journal.js";
import { refuseUntargetedInRegistry } from "../dispatch/editor-gate.js";
import { DialogGuard, type GuardDecision, stampBlockedEditor } from "../editor/dialog-guard.js";
import { isDialogRefusal } from "../bridge/dialog-refusal.js";
import { clientAdvertisesElicitation } from "../editor/dialog-mode.js";
import { playtestRefusal } from "../editor/playtest-mode.js";
import { withAssetLocks, type LockingConfig } from "../dispatch/locking.js";
import { unknownActionMessage } from "../surface/action-schema.js";
import { runsEpicMcpTool, withheldTarget, wrapsEpicTool } from "../surface/registered-surface.js";
import { epicEngineRefusalFor } from "../surface/epic-engines.js";
import { explainMissingAction } from "../sessions/session-surface.js";
import { disabledActionRefusal } from "../surface/category-state.js";
import { leaseConflictMessage } from "../dispatch/leases.js";
import { leasePathsOf } from "../dispatch/lease-paths.js";
import { sessionContext } from "../surface/target-params.js";
import {
  failed,
  isFailure,
  refused,
  runGates,
  succeeded,
  type ActionCall,
  type CallOutcome,
  type Gates,
} from "../dispatch/call.js";

/** What the gates need from the server that owns them. */
export interface GateDeps {
  sessions: SessionRegistry;
  loads: SessionLoads;
  lockingCfg: LockingConfig;
  /** The dialog guard a call to this editor checks, created on first use. */
  dialogGuardFor: (session: EditorSession, canElicit?: boolean) => DialogGuard;
  /**
   * The load a call runs against in place of the session's own: a context
   * projection's, whose registry holds the tasks of the tools it advertises.
   */
  loadView?: (load: SessionLoad) => SessionLoad;
  /** The flow tool and the context its runs start from. */
  flow: { tool: ToolDef; ctx: ToolContext };
}

/** The machine-readable companion of an error (#799), as MACHINE_ERROR carries it. */
export function machineDetails(e: unknown): Record<string, unknown> | undefined {
  return e instanceof McpError && e.details ? { code: e.code, ...e.details } : undefined;
}

/** The button a respond_to_dialog call names: its label, its index into the dialog's buttons, or the dismissal. */
function pressedLabel(params: Record<string, unknown>, dialog: { buttons: string[] } | null): string {
  if (typeof params.buttonLabel === "string" && params.buttonLabel !== "") return params.buttonLabel;
  if (typeof params.buttonIndex === "number" && dialog?.buttons[params.buttonIndex] !== undefined) {
    return dialog.buttons[params.buttonIndex];
  }
  return typeof params.dialogAction === "string" ? params.dialogAction : "";
}

/** A task's own answer: its result, or the failure it reported. A dialog refusal it returned is its result. */
function taskOutcome(registryTask: string, result: TaskResult, notes: string[]): CallOutcome {
  if (!result.success && !isDialogRefusal(result.data)) {
    return failed(ErrorCode.TASK_FAILED, result.error?.message ?? `Task ${registryTask} failed`, true, machineDetails(result.error));
  }
  return succeeded(result.data, notes, true);
}

/** Run one call through every gate, in order, against the editor it was routed to. */
export async function runCall(deps: GateDeps, call: ActionCall<EditorSession>): Promise<CallOutcome> {
  const { sessions, loads } = deps;
  const { session, task, asker, target } = call;
  const graph = (): readonly ToolDef[] => loads.dispatchUnion.tools;
  const isFlow = target.kind === "flow";
  const registryTask = target.kind === "task" ? target.registryTask : task;

  // Set by the gates as they pass, read by the ones after.
  let load: SessionLoad | undefined;
  let taskResult: TaskResult | undefined;

  const gates: Gates<EditorSession> = {
    // A call runs against its own editor's registry, built on demand rather
    // than borrowed from the first project's (D1).
    route: async (_c, next) => {
      if (isFlow) return next();
      try {
        load = await loads.ensure(session);
        if (deps.loadView) load = deps.loadView(load);
      } catch (e) {
        return failed(
          ErrorCode.NOT_CONNECTED,
          `Editor '${session.name}' has no tool surface of its own, so this call cannot ` +
            `be dispatched to it. Running it through another project's registry would execute that project's ` +
            `tasks in this editor, which is exactly what must not happen. Re-register it with ` +
            `project(action='add_editor'). Cause: ${e instanceof Error ? e.message : String(e)}`,
          false,
        );
      }
      if (!load.registry) {
        return failed(
          ErrorCode.NOT_CONNECTED,
          `Editor '${session.name}' has no task registry yet, so this call cannot be dispatched to it. Re-register it with project(action='add_editor').`,
          false,
        );
      }
      return next();
    },

    // Null at one editor without classifying anything (#817, plan 5.2).
    untargeted: async (_c, next) => {
      const refusal = refuseUntargetedInRegistry(sessions, task, call.routing.targeted, graph());
      return refusal ? failed(ErrorCode.INVALID_PARAMS, refusal, false) : next();
    },

    // An action served in this process never reaches the bridge, so the bridge
    // guard alone cannot refuse it.
    "read-only": async (_c, next) => {
      if (asker.access?.() !== "read-only") return next();
      const refusal = readOnlyRefusal(task, graph());
      return refusal ? failed(ErrorCode.WRITE_BLOCKED, refusal, true) : next();
    },

    playtest: async (_c, next) => {
      if (asker.origin !== "mcp") return next();
      const refusal = playtestRefusal(task, session.project.projectDir);
      return refusal ? refused(refusal, true) : next();
    },

    // One guard per editor, shared by every route; the mode is read per call.
    // An action served in this process never reaches the bridge boundary, so it
    // is checked here. A flow is checked when it starts; each step is refused
    // at the guarded bridge.
    dialog: async (_c, next) => {
      const g = deps.dialogGuardFor(session, asker.canElicit !== false && clientAdvertisesElicitation(asker.elicit));
      const preflight = await g.check(task, "action", asker.canElicit === false ? { canElicit: false } : undefined);
      if (!preflight.allow) return refused(preflight.refusal, true);
      if (isFlow) return next();
      // The dialog a press is aimed at, for the answered event.
      const facing = g.current;
      let outcome: CallOutcome;
      try {
        outcome = await next();
      } catch (e) {
        // A refusal can arrive as a throw (taking an asset lock is a bridge
        // call), and is shaped through the guard so it matches the result route.
        if (e instanceof McpError && isDialogRefusal(e.details)) {
          g.noteRefusal(e.details);
          const blocking = g.current;
          if (blocking) return refused(g.refusal(task, blocking), true);
        }
        throw e;
      }
      const result = taskResult;
      if (!result) return outcome;
      const allowed = DialogGuard.actionAllowed(task);

      // A task that failed for its own reasons reports that, not a dialog. A
      // failure DURING a modal is usually caused by it, and the runner returns
      // no data on a throw, so ask the guard, and keep its decision so the
      // refusal below reports what it actually did.
      let postRunDecision: GuardDecision | null = null;
      const failedUnderDialog = !result.success
        && !isDialogRefusal(result.data)
        && !allowed
        && (postRunDecision = await g.check(task, "action")).allow === false;
      if (!result.success && !isDialogRefusal(result.data) && !failedUnderDialog) return outcome;

      // An allow-listed read still SAYS a dialog is up: get_status must never
      // report a healthy editor while the game thread is parked.
      if (allowed) {
        // respond_to_dialog may have just cleared it. refresh, not check: check
        // applies the mode and could press a button on the next prompt unasked.
        const refreshes = result.success ? actionSpecOf(task, graph())?.refreshesDialog : undefined;
        if (refreshes === "press") g.notePress(pressedLabel(call.params, facing), facing);
        if (refreshes) await g.refresh();
        const seen = g.current;
        if (result.success) stampBlockedEditor(result.data, seen, g.modeFor(seen));
        return taskOutcome(registryTask, result, []);
      }

      // Whatever the route, the caller gets ONE refusal shape, built here.
      const fromPlugin = isDialogRefusal(result.data) ? (result.data as Record<string, unknown>) : null;
      if (fromPlugin) g.noteRefusal(fromPlugin);
      const blocking = g.current;
      const decision = blocking ? postRunDecision ?? (await g.check(task, "action")) : null;
      // The check can answer the dialog itself: the person was asked and pressed
      // a button while this call waited. Then nothing is blocking any more, and
      // the call's own result is the answer, not a refusal naming a gone dialog.
      if (blocking && decision?.allow === true && !fromPlugin) {
        const pressed = g.lastPressed;
        const note = `The dialog "${blocking.title}" that appeared during this call was answered`
          + (pressed ? ` (${pressed.button} was pressed)` : "") + ". The call's own result follows.";
        return taskOutcome(registryTask, result, [note]);
      }
      if (blocking && decision) {
        const refusal = decision.allow === false ? decision.refusal : g.refusal(task, blocking);
        // A refusal means nothing ran. Anything else had already started when
        // the dialog appeared (a call in flight when a native dialog appeared
        // was answered by the server, not refused by the plugin), so it may
        // have applied part of its work.
        const started = fromPlugin === null || fromPlugin.inFlight === true;
        return refused(
          started
            ? {
                ...refusal,
                partiallyApplied: true,
                note:
                  `'${registryTask}' had already started when the dialog appeared, so it may `
                  + "have applied some of its changes. Answer the dialog, then read the "
                  + "state back before deciding whether to run it again.",
                // A mutation that completed still has the path it created in here.
                partialResult: result.data ?? null,
              }
            : refusal,
          true,
        );
      }
      return taskOutcome(registryTask, result, []);
    },

    // Turned off by this project's `disable:`, named with the command that turns it back on.
    disabled: async (_c, next) => {
      if (isFlow) return next();
      const refusal = disabledActionRefusal(load!.pristine, load!.categories, task);
      return refusal ? failed(ErrorCode.CATEGORY_DISABLED, refusal, true) : next();
    },

    // An action this editor does not provide is refused naming the editors that
    // do; a misspelled one with the closest spellings, since `action` is
    // advertised as an enum but parsed as a string.
    known: async (_c, next) => {
      if (isFlow) return next();
      const registered = load!.registry!.listRegistered();
      if (sessions.size > 1) {
        const refusal = explainMissingAction(loads.dispatchUnion, task, session.name, registered.includes(task));
        if (refusal) return failed(ErrorCode.NOT_FOUND, refusal, false);
      }
      if (!registered.includes(registryTask)) {
        const dot = registryTask.indexOf(".");
        const category = registryTask.slice(0, dot);
        const tool = load!.registryTools.find((t) => t.name === category);
        return failed(ErrorCode.NOT_FOUND, unknownActionMessage(registryTask.slice(dot + 1), category, Object.keys(tool?.actions ?? {})), false);
      }
      return next();
    },

    // A wrapped engine tool this engine's recorded registry lacks, named before
    // the toolset check, which would only say the toolset is absent.
    engine: async (_c, next) => {
      if (isFlow) return next();
      const refusal = epicEngineRefusalFor(load!.registryTools, task, session.bridge.capabilities?.engineVersion);
      return refusal ? refused(refusal, false) : next();
    },

    // Withheld from the surface because the running plugin lacks its method
    // (6.4). An engine toolset or MCP tool can register after the handshake
    // (#1824), so one that is unknown or reads as absent is asked again.
    withheld: async (_c, next) => {
      if (isFlow) return next();
      const tools = load!.registryTools;
      let withheld = withheldTarget(tools, task, session.bridge);
      const refreshers = [
        { applies: wrapsEpicTool(tools, task), known: session.bridge.epicToolsets, refresh: session.bridge.refreshEpicToolsets },
        { applies: runsEpicMcpTool(tools, task), known: session.bridge.epicMcpTools, refresh: session.bridge.refreshEpicMcpTools },
      ];
      for (const r of refreshers) {
        if (!r.applies || !(withheld || r.known == null) || !r.refresh) continue;
        await r.refresh.call(session.bridge);
        withheld = withheldTarget(tools, task, session.bridge);
      }
      return withheld ? failed(ErrorCode.NOT_FOUND, withheld, false) : next();
    },

    // A change to a path another MCP session holds is refused, naming the
    // holder. While leases are implicit, the change takes a lease on what it
    // names for this session.
    lease: async (_c, next) => {
      const leases = sessions.leases;
      if (!leases?.enforcing) return next();
      const paths = leasePathsOf(task, call.params, graph());
      if (paths.length === 0) return next();
      const hit = leases.conflict(session.name, paths, asker.leaseHolder.id);
      if (hit) return failed(ErrorCode.ASSET_LEASED, leaseConflictMessage(hit.lease, hit.path, leases.now()), true);
      if (leases.implicit) leases.acquire(session.name, paths, asker.leaseHolder, undefined, true);
      return next();
    },

    // Journaled when the client named a group and the action may change state
    // (#1481). A flow's steps are not journaled one by one, so a run is one
    // entry whose files the checkpoints restore.
    journal: async (_c, next) => {
      let outcome: CallOutcome | undefined;
      await journalFor(session).record(task, taskEffect(task, graph()).effect, async () => {
        outcome = await next();
        return taskResult ?? { success: !isFailure(outcome), data: outcome.data };
      }, actionSpecOf(task, graph()));
      return outcome!;
    },

    // Locks are taken in the editor the call runs in, through the GUARDED
    // bridge, so a lock request made while a modal is up is refused as a
    // dialog. Every bridge call the call makes answers to its caller, so a
    // guard that allows overrides asks the person who sent it and nobody else.
    lock: (_c, next) =>
      withCallAsker(
        { elicit: asker.elicit, ...(isFlow ? {} : { subject: task }), access: asker.access, signal: asker.signal },
        () => withAssetLocks(session.guarded, deps.lockingCfg, task, call.params, next, session.lockOwnerId, graph()),
      ),

    run: async () => {
      if (target.kind === "flow") {
        const flowCtx = sessionContext(deps.flow.ctx, session);
        const data = await deps.flow.tool.handler(target.approval ? { ...flowCtx, flowStepApproval: target.approval } : flowCtx, target.input);
        return succeeded(data, [], true);
      }
      const ctx: FlowContext = {
        ...loads.contextFor(session),
        leaseHolder: asker.leaseHolder,
        elicit: asker.elicit,
        onProgress: asker.onProgress,
        signal: asker.signal,
        client: asker.client,
        gatedTask: task,
      };
      const created = await load!.registry!.create(target.registryTask, ctx, target.registryParams);
      taskResult = await created.run();
      return taskOutcome(target.registryTask, taskResult, []);
    },
  };

  const run = async (): Promise<CallOutcome> => {
    try {
      return await runGates(call, gates);
    } catch (e) {
      const code = e instanceof McpError ? e.code : ErrorCode.UNKNOWN;
      return failed(code, e instanceof Error ? e.message : String(e), !isFlow, machineDetails(e));
    }
  };
  const signal = asker.signal;
  if (!signal) return run();
  // A call its caller cancelled first, such as the next call of an interrupted
  // agent turn, is never dispatched (#1902).
  if (signal.aborted) return cancelledOutcome(task, false, !isFlow);
  // Cancelled while it runs: answered now. The work already handed to the
  // editor may finish, but its result is dropped, and the guarded bridge
  // refuses every bridge call the action would make after this.
  return new Promise<CallOutcome>((resolve) => {
    const onAbort = (): void => resolve(cancelledOutcome(task, true, !isFlow));
    signal.addEventListener("abort", onAbort, { once: true });
    void run().then((outcome) => {
      signal.removeEventListener("abort", onAbort);
      resolve(outcome);
    });
  });
}

/** The answer to a call its caller cancelled, before it was dispatched or while it ran. */
export function cancelledOutcome(task: string, started: boolean, attributed: boolean): CallOutcome {
  const dot = task.indexOf(".");
  const label = dot > 0 ? `${task.slice(0, dot)}(${task.slice(dot + 1)})` : task;
  return started
    ? failed(
        ErrorCode.CANCELLED,
        `${label} was cancelled by its caller while it ran. What it had already sent to the editor may have taken ` +
          "effect, and nothing more was sent; read the state back before running it again.",
        attributed,
        { outcome: "unknown", task },
      )
    : failed(ErrorCode.CANCELLED, `${label} was not run: its caller cancelled it first. Nothing was sent to the editor.`, attributed, {
        outcome: "failed",
        task,
      });
}
