// @session independent: The public ue-mcp/hub entry point: the Playtest Hub's contract, with no Node imports.
/**
 * The Playtest Hub's contract for clients that run in a browser or a phone:
 * its types, the rules that work out health and status, the goal graph, the
 * Needs attention list and the run model. `ue-mcp/daemon` exports the same
 * declarations beside helpers that need Node (files, sockets); this entry
 * loads only the contract files, which import nothing but each other, so any
 * bundler can take it.
 *
 * ```ts
 * import { goalStatus, type GoalState } from "ue-mcp/hub";
 * ```
 *
 * Versioned with the daemon contract (DAEMON_API_VERSION).
 */
export * from "./contract/hub-qa.js";
export * from "./contract/hub.js";
export * from "./contract/hub-attention.js";
export * from "./contract/hub-arrangement.js";
export * from "./contract/hub-compose.js";
export * from "./contract/hub-set.js";
export * from "./contract/hub-performers.js";
export type * from "./contract/hub-ports.js";
