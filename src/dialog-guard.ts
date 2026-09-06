/**
 * The dialog guard. One per editor, one implementation, one decision.
 *
 * A modal dialog parks Unreal's game thread. Nothing may run while one is up,
 * whatever raised it and whichever route the caller came in by, and the caller
 * has to be forced to deal with it rather than being left to notice.
 *
 * Everything that can reach an editor delegates here:
 *
 *   GuardedBridge.call   every bridge request, so tool actions, flow steps,
 *                        nested flow runs and handler-internal calls are all
 *                        covered at the one boundary they share
 *   tool dispatch        actions served in this process, which never reach the
 *                        bridge and so cannot be refused by it
 *   the HTTP routes      the same, through the same object
 *
 * There is no second copy of this logic. A call site decides nothing; it asks
 * `check` and does what it says.
 *
 * DETECTION is proactive. The plugin publishes the active modal to its status
 * file from the modal-loop tick, which is the one tick that keeps running while
 * the game thread is parked, so a watcher there knows a dialog appeared even if
 * nothing is being called and the session is completely idle. The plugin's own
 * refusal and an on-demand probe both feed the same state, so a dialog is
 * caught by whichever notices first.
 */
import type { EditorSession } from "./session.js";
import type { IBridge } from "./bridge.js";
import type { ElicitFn } from "./types.js";
import type { DialogMode } from "./user-state.js";

/** The dialog, as every layer describes it. */
export interface BlockingDialog {
  title: string;
  message: string;
  buttons: string[];
  choices: Array<{ buttonLabel: string; respondWith: string }>;
}

/**
 * Bridge methods the plugin answers while a modal is up.
 *
 * Mirrors ModalSafeMethods in BridgeServer.cpp plus the handshake reads served
 * before the gate. Pinned by tests/unit/dialog-modal-safe-parity.test.ts,
 * because a method the plugin serves but this list omits would be read as proof
 * the editor is running.
 */
const MODAL_SAFE_METHODS = new Set([
  "list_dialogs",
  "respond_to_dialog",
  "get_dialog_policy",
  "set_dialog_policy",
  "clear_dialog_policy",
  "get_engine_state",
  "get_bridge_capabilities",
  "get_param_echo",
  "clear_param_echo",
]);

export function isModalSafeMethod(method: string): boolean {
  return MODAL_SAFE_METHODS.has(method);
}

/**
 * Bridge methods that stay callable while a modal is up.
 *
 * `set_dialog_policy` and `clear_dialog_policy` are modal-safe in the plugin,
 * so it will serve them, but an armed policy PRESSES BUTTONS on a dialog
 * already on screen. Letting one through while blocked would answer the modal
 * with no person involved, which is the whole thing `defer` promises not to do.
 * They are armed in advance or not at all.
 */
const BRIDGE_ALLOWED_WHILE_BLOCKED = new Set([
  "list_dialogs",
  "respond_to_dialog",
  "get_dialog_policy",
  "get_engine_state",
  "get_bridge_capabilities",
  "get_param_echo",
]);

/** The actions that exist to get a stuck editor moving again. */
const RECOVERY_ACTIONS = new Set([
  "editor.start_editor",
  "editor.stop_editor",
  "editor.restart_editor",
]);

/** Tool actions that stay callable, or the dialog could never be answered. */
const ACTIONS_ALLOWED_WHILE_BLOCKED = new Set([
  // Read the dialog and answer it.
  "editor.list_dialogs",
  "editor.respond_to_dialog",
  "editor.get_dialog_policy",
  // Work out what is going on.
  "editor.get_engine_state",
  "project.get_status",
  // Editor lifecycle. Permitted because they are how a caller gets out of a
  // stuck editor, and refusing them leaves no way back at all. They are NOT
  // invisible to the dialog: every allowed subject still probes, and every
  // allowed result carries the same "the editor is blocked" stamp, so a modal
  // is reported on these routes rather than passed over.
  "editor.start_editor",
  "editor.stop_editor",
  "editor.restart_editor",
]);

/** True for the refusal the plugin's own gate emits. */
export function isDialogRefusal(v: unknown): boolean {
  return typeof v === "object" && v !== null
    && (v as Record<string, unknown>).dialogBlocking === true;
}

/** Read a dialog out of whatever shape reported it. */
function asDialog(v: unknown): BlockingDialog | null {
  if (typeof v !== "object" || v === null) return null;
  const r = v as Record<string, unknown>;
  const title = typeof r.dialogTitle === "string" ? r.dialogTitle
    : typeof r.title === "string" ? r.title : null;
  if (title === null) return null;
  const message = typeof r.dialogMessage === "string" ? r.dialogMessage
    : typeof r.message === "string" ? r.message : "";
  return {
    title,
    message,
    buttons: Array.isArray(r.buttons) ? (r.buttons as string[]) : [],
    choices: Array.isArray(r.choices)
      ? (r.choices as Array<{ buttonLabel: string; respondWith: string }>)
      : [],
  };
}

/**
 * What one ask did. `shown` is per-ask rather than an instance field: two
 * concurrent asks for different dialogs raced on a shared flag, so a form
 * that never rendered could be recorded as asked and one that did could be
 * skipped.
 */
interface AskOutcome {
  shown: boolean;
  press: DialogPress | null;
}

/** A button press, and whether the editor confirmed it landed. */
export interface DialogPress {
  button: string;
  confirmed: boolean;
}

/** What a caller must do about this call. */
export type GuardDecision =
  | { allow: true }
  | { allow: false; refusal: Record<string, unknown> };

export interface GuardDeps {
  /** How this machine wants a blocking dialog handled. */
  mode: () => DialogMode;
  /** Reads the live dialog list. Modal-safe, so it answers while parked. */
  probe: () => Promise<unknown>;
  /** Presses one button by label. */
  press: (buttonLabel: string) => Promise<unknown>;
  /** Present only when the connected client advertised elicitation. */
  elicit?: () => ElicitFn | undefined;
  /**
   * The editor's published status snapshot, polled so a modal raised while
   * nothing is running is still noticed.
   *
   * Supplied rather than read here, so this is not a second reader of the
   * same file: the caller passes the instance-aware one, which prefers
   * `status.<pid>.json` over the shared file two editors of one project
   * take turns writing, and which reports how old the snapshot is.
   */
  readSnapshot?: () => { modal?: unknown; ageSeconds?: number } | null;
  /** Whether the bridge socket is currently up. */
  isConnected?: () => boolean;
}

/**
 * How old a status file may be and still describe a live editor. The plugin
 * flushes it on a timer while running, so anything older is a leftover.
 */
export const STATUS_STALE_AFTER_MS = 15_000;

export class DialogGuard {
  private blocking: BlockingDialog | null = null;
  /** True when the only evidence of a dialog is a file nobody is updating. */
  private staleStatus = false;
  /** What the last interactive answer did, for callers that report it. */
  private pressed: DialogPress | null = null;
  /** Identity of the dialog the last press was aimed at. */
  private lastAsked: string | null = null;
  /** The ask in flight, so parallel calls share it instead of each asking. */
  private asking: Promise<AskOutcome> | null = null;
  /** Which dialog that in-flight ask is about. */
  private askingFor: string | null = null;
  private poll: NodeJS.Timeout | null = null;

  constructor(private deps: GuardDeps) {}

  /** Replace the dependencies without losing what the guard knows. */
  setDeps(deps: GuardDeps): void {
    this.deps = deps;
  }

  /** What the last interactive answer did, or null if nothing was pressed. */
  get lastPressed(): DialogPress | null {
    return this.pressed;
  }

  /** The dialog currently believed to be blocking, if any. */
  get current(): BlockingDialog | null {
    return this.blocking;
  }

  /** How this machine wants a blocking dialog handled, before elicitation. */
  get mode(): DialogMode {
    return this.deps.mode();
  }

  /** Whether there is anybody to put an elicitation form in front of. */
  get canElicit(): boolean {
    return this.deps.elicit?.() !== undefined;
  }

  /** Record a dialog. Called by the watcher, a probe, or a plugin refusal. */
  note(dialog: BlockingDialog): void {
    this.blocking = dialog;
  }

  /**
   * Record that the editor is clear.
   *
   * Only ever called with positive evidence: a probe that ANSWERED and listed
   * nothing, or a non-modal-safe bridge call that ran. A failed probe is not
   * evidence and must never land here, or a dropped socket would disarm the
   * guard while the dialog is still on screen.
   */
  clear(): void {
    this.blocking = null;
  }

  /**
   * Whether this mode hands over the calls that press a dialog's buttons.
   *
   * The one place that rule lives. stop_editor computed `mode !== "defer"`
   * itself, so the same decision existed twice and could drift: the guard's
   * defer branch omits `choices` entirely while the other kept a stripped
   * version of them.
   *
   * Not conditioned on elicitation. `auto` exists precisely for a caller
   * with nobody to ask, so withholding the press calls from it would leave
   * that caller no way to answer at all. Routes with nobody on them are
   * handled where they are known about, in `refusal`.
   */
  static handsOverPressCalls(mode: DialogMode, canElicit = true): boolean {
    return DialogGuard.effectiveMode(mode, canElicit) !== "defer";
  }

  /**
   * The mode as it actually applies, given whether anyone can be asked.
   *
   * interactive with no channel to a person is not interactive: it would
   * report "interactive" and hand the buttons to the agent anyway, which is
   * auto's behaviour under a mode whose contract is that a PERSON chooses.
   * The mode resolver already falls back to defer when the client advertises
   * no elicitation; this applies the same rule everywhere else.
   */
  static effectiveMode(mode: DialogMode, canElicit: boolean): DialogMode {
    return mode === "interactive" && !canElicit ? "defer" : mode;
  }

  /** True when this bridge method may be sent while a modal is up. */
  static bridgeAllowed(method: string): boolean {
    return BRIDGE_ALLOWED_WHILE_BLOCKED.has(method);
  }

  /** True when this tool action may run while a modal is up. */
  static actionAllowed(taskName: string): boolean {
    return ACTIONS_ALLOWED_WHILE_BLOCKED.has(taskName);
  }

  /**
   * The single decision.
   *
   * `subject` is the bridge method or the `tool.action` being attempted, and is
   * only used to name it back to the caller and to check the allow list.
   */
  async check(
    subject: string,
    kind: "bridge" | "action",
    opts: { canElicit?: boolean } = {},
  ): Promise<GuardDecision> {
    const allowed = kind === "bridge"
      ? DialogGuard.bridgeAllowed(subject)
      : DialogGuard.actionAllowed(subject);

    // An allowed subject still REFRESHES what is known, it just is not refused
    // for it. Returning early without asking meant a cold guard never learned
    // the dialog at all (so get_status reported a healthy editor while the
    // game thread was parked) and a stale one was never corrected (so the very
    // call that answered the dialog came back stamped as blocked).
    const dialog = await this.currentDialog();
    if (allowed) return { allow: true };
    if (!dialog) return { allow: true };
    return this.decideFor(subject, dialog, opts);
  }

  /**
   * Recovery actions: always permitted, whatever is latched.
   *
   * Relaunching or stopping an editor is how a caller gets out of a state
   * where the editor is stuck. An editor whose socket has parked while its
   * status thread keeps publishing holds the latch fresh indefinitely, so
   * gating these refused the only way back forever. They still report the
   * dialog through the same stamp every allowed read carries.
   */
  static isRecoveryAction(taskName: string): boolean {
    return RECOVERY_ACTIONS.has(taskName);
  }

  /**
   * The decision about a dialog the caller ALREADY has, with no probe.
   *
   * `check` is this plus finding the dialog first. A caller that just read it
   * (stop_editor, which must read before it sends a quit) uses this, so the
   * mode is applied in exactly one place without paying for a second read or
   * risking a re-probe that answers differently.
   */
  async decideFor(
    subject: string,
    dialog: BlockingDialog,
    opts: { canElicit?: boolean } = {},
  ): Promise<GuardDecision> {
    // interactive: the dialog goes to the person, and only the button they
    // pick is pressed. Answering it clears the way, so the call proceeds.
    //
    // Asked ONCE per dialog. A single call is checked twice, once before
    // dispatch and once at the bridge, and an in-process action needs the
    // first while a bridge call needs the second. Without this the person was
    // shown the same prompt twice and the button was pressed TWICE, which on a
    // save prompt is two real answers for one action. If the same dialog is
    // still there after a press, pressing again will not help: report it.
    const identity = `${dialog.title} :: ${dialog.message}`;
    // canElicit: false means this route has nobody to ask. An HTTP request
    // has no person on it, and eliciting would raise a form in whichever
    // MCP client last touched this guard and press a real button on its
    // answer, for a request that client never made.
    const mayAsk = opts.canElicit !== false;
    if (mayAsk && this.deps.mode() === "interactive" && this.lastAsked !== identity) {
      // Parallel tool calls share one ask. Clients batch calls, and asking per
      // call put three forms in front of the person and pressed three real
      // buttons for one dialog, with presses two and three landing on whatever
      // was on screen after the first.
      // Shared only for the SAME dialog. Keyed on nothing, a caller that saw a
      // different prompt would await this one's form and inherit its press,
      // answering a question it was never shown.
      if (!this.asking || this.askingFor !== identity) {
        this.askingFor = identity;
        this.asking = this.askUser(dialog).finally(() => {
          this.asking = null;
          this.askingFor = null;
        });
      }
      const outcome = await this.asking;
      const pressed = outcome.press;
      // Recorded only once a form was actually SHOWN. Setting it before the
      // ask meant an elicitation that threw, timed out, or was declined
      // consumed the one chance, and the person was never asked about that
      // dialog again for the life of the process.
      if (outcome.shown) this.lastAsked = identity;
      this.pressed = pressed;
      // Only a CONFIRMED press proves the way is clear. An unknown one is
      // reported, not assumed.
      if (pressed?.confirmed) {
        this.clear();
        return { allow: true };
      }
    }
    return { allow: false, refusal: this.refusal(subject, dialog, opts) };
  }

  /**
   * Re-read what is on screen, deciding nothing.
   *
   * `check` applies the mode, which in interactive raises a form and presses a
   * button. A caller that has just answered a dialog by hand and only wants
   * the state corrected must not do that: it would put a form up for whatever
   * prompt the first answer surfaced and press a button on it, unasked.
   */
  async refresh(): Promise<void> {
    await this.currentDialog();
  }

  /**
   * What is on screen right now.
   *
   * The watcher usually knows already. When it does not, ask the editor. A
   * probe that THROWS is not an answer: the state is left exactly as it was,
   * so an unreachable editor cannot disarm the guard.
   */
  private async currentDialog(): Promise<BlockingDialog | null> {
    // Always ask, even when a dialog is already believed to be up. Trusting
    // the cached value meant a dialog answered by hand in the editor window
    // left the guard latched forever, with no call able to clear it.
    // A disconnected socket cannot be asked, so skip the probe: it would burn a
    // connection attempt per call, twice per gated call, and the full timeout
    // each time against an editor that is down.
    //
    // But it is NOT evidence there is no dialog. isConnected describes the
    // SOCKET, and an editor can be alive and sitting on a modal with its socket
    // dropped, still publishing that modal to its status file. Clearing here
    // deleted what the watcher knew and let everything run against a frozen
    // editor, permanently, which is worse than the cost it saved.
    //
    // So: keep what is known. A genuinely dead editor is already handled, its
    // status file is absent or stale and the watcher has cleared the latch.
    if (this.deps.isConnected?.() === false) {
      // Nothing refreshing the status file means there is no editor to be
      // blocked. Returning the latch unconditionally here is what made a dead
      // editor refuse every action forever, including the two the refusal
      // names, with no way back but restarting the server.
      if (this.staleStatus) {
        this.clear();
        this.lastAsked = null;
        return null;
      }
      return this.blocking;
    }
    let answered: unknown;
    try {
      answered = await this.deps.probe();
    } catch {
      // The editor did not answer, and getting here means the socket was UP:
      // the disconnected case returned above. A live socket that will not
      // answer is an editor whose game thread is not running, which is the
      // exact condition this guard exists for. So keep what is known and never
      // clear.
      //
      // Clearing here on a missing status file was a permanent miss rather
      // than a race: a project whose editor has not published a snapshot yet
      // is the steady state right after launch, so the latch was dropped every
      // time instead of occasionally.
      //
      // With no isConnected to consult there is no way to tell a parked editor
      // from a dead one, and then a stale or absent snapshot is the only
      // evidence available: clearing on it is what keeps a dead editor from
      // refusing every action forever, including the two the refusal names.
      const socketUp = this.deps.isConnected?.() === true;
      if (!socketUp && (this.staleStatus || this.deps.readSnapshot === undefined)) {
        this.clear();
        this.lastAsked = null;
        return null;
      }
      return this.blocking;
    }
    const list = (answered as { dialogs?: unknown })?.dialogs;
    if (!Array.isArray(list)) {
      // Answered, but not in a shape this understands (an older plugin
      // returning "Unknown method" as a result rather than throwing). That is
      // not positive evidence the screen is clear, so keep what we know.
      return this.blocking;
    }
    const first = list[0];
    if (!first) {
      // The editor ANSWERED and listed nothing, so the screen really is clear.
      // That is the only place the asked-once record is reset: clearing after
      // a press must not reset it, or the next check re-prompts for a dialog
      // that press was meant to answer and presses a second button.
      this.clear();
      this.lastAsked = null;
      return null;
    }
    const dialog = asDialog(first);
    if (dialog) this.note(dialog);
    return dialog;
  }

  /** Put it to the person; return the button they chose, or null. */
  private async askUser(dialog: BlockingDialog): Promise<AskOutcome> {
    const elicit = this.deps.elicit?.();
    if (!elicit || dialog.buttons.length === 0) return { shown: false, press: null };
    const LEAVE_OPEN = "Leave the dialog open";
    let answer;
    try {
      answer = await elicit({
        message: [
          "Unreal Editor is blocked on a modal dialog and is waiting for an answer.",
          "Nothing else can run until it is answered.",
          "",
          `Title: ${dialog.title}`,
          dialog.message === "" ? "(no message text)" : dialog.message,
          "",
          "Choose the button to press. Nothing is pressed unless you choose it.",
        ].join("\n"),
        requestedSchema: {
          type: "object",
          properties: {
            button: {
              type: "string",
              title: "Button",
              description: "The dialog's own buttons, in the order it lays them out.",
              enum: [...dialog.buttons, LEAVE_OPEN],
            },
          },
          required: ["button"],
        },
      });
    } catch {
      // The form never rendered. Not an answer, and not a used-up chance.
      return { shown: false, press: null };
    }
    if (answer.action !== "accept") return { shown: true, press: null };
    const chosen = answer.content?.button;
    if (typeof chosen !== "string" || chosen === LEAVE_OPEN) return { shown: true, press: null };
    if (!dialog.buttons.includes(chosen)) return { shown: true, press: null };
    // Three outcomes, not two.
    //
    //   pressed      the editor confirmed it
    //   not pressed  the editor answered and said it did not (wrong label,
    //                dialog already gone, handler refused)
    //   unknown      the frame went out and nothing came back, so whether the
    //                button was pressed genuinely cannot be reported either way
    //
    // Collapsing the third into "not pressed" claims the editor is untouched
    // when it may not be.
    try {
      const reply = await this.deps.press(chosen);
      const r = typeof reply === "object" && reply !== null
        ? (reply as Record<string, unknown>)
        : {};
      // The editor answered and the press went through.
      const confirmed = r.answered === true
        ? !r.methodError && !r.refused && r.success !== false
        : r.success === true;
      if (confirmed) return { shown: true, press: { button: chosen, confirmed: true } };
      // It answered and said it did not press: nothing happened, report silence.
      if (r.answered === true) return { shown: true, press: null };
      // It never answered. The frame was already on the wire, so whether the
      // button was pressed is genuinely unknown and must not be claimed.
      return { shown: true, press: { button: chosen, confirmed: false } };
    } catch {
      return { shown: true, press: null };
    }
  }

  /** The one refusal shape, whatever the route and whatever the mode. */
  refusal(
    subject: string,
    dialog: BlockingDialog,
    opts: { canElicit?: boolean } = {},
  ): Record<string, unknown> {
    const canAsk = opts.canElicit !== false && this.deps.elicit?.() !== undefined;
    return DialogGuard.describeRefusal(subject, dialog, this.deps.mode(), canAsk);
  }

  /**
   * The refusal shape. ONE definition, so every route hands a caller the same
   * fields to branch on.
   *
   * Static because stop_editor builds its refusal without an instance: it runs
   * over its own transport while a quit may be in flight. It used to assemble
   * its own object, which carried `dialogBlocking` but no `refusedMethod`,
   * `dialogTitle`, `dialogMessage` or `error`, so a client reading those got
   * undefined depending on which route refused it.
   */
  static describeRefusal(
    subject: string,
    dialog: BlockingDialog,
    resolvedMode: DialogMode,
    canElicit = true,
  ): Record<string, unknown> {
    const mode = DialogGuard.effectiveMode(resolvedMode, canElicit);
    const common = {
      success: false,
      dialogBlocking: true,
      refusedMethod: subject,
      dialogMode: mode,
      dialogTitle: dialog.title,
      dialogMessage: dialog.message,
      buttons: dialog.buttons,
    };
    const preamble =
      `A modal dialog is blocking the editor, so '${subject}' was refused without running. `
      + "Unreal cannot execute anything else until the dialog is answered. ";

    if (mode === "defer") {
      return {
        ...common,
        error:
          preamble
          + "Dialog mode is defer, so this names the dialog but not the calls that press its "
          + "buttons: answer it in the Unreal Editor window. To answer it from here instead, "
          + "read it with editor(list_dialogs) and press with editor(respond_to_dialog).",
      };
    }
    return {
      ...common,
      choices: dialog.choices,
      error:
        preamble
        + "Read it in dialogMessage, choose a button, and press it with the call beside it in "
        + "choices. Every other action returns this same refusal until then.",
    };
  }

  /**
   * Learn from a bridge reply.
   *
   * A refusal names the dialog. Anything else is evidence the game thread ran,
   * but ONLY for a method the plugin would have refused: a modal-safe method
   * answers either way, so reading the dialog list must not be mistaken for the
   * dialog having gone.
   */
  observe(method: string, result: unknown): void {
    if (isDialogRefusal(result)) {
      const dialog = asDialog(result);
      if (dialog) this.note(dialog);
      return;
    }
    if (!isModalSafeMethod(method)) this.clear();
  }

  /**
   * Watch the editor's status file so a dialog raised while nothing is running
   * is known immediately, rather than at the next call.
   *
   * The plugin refreshes that file from the modal-loop tick, which keeps firing
   * while the game thread is parked. Watching costs nothing on the hot path and
   * needs no bridge traffic; the poll is a fallback for platforms where the
   * watch does not fire.
   */
  startWatching(intervalMs = 1000): void {
    if (this.poll) return;
    const read = () => {
      if (!this.deps.readSnapshot) return;
      let snap: { modal?: unknown; ageSeconds?: number } | null;
      try {
        snap = this.deps.readSnapshot();
      } catch {
        return;
      }
      // No snapshot, or one nobody has refreshed. The plugin removes its file
      // on a clean shutdown and a crash leaves a stale one behind, often
      // recording the very modal that preceded it, so neither is evidence of a
      // live dialog. Believing them refused every action forever.
      const stale = snap === null
        || (snap.ageSeconds !== undefined && snap.ageSeconds * 1000 > STATUS_STALE_AFTER_MS);
      if (stale) {
        this.staleStatus = true;
        this.clear();
        return;
      }
      this.staleStatus = false;
      const modal = snap?.modal;
      const dialog = asDialog(modal);
      if (dialog) this.note(dialog);
      else if (modal === null || modal === undefined) this.clear();
    };
    this.poll = setInterval(read, intervalMs);
    this.poll.unref?.();
    read();
  }

  stopWatching(): void {
    if (this.poll) clearInterval(this.poll);
    this.poll = null;
  }
}

/**
 * A raw bridge that still cannot press a dialog button.
 *
 * Plugin guard tasks run on the RAW bridge on purpose: routing them through
 * GuardedBridge would re-enter the guard pipeline that is running them. That
 * left one real hole, because `set_dialog_policy` is modal-safe in the plugin
 * and WILL be served: a guard task could arm a policy that answers the modal
 * already on screen, with no person involved, under a mode that promises
 * exactly the opposite.
 *
 * Applied to the SESSION's bridge, so it holds on every path: the guarded
 * bridge wraps it, guard tasks get it, and a handler reaching for
 * `ctx.session.bridge` directly gets it too. Wrapping only one caller left the
 * escape one line away.
 *
 * This refuses those two methods and nothing else, from state already held, so
 * it adds no round-trip and cannot recurse.
 */
export function withoutDialogActuation<T extends IBridge>(session: EditorSession, raw: T): T {
  return new Proxy(raw, {
    get(target, prop, receiver) {
      if (prop !== "call") return Reflect.get(target, prop, receiver);
      return async (method: string, params?: Record<string, unknown>, timeoutMs?: number) => {
        const armsAPolicy = method === "set_dialog_policy" || method === "clear_dialog_policy";
        if (!armsAPolicy) return target.call(method, params, timeoutMs);

        // This is the ONLY thing standing in front of an unattended button
        // press: the plugin serves both methods during a modal on purpose, so
        // whatever this lets through presses a button on the dialog already on
        // screen. It therefore fails CLOSED, the same way refuseIfBlocked does
        // at the other boundary, and it asks rather than reading a latch that
        // may never have been armed.
        //
        // Reading `current` alone was not a race: with no readable snapshot
        // the latch is empty in the steady state, so a live modal was missed
        // every time rather than occasionally.
        const guard = existingGuard(session);
        if (!guard) {
          return {
            success: false,
            dialogBlocking: true,
            refusedMethod: method,
            error:
              `'${method}' was refused because this editor has no dialog guard, so whether a `
              + "modal is on screen cannot be established, and an armed policy presses the "
              + "buttons of whatever is. Re-register the editor with project(add_editor).",
          };
        }
        await guard.refresh();
        const dialog = guard.current;
        if (dialog) {
          return {
            ...DialogGuard.describeRefusal(method, dialog, guard.mode, guard.canElicit),
            error:
              `'${method}' was refused: a modal dialog is on screen and an armed policy presses `
              + "its buttons. Arm a policy before a dialog appears, or answer this one with "
              + "editor(respond_to_dialog).",
          };
        }
        return target.call(method, params, timeoutMs);
      };
    },
  });
}

/**
 * Mark a successful result as coming from an editor that is blocked.
 *
 * An allowed read still SAYS a dialog is up: get_status is the first call every
 * client makes, and reporting a healthy editor while the game thread is parked
 * is the one answer it must never give.
 *
 * `editorBlockedByDialog` deliberately is NOT `dialogBlocking`, which means
 * "this call was refused". Stamping that here would have a client treat a
 * successful read as a refusal.
 *
 * Returns the value unchanged when there is nothing to say, or when the shape
 * cannot carry the fields: an array is `typeof "object"`, so it took the
 * properties and then lost them silently in JSON.stringify.
 */
export function stampBlockedEditor(data: unknown, dialog: BlockingDialog | null): unknown {
  if (!dialog) return data;
  if (data === null || typeof data !== "object" || Array.isArray(data)) return data;
  const out = data as Record<string, unknown>;
  out.editorBlockedByDialog = true;
  out.dialogTitle = dialog.title;
  out.dialogMessage = dialog.message;
  out.dialogNote =
    "A modal dialog is blocking this editor. Every other action is refused until it is "
    + "answered: read it with editor(list_dialogs) and press with editor(respond_to_dialog).";
  return out;
}

/** One guard per editor. */
const guards = new WeakMap<EditorSession, DialogGuard>();

export function guardFor(session: EditorSession, deps: GuardDeps): DialogGuard {
  const existing = guards.get(session);
  if (existing) {
    // The guard is kept, but its dependencies are REPLACED. They close over
    // whether the connected client can be elicited, which is not known at
    // startup and differs per call. Caching the first set froze every guard
    // with canElicit=false, so the mode never resolved to interactive and the
    // elicit hook was permanently undefined: interactive mode could not fire
    // on any route, on any server.
    existing.setDeps(deps);
    return existing;
  }
  const created = new DialogGuard(deps);
  guards.set(session, created);
  return created;
}

export function existingGuard(session: EditorSession): DialogGuard | undefined {
  return guards.get(session);
}

/** Test seam. */
export function forgetGuard(session: EditorSession): void {
  guards.get(session)?.stopWatching();
  guards.delete(session);
}
