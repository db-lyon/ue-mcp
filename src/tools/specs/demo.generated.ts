// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd demo handler. */
export const handlerSpecs: HandlerSpecs = {
  "demo_cleanup": {
    "category": "demo",
    "params": [],
    "contractExempt": "Takes no parameters and acts unconditionally: switches levels and deletes the demo scene"
  },
  "demo_get_steps": {
    "category": "demo",
    "params": []
  },
  "demo_go_home": {
    "category": "demo",
    "params": [],
    "contractExempt": "Takes no parameters and acts unconditionally: creates the home level if missing and opens it"
  },
  "demo_step": {
    "category": "demo",
    "params": [
      {
        "name": "step",
        "type": "integer",
        "required": false,
        "description": "Step index to execute, 1 to 19. Omit for the step list",
        "aliases": [
          "stepIndex"
        ]
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  demo_cleanup: "Params: none",
  demo_get_steps: "Params: none",
  demo_go_home: "Params: none",
  demo_step: "Params: step? (or stepIndex)",
};

/** Every key the spec'd demo handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  step: z.number().int().optional().describe("Step index to execute, 1 to 19. Omit for the step list"),
  stepIndex: z.number().int().optional().describe("Alias for step"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
