import { describe, it, expect } from "vitest";
import {
  ERROR_CLASS_TABLE,
  ERROR_CLASSES,
  bodyEnvelope,
  classifyError,
  envelopeOfError,
  errorEnvelope,
  suggestionsIn,
  withBodyEnvelope,
} from "../../../src/dispatch/error-envelope.js";
import { dispatchFlowCall, errorResult, type DispatchDeps } from "../../../src/dispatch/server-dispatch.js";
import { McpError, ErrorCode } from "../../../src/core/errors.js";
import { SessionRegistry } from "../../../src/sessions/session.js";
import { GuardRegistry } from "../../../src/flow/guard.js";
import { unknownActionMessage } from "../../../src/surface/action-schema.js";

/** Every server code, editor token and editor wording the table has to place. */
const CASES: Array<[{ code?: string; editorCode?: string; message: string }, string, boolean]> = [
  [{ code: ErrorCode.UNKNOWN_ACTION, message: "Unknown action 'x'" }, "usage", false],
  [{ code: ErrorCode.INVALID_PARAMS, message: "level.delete_actor needs actorLabel or actorPath" }, "usage", false],
  [{ code: ErrorCode.NOT_FOUND, message: "No editor named 'b'" }, "usage", false],
  [{ code: ErrorCode.NOT_CONNECTED, message: "Not connected to editor bridge." }, "precondition", true],
  [{ code: ErrorCode.CONNECTION_LOST, message: "socket closed" }, "precondition", true],
  [{ code: ErrorCode.PROJECT_NOT_LOADED, message: "no project" }, "precondition", false],
  [{ code: ErrorCode.ASSET_LOCKED, message: "held by session 2" }, "precondition", true],
  [{ code: ErrorCode.WRITE_BLOCKED, message: "checked out by alice" }, "precondition", false],
  [{ code: ErrorCode.BRIDGE_TIMEOUT, message: "Bridge call 'x' timed out after 30s." }, "failure", false],
  [{ code: ErrorCode.NO_HANDLER, message: "no handler" }, "internal", false],
  [{ code: ErrorCode.BRIDGE_ERROR, message: "Bridge error: Unknown method: foo." }, "precondition", false],
  [{ code: ErrorCode.BRIDGE_ERROR, message: "Bridge error: handler threw" }, "internal", false],
  [{ code: "DIALOG_BLOCKED", message: "A modal dialog is up" }, "precondition", true],
  [{ code: "CHECK_FAILED", message: "Unsaved packages" }, "precondition", false],
  [{ code: "TASK_FAILED", editorCode: "invalid_params", message: "'edits' is required" }, "usage", false],
  [{ code: "TASK_FAILED", editorCode: "bone_not_found", message: "No bone 'x'" }, "usage", false],
  [{ code: "TASK_FAILED", editorCode: "not_ready", message: "Skeleton not ready" }, "precondition", true],
  [{ code: "TASK_FAILED", editorCode: "protected_asset", message: "Protected asset cannot be modified" }, "precondition", false],
  [{ code: "TASK_FAILED", editorCode: "unsupported_engine_version", message: "requires Unreal Engine 5.6" }, "precondition", false],
  [{ code: "TASK_FAILED", editorCode: "save_failed", message: "save failed" }, "failure", false],
  [{ code: "TASK_FAILED", message: "Missing required parameter 'assetPath'" }, "usage", false],
  [{ code: "TASK_FAILED", message: "Asset not found: /Game/X" }, "usage", false],
  [{ code: "TASK_FAILED", message: "Editor world not available" }, "precondition", false],
  [{ code: "TASK_FAILED", message: "Blueprint 'BP_X' already exists" }, "precondition", false],
  [{ code: "TASK_FAILED", message: "Niagara class is unavailable (plugin not loaded?)" }, "precondition", false],
  [{ code: "TASK_FAILED", message: "Failed to create Blueprint asset" }, "failure", false],
  [{ code: "UNKNOWN", message: "Cannot read properties of undefined" }, "internal", false],
  [{ code: "UNKNOWN", message: "flowName is required" }, "usage", false],
];

describe("the error class table", () => {
  for (const [input, cls, retryable] of CASES) {
    it(`${input.code ?? ""}${input.editorCode ? `/${input.editorCode}` : ""}: ${input.message} is ${cls}`, () => {
      expect(classifyError(input)).toEqual({ class: cls, retryable });
    });
  }

  it("uses only the four classes and gives every rule a matcher", () => {
    for (const rule of ERROR_CLASS_TABLE) {
      expect(ERROR_CLASSES).toContain(rule.class);
      expect(rule.code ?? rule.editorCode ?? rule.text).toBeDefined();
    }
  });

  it("falls back to failure for a message nothing names", () => {
    expect(classifyError({ code: "TASK_FAILED", message: "the graph did not settle" }).class).toBe("failure");
  });
});

describe("the envelope", () => {
  it("lifts the suggestions out of an unknown-action message", () => {
    const message = unknownActionMessage("complie", "blueprint", ["compile", "compile_all", "create"]);
    const env = errorEnvelope({ code: ErrorCode.NOT_FOUND, message });
    expect(env.class).toBe("usage");
    expect(env.didYouMean).toEqual(expect.arrayContaining(["compile"]));
    expect(env.retryable).toBe(false);
  });

  it("reads suggestions from the body before the message", () => {
    expect(errorEnvelope({ message: "No bone 'spine'", body: { candidates: ["spine_01"] } }).didYouMean).toEqual(["spine_01"]);
    expect(suggestionsIn("Did you mean 'spine_01'?")).toEqual(["spine_01"]);
    expect(suggestionsIn("Closest: a, b")).toEqual(["a", "b"]);
    expect(suggestionsIn("nothing offered")).toBeUndefined();
  });

  it("carries the step path inside a flow", () => {
    const env = envelopeOfError(new McpError(ErrorCode.ASSET_LOCKED, "busy"), "2/1");
    expect(env).toEqual({ class: "precondition", message: "busy", retryable: true, step: "2/1" });
  });

  it("classes flowkit's option and check errors", () => {
    const opt = Object.assign(new Error("Task \"x\": option \"a\" is required"), { name: "TaskOptionsError" });
    expect(envelopeOfError(opt).class).toBe("usage");
    const check = Object.assign(new Error("dirty"), { name: "CheckFailedError" });
    expect(envelopeOfError(check).class).toBe("precondition");
  });

  it("attaches to a success:false body under errorEnvelope, keeping error a string", () => {
    const body = { success: false, error: "Missing required parameter 'assetPath'", errorCode: "invalid_params" };
    const out = withBodyEnvelope(body) as Record<string, unknown>;
    expect(out.error).toBe(body.error);
    expect(out.errorEnvelope).toEqual({ class: "usage", message: body.error, retryable: false });
    expect(withBodyEnvelope({ success: true })).toEqual({ success: true });
    expect(bodyEnvelope([1, 2])).toBeUndefined();
  });
});

describe("every error result carries the envelope", () => {
  const machine = (texts: string[]) => {
    const line = texts.find((t) => t.startsWith("MACHINE_ERROR="));
    return line ? JSON.parse(line.slice("MACHINE_ERROR=".length)) as Record<string, unknown> : undefined;
  };

  it("keeps the Error [CODE] line first and adds the machine block", () => {
    const result = errorResult("INVALID_PARAMS", "bad");
    const texts = result.content.map((b) => b.text);
    expect(texts[0]).toBe("Error [INVALID_PARAMS]: bad");
    expect(machine(texts)).toEqual({ code: "INVALID_PARAMS", error: { class: "usage", message: "bad", retryable: false } });
  });

  it("keeps an error's details beside the envelope", () => {
    const e = new McpError(ErrorCode.BRIDGE_TIMEOUT, "timed out", { outcome: "unknown", operationId: "7" });
    const m = machine(errorResult(e.code, e.message, [], e).content.map((b) => b.text))!;
    expect(m.outcome).toBe("unknown");
    expect(m.operationId).toBe("7");
    expect((m.error as Record<string, unknown>).class).toBe("failure");
  });

  it("reaches the flow tool's errors", async () => {
    const sessions = new SessionRegistry(new GuardRegistry());
    sessions.register({});
    const deps = {
      sessions,
      loads: { dispatchUnion: { tools: [] } },
      lockingCfg: { enabled: false, ttlSeconds: 0 },
      dialogGuardFor: () => ({ check: async () => ({ allow: true }) }),
      elicit: () => undefined,
      client: () => undefined,
    } as unknown as DispatchDeps;
    const result = await dispatchFlowCall(
      deps,
      { name: "flow", description: "", schema: {}, actions: {}, handler: async () => { throw new Error("flowName is required"); } },
      { bridge: sessions.active.guarded, project: sessions.active.project } as never,
      { action: "run" },
    );
    const m = machine(result.content.map((b) => b.text))!;
    expect((m.error as Record<string, unknown>).class).toBe("usage");
  });
});
