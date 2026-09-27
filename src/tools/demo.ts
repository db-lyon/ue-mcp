import type { BridgeActionSpec, ChildOutcome, FlowActionSpec, ToolDef } from "../core/types.js";
import { categoryTool } from "../surface/category-tool.js";
import { specBp, schema as specSchema } from "./specs/demo.generated.js";
import { DEMO_STEPS, demoFlowName, demoStepIndex, demoUnreadParams, tagDemoStep } from "./demo-steps.js";

type StepRun =
  | { kind: "list"; answer: ChildOutcome }
  | { kind: "invalid"; index: number }
  | { kind: "step"; index: number; answer: ChildOutcome };

/**
 * A flow-backed action whose parameters are a recorded C++ spec: it keeps the
 * spec's description, signature and parameter checks.
 */
function specFlow<C>(spec: BridgeActionSpec, flow: Pick<FlowActionSpec<C>, "inputs" | "compose" | "result" | "expand">): FlowActionSpec<C> {
  return {
    kind: "flow",
    effect: spec.effect,
    description: spec.description,
    paramSpec: spec.paramSpec,
    paramChoices: spec.paramChoices,
    specMethod: spec.bridge,
    ...flow,
  };
}

/** demo(step): the step list, or the step's demo_step_N flow reduced to the step's answer. */
const stepFlow: Pick<FlowActionSpec<StepRun>, "inputs" | "compose" | "result" | "expand"> = {
  inputs: { step: specSchema.step, stepIndex: specSchema.stepIndex },
  expand: (input) => {
    const index = demoStepIndex(input);
    if (index === undefined) return [{ task: "demo.get_steps" }];
    return index >= 1 && index <= DEMO_STEPS.length ? [{ flow: demoFlowName(index) }] : [];
  },
  compose: async (run, input) => {
    const index = demoStepIndex(input);
    if (index === undefined) return { kind: "list", answer: await run("demo.get_steps", {}) };
    if (index < 1 || index > DEMO_STEPS.length) return { kind: "invalid", index };
    return { kind: "step", index, answer: await run({ flow: demoFlowName(index) }, {}) };
  },
  result: (outcome, input) => {
    if (outcome.kind === "invalid") {
      return { success: false, error: `Invalid step index ${outcome.index}. Valid range: 1-19` };
    }
    let answer: Record<string, unknown>;
    if (outcome.kind === "list") {
      if (outcome.answer.data === undefined) throw outcome.answer.error ?? new Error("demo.get_steps did not answer");
      answer = { ...outcome.answer.data };
    } else {
      const step = DEMO_STEPS[outcome.index - 1];
      if (!outcome.answer.steps) throw outcome.answer.error ?? new Error(`${demoFlowName(outcome.index)} did not run`);
      answer = tagDemoStep(step.reduce(outcome.answer.steps), step);
    }
    // What the editor reported for the keys a step never reads, on a success.
    const unread = demoUnreadParams(input);
    return unread.length > 0 && answer.success !== false ? { ...answer, paramsNotRead: unread } : answer;
  },
};

export const demoTool: ToolDef = categoryTool(
  "demo",
  "Neon Shrine demo scene builder and cleanup.",
  {
    step:    specFlow(specBp("mutate", "Execute demo step. Steps are NOT idempotent: each one spawns unconditionally, so a replay leaves a second set of Demo_ actors, and the result says created rather than a bare success. No inverse is emitted. No step records what it individually created, and demo(cleanup) is not a substitute: it removes the whole demo scene, and on the way it creates /Game/MCP_Home if missing and switches the editor to it, then deletes by label prefix in whatever level is then open. The response names it as guidance with rollbackPossible=false, so the flow runner never invokes it as an undo.", "demo_step"), stepFlow),
    get_steps: specBp("read", "List every demo step up front: index, id and description, plus a count. Use this to see what the 19 steps build before running any of them.", "demo_get_steps"),
    cleanup: specBp("mutate", "Remove demo assets and actors. Switches editor to /Game/MCP_Home before deleting so the editor is never left on an Untitled map, then collects garbage so the unloaded /Game/Demo/DemoLevel is deleted too and the next run starts from a new level; demoLevelRemoved reports it. unchanged=true only when nothing was deleted AND the home level already existed AND the editor was already in it, because anchoring to that level is itself a change this call makes. No inverse of its own - rebuilding means running step 1 through 19 again.", "demo_cleanup"),
    go_home: specBp("mutate", "Switch the editor to /Game/MCP_Home (creating it on first use). Use this before any operation that would leave the editor on an Untitled map. Reports alreadyOpen=true when the home level was already the open one, and otherwise rolls back by reopening the level that WAS open through level(load) - marked lossy when the home level had to be created, since that package stays on disk. A previously open map with no content path (unsaved or Untitled) has no inverse and the response says so.", "demo_go_home"),
  },
  {
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration.
    ...specSchema,
  },
);
