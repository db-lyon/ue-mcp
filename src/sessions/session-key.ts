// @session independent: The default session's key, a constant.
/**
 * The key of the session that has no project bound. A leaf, so per-session
 * stores outside the registry can partition by it without importing the
 * registry and everything the registry loads.
 */
export const DEFAULT_SESSION_KEY = "";
