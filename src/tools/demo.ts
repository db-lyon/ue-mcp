import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const demoTools: ToolDef[] = [
  bt("demo_scene_from_nothing",
    "Execute steps of the Neon Shrine demo scene builder. Call with stepIndex to advance through the demo.",
    {
      stepIndex: z.number().optional().describe("Step index to execute. Omit for step list."),
    },
    "demo_step", (p) => p.stepIndex !== undefined ? { step: p.stepIndex } : {}),

  bt("demo_cleanup",
    "Remove demo level, all Demo_ actors, and demo material assets.",
    {}, "demo_cleanup"),
];
