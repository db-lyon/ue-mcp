import { z } from "zod";

// ── Composite types ──────────────────────────────────────────────────────────
// Shared UE geometry types used across many tool schemas.

export const Vec3 = z.object({ x: z.number(), y: z.number(), z: z.number() });
export const Rotator = z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() });
export const Color = z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() });
export const Quat = z.object({ x: z.number(), y: z.number(), z: z.number(), w: z.number() });
