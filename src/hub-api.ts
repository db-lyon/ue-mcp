// @session independent: The public ue-mcp/hub entry point: the Playtest Hub's contract, with no Node imports.
/**
 * The Playtest Hub's contract for clients that run in a browser or a phone:
 * clips, tracks, arrangements, tests and suites, the rules that validate them
 * and work out health, and the run model. `ue-mcp/daemon` exports the same
 * declarations beside helpers that need Node; this entry loads only the
 * contract files, which import nothing but each other, so any bundler can
 * take it.
 *
 * ```ts
 * import { suiteHealth, type SuiteState } from "ue-mcp/hub";
 * ```
 *
 * Versioned with the daemon contract (DAEMON_API_VERSION).
 */
export * from "./contract/hub.js";
export * from "./contract/hub-performers.js";
export type * from "./contract/hub-ports.js";
