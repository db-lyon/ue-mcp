// @session independent: Maps a native handler's recorded contract onto its action.
import type { z } from "zod";
import type { ActionSpec } from "../surface/tool-types.js";
import type { HandlerSpec } from "../surface/handler-spec.js";

/** What a native handler with a recorded spec adds to its manifest-shaped action (#1282). */
export interface NativeContractExtras {
  /** The handler's recorded contract and the bridge method it belongs to. */
  contract?: { method: string; spec: HandlerSpec };
  /** The flat category keys built from the specs, used in place of the manifest's `schema`. */
  zodSchema?: Record<string, z.ZodType>;
}

/** The action fields a recorded contract sets: the spec, its choices, strict keys, and the recording itself. */
export function contractActionFields(contract: NativeContractExtras["contract"]): Partial<ActionSpec> {
  if (!contract) return {};
  return {
    paramSpec: contract.spec.params,
    paramChoices: contract.spec.choices,
    strictParams: true,
    recordedContract: contract,
  };
}
