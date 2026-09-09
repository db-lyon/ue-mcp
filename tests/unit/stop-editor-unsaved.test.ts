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
    // The quit and nothing else: the gate decides the dialog question.
    expect(bridge.methods()).toEqual(["request_editor_shutdown"]);
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

  it("sends the quit, so the EDITOR raises its own save prompt", async () => {
    // The point of the change this pins: the editor's Save Content prompt is
    // the question, and it carries the user's three real answers. Refusing
    // before the quit meant Unreal never raised it, and the server put its own
    // refusal there instead - one that told the caller to run save_dirty and
    // write the unsaved work with nobody asked.
    //
    // requireClean is what suppressed the prompt, so it must be false.
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return NO_DIALOGS;
      if (method === "request_editor_shutdown") return DIRTY_REFUSAL;
      return { success: true };
    });
    openBridges.push(bridge);

    await stopEditor(makeProject(bridge.port));

    // The dirty check, which refuses inside the engine without scheduling a
    // close, and nothing after it.
    expect(bridge.methods()).toEqual(["request_editor_shutdown"]);
    expect(bridge.calls[0].params.requireClean).toBe(false);
  });

  it("refuses when the plugin build cannot say what is dirty, rather than guessing clean", async () => {
    // Answers the dialog question, answers nothing else: this case is about
    // an unknown DIRTY state, and a bridge that answers nothing at all is the
    // separate unknown-dialog-state refusal.
    const bridge = await startFakeBridge((method) => (method === "list_dialogs" ? NO_DIALOGS : null));
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port));

    expect(result.success).toBe(false);
    expect(result.refusedReason).toBe("unknown-dirty-state");
    expect(result.message).toContain("cannot be established");
    expect(bridge.methods()).not.toContain("execute_python");
  });

  it("refuses on the dirty list from an older plugin build too", async () => {
    const bridge = await startFakeBridge((method) => {
      if (method === "list_dialogs") return NO_DIALOGS;
      return method === "list_dirty_packages"
        ? { success: true, content: [{ package: "/Game/Blueprints/BP_Door" }], maps: [] }
        : null;
    });
    openBridges.push(bridge);

    const result = await stopEditor(makeProject(bridge.port));

    expect(result.success).toBe(false);
    expect(result.refusedReason).toBe("unsaved-work");
    expect(result.dirtyPackages).toEqual(["/Game/Blueprints/BP_Door"]);
    expect(bridge.methods()).not.toContain("execute_python");
  });
});


describe("stop_editor reports a blocking dialog in full", () => {
  it("does not probe for a dialog of its own before quitting", async () => {
    // The quit is withheld by the GATE, not here: see dialog-gate-wiring.test.ts,
    // "refuses stop_editor, and the quit never reaches the editor". This pins
    // only that no second copy of that decision survives in stopEditor.
    const bridge = await startFakeBridge((method) =>
      method === "list_dialogs" ? SAVE_CONTENT_DIALOG : { success: true },
    );
    openBridges.push(bridge);

    await stopEditor(makeProject(bridge.port));

    // No probe before the quit.
    expect(bridge.methods()[0]).toBe("request_editor_shutdown");
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

    // The point is that a malformed state file does not throw. What the stop
    // then decides is the ordinary path, not this case.
    const result = await stopEditor(projectDir);

    expect(result.success).toBe(false);
    expect(typeof result.message).toBe("string");
  });
});
