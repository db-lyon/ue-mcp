/**
 * Parameter specs for flow-backed actions that no bridge method records.
 *
 * Same shape as a recorded C++ spec, so the action keeps its description,
 * signature and checks. Keyed by `category.action`. The audits read these
 * through `flowSpecBp`, as they read a generated module through `specBp`.
 */
import { categorySchema, makeSpecBp, type HandlerSpecs } from "./handler-spec.js";

export const FLOW_ACTION_SPECS: HandlerSpecs = {
  // Every demo step is a flow, so no bridge method is left to record it from.
  "demo.step": {
    category: "demo",
    params: [{
      name: "step",
      type: "integer",
      required: false,
      description: "Step index to execute, 1 to 19. Omit for the step list",
      aliases: ["stepIndex"],
    }],
  },
};

/** Every key the flow specs declare, aliases included. */
export const flowSpecSchema = categorySchema(FLOW_ACTION_SPECS);

/** Declare a flow action's spec: effect, summary, `category.action`. */
export const flowSpecBp = makeSpecBp(FLOW_ACTION_SPECS);
