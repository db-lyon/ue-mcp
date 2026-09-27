/**
 * editor(restart_editor) as a declared flow: stop_editor, an editor-state
 * probe when the stop failed, start_editor unless an interactive editor of
 * this project is still up, then a bridge reconnect. The reducer shapes the
 * steps into the answer the action has always given.
 */
import type { ChildOutcome, FlowActionSpec } from "../core/types.js";

export const RESTART_FLOW = "ue-mcp.editor/restart";

const NO_PROJECT = "No project loaded. Use project(action='set_project') first.";

const restartFlow = {
  description: "Stop the loaded project's editor, then start it again and reconnect.",
  // Without a loaded project there is no editor this is about (#819).
  checks: [{ when: "!project.path", action: "error", message: NO_PROJECT }],
  steps: {
    // editor(stop_editor) exactly as it behaves on its own.
    1: { task: "editor.stop_editor", ignore_failure: true },
    // A failed stop matters only while an interactive editor of this project is still up.
    2: { task: "editor.get_engine_state", options: { probeWindows: false }, when: "${steps.1.success} == false" },
    3: {
      task: "editor.start_editor",
      ignore_failure: true,
      when: "${steps.1.success} == true || ${steps.2.processes.length} == 0",
    },
    // start_editor may already have connected; a connect on a live socket is a no-op either way.
    4: { task: "ue-mcp.reconnect", options: { timeoutMs: 5000 }, when: "${steps.3.success} == true && !editor.connected" },
  },
};

type Steps = NonNullable<ChildOutcome["steps"]>;

/** The answer a step gave, or its failure rethrown, as the composition it replaced threw. */
function answered(step: Steps[number] | undefined, what: string): Record<string, unknown> {
  if (step?.data) return step.data;
  throw step?.error ?? new Error(`${what} did not answer`);
}

export const restartEditorAction: Pick<FlowActionSpec<ChildOutcome>, "flows" | "inputs" | "compose" | "result"> = {
  flows: { [RESTART_FLOW]: restartFlow },
  inputs: {},
  compose: (run) => run({ flow: RESTART_FLOW }, {}),
  result: (outcome) => {
    const steps = outcome.steps ?? [];
    if (steps.length === 0) {
      if (outcome.error?.message.includes(NO_PROJECT)) return { success: false, message: NO_PROJECT };
      throw outcome.error ?? new Error("restart_editor did not run");
    }
    const [stop, probe, start] = steps;
    const stopped = answered(stop, "stop_editor");
    if (probe && !probe.skipped) answered(probe, "get_engine_state");
    if (!start || start.skipped) return { success: false, message: `Failed to stop editor: ${stopped.message}` };
    // start_editor's note about how its progress rendered is its own; the restart never carried it.
    const { progressDisplayNote: _note, ...result } = answered(start, "start_editor");
    return result;
  },
};
