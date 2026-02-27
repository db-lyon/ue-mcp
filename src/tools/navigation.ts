import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const navigationTools: ToolDef[] = [
  bt("rebuild_navigation", "Trigger a navigation mesh rebuild.", {}),
  bt("get_navmesh_info", "Get information about the navigation system and nav data.", {}),
  bt("project_point_to_navigation", "Project a world point onto the navigation mesh.", {
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }),
    extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
  }),
  bt("spawn_nav_modifier_volume", "Place a Nav Modifier Volume actor.", {
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }),
    extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    areaClass: z.string().optional(),
  }),
];
