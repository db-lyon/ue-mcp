// @session independent: The public ue-mcp/daemon entry point: the contract, and helpers that read its records.
/**
 * Public surface for building on the ue-mcp daemon: the HTTP and event types a
 * UI reads, the extension contract an in-daemon extension implements, and the
 * helpers to find a project's daemon and to sign a UI bundle.
 *
 * ```ts
 * import type { DaemonExtension } from "ue-mcp/daemon";
 *
 * const extension: DaemonExtension = {
 *   activate(api) {
 *     api.route("GET", "/hello", () => ({ body: { editors: api.editors() } }));
 *   },
 * };
 * export default extension;
 * ```
 *
 * Versioned by DAEMON_API_VERSION (HTTP, events, shim) and
 * EXTENSION_API_VERSION (the extension contract). Within one version, changes
 * are additive. Everything contract/daemon.ts declares is public; the helpers
 * below take and return only its types.
 */
export * from "./contract/daemon.js";
export * from "./contract/hub.js";
export * from "./contract/hub-performers.js";
export * from "./contract/hub-ports.js";
export { readDiscovery, readLiveDiscovery, discoveryPath } from "./daemon/discovery.js";
export { parseContextMeasurement, scopeBucket } from "./surface/context/context-measurement.js";
export { signBundle, verifyBundle } from "./daemon/ui-bundles.js";
