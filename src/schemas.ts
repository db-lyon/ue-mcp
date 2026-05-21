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
    http: z
      .object({
        enabled: z.boolean().optional(),
        port: z.number().int().min(1).max(65535).optional(),
        host: z.string().optional(),
      })
      .optional(),
    /** Absolute paths to Claude Code settings.json files where the ue-mcp
     *  PostToolUse hook was installed. Used to symmetrically uninstall when
     *  feedback is disabled or the user opts out of agent prompts. */
    installedHooks: z.array(z.string()).optional(),
    /** Feedback flow behavior. Default "interactive" runs the MCP
     *  elicitation approval prompt. "auto-approve" posts immediately
     *  without prompting (still scrubbed). "defer" writes the scrubbed
     *  payload to ~/.ue-mcp/pending-feedback/ for later review via
     *  `npx ue-mcp feedback list/approve/discard`. Env var
     *  UE_MCP_FEEDBACK_MODE overrides this at runtime. */
    feedback: z
      .object({
        mode: z.enum(["interactive", "auto-approve", "defer"]).optional(),
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
