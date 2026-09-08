import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const demoTool: ToolDef = categoryTool(
  "demo",
  "Neon Shrine demo scene builder and cleanup.",
  {
    step:    bp("mutate", "Execute demo step. Steps are NOT idempotent: each one spawns unconditionally, so a replay leaves a second set of Demo_ actors, and the result says created rather than a bare success. No inverse is emitted. No step records what it individually created, and demo(cleanup) is not a substitute: it removes the whole demo scene, and on the way it creates /Game/MCP_Home if missing and switches the editor to it, then deletes by label prefix in whatever level is then open. The response names it as guidance with rollbackPossible=false, so the flow runner never invokes it as an undo. Params: stepIndex?", "demo_step", (p) => p.stepIndex !== undefined ? { step: p.stepIndex } : {}),
    get_steps: bp("read", "List every demo step up front: index, id and description, plus a count. Use this to see what the 19 steps build before running any of them. Params: none", "demo_get_steps"),
    cleanup: bp("mutate", "Remove demo assets and actors. Switches editor to /Game/MCP_Home before deleting so the editor is never left on an Untitled map. unchanged=true only when nothing was deleted AND the home level already existed AND the editor was already in it, because anchoring to that level is itself a change this call makes. No inverse of its own - rebuilding means running step 1 through 19 again. Params: none", "demo_cleanup"),
    go_home: bp("mutate", "Switch the editor to /Game/MCP_Home (creating it on first use). Use this before any operation that would leave the editor on an Untitled map. Reports alreadyOpen=true when the home level was already the open one, and otherwise rolls back by reopening the level that WAS open through level(load) - marked lossy when the home level had to be created, since that package stays on disk. A previously open map with no content path (unsaved or Untitled) has no inverse and the response says so. Params: none", "demo_go_home"),
  },
  undefined,
  {
    stepIndex: z.number().optional().describe("Step index to execute. Omit for step list."),
  },
);
