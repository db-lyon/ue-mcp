import { z } from "zod";

/**
 * What a guard is, declared once and used by both things that can declare one:
 * a plugin manifest and a project's `ue-mcp.yml`.
 *
 * A guard used to be a task whose NAME encoded everything: `guard.sandbox.
 * beforeWrite` meant a guard called sandbox, running before the call, applying
 * only to writes. That encoding could not express anything else, so ordering
 * was unreachable, a guard wanting both hooks had to be two tasks that did not
 * know they were related, and a misspelling produced an ordinary task nobody
 * ever called rather than an error.
 *
 * The four "phases" were also never four things. They are two hooks and two
 * scopes, so they are two fields.
 */

/**
 * Which calls a guard sees.
 *
 *   all        every call, including reads. For audit and rate limiting.
 *   mutations  every call that changes editor state, whether or not it names
 *              an asset. This is what "block everything that mutates" means:
 *              spawning an actor and starting a play session are in, reads are
 *              out.
 *   writes     calls that modify content already on disk. Narrower than
 *              mutations on purpose: it is the source-control question, and a
 *              call that creates something new modifies nothing yet.
 */
export const GuardScopeSchema = z.enum(["all", "mutations", "writes"]).default("all");

/** One hook: the task to run, and the options it is built with. */
export const GuardHookSchema = z.object({
  class_path: z.string().min(1, "a guard hook needs the class_path of the task that implements it"),
  options: z.record(z.unknown()).default({}),
});

export const GuardDeclarationSchema = z
  .object({
    description: z.string().optional(),
    /**
     * `writes` costs nothing on a read: the pipeline only resolves which files
     * a call touches when a guard asks for it.
     */
    scope: GuardScopeSchema,
    /** Lower runs first before the call, and last after it. */
    order: z.number().int().default(0),
    /** Runs before the call. Denying here means the call never happens. */
    before: GuardHookSchema.optional(),
    /** Runs after a successful call, and may replace its result. */
    after: GuardHookSchema.optional(),
  })
  .refine((g) => g.before !== undefined || g.after !== undefined, {
    message:
      "a guard needs a before hook, an after hook, or both. One with neither would be "
      + "registered, reported, and never run, which is indistinguishable from having no guard.",
  });

/** Guards keyed by name. The name is an identifier, not a phase encoding. */
export const GuardsSchema = z
  .record(
    z.string().regex(/^[a-z][a-z0-9_-]*$/i, {
      message:
        "a guard name must start with a letter and hold only letters, digits, underscores "
        + "and hyphens",
    }),
    GuardDeclarationSchema,
  )
  .default({});

export type GuardScope = z.infer<typeof GuardScopeSchema>;
export type GuardHook = z.infer<typeof GuardHookSchema>;
export type GuardDeclaration = z.infer<typeof GuardDeclarationSchema>;
export type GuardDeclarations = z.infer<typeof GuardsSchema>;
