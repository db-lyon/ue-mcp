import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const demoTool: ToolDef = categoryTool(
  "demo",
  "Neon Shrine demo scene builder and cleanup.",
  {
    step:    bp("demo_step", (p) => p.stepIndex !== undefined ? { step: p.stepIndex } : {}),
    cleanup: bp("demo_cleanup"),
  },
  `- step: Execute demo step. Params: stepIndex? (omit for step list)
- cleanup: Remove demo assets and actors`,
  {
    stepIndex: z.number().optional().describe("Step index to execute. Omit for step list."),
  },
);
