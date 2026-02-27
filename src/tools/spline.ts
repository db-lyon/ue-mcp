import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const splineTools: ToolDef[] = [
  bt("get_spline_info", "Get spline information: points, length, closed loop status.", {
    actorLabel: z.string().describe("Actor label with the spline component"),
  }),
  bt("set_spline_points", "Set control points on a spline component.", {
    actorLabel: z.string(),
    points: z.array(z.object({ x: z.number(), y: z.number(), z: z.number() })),
    closedLoop: z.boolean().optional(),
  }),
];
