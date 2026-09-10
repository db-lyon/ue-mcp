/**
 * The gate is WIRED, not just correct in isolation.
 *
 * tests/unit/dialog-gate.test.ts drives runWithDialogGate directly with a fake
 * `run`, so it proves the gate's logic and nothing about whether anything calls
 * it. Deleting the call from src/index.ts left that suite green, tsc clean and
 * the golden baseline unchanged: only the live tests caught it, and the live
 * suite needs a running Unreal editor and does not run in CI.
 *
 * So this starts the real server over stdio against a stub bridge that answers
 * like an editor sitting on a modal, and calls real tools through the real MCP
 * surface. It needs no engine.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocketServer, type WebSocket } from "ws";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

const DIALOG = {
  title: "Save Content",
  message: "Select Content to Save",
  buttons: ["Save Selected", "Don't Save", "Cancel"],
  choices: [
    { buttonLabel: "Save Selected", respondWith: "editor(action='respond_to_dialog', buttonLabel=\"Save Selected\")" },
    { buttonLabel: "Don't Save", respondWith: "editor(action='respond_to_dialog', buttonLabel=\"Don't Save\")" },
    { buttonLabel: "Cancel", respondWith: "editor(action='respond_to_dialog', buttonLabel=\"Cancel\")" },
  ],
};

/** The refusal the plugin's own gate emits, reproduced exactly. */
function refusal(method: string) {
  return {
    success: false,
    dialogBlocking: true,
    refusedMethod: method,
    dialogTitle: DIALOG.title,
    dialogMessage: DIALOG.message,
    buttons: DIALOG.buttons,
    choices: DIALOG.choices,
    error: `A modal dialog is blocking the editor, so '${method}' was refused without running.`,
  };
}

/** Methods the real plugin serves while a modal is up. */
const MODAL_SAFE = new Set([
  "list_dialogs",
  "respond_to_dialog",
  "get_dialog_policy",
  "set_dialog_policy",
  "clear_dialog_policy",
  "get_engine_state",
  "get_bridge_capabilities",
  "get_param_echo",
]);

/** An editor stuck on a modal: modal-safe methods answer, everything else is refused. */
class StubBridge {
  readonly port: number;
  private wss: WebSocketServer;
  /** Every method the server actually dispatched to the editor. */
  readonly seen: string[] = [];
  /** When set, the first list_dialogs reports clear and the rest report a modal. */
  appearAfterFirstProbe = false;
  /** When set, every list_dialogs reports a clear editor. */
  reportNoDialog = false;
  /** Flip to raise a modal AFTER startup, leaving the guard genuinely cold. */
  raiseModalNow = false;
  /** When set, answering the first dialog surfaces a second, different one. */
  secondDialogAfterPress = false;
  private pressed = 0;
  private probes = 0;

  private constructor(wss: WebSocketServer, port: number) {
    this.wss = wss;
    this.port = port;
    wss.on("connection", (ws: WebSocket) => {
      ws.on("message", (raw) => {
        let req: { id?: unknown; method?: string };
        try {
          req = JSON.parse(String(raw));
        } catch {
          return;
        }
        const method = String(req.method ?? "");
        this.seen.push(method);

        if (method === "respond_to_dialog") this.pressed += 1;
        let modalUp = this.raiseModalNow ? true : !this.reportNoDialog;
        if (method === "list_dialogs" && this.appearAfterFirstProbe) {
          modalUp = this.probes++ > 0;
        }
        const result = this.reportNoDialog && !this.raiseModalNow && !MODAL_SAFE.has(method)
          ? { success: true }
          : MODAL_SAFE.has(method)
          ? method === "list_dialogs"
            ? {
                success: true,
                dialogs: modalUp
                  ? [
                      this.secondDialogAfterPress && this.pressed > 0
                        ? { ...DIALOG, title: "Delete Assets", message: "a second prompt" }
                        : DIALOG,
                    ]
                  : [],
                count: modalUp ? 1 : 0,
              }
            : { success: true }
          : refusal(method);

        ws.send(JSON.stringify({ jsonrpc: "2.0", id: req.id ?? null, result }));
      });
    });
  }

  static async start(): Promise<StubBridge> {
    const wss = new WebSocketServer({ host: "127.0.0.1", port: 0 });
    await new Promise<void>((resolve) => wss.once("listening", resolve));
    const addr = wss.address();
    const port = typeof addr === "object" && addr ? addr.port : 0;
    return new StubBridge(wss, port);
  }

  async close(): Promise<void> {
    await new Promise<void>((resolve) => this.wss.close(() => resolve()));
  }
}

function writeFixtureProject(dir: string): string {
  const projectDir = path.join(dir, "GateWiring");
  fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
  const uproject = path.join(projectDir, "GateWiring.uproject");
  fs.writeFileSync(
    uproject,
    JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }, null, 2),
  );
  return uproject;
}

let bridge: StubBridge;
let client: Client;
let sandbox: string;

beforeAll(async () => {
  bridge = await StubBridge.start();
  sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-gate-wiring-"));
  const uproject = writeFixtureProject(sandbox);

  const env: Record<string, string> = {
    ...(process.env as Record<string, string>),
    UE_MCP_PORT: String(bridge.port),
    UE_MCP_HOST: "127.0.0.1",
    // Named explicitly: the harness advertises no elicitation, so the default
    // would be defer, and auto is the mode that hands back the press calls.
    UE_MCP_DIALOG_MODE: "auto",
    UE_MCP_STATE_DIR: path.join(sandbox, "state"),
    UE_MCP_CONFIG_DIR: path.join(sandbox, "config"),
  };
  for (const k of Object.keys(env)) {
    if (k.startsWith("UE_MCP_") && !["UE_MCP_PORT", "UE_MCP_HOST", "UE_MCP_DIALOG_MODE", "UE_MCP_STATE_DIR", "UE_MCP_CONFIG_DIR"].includes(k)) {
      delete env[k];
    }
  }

  client = new Client({ name: "ue-mcp-gate-wiring", version: "1.0.0" }, { capabilities: {} });
  await client.connect(
    new StdioClientTransport({
      command: process.execPath,
      args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
      cwd: REPO_ROOT,
      env,
      stderr: fs.openSync(path.join(sandbox, "server.log"), "a"),
    }),
  );
}, 180_000);

afterAll(async () => {
  await client?.close().catch(() => {});
  await bridge?.close();
  fs.rmSync(sandbox, { recursive: true, force: true });
});

/** The first text block of a tool result, parsed when it is JSON. */
function body(res: unknown): Record<string, unknown> {
  const blocks = (res as { content?: Array<{ type: string; text?: string }> }).content ?? [];
  const text = blocks.find((b) => b.type === "text" && !b.text?.startsWith("MACHINE_"))?.text ?? "";
  try {
    return JSON.parse(text) as Record<string, unknown>;
  } catch {
    return { raw: text };
  }
}

describe("the gate is wired into the real server", () => {
  it("refuses an editor-bound read, with the dialog and its buttons", async () => {
    const res = await client.callTool({ name: "level", arguments: { action: "get_outliner", limit: 1 } });
    const out = body(res);
    expect(out.dialogBlocking).toBe(true);
    expect(out.dialogTitle).toBe("Save Content");
    expect(out.buttons).toEqual(DIALOG.buttons);
  }, 60_000);

  it("refuses a write WITHOUT RUNNING IT, not after the fact", async () => {
    // The whole design rests on "refused without running". Asserting only
    // dialogBlocking cannot tell that apart from "ran, then reported": with
    // the preflight deleted entirely, the post-run check produces a refusal
    // that looks the same to a client, and every assertion here still passed.
    const before = bridge.seen.length;
    const res = await client.callTool({
      name: "level",
      arguments: { action: "place_actor", actorClass: "StaticMeshActor", label: "ShouldNeverSpawn" },
    });
    const out = body(res);
    expect(out.dialogBlocking).toBe(true);

    // Nothing was attempted against the editor.
    const attempted = bridge.seen.slice(before).filter((m) => m !== "list_dialogs");
    expect(attempted, `editor was asked to: ${attempted.join(", ")}`).toEqual([]);
    // And it does not describe itself as having got part-way, which is what
    // the after-the-fact path reports.
    expect(out.partiallyApplied).toBeUndefined();
    expect(out.partialResult).toBeUndefined();
    expect(String(out.error)).toContain("was refused without running");
  }, 60_000);

  it("refuses an action served in this process that never reaches the bridge", async () => {
    // The hole the server half exists for: the plugin cannot refuse what it
    // never sees, so without the latch this answered normally.
    const res = await client.callTool({
      name: "project",
      arguments: { action: "search_tools", query: "spawn actor" },
    });
    expect(body(res).dialogBlocking).toBe(true);
  }, 60_000);

  it("refuses the flow tool, which is a second route to the editor", async () => {
    const res = await client.callTool({ name: "flow", arguments: { action: "list" } });
    expect(body(res).dialogBlocking).toBe(true);
  }, 60_000);

  it("keeps list_dialogs reachable, and reading it does not disarm the gate", async () => {
    // Following the refusal's own advice used to clear the latch, after which
    // in-process actions ran free while the editor was still frozen.
    const listed = await client.callTool({ name: "editor", arguments: { action: "list_dialogs" } });
    const listedBody = body(listed);
    // Not refused...
    expect(listedBody.dialogBlocking).toBeUndefined();
    // ...but it still SAYS the editor is blocked. get_status is the first call
    // every client makes, and reporting a healthy editor while the game thread
    // is parked is the one answer it must never give.
    expect(listedBody.editorBlockedByDialog).toBe(true);

    const after = await client.callTool({
      name: "project",
      arguments: { action: "search_tools", query: "spawn actor" },
    });
    expect(body(after).dialogBlocking).toBe(true);
  }, 60_000);

  it("keeps respond_to_dialog reachable, so the modal can be answered", async () => {
    const res = await client.callTool({
      name: "editor",
      arguments: { action: "respond_to_dialog", buttonLabel: "Cancel" },
    });
    expect(body(res).dialogBlocking).toBeUndefined();
    expect(bridge.seen).toContain("respond_to_dialog");
  }, 60_000);

  it("refuses arming a dialog policy, which would press a button", async () => {
    const res = await client.callTool({
      name: "editor",
      arguments: { action: "set_dialog_policy", pattern: "Save Content", response: "no" },
    });
    expect(body(res).dialogBlocking).toBe(true);
    expect(bridge.seen).not.toContain("set_dialog_policy");
  }, 60_000);

  // The editor lifecycle actions. Gated like everything else; no coverage of
  // these existed through the real gate while they were exempt.

  it("refuses stop_editor, and the quit never reaches the editor", async () => {
    const before = bridge.seen.length;
    const res = await client.callTool({ name: "editor", arguments: { action: "stop_editor" } });
    const out = body(res);

    expect(out.dialogBlocking).toBe(true);
    expect(out.dialogTitle).toBe("Save Content");
    expect(out.buttons).toEqual(DIALOG.buttons);

    // The assertion that matters: no quit reaches an editor sitting on a modal.
    const attempted = bridge.seen.slice(before).filter((m) => m !== "list_dialogs");
    expect(attempted, `editor was asked to: ${attempted.join(", ")}`).toEqual([]);
    expect(bridge.seen).not.toContain("request_editor_shutdown");
  }, 60_000);

  it("refuses restart_editor, which is stop_editor with a relaunch after it", async () => {
    const before = bridge.seen.length;
    const res = await client.callTool({ name: "editor", arguments: { action: "restart_editor" } });

    expect(body(res).dialogBlocking).toBe(true);
    const attempted = bridge.seen.slice(before).filter((m) => m !== "list_dialogs");
    expect(attempted, `editor was asked to: ${attempted.join(", ")}`).toEqual([]);
    expect(bridge.seen).not.toContain("request_editor_shutdown");
  }, 60_000);

  it("refuses start_editor, so a second editor is not launched over a stuck one", async () => {
    const res = await client.callTool({ name: "editor", arguments: { action: "start_editor" } });
    expect(body(res).dialogBlocking).toBe(true);
  }, 60_000);
});

describe("micro mode can still answer its own dialog", () => {
  let microClient: Client;
  let microSandbox: string;

  beforeAll(async () => {
    microSandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-gate-micro-"));
    const uproject = writeFixtureProject(microSandbox);
    const env: Record<string, string> = {
      ...(process.env as Record<string, string>),
      UE_MCP_PORT: String(bridge.port),
      UE_MCP_HOST: "127.0.0.1",
      UE_MCP_DIALOG_MODE: "auto",
      UE_MCP_CONTEXT_STRATEGY: "micro",
      UE_MCP_STATE_DIR: path.join(microSandbox, "state"),
      UE_MCP_CONFIG_DIR: path.join(microSandbox, "config"),
    };
    microClient = new Client({ name: "ue-mcp-gate-micro", version: "1.0.0" }, { capabilities: {} });
    await microClient.connect(
      new StdioClientTransport({
        command: process.execPath,
        args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
        cwd: REPO_ROOT,
        env,
        stderr: fs.openSync(path.join(microSandbox, "server.log"), "a"),
      }),
    );
  }, 180_000);

  afterAll(async () => {
    await microClient?.close().catch(() => {});
    fs.rmSync(microSandbox, { recursive: true, force: true });
  });

  it("advertises the gateway, so every category call arrives as tools.call", async () => {
    const listed = await microClient.listTools();
    const names = listed.tools.map((t) => t.name).sort();
    // The gateway fronts every category. `flow` is advertised alongside it,
    // which is the second route to the editor and why it needs its own gate.
    expect(names).toEqual(["flow", "tools"]);
  }, 60_000);

  it("refuses a real call made through the gateway", async () => {
    const res = await microClient.callTool({
      name: "tools",
      arguments: { action: "call", category: "level", method: "get_outliner", args: { limit: 1 } },
    });
    expect(body(res).dialogBlocking).toBe(true);
  }, 60_000);

  it("lets respond_to_dialog through the gateway, so the modal is answerable", async () => {
    // Gating on the tool the client named made this "tools.call", which is not
    // on the allowlist, so the ONE call that clears the dialog was refused too
    // and micro mode could never escape without a human at the editor.
    const before = bridge.seen.filter((m) => m === "respond_to_dialog").length;
    const res = await microClient.callTool({
      name: "tools",
      arguments: {
        action: "call",
        category: "editor",
        method: "respond_to_dialog",
        args: { buttonLabel: "Cancel" },
      },
    });
    expect(body(res).dialogBlocking).toBeUndefined();
    expect(bridge.seen.filter((m) => m === "respond_to_dialog").length).toBe(before + 1);
  }, 60_000);

  it("lets list_dialogs through the gateway, and still says the editor is blocked", async () => {
    const res = await microClient.callTool({
      name: "tools",
      arguments: { action: "call", category: "editor", method: "list_dialogs", args: {} },
    });
    const out = body(res);
    expect(out.dialogBlocking).toBeUndefined();
    // The stamp is decided on the UNWRAPPED name. Keyed on the tool the client
    // named it would be "tools.call", which is not allow-listed, so this read
    // would carry no warning at all through the gateway.
    expect(out.editorBlockedByDialog).toBe(true);
  }, 60_000);
});

describe("interactive mode, end to end, with a client that can be asked", () => {
  let liveClient: Client;
  let liveSandbox: string;
  const forms: Array<{ message: string; buttons: string[] }> = [];

  beforeAll(async () => {
    liveSandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-gate-interactive-"));
    const uproject = writeFixtureProject(liveSandbox);
    const env: Record<string, string> = {
      ...(process.env as Record<string, string>),
      UE_MCP_PORT: String(bridge.port),
      UE_MCP_HOST: "127.0.0.1",
      // NOT pinned. The default must resolve to interactive because the client
      // below advertises elicitation, and that resolution is what broke: the
      // guard cached deps built at startup with canElicit=false, so interactive
      // could never fire on any route and the whole suite still went green.
      UE_MCP_STATE_DIR: path.join(liveSandbox, "state"),
      UE_MCP_CONFIG_DIR: path.join(liveSandbox, "config"),
    };
    delete env.UE_MCP_DIALOG_MODE;

    liveClient = new Client(
      { name: "ue-mcp-gate-interactive", version: "1.0.0" },
      { capabilities: { elicitation: {} } },
    );
    const { ElicitRequestSchema } = await import("@modelcontextprotocol/sdk/types.js");
    liveClient.setRequestHandler(ElicitRequestSchema, async (req) => {
      const schema = req.params.requestedSchema as {
        properties?: { button?: { enum?: string[] } };
      };
      const buttons = schema.properties?.button?.enum ?? [];
      forms.push({ message: String(req.params.message ?? ""), buttons });
      return { action: "accept", content: { button: "Cancel" } };
    });

    await liveClient.connect(
      new StdioClientTransport({
        command: process.execPath,
        args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
        cwd: REPO_ROOT,
        env,
        stderr: fs.openSync(path.join(liveSandbox, "server.log"), "a"),
      }),
    );
  }, 180_000);

  afterAll(async () => {
    await liveClient?.close().catch(() => {});
    fs.rmSync(liveSandbox, { recursive: true, force: true });
  });

  it("raises the form on the first call and presses only the chosen button", async () => {
    // End to end, through the shipped server. This dialog renders small enough
    // for a form to carry whole, so there is no handover round trip: the person
    // is asked on the call that met it, and nothing depends on anyone knowing
    // to call again.
    forms.length = 0;
    const before = bridge.seen.filter((m) => m === "respond_to_dialog").length;

    await liveClient.callTool({
      name: "level",
      arguments: { action: "get_outliner", limit: 1 },
    });

    expect(forms.length, "no elicitation form was shown").toBe(1);
    // The dialog's own buttons, plus the option to leave it alone.
    expect(forms[0].buttons).toEqual([...DIALOG.buttons, "Leave the dialog open"]);
    // Title first, then the question, which are the two lines a client that
    // collapses the rest is guaranteed to draw.
    const lines = forms[0].message.split(String.fromCharCode(10));
    expect(lines[0]).toContain(DIALOG.title);
    expect(lines[1]).toContain("Select Content to Save");
    // And the button they chose was actually pressed. Exactly one.
    expect(bridge.seen.filter((m) => m === "respond_to_dialog").length).toBe(before + 1);
  }, 60_000);

  it("does not raise a second form for a dialog already asked about", async () => {
    forms.length = 0;
    const before = bridge.seen.filter((m) => m === "respond_to_dialog").length;
    await liveClient.callTool({ name: "level", arguments: { action: "get_outliner", limit: 1 } });
    expect(forms.length, "the person was asked twice for one dialog").toBe(0);
    expect(bridge.seen.filter((m) => m === "respond_to_dialog").length).toBe(before);
  }, 60_000);

  it("resolves the mode to interactive, not the defer fallback", async () => {
    // An in-process action, so only the server can refuse it. The stub never
    // dismisses the dialog, so the guard does not re-prompt for the one it has
    // already asked about; what is being checked is the MODE it resolved.
    const res = await liveClient.callTool({
      name: "project",
      arguments: { action: "search_tools", query: "spawn actor" },
    });
    const out = body(res);
    expect(out.dialogBlocking).toBe(true);
    expect(out.dialogMode).toBe("interactive");
  }, 60_000);

  it("does not prompt twice for the same dialog, or press two buttons for one call", async () => {
    // The preflight and the bridge boundary both check. Asking at each showed
    // the person the same prompt twice and pressed two real buttons for one
    // action, which on a save prompt is two answers to one question.
    forms.length = 0;
    const before = bridge.seen.filter((m) => m === "respond_to_dialog").length;
    await liveClient.callTool({ name: "level", arguments: { action: "get_outliner", limit: 1 } });
    expect(forms.length).toBeLessThanOrEqual(1);
    expect(bridge.seen.filter((m) => m === "respond_to_dialog").length - before)
      .toBeLessThanOrEqual(1);
  }, 60_000);
});

describe("the blocked-editor stamp", () => {
  it("is attached to an object result, and never mangles a non-object one", async () => {
    // typeof [] === "object", so an array result had the four properties
    // attached and then silently dropped by JSON.stringify. Nothing noticed,
    // because no allow-listed action returns a bare array today.
    const listed = await client.callTool({ name: "editor", arguments: { action: "list_dialogs" } });
    const out = body(listed);
    expect(out.editorBlockedByDialog).toBe(true);
    expect(out.dialogTitle).toBe(DIALOG.title);
    expect(Array.isArray(out.dialogs)).toBe(true);
    // The result's own payload survives the stamp intact.
    expect((out.dialogs as unknown[]).length).toBe(1);
  }, 60_000);

  it("says nothing about a dialog when there is none", async () => {
    // A fresh server against a bridge that reports a clear editor.
    const clearBridge = await StubBridge.start();
    clearBridge.reportNoDialog = true;
    const sandbox2 = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-gate-clear-"));
    const uproject = writeFixtureProject(sandbox2);
    const c = new Client({ name: "ue-mcp-gate-clear", version: "1.0.0" }, { capabilities: {} });
    try {
      await c.connect(
        new StdioClientTransport({
          command: process.execPath,
          args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
          cwd: REPO_ROOT,
          env: {
            ...(process.env as Record<string, string>),
            UE_MCP_PORT: String(clearBridge.port),
            UE_MCP_HOST: "127.0.0.1",
            UE_MCP_DIALOG_MODE: "auto",
            UE_MCP_STATE_DIR: path.join(sandbox2, "state"),
            UE_MCP_CONFIG_DIR: path.join(sandbox2, "config"),
          },
          stderr: fs.openSync(path.join(sandbox2, "server.log"), "a"),
        }),
      );
      const res = await c.callTool({ name: "project", arguments: { action: "get_status" } });
      const out = body(res);
      expect(out.editorBlockedByDialog).toBeUndefined();
      expect(out.dialogNote).toBeUndefined();
    } finally {
      await c.close().catch(() => {});
      await clearBridge.close();
      fs.rmSync(sandbox2, { recursive: true, force: true });
    }
  }, 120_000);
});

describe("the preflight, on a guard that knows nothing yet", () => {
  /**
   * The one case only the preflight can catch.
   *
   * An in-process action never reaches the bridge, so the bridge boundary
   * cannot refuse it, and the post-run check only fires if the guard already
   * knows about a dialog. On a cold guard nothing has told it, so without the
   * preflight the very first call runs against a frozen editor and reports
   * success. Every other test in this file is preceded by a call that arms the
   * guard, which is why deleting the preflight left them all green.
   */
  it("refuses the FIRST in-process call, before anything has armed the guard", async () => {
    const coldBridge = await StubBridge.start();
    coldBridge.reportNoDialog = true;
    const sandbox3 = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-gate-cold-"));
    const uproject = writeFixtureProject(sandbox3);
    const cold = new Client({ name: "ue-mcp-gate-cold", version: "1.0.0" }, { capabilities: {} });
    try {
      await cold.connect(
        new StdioClientTransport({
          command: process.execPath,
          args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
          cwd: REPO_ROOT,
          env: {
            ...(process.env as Record<string, string>),
            UE_MCP_PORT: String(coldBridge.port),
            UE_MCP_HOST: "127.0.0.1",
            UE_MCP_DIALOG_MODE: "auto",
            UE_MCP_STATE_DIR: path.join(sandbox3, "state"),
            UE_MCP_CONFIG_DIR: path.join(sandbox3, "config"),
          },
          stderr: fs.openSync(path.join(sandbox3, "server.log"), "a"),
        }),
      );

      // The modal appears AFTER startup, so nothing has armed the guard: the
      // startup handshake saw a clear editor and no bridge call has been
      // refused. This is the only state in which the preflight is the sole
      // defence, which is why deleting it left every other test green.
      coldBridge.raiseModalNow = true;

      const res = await cold.callTool({
        name: "project",
        arguments: { action: "search_tools", query: "spawn actor" },
      });
      const out = body(res);
      expect(out.dialogBlocking, "an in-process action ran against a frozen editor").toBe(true);
      // And refused BEFORE running, not reported afterwards. Without the
      // preflight the action executes and the post-run check reports it, which
      // is indistinguishable to a client except for these three fields.
      expect(out.partiallyApplied, "the action ran before it was refused").toBeUndefined();
      expect(out.note).toBeUndefined();
      expect(out.partialResult).toBeUndefined();
    } finally {
      await cold.close().catch(() => {});
      await coldBridge.close();
      fs.rmSync(sandbox3, { recursive: true, force: true });
    }
  }, 120_000);
});

describe("answering a dialog does not answer the next one for you", () => {
  it("refreshes after a press without raising a form or pressing again", async () => {
    // The post-press re-probe used to apply the mode, so in interactive it put
    // a form up for whatever prompt the answer surfaced and pressed a button
    // on it, unasked, then threw the decision away.
    const b = await StubBridge.start();
    b.secondDialogAfterPress = true;
    const sandbox4 = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-gate-2nd-"));
    const uproject = writeFixtureProject(sandbox4);
    const forms: string[] = [];
    const c = new Client(
      { name: "ue-mcp-gate-2nd", version: "1.0.0" },
      { capabilities: { elicitation: {} } },
    );
    const { ElicitRequestSchema } = await import("@modelcontextprotocol/sdk/types.js");
    c.setRequestHandler(ElicitRequestSchema, async (req) => {
      forms.push(String(req.params.message ?? ""));
      return { action: "accept", content: { button: "Cancel" } };
    });
    try {
      await c.connect(
        new StdioClientTransport({
          command: process.execPath,
          args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
          cwd: REPO_ROOT,
          env: {
            ...(process.env as Record<string, string>),
            UE_MCP_PORT: String(b.port),
            UE_MCP_HOST: "127.0.0.1",
            UE_MCP_STATE_DIR: path.join(sandbox4, "state"),
            UE_MCP_CONFIG_DIR: path.join(sandbox4, "config"),
            // auto, because this case is about the RE-PROBE after a press and
            // auto is the only mode in which the agent gets to press at all.
            // The client still advertises elicitation, so a form remains
            // possible and "no form was raised" still asserts something.
            UE_MCP_DIALOG_MODE: "auto",
          },
          stderr: fs.openSync(path.join(sandbox4, "server.log"), "a"),
        }),
      );

      forms.length = 0;
      const before = b.seen.filter((m) => m === "respond_to_dialog").length;
      await c.callTool({
        name: "editor",
        arguments: { action: "respond_to_dialog", buttonLabel: "Cancel" },
      });

      // Exactly the press the caller asked for, and no form for the prompt it
      // surfaced.
      expect(b.seen.filter((m) => m === "respond_to_dialog").length).toBe(before + 1);
      expect(forms, `a form was raised for: ${forms.join(" | ")}`).toEqual([]);
    } finally {
      await c.close().catch(() => {});
      await b.close();
      fs.rmSync(sandbox4, { recursive: true, force: true });
    }
  }, 120_000);
});

describe("a tool call is the agent, so interactive refuses it the button", () => {
  it("never sends the press to the editor when the person is the one being asked", async () => {
    // The whole leak, end to end. editor.respond_to_dialog was on the
    // always-allowed list and the list never consulted the mode, so an agent
    // could answer a modal under interactive: the mode decided whether a form
    // went up first and nothing else. Allow-listed means "safe to send while
    // the game thread is parked", which is not "may answer the question".
    const b = await StubBridge.start();
    const sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-gate-press-"));
    const uproject = writeFixtureProject(sandbox);
    const c = new Client(
      { name: "ue-mcp-gate-press", version: "1.0.0" },
      { capabilities: { elicitation: {} } },
    );
    const { ElicitRequestSchema } = await import("@modelcontextprotocol/sdk/types.js");
    // Accepting would prove nothing: the point is that the agent's press is
    // refused before any of this, so no form is raised for it either.
    c.setRequestHandler(ElicitRequestSchema, async () => ({ action: "decline" }));
    try {
      await c.connect(
        new StdioClientTransport({
          command: process.execPath,
          args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
          cwd: REPO_ROOT,
          env: {
            ...(process.env as Record<string, string>),
            UE_MCP_PORT: String(b.port),
            UE_MCP_HOST: "127.0.0.1",
            UE_MCP_STATE_DIR: path.join(sandbox, "state"),
            UE_MCP_CONFIG_DIR: path.join(sandbox, "config"),
            UE_MCP_DIALOG_MODE: "interactive",
          },
          stderr: fs.openSync(path.join(sandbox, "server.log"), "a"),
        }),
      );

      const before = b.seen.filter((m) => m === "respond_to_dialog").length;
      const res = await c.callTool({
        name: "editor",
        arguments: { action: "respond_to_dialog", buttonLabel: "Cancel" },
      });
      const text = JSON.stringify(res.content);

      expect(b.seen.filter((m) => m === "respond_to_dialog").length).toBe(before);
      expect(text).toContain("dialogBlocking");
      expect(text).toContain("interactive");
      // And it does not hand back the calls that would do it anyway.
      expect(text).not.toContain("respondWith");
    } finally {
      await c.close().catch(() => {});
      await b.close();
      fs.rmSync(sandbox, { recursive: true, force: true });
    }
  }, 120_000);
});
