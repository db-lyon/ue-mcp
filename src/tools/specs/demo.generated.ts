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
    "params": [
      {
        "name": "discardUnsaved",
        "type": "boolean",
        "required": false,
        "description": "Switch even though the open map has unsaved changes, discarding them (default false)"
      }
    ],
    "contractExempt": "Creates the home level if missing and opens it before the contract path can fail"
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
  demo_go_home: "Params: discardUnsaved?",
  demo_step: "Params: step? (or stepIndex)",
};

/** Every key the spec'd demo handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  discardUnsaved: z.boolean().optional().describe("Switch even though the open map has unsaved changes, discarding them (default false)"),
  step: z.number().int().optional().describe("Step index to execute, 1 to 19. Omit for the step list"),
  stepIndex: z.number().int().optional().describe("Alias for step"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
