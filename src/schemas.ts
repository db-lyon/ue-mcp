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
    // Editor bridge WebSocket. `port` pins the port instead of deriving it
    // from the project root path (see port.ts). Pin it only when you need a
    // fixed, well-known port; leaving it unset gives each worktree a stable,
    // collision-resistant derived port.
    bridge: z
      .object({
        port: z.number().int().min(1).max(65535).optional(),
        // Per-project equivalent of UE_MCP_HOST (#817). The env var is a global
        // default that applies to every session at once; this is how one
        // project reaches an editor on another host without moving the rest.
        host: z.string().optional(),
      })
      .optional(),
    // Per-project equivalents of UE_EDITOR_PATH and UE_BUILD_TOOL_PATH (#817).
    // Both env vars are global, so with more than one project they force every
    // editor through one binary even when the projects are on different engine
    // versions. Unset means the engine the project's EngineAssociation names.
    editor: z
      .object({
        path: z.string().optional(),
        buildToolPath: z.string().optional(),
      })
      .optional(),
    // Per-project equivalent of UE_MCP_ENV (#817): which `ue-mcp.<env>.yml`
    // overlay this project merges. The env var still wins and still applies to
    // every project at once.
    env: z.string().optional(),
    // Per-asset exclusive locking for concurrent agents. Opt-in: `enabled`
    // wraps mutating dispatch in acquire/release around the shared bridge
    // registry. `ttlSeconds` is the lease length a crashed agent's locks
    // survive before auto-release (default 300). See locking.ts.
    locking: z
      .object({
        enabled: z.boolean().optional(),
        ttlSeconds: z.number().int().min(1).optional(),
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
        strategy: z.enum(["full", "lean", "micro"]).optional(),
      })
      .optional(),
    // Play In Editor. `allowIgnoreBlueprintErrors` pre-authorizes
    // editor(play_in_editor_ignore_blueprint_errors), the one action that
    // suppresses the editor's unresolved-Blueprint-error prompt. Off by
    // default: without it every call blocks on an MCP approval prompt.
    // Belongs in an untracked layer (ue-mcp.local.yml) unless the whole team
    // really wants PIE to start over known Blueprint errors.
    pie: z
      .object({
        allowIgnoreBlueprintErrors: z.boolean().optional(),
      })
      .optional(),
    // Per-plugin runtime config, keyed by plugin slug (`recipes`, i.e. the
    // package name minus `ue-mcp-`). `groups` toggles whole flow groups on/off
    // (opt-out: a group is enabled unless set to false). Lives here so it rides
    // the same global < project < env < local merge as the rest of this block,
    // letting personal toggles live in an untracked layer without dirtying the
    // tracked ue-mcp.yml. See plugin-groups.ts.
    pluginConfig: z
      .record(
        z
          .object({
            groups: z.record(z.boolean()).optional(),
          })
          .passthrough(),
      )
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
