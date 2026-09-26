/**
 * The per-call editor target (#817). Injected into every category tool only
 * while this server drives more than one editor, so a single-editor client
 * sees the schema it has always seen.
 */
export const EDITOR_TARGET_PARAM = "editor";

/**
 * The destination editor for an action that moves content between two of them
 * (#817). `editor` says where a call runs; this says where its output lands.
 */
export const MIGRATE_TARGET_PARAM = "toEditor";

/** Routing parameter names the dispatcher consumes before a call reaches a
 *  handler. The single source: action-schema.ts builds ROUTING_PARAMS from it,
 *  and scripts/generate-epic-actions.mjs imports it so a generated surface
 *  never declares one. */
export const ROUTING_PARAM_NAMES = ["action", "timeoutMs", "select", "omit", EDITOR_TARGET_PARAM, MIGRATE_TARGET_PARAM];
