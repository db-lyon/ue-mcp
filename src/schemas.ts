import { z } from "zod";

// ── Composite types ──────────────────────────────────────────────────────────
// Shared UE geometry types used across many tool schemas.

export const Vec3 = z.object({ x: z.number(), y: z.number(), z: z.number() });
export const Rotator = z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() });
export const Color = z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() });
export const Quat = z.object({ x: z.number(), y: z.number(), z: z.number(), w: z.number() });

// ── Project / tooling file shapes ────────────────────────────────────────────
// Validated at trust boundaries (JSON.parse on files the user can hand-edit).

export const UProjectSchema = z
  .object({
    EngineAssociation: z.string().optional(),
    Plugins: z
      .array(
        z
          .object({
            Name: z.string().optional(),
            Enabled: z.boolean().optional(),
          })
          .passthrough(),
      )
      .optional(),
  })
  .passthrough();
export type UProjectFile = z.infer<typeof UProjectSchema>;

export const UeMcpConfigSchema = z
  .object({
    contentRoots: z.array(z.string()).optional(),
    disable: z.array(z.string()).optional(),
    // Native (Epic 5.8 ToolsetRegistry) tool surfacing. Enabled by default;
    // `exclude` names ue-mcp categories that should NOT be enriched with Epic
    // tools (they stay reachable via the `epic` gateway). See epic-enrich.ts.
    nativeTools: z
      .object({
        enabled: z.boolean().optional(),
        exclude: z.array(z.string()).optional(),
      })
      .optional(),
    http: z
      .object({
        enabled: z.boolean().optional(),
        port: z.number().int().min(1).max(65535).optional(),
        host: z.string().optional(),
      })
      .optional(),
    // Context-seeding strategy. `full` (default) advertises every action inline
    // in each category tool's description + trimmed server instructions. `lean`
    // collapses tool descriptions to a one-line summary, trims the instructions,
    // and moves the action catalog behind on-demand discovery (the `catalog`
    // tool + per-category `describe` action). See lean-context.ts.
    context: z
      .object({
        strategy: z.enum(["full", "lean"]).optional(),
      })
      .optional(),
    // Long-lived relay daemon. Enabled by default (opt-out): stdio sessions
    // attach to a shared daemon that holds one warm editor connection, survives
    // client restarts, and buffers requests across editor restarts. See proxy.ts.
    proxy: z
      .object({
        enabled: z.boolean().optional(),
        port: z.number().int().min(1).max(65535).optional(),
        host: z.string().optional(),
      })
      .optional(),
  })
  .passthrough();
export type UeMcpConfigFile = z.infer<typeof UeMcpConfigSchema>;

export const UPluginSchema = z
  .object({
    VersionName: z.string().optional(),
  })
  .passthrough();
export type UPluginFile = z.infer<typeof UPluginSchema>;
