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
import { elicitationNeedsRelay } from "./client-quirks.js";

/** The dialog, as every layer describes it. */
/** One tickable row of a dialog that asks a question per item. */
export interface DialogItem {
  index: number;
  /** The row's first cell, which is the asset name on a save prompt. */
  label: string;
  /** Every cell in Slate order: name, package path, class path. */
  cells: string[];
  checked: boolean;
}

export interface BlockingDialog {
  title: string;
  message: string;
  buttons: string[];
  choices: Array<{ buttonLabel: string; respondWith: string }>;
  /**
   * The rows this dialog lets a person tick, when it has any.
   *
   * Save Content is N questions, not one: a checkbox per unsaved package, and
   * "Save Selected" saves whatever is ticked. Without these the only honest
   * offer was all or nothing.
   */
  items?: DialogItem[];
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

/** Reads and the press. Nothing that acts on the editor, with no exceptions. */
const ACTIONS_ALLOWED_WHILE_BLOCKED = new Set([
  // Read the dialog and answer it.
  "editor.list_dialogs",
  "editor.respond_to_dialog",
  "editor.get_dialog_policy",
  // Work out what is going on.
  "editor.get_engine_state",
  "project.get_status",
]);

/**
 * The allow-listed actions that ANSWER the dialog rather than describe it.
 *
 * Being allow-listed says a call is safe to send while the game thread is
 * parked. It does not say who may make it. These are the ones that press a
 * button, so they are additionally subject to the mode: under interactive and
 * defer the answer is the person's to give, and a press arriving on the action
 * route is an agent giving it instead.
 */
const PRESS_ACTIONS = new Set(["editor.respond_to_dialog"]);

/**
 * A dialog's message flattened onto one line, for a client that shows one.
 *
 * Unreal's own prompts are laid out over many lines (the shutdown Save Content
 * prompt lists a package per line), and a renderer that keeps two of them shows
 * two package names and hides the question. Flattened, the same budget carries
 * the question itself.
 */
export function oneLine(text: string, limit = 220): string {
  const flat = text.replace(/\s+/g, " ").trim();
  if (flat.length <= limit) return flat;
  return `${flat.slice(0, limit).trimEnd()} [continues below]`;
}

/**
 * A blocking dialog, rendered for a person to read.
 *
 * Unreal flattens a Slate dialog into lines, so a save prompt arrives as its
 * prompt, then the column headers, then one line per cell, then the button
 * labels. Printed raw that is an unreadable run of paths. This pulls the asset
 * rows back out and lays them in a table, and passes anything it does not
 * recognise through untouched.
 */
export function renderDialog(dialog: BlockingDialog): string {
  const raw = dialog.message.split("\n").map((l) => l.trim()).filter((l) => l !== "");
  const buttons = new Set(dialog.buttons.map((b) => b.trim()));
  // The button labels are repeated at the end of the flattened text.
  const body = raw.filter((l) => !buttons.has(l));

  // An asset row is a name followed by two object paths: the package and the
  // class. That shape is what makes a row findable without knowing the column
  // count in advance.
  const rows: Array<[string, string, string]> = [];
  const consumed = new Set<number>();
  for (let i = 0; i + 2 < body.length; i++) {
    // The THIRD cell has to be a class path. Without that the rule matched any
    // sentence followed by two paths, so a prompt that merely lists packages
    // had its own question eaten as if it were an asset name.
    if (!body[i].startsWith("/") && body[i + 1].startsWith("/") && body[i + 2].startsWith("/Script/")) {
      rows.push([body[i], body[i + 1], body[i + 2]]);
      consumed.add(i).add(i + 1).add(i + 2);
      i += 2;
    }
  }

  const HEADERS = ["Asset", "File", "Type"];
  const prose = body.filter((l, i) => !consumed.has(i) && !HEADERS.includes(l));
  const out: string[] = [];
  if (prose.length > 0) out.push(prose.join("\n"));
  if (rows.length > 0) {
    const cols = HEADERS.map((h, c) => Math.max(h.length, ...rows.map((r) => r[c].length)));
    const line = (cells: string[]) =>
      "  " + cells.map((cell, c) => cell.padEnd(cols[c])).join("  ").trimEnd();
    out.push("");
    out.push(line(HEADERS));
    out.push(line(cols.map((w) => "-".repeat(w))));
    for (const r of rows) out.push(line(r));
  }
  return out.join("\n");
}

/** True for the refusal the plugin's own gate emits. */
export function isDialogRefusal(v: unknown): boolean {
  return typeof v === "object" && v !== null
    && (v as Record<string, unknown>).dialogBlocking === true;
}

/** Read a dialog out of whatever shape reported it. */
/** Schema key for one tickable row. Kept in one place so both ends agree. */
function itemKey(index: number): string {
  return `item_${index}`;
}

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
    ...(Array.isArray(r.items) && r.items.length > 0
      ? { items: (r.items as DialogItem[]) }
      : {}),
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

/**
 * Where interactive has got to with one dialog.
 *
 *   relay    the whole dialog has just been handed back, nothing asked yet
 *   asking   the caller has had it, so the form is up or has been
 *
 * Reported as `dialogPhase` so a caller can tell "you have not seen this yet"
 * from "the person is looking at it", which are otherwise the same refusal.
 */
export type DialogPhase = "relay" | "asking";

/** What a caller must do about this call. */
export type GuardDecision =
  | { allow: true }
  | { allow: false; refusal: Record<string, unknown> };

export interface GuardDeps {
  /** How this machine wants a blocking dialog handled. */
  mode: () => DialogMode;
  /** Reads the live dialog list. Modal-safe, so it answers while parked. */
  probe: () => Promise<unknown>;
  /**
   * Presses one button by label, applying any per-item ticks first.
   *
   * One call, because a modal is exactly where a caller cannot be relied on
   * to come back: setting the ticks and pressing the button either both
   * happen or neither does.
   */
  press: (
    buttonLabel: string,
    items?: Array<{ index: number; checked: boolean }>,
  ) => Promise<unknown>;
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

/** How big a rendered dialog has to be before a form cannot carry it. */
const RELAY_OVER_LINES = 12;
const RELAY_OVER_CHARS = 700;

export class DialogGuard {
  private blocking: BlockingDialog | null = null;
  /** True when the only evidence of a dialog is a file nobody is updating. */
  private staleStatus = false;
  /** What the last interactive answer did, for callers that report it. */
  private pressed: DialogPress | null = null;
  /** Identity of the dialog the last press was aimed at. */
  private lastAsked: string | null = null;
  /**
   * Every dialog whose full text has already gone back to a caller.
   *
   * A SET, not the one slot `lastAsked` uses, because two dialogs can be in
   * play at once (parallel callers, or a prompt raised behind another) and one
   * slot thrashes between them: telling the caller about the second forgets the
   * first, so the first is relayed a second time and its form never goes up.
   * Bounded by the same clean screen that resets everything else.
   */
  private told = new Set<string>();
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
   * Forget what has been said about a dialog, so the next one starts over.
   *
   * Both records are per-dialog and both are reset together: told-but-not-asked
   * is a real state (the caller has the text and has not retried yet), and
   * clearing one without the other either asks about a dialog nobody has read
   * or re-reads one already answered.
   *
   * Only ever called where the screen is PROVEN clear, alongside `clear`.
   */
  private forgetDialogRecords(): void {
    this.lastAsked = null;
    this.told.clear();
  }

  /**
   * Whether the AGENT gets to answer the dialog in this mode.
   *
   * The one place that rule lives, and it settles two things that used to be
   * settled separately: whether a refusal advertises the calls that press the
   * buttons, and whether a press arriving as a tool call is accepted. They are
   * the same question. Advertising without enforcing is how an agent came to
   * answer a modal under interactive, which is the one outcome that mode
   * exists to prevent.
   *
   * `auto` is the only mode whose contract hands the choice to the agent.
   * interactive puts the question to the person over an elicitation form and
   * presses only what they pick; defer waits for them at the editor's own
   * window. In neither is a button the agent chose an acceptable answer, so
   * neither names the press calls and neither accepts one.
   *
   * canElicit is still read, because interactive with nobody to ask is defer
   * (see effectiveMode) rather than a quiet promotion to auto.
   */
  static handsOverPressCalls(mode: DialogMode, canElicit = true): boolean {
    return DialogGuard.effectiveMode(mode, canElicit) === "auto";
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
    if (allowed) {
      // Allow-listed means "safe to send while the game thread is parked", not
      // "may answer the question". respond_to_dialog is both, so who presses is
      // still the mode's decision, and it was not being asked: an agent could
      // press a button under interactive, which is exactly what that mode
      // promises cannot happen.
      //
      // Only the ACTION route is gated. The press the guard itself makes, on
      // the button the person picked in the elicitation form, goes over the
      // bridge; gating that would refuse the one press interactive exists to
      // make.
      if (
        dialog
        && kind === "action"
        && PRESS_ACTIONS.has(subject)
        && !DialogGuard.handsOverPressCalls(this.deps.mode(), this.canAsk(opts))
      ) {
        return { allow: false, refusal: this.refusal(subject, dialog, opts) };
      }
      return { allow: true };
    }
    if (!dialog) return { allow: true };
    return this.decideFor(subject, dialog, opts);
  }

  /**
   * Whether this call has a person on it who can be shown a form.
   *
   * Both halves matter: the route has to carry somebody (an HTTP request does
   * not) and the client has to have advertised elicitation. Computed once here
   * so the gate and the refusal cannot answer it differently.
   */
  private canAsk(opts: { canElicit?: boolean }): boolean {
    return opts.canElicit !== false && this.deps.elicit?.() !== undefined;
  }

  /**
   * Whether this client's rendering makes the handover worth a call.
   *
   * Asked of the live elicit function rather than stored, because the client is
   * only knowable once one has connected and this guard outlives connections.
   */
  /**
   * Whether the text has to go back before a form can carry it.
   *
   * Two conditions, not one. A client that collapses a long elicitation is
   * only a problem when there IS a long elicitation: a save prompt renders as
   * a line and a short table, which every client shows in full, and relaying
   * that costs a round trip AND leaves raising the form to the agent, which is
   * not something to depend on. So the form goes up on the first call unless
   * the rendered block is genuinely too big for one.
   */
  private needsRelay(dialog: BlockingDialog): boolean {
    if (!elicitationNeedsRelay(this.deps.elicit?.()?.client?.())) return false;
    const rendered = renderDialog(dialog);
    return rendered.split("\n").length > RELAY_OVER_LINES
      || rendered.length > RELAY_OVER_CHARS;
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
    // For a dialog the form cannot hold, it goes up on the SECOND call.
    //
    // An elicitation form is a few lines tall and the client decides how many
    // of them to draw; one keeps the opening line or two and collapses the
    // rest behind "(+N more lines)" with no way to expand it. Ordering the
    // lines buys the title and a flattened gist, and that is all it can buy:
    // a long prompt still has a tail nothing in the form can reach.
    //
    // A tool result has no such budget, and the assistant's own reply has none
    // either. So a dialog that does not fit answers the first gated call whole
    // and refuses, which ends that call and puts the text in front of the
    // person through the transcript. The next call raises the form over what
    // they have already read.
    //
    // EVERY dialog, with no exemption for the action that met it and none for
    // a message that looks short enough to fit. Both exemptions were tried and
    // both were wrong: stop_editor is where Unreal's own Save Content prompt
    // appears, so exempting the recovery actions exempts the case this exists
    // for, and "short enough" is a guess about a budget the client never
    // states, where a line that fits logically still wraps on screen. A rule
    // that holds for some dialogs is one nobody can rely on for any.
    //
    // Whichever call relays also refuses, so the two phases cannot both land
    // inside one: a refusal never reaches the bridge, and the second check that
    // a dispatched call would make never happens.
    //
    // Whether this client needs it is the client's question, not the mode's. A
    // client that renders the whole message gains nothing from the round trip,
    // and the default for one nobody has checked is to relay: the two failure
    // modes are not equal.
    const interactive =
      DialogGuard.effectiveMode(this.deps.mode(), this.canAsk(opts)) === "interactive";
    if (interactive && this.needsRelay(dialog) && !this.told.has(identity)) {
      this.told.add(identity);
      return { allow: false, refusal: this.refusal(subject, dialog, opts, "relay") };
    }
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
        this.forgetDialogRecords();
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
        this.forgetDialogRecords();
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
      this.forgetDialogRecords();
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
    // Which line comes FIRST is the only lever there is over how this reads.
    //
    // A client renders the elicitation message itself, and at least one shows
    // the opening line or two and collapses the rest behind "(+N more lines)".
    // This used to open with two lines of boilerplate, so what got collapsed
    // was the dialog's own title and text: the person was asked to choose a
    // button for a question they could not see. Nothing in the protocol asks a
    // client for more room and there is no richer rendering to fall back on, so
    // the title and the gist go first and the boilerplate goes last.
    const rendered = renderDialog(dialog);
    const body = rendered === "" ? "(no message text)" : rendered;
    // No blank line between them. A client that keeps only the opening line or
    // two must spend both on the title and the question, not one on padding.
    const message = [
      `Unreal is blocked: ${dialog.title === "" ? "(untitled dialog)" : dialog.title}`,
      body,
    ];
    message.push("", "Nothing else can run until this is answered, and nothing is pressed unless you choose it.");
    // One toggle per row the dialog lets a person tick.
    //
    // Save Content is N questions, not one, and "Save Selected" saves whatever
    // is ticked. Offering the buttons alone made it all or nothing, so a person
    // who wanted two of five files had no way to say so.
    //
    // Only rows carrying a path: the header's select-all box owns the column
    // headings, and a toggle labelled "Asset" is not a question anybody asked.
    const tickable = (dialog.items ?? []).filter((i) => i.cells.some((c) => c.startsWith("/")));
    const itemProps: Record<string, unknown> = {};
    for (const item of tickable) {
      itemProps[itemKey(item.index)] = {
        type: "boolean",
        title: item.label === "" ? `Item ${item.index}` : item.label,
        description: item.cells.find((c) => c.startsWith("/")) ?? "",
        default: item.checked,
      };
    }
    try {
      answer = await elicit({
        message: message.join("\n"),
        requestedSchema: {
          type: "object",
          properties: {
            ...itemProps,
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
      // What the person ticked travels WITH the button. Sending it separately
      // would leave a modal holding a selection nobody had pressed anything on.
      const selections = tickable.map((item) => ({
        index: item.index,
        checked: answer.content?.[itemKey(item.index)] === true,
      }));
      // Called with ONE argument when there is nothing to tick, so a dialog
      // that offers no items presses exactly as it always did.
      const reply = selections.length > 0
        ? await this.deps.press(chosen, selections)
        : await this.deps.press(chosen);
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
    phase: DialogPhase = "asking",
  ): Record<string, unknown> {
    return DialogGuard.describeRefusal(
      subject,
      dialog,
      this.deps.mode(),
      this.canAsk(opts),
      phase,
    );
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
    phase: DialogPhase = "asking",
  ): Record<string, unknown> {
    const mode = DialogGuard.effectiveMode(resolvedMode, canElicit);
    // Only interactive has two phases. auto and defer say everything they have
    // to say on the first refusal and repeat it, so a phase they never enter
    // must not appear in what they report.
    const dialogPhase = mode === "interactive" ? phase : "asking";
    const common = {
      success: false,
      dialogBlocking: true,
      refusedMethod: subject,
      dialogMode: mode,
      dialogPhase,
      dialogTitle: dialog.title,
      dialogMessage: dialog.message,
      buttons: dialog.buttons,
    };
    const preamble =
      `A modal dialog is blocking the editor, so '${subject}' was refused without running. `
      + "Unreal cannot execute anything else until the dialog is answered. ";

    // Only auto hands the decision over, so only auto is told how to press.
    //
    // The other two used to be handed it as well: defer withheld `choices` and
    // then named editor(respond_to_dialog) in the next sentence, and
    // interactive shared auto's branch outright, so a person who declined the
    // form had their refusal converted into the agent's authority to answer.
    // Naming the call is what makes it happen, and the gate in `check` now
    // refuses it either way, so saying it would only describe a call that
    // comes back refused.
    if (mode === "interactive") {
      // Phase one. The form is a few lines tall and the client decides how many
      // it draws, so the dialog is handed over here, where nothing is truncated,
      // and the form goes up on the next call. Quoting it is the point of the
      // round trip: an assistant's own reply has no line budget either, and it
      // is what the person actually reads.
      if (dialogPhase === "relay") {
        return {
          ...common,
          error:
            preamble
            + "Dialog mode is interactive, so the question belongs to the person, and their form "
            + "goes up on the NEXT call rather than this one. The form is only a few lines tall "
            + "and a client may collapse the rest of it, so read dialogTitle and dialogMessage "
            + "above and QUOTE THEM IN FULL in your reply, then retry this action to raise the "
            + "form over what you quoted. Do not recommend a button and do not answer it: nothing "
            + "here can, and a press sent from here is refused.",
        };
      }
      return {
        ...common,
        error:
          preamble
          + "Dialog mode is interactive, so the question belongs to the person: it is put to them "
          + "in a form and only the button THEY choose is pressed. Nothing here can answer it, and "
          + "a press sent from here is refused. Wait for them, or let them answer it in the Unreal "
          + "Editor window. Every other action returns this same refusal until then.",
      };
    }
    if (mode === "defer") {
      return {
        ...common,
        error:
          preamble
          + "Dialog mode is defer, so this names the dialog but not the calls that press its "
          + "buttons, and a press sent from here is refused: a person answers it in the Unreal "
          + "Editor window. Every other action returns this same refusal until then.",
      };
    }
    return {
      ...common,
      choices: dialog.choices,
      error:
        preamble
        + "Dialog mode is auto, so the decision is yours. Read it in dialogMessage, choose a "
        + "button, and press it with the call beside it in choices. Every other action returns "
        + "this same refusal until then.",
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
        const guard = await ensureGuard(session);
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
export function stampBlockedEditor(
  data: unknown,
  dialog: BlockingDialog | null,
  mode: DialogMode = "defer",
): unknown {
  if (!dialog) return data;
  if (data === null || typeof data !== "object" || Array.isArray(data)) return data;
  const out = data as Record<string, unknown>;
  out.editorBlockedByDialog = true;
  out.dialogTitle = dialog.title;
  out.dialogMessage = dialog.message;
  // The note takes the mode for the same reason the refusal does, and it
  // matters more here: this rides on get_status, the first call every client
  // makes. It used to name editor(respond_to_dialog) under every mode, so the
  // opening read of an interactive session handed the agent the one call that
  // mode forbids it, before anything had been refused.
  //
  // Defaults to defer, the mode that says least, so a caller that has not
  // resolved a mode cannot leak a wider one by omission.
  out.dialogNote = DialogGuard.handsOverPressCalls(mode)
    ? "A modal dialog is blocking this editor. Every other action is refused until it is "
      + "answered: read it with editor(list_dialogs) and press with editor(respond_to_dialog)."
    : "A modal dialog is blocking this editor. Every other action is refused until it is "
      + `answered, and in ${mode} mode it is answered by a person, not from here: read it with `
      + "editor(list_dialogs) if you need to see it, but a press sent from here is refused.";
  return out;
}

/** One guard per editor. */
const guards = new WeakMap<EditorSession, DialogGuard>();

/**
 * The guard for a session, created from the session itself if it has none.
 *
 * The gate fails closed: no guard means the boundary cannot establish whether
 * a modal is up, so it refuses. That is only safe if every session HAS one,
 * and guards were created in one startup pass, so a session registered any
 * other way had none and was refused everything it ever tried.
 *
 * Every dependency here is derivable from the session, so there is no reason
 * for that gap to exist. What startup adds on top is the client's elicitation
 * capability, which is not knowable here; guardFor replaces the deps, so that
 * pass upgrades this guard rather than competing with it.
 */
export async function ensureGuard(session: EditorSession): Promise<DialogGuard> {
  const existing = guards.get(session);
  if (existing) return existing;
  // Dynamic, because editor-control imports this module: a static import here
  // would close the cycle.
  const { resolveDialogMode } = await import("./editor-control.js");
  const { readEngineSnapshot } = await import("./engine-observer.js");
  const guard = guardFor(session, {
    mode: () => resolveDialogMode({ projectDir: session.projectDir, canElicit: false }).mode,
    probe: () => session.guarded.call("list_dialogs", {}),
    press: (buttonLabel: string, items?: Array<{ index: number; checked: boolean }>) =>
      session.guarded.call("respond_to_dialog", { buttonLabel, ...(items ? { items } : {}) }),
    isConnected: () => session.bridge.isConnected,
    readSnapshot: () => {
      const proj = session.project.projectPath ?? session.bridge.getTarget().projectPath ?? null;
      return proj ? readEngineSnapshot(proj) : null;
    },
  });
  guard.startWatching();
  return guard;
}

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
