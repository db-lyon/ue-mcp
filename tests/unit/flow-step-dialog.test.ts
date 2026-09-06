import { describe, it, expect, vi, afterEach } from "vitest";
import { handlerTaskClass } from "../../src/flow/task-factory.js";
import { guardFor, forgetGuard, type BlockingDialog } from "../../src/dialog-guard.js";
import type { EditorSession } from "../../src/session.js";
import type { FlowContext } from "../../src/flow/context.js";

const DIALOG: BlockingDialog = {
  title: "Save Content",
  message: "Select Content to Save",
  buttons: ["Save Selected", "Don't Save"],
  choices: [{ buttonLabel: "Don't Save", respondWith: "editor(respond_to_dialog)" }],
};

function armed(dialogUp: boolean) {
  const session = {} as unknown as EditorSession;
  guardFor(session, {
    mode: () => "defer",
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

  const run = async (taskName: string, dialogUp: boolean, options: Record<string, unknown> = {}) => {
    const session = armed(dialogUp);
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

  it("still lets the step that answers the dialog run", async () => {
    const { result, fn } = await run("editor.respond_to_dialog", true);
    expect(result.success).toBe(true);
    expect(fn, "the one call that clears the dialog was refused").toHaveBeenCalledTimes(1);
  });

  it("unwraps the micro gateway instead of judging the wrapper", async () => {
    // The gateway arrives as one task carrying the real category and method in
    // its options. Asking the allowlist about "tools.call" refused
    // respond_to_dialog, so micro mode could never escape a dialog.
    const { result, fn } = await run("tools.call", true, {
      category: "editor",
      method: "respond_to_dialog",
    });
    expect(result.success).toBe(true);
    expect(fn).toHaveBeenCalledTimes(1);
  });
});
