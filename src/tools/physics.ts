import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const physicsTools: ToolDef[] = [
  bt("set_collision_profile", "Set the collision profile on an actor's primitive component.", {
    actorLabel: z.string(), profileName: z.string().describe("Collision profile preset name"),
  }),
  bt("set_simulate_physics", "Enable or disable physics simulation on an actor.", {
    actorLabel: z.string(), simulate: z.boolean(),
  }),
  bt("set_collision_enabled", "Set the collision enabled state on an actor's component.", {
    actorLabel: z.string(), collisionEnabled: z.string().describe("'NoCollision', 'QueryOnly', 'PhysicsOnly', 'QueryAndPhysics'"),
  }),
  bt("set_physics_properties", "Set physics properties (mass, damping, gravity) on an actor.", {
    actorLabel: z.string(),
    mass: z.number().optional(), linearDamping: z.number().optional(),
    angularDamping: z.number().optional(), enableGravity: z.boolean().optional(),
  }),
];
