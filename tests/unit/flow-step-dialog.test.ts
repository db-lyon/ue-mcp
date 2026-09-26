import { describe, it, expect, vi, afterEach } from "vitest";
import { handlerTaskClass } from "../../src/flow/task-factory.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildMicroGateway } from "../../src/micro-context.js";
import { categoryTool } from "../../src/category-tool.js";
import { guardFor, forgetGuard, type BlockingDialog } from "../../src/editor/dialog-guard.js";
import type { EditorSession } from "../../src/sessions/session.js";
import type { FlowContext } from "../../src/flow/context.js";

const DIALOG: BlockingDialog = {
  title: "Save Content",
  message: "Select Content to Save",
  buttons: ["Save Selected", "Don't Save"],
  choices: [{ buttonLabel: "Don't Save", respondWith: "editor(respond_to_dialog)" }],
};

function armed(dialogUp: boolean, mode: "defer" | "auto" | "interactive" = "defer") {
  const session = {} as unknown as EditorSession;
  guardFor(session, {
    mode: () => mode,
    probe: async () => ({ dialogs: dialogUp ? [DIALOG] : [] }),
    press: async () => ({ success: true }),
  });
  return session;
}

/**
 * A flow runs for minutes. The tool route checks once, before flow.run starts,
 * so a modal raised at step 3 was missed by every step after it. Bridge-backed
 * steps are covered at the bridge boundary; an in-process handler never goes
 * near it, so that route simply had no gate on it.
 */
describe("a modal appearing mid-flow stops the steps after it", () => {
  const sessions: EditorSession[] = [];
  afterEach(() => {
    for (const s of sessions.splice(0)) forgetGuard(s);
  });

  const run = async (
    taskName: string,
    dialogUp: boolean,
    options: Record<string, unknown> = {},
    mode: "defer" | "auto" | "interactive" = "defer",
  ) => {
    const session = armed(dialogUp, mode);
    sessions.push(session);
    const fn = vi.fn(async () => ({ ok: true }));
    const Task = handlerTaskClass(taskName, fn);
    const ctx = { session } as unknown as FlowContext;
    const task = new (Task as new (c: unknown, o: unknown) => { execute: () => Promise<unknown> })(
      ctx,
      options,
    );
    return { result: (await task.execute()) as Record<string, unknown>, fn };
  };

  it("refuses an in-process step while a dialog is up, and never calls the handler", async () => {
    const { result, fn } = await run("asset.list", true);
    expect(result.success).toBe(false);
    expect((result.data as Record<string, unknown>).dialogBlocking).toBe(true);
    expect(fn, "the step ran against a parked editor").not.toHaveBeenCalled();
  });

  it("runs the step normally when nothing is blocking", async () => {
    const { result, fn } = await run("asset.list", false);
    expect(result.success).toBe(true);
    expect(fn).toHaveBeenCalledTimes(1);
  });

  it("still lets the step that answers the dialog run, in the mode that may answer", async () => {
    const { result, fn } = await run("editor.respond_to_dialog", true, {}, "auto");
    expect(result.success).toBe(true);
    expect(fn, "the one call that clears the dialog was refused").toHaveBeenCalledTimes(1);
  });

  it("refuses that same step under a mode where the person answers", async () => {
    // A flow step is the agent, whatever it is named. defer says a person
    // answers the dialog in the editor window and interactive says a person
    // answers the form, so a step pressing a button is the agent taking a
    // decision neither mode gave it. This route used to skip the guard
    // entirely for an allow-listed subject, so it walked around the gate
    // rather than through it.
    for (const mode of ["defer", "interactive"] as const) {
      const { result, fn } = await run("editor.respond_to_dialog", true, {}, mode);
      expect(result.success, mode).toBe(false);
      expect(fn, mode).not.toHaveBeenCalled();
    }
  });

  // A micro step names tools.call; the registry creates the target task, so the
  // guard judges respond_to_dialog, never the wrapper. Judging "tools.call"
  // refused the one call that clears a dialog.
  const viaGateway = async (mode: "auto" | "interactive") => {
    const session = armed(true, mode);
    sessions.push(session);
    const fn = vi.fn(async () => ({ ok: true }));
    const editor = categoryTool("editor", "Editor", {
      respond_to_dialog: { kind: "handler", effect: "mutate", handler: fn },
    });
    const registry = buildFlowRegistry([buildMicroGateway([editor]), editor]);
    const task = await registry.create("tools.call", { session } as unknown as FlowContext, {
      category: "editor", method: "respond_to_dialog", args: {},
    });
    return { result: (await task.run()) as unknown as Record<string, unknown>, fn };
  };

  it("unwraps the micro gateway instead of judging the wrapper", async () => {
    const { result, fn } = await viaGateway("auto");
    expect(result.success).toBe(true);
    expect(fn).toHaveBeenCalledTimes(1);
  });

  it("unwraps the gateway when refusing it too, so the wrapper is not a way past the mode", async () => {
    const { result, fn } = await viaGateway("interactive");
    expect(result.success).toBe(false);
    expect(fn).not.toHaveBeenCalled();
  });
});
