import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const performanceTools: ToolDef[] = [
  bt("get_editor_performance_stats", "Get editor performance statistics: actor counts, top classes.", {}),
  bt("run_stat_command", "Execute a stat console command.", {
    command: z.string().describe("Stat command (e.g. 'fps', 'unit', 'game')"),
  }),
  bt("set_scalability", "Set scalability quality levels.", {
    level: z.string().describe("Quality: 'Low', 'Medium', 'High', 'Epic', 'Cinematic'"),
  }),
  bt("capture_screenshot", "Capture a high-resolution screenshot.", {
    filename: z.string().optional(), resolution: z.number().optional(),
  }),
  bt("get_viewport_info", "Get viewport camera location and rotation.", {}),
  bt("set_viewport_camera", "Set the viewport camera location and rotation.", {
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
  }),
  bt("focus_viewport_on_actor", "Focus the viewport camera on a specific actor.", {
    actorLabel: z.string(),
  }),
];
