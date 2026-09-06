/**
 * Nothing in this server presses a dialog button by itself, and stop_editor
 * never discards unsaved work.
 *
 * A modal dialog is a question for a person. stop_editor therefore asks two
 * questions before it sends anything: is a dialog blocking the editor, and is
 * any package unsaved. Either one refuses, reports the whole question, and
 * sends no quit.
 *
 * These are safety defaults, so the assertions are deliberately about what does
 * NOT go out over the socket: a stop must never send set_dialog_policy (arming
 * an answer for a prompt nobody has read) and must never send respond_to_dialog
 * unless a person chose that button through the elicitation prompt. A change
 * that puts auto-arming or auto-pressing back fails here.
 */

import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { AddressInfo } from "node:net";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { WebSocketServer, type WebSocket as ServerSocket } from "ws";
import type { EditorProcess } from "../../src/engine-observer.js";
import type { ElicitFn, ElicitParams, ElicitResult } from "../../src/types.js";

vi.mock("../../src/engine-observer.js", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../src/engine-observer.js")>();
  return {
    ...actual,
    listEditorProcesses: vi.fn(async () => []),
    findInteractiveEditors: vi.fn(async () => []),
    findEditorByPid: vi.fn(async () => null),
    readEngineState: vi.fn(async () => ({
      running: true,
      processes: [],
      log: { logPath: null, secondsSinceWrite: null, phase: "unknown", blocking: false, lastLine: null, tail: [], errors: [], warnings: [] },
      snapshot: null,
      dialogs: [],
      summary: "stubbed engine state.",
      blocked: false,
    })),
  };
});

const observer = await import("../../src/engine-observer.js");
const { stopEditor, resolveDialogMode, clientAdvertisesElicitation } = await import("../../src/editor-control.js");
const { bridgeLockfilePath } = await import("../../src/editor-target.js");
const { setDialogMode } = await import("../../src/user-state.js");

const findInteractiveEditors = vi.mocked(observer.findInteractiveEditors);
const findEditorByPid = vi.mocked(observer.findEditorByPid);

const EDITOR_PID = 4242;

/** Every call that would press or pre-answer a button on somebody's behalf. */
const BUTTON_PRESSING_METHODS = ["set_dialog_policy", "respond_to_dialog"];

interface FakeBridge {
  /** Every {method, params} the stop sent, in order. */
  calls: { method: string; params: Record<string, unknown> }[];
  methods: () => string[];
  port: number;
  /** Stop listening and drop the sockets, so the port goes quiet. */
  goQuiet: () => void;
  close: () => Promise<void>;
}

/**
 * A bridge that answers on a real socket. `stopEditor` deliberately opens its
 * own connection per call rather than going through the session bridge, so the
 * only honest way to observe what it sends is to listen for it.
 */
/**
 * Returned by a fake bridge's `reply` to model the case D8 is about: the frame
 * arrived and the socket died before any answer came back. callBridgeOnce turns
 * that into its `silent` reply, which is the same value an 8s timeout produces,
 * and it does so AFTER the press has gone out.
 */
const DROP_SOCKET = Symbol("drop-socket");

async function startFakeBridge(
  reply: (
    method: string,
    params: Record<string, unknown>,
  ) => Record<string, unknown> | null | typeof DROP_SOCKET,
): Promise<FakeBridge> {
  const wss = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve) => wss.once("listening", resolve));
  const calls: { method: string; params: Record<string, unknown> }[] = [];
  const sockets = new Set<ServerSocket>();

  wss.on("connection", (socket) => {
    sockets.add(socket);
    socket.on("close", () => sockets.delete(socket));
    socket.on("message", (raw) => {
      const message = JSON.parse(String(raw)) as { id?: string; method: string; params?: Record<string, unknown> };
      calls.push({ method: message.method, params: message.params ?? {} });
      const result = reply(message.method, message.params ?? {});
      if (result === DROP_SOCKET) {
        socket.terminate();
        return;
      }
      if (result === null) {
        socket.send(JSON.stringify({ id: message.id, error: { code: -32601, message: "Unknown method" } }));
        return;
      }
      socket.send(JSON.stringify({ id: message.id, result }));
    });
  });

  return {
    calls,
    methods: () => calls.map((c) => c.method),
    port: (wss.address() as AddressInfo).port,
    goQuiet: () => {
      for (const socket of sockets) socket.terminate();
      wss.close();
    },
    close: () =>
      new Promise<void>((resolve) => {
        for (const socket of sockets) socket.terminate();
        wss.close(() => resolve());
      }),
  };
}

/**
 * An elicitation gate shaped like the one the shipped server hands to tools.
 *
 * This shape matters more than it looks. The server builds the gate at startup,
 * before any client has connected, so the function is ALWAYS there and its
 * presence says nothing about whether the user can be asked; the capability is
 * read live, per call. A test that passes a bare function, or none at all, is
 * testing a shape production never produces, which is how "defaults to defer
 * without elicitation" passed while every real client was being handed
 * interactive.
 */
function makeGate(
  advertises: boolean,
  answer: (params: ElicitParams) => ElicitResult = () => ({ action: "decline" }),
): { fn: ElicitFn; asked: () => number; lastParams: () => ElicitParams | null } {
  let asked = 0;
  let last: ElicitParams | null = null;
  const fn: ElicitFn = async (params) => {
    asked++;
    last = params;
    if (!advertises) {
      // What the real gate does for a client that advertised nothing.
      throw new Error("Connected MCP client did not advertise the `elicitation` capability");
    }
    return answer(params);
  };
  fn.clientAdvertisesElicitation = () => advertises;
  return { fn, asked: () => asked, lastParams: () => last };
}

const temporaryRoots: string[] = [];
const openBridges: FakeBridge[] = [];

function makeProject(port: number): string {
  const projectDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-stop-"));
  temporaryRoots.push(projectDir);
  const projectPath = path.join(projectDir, "Demo.uproject");
  fs.writeFileSync(projectPath, JSON.stringify({ EngineAssociation: "5.8" }));

  const lockfile = bridgeLockfilePath(projectDir);
  fs.mkdirSync(path.dirname(lockfile), { recursive: true });
  fs.writeFileSync(lockfile, JSON.stringify({ port, pid: EDITOR_PID }));

  const editor: EditorProcess = {
    pid: EDITOR_PID,
    commandLine: "",
    projectPath,
    headless: false,
    responding: true,
    windowTitle: null,
  };
  findEditorByPid.mockResolvedValue(editor);
  findInteractiveEditors.mockResolvedValue([editor]);
  return projectDir;
}

let savedHost: string | undefined;
let savedDialogMode: string | undefined;
let savedUserState: string | undefined;

beforeEach(() => {
  savedHost = process.env.UE_MCP_HOST;
  delete process.env.UE_MCP_HOST;
  // The dialog handling mode is read from the environment and from
  // ~/.ue-mcp/state.json. Point both somewhere empty so these assertions are
  // about the defaults rather than about whatever this machine's owner set.
  savedDialogMode = process.env.UE_MCP_DIALOG_MODE;
  delete process.env.UE_MCP_DIALOG_MODE;
  savedUserState = process.env.UE_MCP_USER_STATE;
  const stateRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-state-"));
  temporaryRoots.push(stateRoot);
  process.env.UE_MCP_USER_STATE = path.join(stateRoot, "state.json");
});

afterEach(async () => {
  if (savedHost === undefined) delete process.env.UE_MCP_HOST;
  else process.env.UE_MCP_HOST = savedHost;
  if (savedDialogMode === undefined) delete process.env.UE_MCP_DIALOG_MODE;
  else process.env.UE_MCP_DIALOG_MODE = savedDialogMode;
  if (savedUserState === undefined) delete process.env.UE_MCP_USER_STATE;
  else process.env.UE_MCP_USER_STATE = savedUserState;
  vi.clearAllMocks();
  findInteractiveEditors.mockResolvedValue([]);
  findEditorByPid.mockResolvedValue(null);
  while (openBridges.length > 0) await openBridges.pop()!.close();
  while (temporaryRoots.length > 0) fs.rmSync(temporaryRoots.pop()!, { recursive: true, force: true });
});

const NO_DIALOGS = { success: true, dialogs: [], count: 0 };

/** The editor's answer when its own dirty check refuses the shutdown. */
const DIRTY_REFUSAL = {
  success: false,
  error: "Editor has dirty content or map packages; save or discard them before requesting shutdown",
  scheduled: false,
  requireClean: true,
  dirtyContentPackages: ["/Game/Materials/M_Rock"],
  dirtyMapPackages: ["/Temp/Untitled_1"],
};

/** The real shutdown prompt: a long message and three buttons, none of them "OK". */
const SAVE_CONTENT_MESSAGE =
  "The following content and maps have been modified and are not saved.\n" +
  "Select the ones you want to save, or choose Don't Save to close without saving any of them.\n" +
  "Closing without saving discards every change made since the last save, and this cannot be undone.";

const SAVE_CONTENT_DIALOG = {
  success: true,
  count: 1,
  dialogs: [
    {
      title: "Save Content",
      message: SAVE_CONTENT_MESSAGE,
      messageTruncated: false,
      buttons: ["Save Selected", "Don't Save", "Cancel"],
      choices: [
        { buttonLabel: "Save Selected", respondWith: "editor(action='respond_to_dialog', buttonLabel='Save Selected')" },
        { buttonLabel: "Don't Save", respondWith: "editor(action='respond_to_dialog', buttonLabel=\"Don't Save\")" },
        { buttonLabel: "Cancel", respondWith: "editor(action='respond_to_dialog', buttonLabel='Cancel')" },
      ],
      policyMatched: false,
    },
  ],
};

describe("stop_editor never presses a button and never arms one", () => {
  it("arms no dialog policy and presses nothing while refusing a dirty editor", async () => {
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return NO_DIALOGS;
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);

    await stopEditor(makeProject(bridge.port));

    for (const forbidden of BUTTON_PRESSING_METHODS) {
      expect(bridge.methods()).not.toContain(forbidden);
    }
    expect(bridge.methods()).not.toContain("execute_python");
  });

  it("arms no dialog policy and presses nothing while stopping a clean editor", async () => {
    let bridge: FakeBridge;
    bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return NO_DIALOGS;
      if (method === "request_editor_shutdown") {
        setTimeout(() => bridge.goQuiet(), 50);
        return { success: true, scheduled: true, dirtyContentPackages: [], dirtyMapPackages: [] };
      }
      return { success: true };
    });
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port));

    expect(result.success).toBe(true);
    expect(bridge.methods()).toEqual(["list_dialogs", "request_editor_shutdown"]);
  });

  it("arms no dialog policy and presses nothing when a dialog is already blocking", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    await stopEditor(makeProject(bridge.port));

    for (const forbidden of BUTTON_PRESSING_METHODS) {
      expect(bridge.methods()).not.toContain(forbidden);
    }
  });
});

describe("stop_editor refuses rather than discarding unsaved work", () => {
  it("names every dirty package and how to keep it", async () => {
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return NO_DIALOGS;
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port));

    expect(result.success).toBe(false);
    expect(result.refusedReason).toBe("unsaved-work");
    expect(result.dirtyPackages).toEqual(["/Game/Materials/M_Rock", "/Temp/Untitled_1"]);
    expect(result.message).toContain("/Game/Materials/M_Rock");
    expect(result.message).toContain("/Temp/Untitled_1");
    expect(result.message).toContain("save_dirty");
    // No flag exists that would discard them, so none is offered.
    expect(result.message).not.toContain("discardUnsaved");
  });

  it("sends no quit at all, so no save prompt is raised and nothing hangs", async () => {
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return NO_DIALOGS;
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);

    await stopEditor(makeProject(bridge.port));

    // The dirty check, which refuses inside the engine without scheduling a
    // close, and nothing after it.
    expect(bridge.methods()).toEqual(["list_dialogs", "request_editor_shutdown"]);
    expect(bridge.calls[1].params.requireClean).toBe(true);
  });

  it("refuses when the plugin build cannot say what is dirty, rather than guessing clean", async () => {
    const bridge = await startFakeBridge(() => null);
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port));

    expect(result.success).toBe(false);
    expect(result.refusedReason).toBe("unknown-dirty-state");
    expect(result.message).toContain("cannot be established");
    expect(bridge.methods()).not.toContain("execute_python");
  });

  it("refuses on the dirty list from an older plugin build too", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dirty_packages"
        ? { success: true, content: [{ package: "/Game/Blueprints/BP_Door" }], maps: [] }
        : null,
    );
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port));

    expect(result.success).toBe(false);
    expect(result.refusedReason).toBe("unsaved-work");
    expect(result.dirtyPackages).toEqual(["/Game/Blueprints/BP_Door"]);
    expect(bridge.methods()).not.toContain("execute_python");
  });
});

describe("stop_editor reports a blocking dialog in full", () => {
  it("returns immediately with the exact title, the whole message and every button", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    // No capability, so the mode is defer: the dialog is reported for
    // recognition, and the calls that press it are not part of that.
    const result = await stopEditor(makeProject(bridge.port), { elicit: makeGate(false).fn });

    expect(result.success).toBe(false);
    expect(result.refusedReason).toBe("blocking-dialog");
    // The SAME shape every other route hands back, not just the one flag. This
    // used to assemble its own object, so refusedMethod, dialogTitle,
    // dialogMessage and error were undefined here and defined everywhere else,
    // depending on which route did the refusing.
    expect(result.dialogBlocking).toBe(true);
    expect(result.refusedMethod).toBe("editor.stop_editor");
    expect(result.dialogTitle).toBe("Save Content");
    expect(typeof result.dialogMessage).toBe("string");
    expect(Array.isArray(result.buttons)).toBe(true);
    expect(String(result.error)).toContain("was refused without running");
    expect(result.blockingDialog?.title).toBe("Save Content");
    // The WHOLE message, every line of it.
    expect(result.blockingDialog?.message).toBe(SAVE_CONTENT_MESSAGE);
    for (const line of SAVE_CONTENT_MESSAGE.split("\n")) {
      expect(result.message).toContain(line);
    }
    // Every button, in the dialog's own order, so the window can be identified.
    expect(result.blockingDialog?.buttons).toEqual(["Save Selected", "Don't Save", "Cancel"]);
    for (const label of ["Save Selected", "Don't Save", "Cancel"]) {
      expect(result.message).toContain(label);
    }
    // No button is recommended.
    expect(result.message.toLowerCase()).not.toContain("recommend");
  });

  it("reports it for recognition in defer: nothing that presses anything, anywhere in the payload", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port), { elicit: makeGate(false).fn });

    expect(result.dialogMode).toBe("defer");
    // Not in the prose, and not in the structured half either: `choices` keeps
    // the labels and drops respondWith, so there is nothing to copy and run.
    expect(result.message).not.toContain("respond_to_dialog");
    expect(JSON.stringify(result.blockingDialog)).not.toContain("respond_to_dialog");
    expect(result.blockingDialog?.choices.map((c) => c.buttonLabel)).toEqual([
      "Save Selected",
      "Don't Save",
      "Cancel",
    ]);
    for (const choice of result.blockingDialog?.choices ?? []) {
      expect(choice.respondWith).toBeUndefined();
    }
  });

  it("keeps every press call in auto, where answering it is the agent's job", async () => {
    process.env.UE_MCP_DIALOG_MODE = "auto";
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port), { elicit: makeGate(false).fn });

    expect(result.dialogMode).toBe("auto");
    expect(result.message).toContain("editor(action='respond_to_dialog', buttonLabel='Save Selected')");
    expect(result.message).toContain("editor(action='respond_to_dialog', buttonLabel=\"Don't Save\")");
    expect(result.message).toContain("editor(action='respond_to_dialog', buttonLabel='Cancel')");
    expect(result.blockingDialog?.choices.every((c) => typeof c.respondWith === "string")).toBe(true);
  });

  it("does not send the quit while a dialog is up", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    await stopEditor(makeProject(bridge.port));

    expect(bridge.methods()).toEqual(["list_dialogs"]);
  });
});

describe("stop_editor puts a blocking dialog to the user through elicitation", () => {
  it("offers the dialog's own buttons and presses only the one chosen", async () => {
    let asked: ElicitParams | null = null;
    let dialogUp = true;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return dialogUp ? SAVE_CONTENT_DIALOG : NO_DIALOGS;
      if (method === "respond_to_dialog") {
        dialogUp = false;
        return { success: true, clickedButton: "Save Selected" };
      }
      if (method === "request_editor_shutdown") {
        setTimeout(() => bridge.goQuiet(), 50);
        return { success: true, scheduled: true, dirtyContentPackages: [], dirtyMapPackages: [] };
      }
      return { success: true };
    });
    openBridges.push(bridge);

    const elicit = async (params: ElicitParams): Promise<ElicitResult> => {
      asked = params;
      return { action: "accept", content: { button: "Save Selected" } };
    };

    const result = await stopEditor(makeProject(bridge.port), { elicit });

    // The prompt carries the full question and the dialog's real buttons.
    expect(asked!.message).toContain("Save Content");
    expect(asked!.message).toContain(SAVE_CONTENT_MESSAGE);
    expect(asked!.requestedSchema.properties.button).toMatchObject({
      enum: ["Save Selected", "Don't Save", "Cancel", "Leave the dialog open"],
    });

    // Exactly the button the user picked, and only because they picked it.
    const pressed = bridge.calls.filter((c) => c.method === "respond_to_dialog");
    expect(pressed).toHaveLength(1);
    expect(pressed[0].params.buttonLabel).toBe("Save Selected");
    expect(bridge.methods()).not.toContain("set_dialog_policy");
    expect(result.dialogAnsweredByUser).toBe("Save Selected");
    expect(result.success).toBe(true);
  });

  /**
   * D8: the button was sent and the editor never said what it did with it.
   *
   * callBridgeOnce returns the same `silent` reply for an 8s timeout and for a
   * socket error, and both happen AFTER the frame is on the wire. The stop path
   * read that as "nothing was pressed", so answeredByUser stayed undefined and
   * the unsaved-work refusal printed "Nothing was sent to the editor, so no
   * save prompt is open and the editor is exactly as it was" - after the user
   * picked Save All and packages may already have been written to disk.
   *
   * The repo's rule is that a dialog is never answered on the user's behalf and
   * that what happened to it is reported exactly. "We do not know" is a report;
   * "nothing happened" is a claim, and it was the wrong one.
   */
  it("does not claim the editor is untouched when the press went out unanswered", async () => {
    let dialogUp = true;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return dialogUp ? SAVE_CONTENT_DIALOG : NO_DIALOGS;
      if (method === "respond_to_dialog") {
        // The press arrived. The editor then went away without answering.
        dialogUp = false;
        return DROP_SOCKET;
      }
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);

    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Save Selected" } }));
    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    // The frame really did go out.
    const pressed = bridge.calls.filter((c) => c.method === "respond_to_dialog");
    expect(pressed).toHaveLength(1);
    expect(pressed[0].params.buttonLabel).toBe("Save Selected");

    expect(result.refusedReason).toBe("unsaved-work");
    // Not reported as answered: nothing acknowledged the press.
    expect(result.dialogAnsweredByUser).toBeUndefined();
    // And not reported as silence either.
    expect(result.dialogPressUnconfirmed).toBe("Save Selected");
    expect(result.message).not.toContain("Nothing was sent to the editor");
    expect(result.message).toContain("was sent to the editor and it did not answer");
    expect(result.message).toContain("Save Selected");
  });

  /**
   * The other half of the same distinction: the editor DID answer and said it
   * pressed nothing (an unregistered method on an older plugin build, or a
   * handler that refused). Nothing happened to the dialog, so "nothing was
   * sent" is the honest report and must survive.
   */
  it("still reports silence when the editor answers that it pressed nothing", async () => {
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return SAVE_CONTENT_DIALOG;
      if (method === "respond_to_dialog") return null; // JSON-RPC error: unknown method
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);

    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Save Selected" } }));
    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(result.dialogAnsweredByUser).toBeUndefined();
    expect(result.dialogPressUnconfirmed).toBeUndefined();
    expect(result.refusedReason).toBe("blocking-dialog");
  });

  it("presses nothing when the user declines, and reports the dialog instead", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port), {
      elicit: async () => ({ action: "decline" }),
    });

    expect(bridge.methods()).toEqual(["list_dialogs"]);
    expect(result.refusedReason).toBe("blocking-dialog");
    expect(result.dialogAnsweredByUser).toBeUndefined();
  });

  it("presses nothing when the user chooses to leave the dialog open", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port), {
      elicit: async () => ({ action: "accept", content: { button: "Leave the dialog open" } }),
    });

    expect(bridge.methods()).toEqual(["list_dialogs"]);
    expect(result.refusedReason).toBe("blocking-dialog");
  });

  it("presses nothing when the client has no elicitation UI", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port), {
      elicit: async () => {
        throw new Error("client did not advertise the elicitation capability");
      },
    });

    expect(bridge.methods()).toEqual(["list_dialogs"]);
    expect(result.refusedReason).toBe("blocking-dialog");
    expect(result.message).toContain("Save Content");
  });
});

/**
 * The same rule on the plugin side. It cannot be exercised from here without an
 * editor, so it is asserted against the source: the module must arm no policy
 * of its own, and must invent no answer for a dialog nobody armed one for.
 */
describe("the plugin arms no policy and invents no answer", () => {
  const read = (relative: string): string =>
    fs.readFileSync(new URL(`../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/${relative}`, import.meta.url), "utf8");

  it("registers no built-in dialog policy at module startup", () => {
    const module = read("Private/UE_MCP_Bridge.cpp");
    expect(module).not.toContain("AddDefaultPolicy");
    // The patterns that used to be armed here, each of which answered a save
    // prompt with "discard" before anyone had read it.
    for (const pattern of ["Save Content", "Save Changes", "save the level", "already exists"]) {
      expect(module).not.toContain(`TEXT("${pattern}")`);
    }
  });

  it("offers no way for the module to add a policy at all", () => {
    expect(read("Private/Handlers/DialogHandlers.h")).not.toContain("AddDefaultPolicy");
    expect(read("Private/Handlers/DialogHandlers.cpp")).not.toContain("AddDefaultPolicy");
  });

  it("hands an unarmed dialog back to the user instead of synthesizing a reply", () => {
    const source = read("Private/Handlers/DialogHandlers.cpp");
    const handler = source.slice(
      source.indexOf("EAppReturnType::Type FDialogHandlers::HandleModalDialog(EAppMsgType::Type"),
      source.indexOf("TSharedPtr<FJsonValue> FDialogHandlers::SetDialogPolicy"),
    );
    expect(handler).toContain("FMessageDialog::Open(MsgType, Text, Title)");
    // The old fallback picked an answer from the message type. Every branch of
    // it returned a button nobody had chosen.
    expect(handler).not.toContain("switch (MsgType)");
    expect(handler).not.toContain("auto-defaulted");
  });

  it("reports a dialog's message whole", () => {
    const source = read("Private/Handlers/DialogHandlers.cpp");
    const listDialogs = source.slice(
      source.indexOf("TSharedPtr<FJsonValue> FDialogHandlers::ListDialogs"),
      source.indexOf("TSharedPtr<FJsonValue> FDialogHandlers::RespondToDialog"),
    );
    expect(listDialogs).toContain('SetStringField(TEXT("message"), Message)');
    expect(listDialogs).toContain('SetBoolField(TEXT("messageTruncated"), false)');
    expect(listDialogs).toContain('respondWith');
    expect(listDialogs).not.toContain(".Left(");
  });
});

/**
 * Dialog handling modes (interactive / auto / defer).
 *
 * The mode decides who answers a modal blocking the editor, and exactly one of
 * the three ever reaches a button: interactive, through the user's own pick in
 * the elicitation form. The default is the part with teeth - it resolves to
 * interactive only when the client advertised elicitation, and falls back to
 * defer, never to auto, so nothing can arrive at "let the agent decide" because
 * asking the user was unavailable.
 */
describe("dialog handling mode resolution", () => {
  it("defaults to interactive when the client advertised elicitation", () => {
    const resolved = resolveDialogMode({ canElicit: true, env: {} });
    expect(resolved.mode).toBe("interactive");
    expect(resolved.source).toContain("default");
  });

  it("defaults to defer, never auto, when it did not", () => {
    const resolved = resolveDialogMode({ canElicit: false, env: {} });
    expect(resolved.mode).toBe("defer");
    expect(resolved.source).toContain("default");
  });

  it("honours UE_MCP_DIALOG_MODE over everything else", () => {
    for (const mode of ["interactive", "auto", "defer"] as const) {
      const resolved = resolveDialogMode({ canElicit: true, env: { UE_MCP_DIALOG_MODE: mode } });
      expect(resolved.mode).toBe(mode);
      expect(resolved.source).toBe(`UE_MCP_DIALOG_MODE=${mode}`);
    }
  });

  it("ignores an env value that names no mode, and says it ignored it", () => {
    const resolved = resolveDialogMode({ canElicit: false, env: { UE_MCP_DIALOG_MODE: "auto-approve" } });
    expect(resolved.mode).toBe("defer");
    expect(resolved.source).toContain("was ignored");
  });

  it("reads the stored preference, per project first, then per user", () => {
    const projectDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-pref-"));
    temporaryRoots.push(projectDir);

    setDialogMode("auto");
    expect(resolveDialogMode({ canElicit: true, env: {} }).mode).toBe("auto");
    expect(resolveDialogMode({ projectDir, canElicit: true, env: {} }).mode).toBe("auto");

    setDialogMode("defer", projectDir);
    expect(resolveDialogMode({ projectDir, canElicit: true, env: {} }).mode).toBe("defer");
    // The per-user answer is untouched by the per-project one.
    expect(resolveDialogMode({ canElicit: true, env: {} }).mode).toBe("auto");

    setDialogMode(undefined, projectDir);
    setDialogMode(undefined);
    expect(resolveDialogMode({ canElicit: true, env: {} }).source).toContain("default");
  });
});

describe("stop_editor honours the dialog handling mode", () => {
  /** A bridge with the Save Content modal up, and every call it was sent. */
  const withBlockingDialog = async (): Promise<FakeBridge> => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);
    return bridge;
  };

  it("interactive is the default with elicitation, and presses only the chosen button", async () => {
    let dialogUp = true;
    let asked = 0;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return dialogUp ? SAVE_CONTENT_DIALOG : NO_DIALOGS;
      if (method === "respond_to_dialog") {
        dialogUp = false;
        return { success: true, clickedButton: "Cancel" };
      }
      if (method === "request_editor_shutdown") {
        setTimeout(() => bridge.goQuiet(), 50);
        return { success: true, scheduled: true, dirtyContentPackages: [], dirtyMapPackages: [] };
      }
      return { success: true };
    });
    openBridges.push(bridge);

    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Cancel" } }));
    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 5 });

    asked = gate.asked();
    expect(asked).toBe(1);
    expect(result.dialogMode).toBe("interactive");
    expect(result.dialogModeSource).toContain("default");
    const pressed = bridge.calls.filter((c) => c.method === "respond_to_dialog");
    expect(pressed).toHaveLength(1);
    expect(pressed[0].params.buttonLabel).toBe("Cancel");
  });

  it("defers by default for a client that advertised no elicitation, though the server still has a gate", async () => {
    const bridge = await withBlockingDialog();
    // Exactly what production hands over: a function that is always present,
    // for a client that advertised nothing.
    const gate = makeGate(false);

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn });

    expect(gate.asked()).toBe(0);
    expect(result.dialogMode).toBe("defer");
    expect(result.dialogModeSource).toContain("did not advertise elicitation");
    expect(result.message).toContain("Dialog handling mode: defer");
    // Quoted whole, so the person knows what they are looking at in the editor.
    expect(result.message).toContain("Save Content");
    expect(result.message).toContain(SAVE_CONTENT_MESSAGE);
    expect(result.message).toContain("Unreal Editor window");
    for (const forbidden of BUTTON_PRESSING_METHODS) {
      expect(bridge.methods()).not.toContain(forbidden);
    }
    expect(bridge.methods()).toEqual(["list_dialogs"]);
  });

  it("defer presses nothing and elicits nothing even when the client could be asked", async () => {
    process.env.UE_MCP_DIALOG_MODE = "defer";
    const bridge = await withBlockingDialog();
    let asked = 0;

    const result = await stopEditor(makeProject(bridge.port), {
      elicit: async () => {
        asked++;
        return { action: "accept", content: { button: "Don't Save" } };
      },
    });

    expect(asked).toBe(0);
    expect(result.dialogMode).toBe("defer");
    expect(result.dialogModeSource).toBe("UE_MCP_DIALOG_MODE=defer");
    expect(bridge.methods()).toEqual(["list_dialogs"]);
  });

  it("auto presses nothing server-side and hands the whole dialog to the agent", async () => {
    process.env.UE_MCP_DIALOG_MODE = "auto";
    const bridge = await withBlockingDialog();
    let asked = 0;

    const result = await stopEditor(makeProject(bridge.port), {
      elicit: async () => {
        asked++;
        return { action: "accept", content: { button: "Don't Save" } };
      },
    });

    // Nothing pressed, nothing elicited: in auto mode the agent decides, and
    // deciding is a separate respond_to_dialog call it has to make itself.
    expect(asked).toBe(0);
    expect(bridge.methods()).toEqual(["list_dialogs"]);
    for (const forbidden of BUTTON_PRESSING_METHODS) {
      expect(bridge.methods()).not.toContain(forbidden);
    }
    expect(result.dialogMode).toBe("auto");
    expect(result.dialogModeSource).toBe("UE_MCP_DIALOG_MODE=auto");
    expect(result.blockingDialog?.buttons).toEqual(["Save Selected", "Don't Save", "Cancel"]);
    // Every button, with the exact call that presses it.
    expect(result.message).toContain("editor(action='respond_to_dialog', buttonLabel='Save Selected')");
    expect(result.message).toContain("editor(action='respond_to_dialog', buttonLabel=\"Don't Save\")");
    expect(result.message).toContain("editor(action='respond_to_dialog', buttonLabel='Cancel')");
  });

  it("interactive without an elicitation channel blocks rather than deciding anyway", async () => {
    process.env.UE_MCP_DIALOG_MODE = "interactive";
    const bridge = await withBlockingDialog();
    const gate = makeGate(false);

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn });

    expect(gate.asked()).toBe(0);
    expect(result.dialogMode).toBe("interactive");
    expect(result.refusedReason).toBe("blocking-dialog");
    expect(result.message).toContain("did not advertise that capability");
    expect(bridge.methods()).toEqual(["list_dialogs"]);
  });

  it("takes the per-project stored preference over the default", async () => {
    const bridge = await withBlockingDialog();
    const projectDir = makeProject(bridge.port);
    setDialogMode("auto", projectDir);

    let asked = 0;
    const result = await stopEditor(projectDir, {
      elicit: async () => {
        asked++;
        return { action: "accept", content: { button: "Don't Save" } };
      },
    });

    expect(asked).toBe(0);
    expect(result.dialogMode).toBe("auto");
    expect(result.dialogModeSource).toContain("dialog.mode in");
    expect(bridge.methods()).toEqual(["list_dialogs"]);
  });
});

/**
 * Whether the user can be asked is a property of the CONNECTED client.
 *
 * The shipped server always has an elicitation gate: it is built at startup,
 * before a client has connected. Reading its presence as "the client supports
 * elicitation" put every client into the interactive path and reported the
 * reason as "advertised elicitation", which for a whole class of clients was
 * simply untrue.
 */
describe("elicitation capability, not the presence of a gate", () => {
  it("is false with no gate at all", () => {
    expect(clientAdvertisesElicitation(undefined)).toBe(false);
  });

  it("asks the gate that carries a probe, and believes the answer", () => {
    expect(clientAdvertisesElicitation(makeGate(true).fn)).toBe(true);
    expect(clientAdvertisesElicitation(makeGate(false).fn)).toBe(false);
  });

  it("takes a gate with no probe at face value", () => {
    // Tests and embedders hand one over deliberately; there is nobody else to
    // ask about it.
    const bare: ElicitFn = async () => ({ action: "decline" });
    expect(clientAdvertisesElicitation(bare)).toBe(true);
  });

  it("resolves the default from the capability rather than from the function", () => {
    expect(resolveDialogMode({ canElicit: clientAdvertisesElicitation(makeGate(false).fn), env: {} }).mode).toBe("defer");
    expect(resolveDialogMode({ canElicit: clientAdvertisesElicitation(makeGate(true).fn), env: {} }).mode).toBe("interactive");
  });
});

describe("a stored mode is read the way the env value is", () => {
  it("accepts a spelling that differs only in case or padding", () => {
    fs.writeFileSync(
      process.env.UE_MCP_USER_STATE!,
      JSON.stringify({ preferences: { dialog: { mode: "  AUTO  " } } }),
    );
    expect(resolveDialogMode({ canElicit: true, env: {} }).mode).toBe("auto");
  });

  it("still refuses a value that names no mode", () => {
    fs.writeFileSync(
      process.env.UE_MCP_USER_STATE!,
      JSON.stringify({ preferences: { dialog: { mode: "auto-approve" } } }),
    );
    expect(resolveDialogMode({ canElicit: false, env: {} }).mode).toBe("defer");
  });
});

/**
 * The dialog that comes up BEHIND the quit.
 *
 * This is the only other place the server can reach a button, so it is the only
 * other place that can get this wrong. It presses only what the user picked,
 * and whatever happens afterwards the result has to describe what this call
 * actually did: a report that says no dialog was up and that nothing here
 * presses a button, seconds after pressing one, is the failure this branch
 * exists to prevent.
 */
describe("a dialog raised behind the quit", () => {
  /**
   * A bridge that is clean until the quit goes out and then raises the Save
   * Content modal, with the port staying up throughout.
   */
  const lateDialogBridge = async (opts: { quitOnAnswer: boolean }): Promise<FakeBridge> => {
    let quitSent = false;
    let dialogUp = false;
    let bridge: FakeBridge;
    bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") {
        return dialogUp ? SAVE_CONTENT_DIALOG : NO_DIALOGS;
      }
      if (method === "request_editor_shutdown") {
        quitSent = true;
        dialogUp = true;
        return { success: true, scheduled: true, dirtyContentPackages: [], dirtyMapPackages: [] };
      }
      if (method === "respond_to_dialog") {
        dialogUp = false;
        if (opts.quitOnAnswer) setTimeout(() => bridge.goQuiet(), 10);
        return { success: true, clickedButton: "Save Selected" };
      }
      void quitSent;
      return { success: true };
    });
    openBridges.push(bridge);
    return bridge;
  };

  it("presses only the user's pick, and says so even when the editor still does not close", async () => {
    const bridge = await lateDialogBridge({ quitOnAnswer: false });
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Save Selected" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(gate.asked()).toBe(1);
    const pressed = bridge.calls.filter((c) => c.method === "respond_to_dialog");
    expect(pressed).toHaveLength(1);
    expect(pressed[0].params.buttonLabel).toBe("Save Selected");
    expect(bridge.methods()).not.toContain("set_dialog_policy");

    // The dialog is gone by the time this returns, and the account of it is not.
    expect(result.success).toBe(false);
    expect(result.dialogAnsweredByUser).toBe("Save Selected");
    expect(result.dialogMode).toBe("interactive");
    expect(result.dialogModeSource).toBeTruthy();
    // The sentence that used to deny the press that had just happened.
    expect(result.message).not.toContain("nothing here presses a button for you");
    expect(result.message).not.toContain("no dialog was up when the quit went out");
    expect(result.message).toContain('You answered its dialog with "Save Selected"');
  });

  /**
   * The report is written from an engine snapshot read BEFORE the elicitation,
   * so once the user answers, that snapshot still holds the dialog they just
   * dismissed. Rendering it produced one message that said all three of these
   * at once: "nothing was sent to it and nothing was answered for you", a
   * respond_to_dialog call for each of the gone dialog's buttons, and "you
   * answered its dialog with X and no dialog is up now".
   *
   * Every other test in this file runs against the whole-file readEngineState
   * mock, whose `snapshot: null` sends blockedStopDetail straight to
   * `state.summary` and makes the modal branch unreachable. This one supplies a
   * real snapshot, so the branch renders.
   */
  it("does not quote the dialog the user just answered, nor hand back its buttons", async () => {
    let dialogUp = false;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return dialogUp ? SAVE_CONTENT_DIALOG : NO_DIALOGS;
      if (method === "request_editor_shutdown") {
        dialogUp = true;
        return { success: true, scheduled: true, dirtyContentPackages: [], dirtyMapPackages: [] };
      }
      if (method === "respond_to_dialog") {
        dialogUp = false;
        return { success: true, clickedButton: "Save Selected" };
      }
      return { success: true };
    });
    openBridges.push(bridge);

    // The snapshot tells the same story the bridge does: the modal is in it
    // while it is up, and gone the moment the button is pressed.
    const readEngineState = vi.mocked(observer.readEngineState);
    const stubbed = readEngineState.getMockImplementation();
    readEngineState.mockImplementation(async () => ({
      running: true,
      processes: [],
      log: { logPath: null, secondsSinceWrite: null, phase: "unknown", blocking: false, lastLine: null, tail: [], errors: [], warnings: [] },
      snapshot: dialogUp
        ? { modal: { title: "Save Content", message: SAVE_CONTENT_MESSAGE, buttons: ["Save Selected", "Don't Save", "Cancel"] } }
        : { modal: null },
      dialogs: [],
      processProbeFailed: false,
      runningEvidence: "bridge-snapshot" as const,
      snapshotSource: "bridge" as const,
      summary: "The editor is still running and its bridge is answering.",
      blocked: dialogUp,
    }));

    let result;
    try {
      const gate = makeGate(true, () => ({ action: "accept", content: { button: "Save Selected" } }));
      result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });
    } finally {
      if (stubbed) readEngineState.mockImplementation(stubbed);
    }

    expect(result.success).toBe(false);
    expect(result.dialogAnsweredByUser).toBe("Save Selected");
    // What the call did, said once.
    expect(result.message).toContain('You answered its dialog with "Save Selected"');
    // The three false claims, none of which may survive a press.
    expect(result.message).not.toContain("nothing was answered for you");
    expect(result.message).not.toContain("A modal dialog is blocking the editor");
    expect(result.message).not.toContain("respond_to_dialog");
    // And nothing of the answered dialog is offered back as still pressable.
    expect(result.message).not.toContain("Buttons, in the order the dialog lays them out");
    expect(result.blockingDialog).toBeUndefined();
  });

  it("reports the quit as successful when the answer releases it", async () => {
    const bridge = await lateDialogBridge({ quitOnAnswer: true });
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Save Selected" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(result.success).toBe(true);
    expect(result.dialogAnsweredByUser).toBe("Save Selected");
    expect(result.dialogMode).toBe("interactive");
    expect(result.message).toContain("Save Selected");
  });

  it("presses nothing and asks nothing in auto, and hands the dialog back instead", async () => {
    process.env.UE_MCP_DIALOG_MODE = "auto";
    const bridge = await lateDialogBridge({ quitOnAnswer: false });
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Don't Save" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(gate.asked()).toBe(0);
    expect(bridge.methods()).not.toContain("respond_to_dialog");
    expect(result.dialogMode).toBe("auto");
    expect(result.refusedReason).toBe("blocking-dialog");
    expect(result.blockingDialog?.title).toBe("Save Content");
    expect(result.dialogAnsweredByUser).toBeUndefined();
  });

  it("presses nothing and asks nothing in defer", async () => {
    process.env.UE_MCP_DIALOG_MODE = "defer";
    const bridge = await lateDialogBridge({ quitOnAnswer: false });
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Don't Save" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(gate.asked()).toBe(0);
    expect(bridge.methods()).not.toContain("respond_to_dialog");
    expect(result.dialogMode).toBe("defer");
    expect(result.message).toContain("Unreal Editor window");
    // The late dialog follows the same rule as the early one: recognition only.
    expect(result.message).not.toContain("respond_to_dialog");
    expect(JSON.stringify(result.blockingDialog)).not.toContain("respond_to_dialog");
  });
});

/**
 * Every result of a stop that met a dialog carries the mode, whatever the call
 * went on to do. The doc promises that, and the paths after the dialog are
 * where it used to be dropped: they run only once a dialog has been answered
 * and cleared, which is exactly when a caller wants to know which mode was in
 * force.
 */
describe("the mode travels with every result that met a dialog", () => {
  it("is on the unsaved-work refusal that follows an answered dialog", async () => {
    let dialogUp = true;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return dialogUp ? SAVE_CONTENT_DIALOG : NO_DIALOGS;
      if (method === "respond_to_dialog") {
        dialogUp = false;
        return { success: true, clickedButton: "Cancel" };
      }
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Cancel" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(result.refusedReason).toBe("unsaved-work");
    expect(result.dialogMode).toBe("interactive");
    expect(result.dialogModeSource).toBeTruthy();
    expect(result.dialogAnsweredByUser).toBe("Cancel");
  });

  it("is on the unknown-dirty-state refusal that follows an answered dialog", async () => {
    let dialogUp = true;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return dialogUp ? SAVE_CONTENT_DIALOG : NO_DIALOGS;
      if (method === "respond_to_dialog") {
        dialogUp = false;
        return { success: true, clickedButton: "Cancel" };
      }
      return null;
    });
    openBridges.push(bridge);
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Cancel" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(result.refusedReason).toBe("unknown-dirty-state");
    expect(result.dialogMode).toBe("interactive");
    expect(result.dialogAnsweredByUser).toBe("Cancel");
  });

  it("is absent when no dialog was ever in the way", async () => {
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return NO_DIALOGS;
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port), { elicit: makeGate(true).fn });

    expect(result.refusedReason).toBe("unsaved-work");
    expect(result.dialogMode).toBeUndefined();
    expect(result.dialogModeSource).toBeUndefined();
  });
});

/**
 * What the prose says has to match what the call did.
 *
 * Carrying the fields is half of it. The other half is that no sentence in the
 * same payload denies the thing the fields record: a stop that pressed a button
 * the user chose cannot also say the editor is exactly as it was, that nothing
 * was answered, or that the editor was never asked to quit.
 */
describe("no result denies the press it just made", () => {
  /** A bridge with a dialog up that answers, then refuses on dirty packages. */
  const answerThenDirty = async (): Promise<FakeBridge> => {
    let dialogUp = true;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return dialogUp ? SAVE_CONTENT_DIALOG : NO_DIALOGS;
      if (method === "respond_to_dialog") {
        dialogUp = false;
        return { success: true, clickedButton: "Save Selected" };
      }
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);
    return bridge;
  };

  it("does not claim the editor is untouched after the user's button was pressed", async () => {
    const bridge = await answerThenDirty();
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Save Selected" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(result.refusedReason).toBe("unsaved-work");
    expect(result.dialogAnsweredByUser).toBe("Save Selected");
    // The sentence that was false: "Save Selected" may have written packages.
    expect(result.message).not.toContain("the editor is exactly as it was");
    expect(result.message).not.toContain("Nothing was sent to the editor");
    expect(result.message).toContain('You answered its dialog with "Save Selected"');
    // Still names every dirty package and offers no way to discard them.
    expect(result.message).toContain("/Game/Materials/M_Rock");
    expect(result.message).not.toContain("discardUnsaved");
  });

  it("keeps the untouched sentence when no dialog was ever answered", async () => {
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return NO_DIALOGS;
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port), { elicit: makeGate(true).fn });

    expect(result.dialogAnsweredByUser).toBeUndefined();
    expect(result.message).toContain("the editor is exactly as it was");
  });

  it("describes a dialog that came up behind the one the user answered, before the quit", async () => {
    const second = {
      success: true,
      count: 1,
      dialogs: [
        {
          title: "Discard Changes",
          message: "There are unsaved changes in the level.",
          messageTruncated: false,
          buttons: ["Discard", "Keep"],
          choices: [
            { buttonLabel: "Discard", respondWith: "editor(action='respond_to_dialog', buttonLabel='Discard')" },
            { buttonLabel: "Keep", respondWith: "editor(action='respond_to_dialog', buttonLabel='Keep')" },
          ],
          policyMatched: false,
        },
      ],
    };
    let answered = false;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return answered ? second : SAVE_CONTENT_DIALOG;
      if (method === "respond_to_dialog") {
        answered = true;
        return { success: true, clickedButton: "Cancel" };
      }
      return { success: true };
    });
    openBridges.push(bridge);
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Cancel" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(result.blockingDialog?.title).toBe("Discard Changes");
    expect(result.dialogAnsweredByUser).toBe("Cancel");
    // The lead line used to say nothing had been answered, in the same payload
    // that names the button that was pressed.
    expect(result.message).not.toContain("nothing was answered for you");
    expect(result.message).toContain('You answered "Cancel" and this one came up behind it');
    // Before the quit, so this one is still true here.
    expect(result.message).toContain("The editor was not asked to quit");
  });

  it("describes a successor to a dialog answered AFTER the quit went out", async () => {
    const second = {
      success: true,
      count: 1,
      dialogs: [
        {
          title: "Discard Changes",
          message: "There are unsaved changes in the level.",
          messageTruncated: false,
          buttons: ["Discard", "Keep"],
          choices: [
            { buttonLabel: "Discard", respondWith: "editor(action='respond_to_dialog', buttonLabel='Discard')" },
            { buttonLabel: "Keep", respondWith: "editor(action='respond_to_dialog', buttonLabel='Keep')" },
          ],
          policyMatched: false,
        },
      ],
    };
    let quitSent = false;
    let answered = false;
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") {
        if (!quitSent) return NO_DIALOGS;
        return answered ? second : SAVE_CONTENT_DIALOG;
      }
      if (method === "request_editor_shutdown") {
        quitSent = true;
        return { success: true, scheduled: true, dirtyContentPackages: [], dirtyMapPackages: [] };
      }
      if (method === "respond_to_dialog") {
        answered = true;
        return { success: true, clickedButton: "Save Selected" };
      }
      return { success: true };
    });
    openBridges.push(bridge);
    const gate = makeGate(true, () => ({ action: "accept", content: { button: "Save Selected" } }));

    const result = await stopEditor(makeProject(bridge.port), { elicit: gate.fn, confirmPollMs: 2 });

    expect(result.blockingDialog?.title).toBe("Discard Changes");
    expect(result.dialogAnsweredByUser).toBe("Save Selected");
    expect(result.dialogMode).toBe("interactive");
    // Three sentences that were all false at once on this path.
    expect(result.message).not.toContain("nothing was answered for you");
    expect(result.message).not.toContain("The editor was not asked to quit");
    expect(result.message).not.toContain("nothing here presses a button for you");
    // What is true: the quit went out first, and this dialog came after it.
    expect(result.message).toContain("The quit went out before this dialog appeared");
    expect(result.message).toContain('You answered "Save Selected" and this one came up behind it');
  });
});

/**
 * A corrupt preferences file must not take an editor action down with it.
 *
 * Nothing in the editor lifecycle read user state until the dialog mode did, so
 * this is the diff that could make stop_editor throw on a file it never used to
 * open. JSON.parse succeeds on four characters of `null`, and every reader then
 * dereferences it.
 */
describe("a state.json that parses to something that is not a state object", () => {
  const corrupt = ["null", "[]", "3", "true", '"defer"', "[1,2,3]"];

  for (const body of corrupt) {
    it(`resolves the mode without throwing on ${body}`, () => {
      fs.writeFileSync(process.env.UE_MCP_USER_STATE!, body);
      expect(() => resolveDialogMode({ canElicit: false, env: {} })).not.toThrow();
      expect(resolveDialogMode({ canElicit: false, env: {} }).mode).toBe("defer");
      expect(resolveDialogMode({ canElicit: true, env: {} }).mode).toBe("interactive");
    });
  }

  it("lets stop_editor run its normal course rather than dying with a TypeError", async () => {
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);
    const projectDir = makeProject(bridge.port);
    fs.writeFileSync(process.env.UE_MCP_USER_STATE!, "null");

    const result = await stopEditor(projectDir, { elicit: makeGate(false).fn });

    expect(result.refusedReason).toBe("blocking-dialog");
    expect(result.dialogMode).toBe("defer");
  });
});

describe("stop_editor defers the mode decision instead of pre-judging it", () => {
  it("consults the guard in auto too, not only in interactive", async () => {
    // The call to the guard used to be gated on `mode === "interactive" &&
    // canElicit`, which is a second copy of the decision decideFor already
    // makes. The two could disagree, and did.
    const { DialogGuard } = await import("../../src/dialog-guard.js");
    for (const mode of ["auto", "defer", "interactive"] as const) {
      const guard = new DialogGuard({
        mode: () => mode,
        probe: async () => ({ dialogs: [] }),
        press: async () => ({ success: true, answered: true }),
        // A channel to a person, so interactive stays interactive: with none,
        // it correctly falls back to defer.
        elicit: () => (async () => ({ action: "decline" })) as never,
      });
      const decision = await guard.decideFor("editor.stop_editor", {
        title: "Save Content",
        message: "m",
        buttons: ["Cancel"],
        choices: [{ buttonLabel: "Cancel", respondWith: "x" }],
      });
      // Every mode reaches a decision, and only defer withholds the calls.
      expect(decision.allow, mode).toBe(false);
      const refusal = (decision as { refusal: Record<string, unknown> }).refusal;
      expect(refusal.choices === undefined, mode).toBe(mode === "defer");
    }
  });

  it("takes the press-call rule from the guard rather than recomputing it", async () => {
    const { DialogGuard } = await import("../../src/dialog-guard.js");
    // One rule, one place. stop_editor computed `mode !== "defer"` itself, so
    // the same decision existed twice and the renderings disagreed about what
    // defer strips.
    expect(DialogGuard.handsOverPressCalls("auto")).toBe(true);
    expect(DialogGuard.handsOverPressCalls("interactive")).toBe(true);
    expect(DialogGuard.handsOverPressCalls("defer")).toBe(false);
  });

  it("withholds the press calls under interactive when nobody can be asked", async () => {
    const { DialogGuard } = await import("../../src/dialog-guard.js");
    // interactive with no channel to a person is not interactive: handing the
    // buttons over is auto's behaviour under a mode that promises a person
    // chooses. auto is unaffected, because it exists for exactly that caller.
    expect(DialogGuard.handsOverPressCalls("interactive", false)).toBe(false);
    expect(DialogGuard.effectiveMode("interactive", false)).toBe("defer");
    expect(DialogGuard.handsOverPressCalls("auto", false)).toBe(true);
    expect(DialogGuard.effectiveMode("auto", false)).toBe("auto");
    expect(DialogGuard.effectiveMode("interactive", true)).toBe("interactive");
  });
});

describe("the lifecycle actions share the editor's one guard", () => {
  it("restart forwards the guard it was given to the stop half", async () => {
    // Without forwarding, the stop half fell back to constructing a second
    // DialogGuard with its own ask-once record, so a restart asked the person
    // again about a dialog already put to them and pressed a second button.
    const { DialogGuard } = await import("../../src/dialog-guard.js");
    const { restartEditor } = await import("../../src/editor-control.js");
    let asks = 0;
    const guard = new DialogGuard({
      mode: () => "interactive",
      probe: async () => ({ dialogs: [] }),
      press: async () => ({ success: true, answered: true }),
      elicit: () => (async () => {
        asks += 1;
        return { action: "decline" };
      }) as never,
    });
    // Already asked about this dialog through the session guard.
    await guard.decideFor("editor.stop_editor", {
      title: "Save Content", message: "m", buttons: ["Cancel"], choices: [],
    });
    expect(asks).toBe(1);

    // A restart with no project refuses before touching an editor; what is
    // being checked is that the guard is threaded, not the restart itself.
    const res = await restartEditor(
      { projectPath: null, projectDir: null } as never,
      undefined,
      { guard },
    );
    expect(res.success).toBe(false);
    // The one guard was reused, so nothing asked a second time.
    expect(asks).toBe(1);
  });
});

describe("a restart refused by a dialog is recognisable as one", () => {
  it("carries the refusal fields through from the stop half", async () => {
    const { restartDialogAccount } = await import("../../src/editor-control.js");
    const { isDialogRefusal } = await import("../../src/dialog-guard.js");
    // The forwarding copied only the account of WHAT HAPPENED (mode, source,
    // the dialog itself), dropping dialogBlocking and the rest, so a restart
    // refused by a dialog was invisible to isDialogRefusal and to every client
    // branching on that flag.
    const account = restartDialogAccount({
      success: false,
      message: "blocked",
      dialogBlocking: true,
      refusedReason: "blocking-dialog",
      refusedMethod: "editor.stop_editor",
      dialogTitle: "Save Content",
      dialogMessage: "Select Content to Save",
      buttons: ["Cancel", "Don't Save"],
      choices: [{ buttonLabel: "Cancel", respondWith: "Cancel" }],
      error: "A modal dialog is blocking the editor",
      dialogMode: "defer",
      dialogModeSource: "default",
    });

    expect(isDialogRefusal(account), "a refused restart must read as refused").toBe(true);
    expect(account.refusedMethod).toBe("editor.stop_editor");
    expect(account.dialogTitle).toBe("Save Content");
    expect(account.dialogMessage).toBe("Select Content to Save");
    expect(account.buttons).toEqual(["Cancel", "Don't Save"]);
    expect(account.choices).toEqual([{ buttonLabel: "Cancel", respondWith: "Cancel" }]);
    expect(account.error).toBe("A modal dialog is blocking the editor");
    expect(account.refusedReason).toBe("blocking-dialog");
    expect(account.dialogMode).toBe("defer");
  });

  it("says nothing about a dialog when the stop never met one", async () => {
    const { restartDialogAccount } = await import("../../src/editor-control.js");
    const { isDialogRefusal } = await import("../../src/dialog-guard.js");
    const account = restartDialogAccount({ success: true, message: "stopped" });
    expect(isDialogRefusal(account)).toBe(false);
    expect(Object.keys(account)).toEqual([]);
  });
});
