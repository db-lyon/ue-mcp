/** Routing parameter names the dispatcher consumes before a call reaches a
 *  handler. The single source: action-schema.ts builds ROUTING_PARAMS from it,
 *  and scripts/generate-epic-actions.mjs imports it so a generated surface
 *  never declares one. */
export const ROUTING_PARAM_NAMES = ["action", "timeoutMs", "select", "omit", "editor", "toEditor"];
