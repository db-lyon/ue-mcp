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
 * are additive.
 */
export { DAEMON_API_VERSION, readDiscovery, readLiveDiscovery, discoveryPath, type DaemonDiscovery } from "./daemon/discovery.js";
export {
  EXTENSION_API_VERSION,
  type ActionResult,
  type DaemonExtension,
  type DaemonExtensionApi,
  type DaemonHealth,
  type EditorStatus,
  type ExtensionHandler,
  type ExtensionRequest,
  type ExtensionResponse,
} from "./daemon/extension-api.js";
export type { DaemonEvent } from "./daemon/events.js";
export type { DisconnectCause } from "./daemon/disconnect.js";
export { signBundle, verifyBundle, type UiManifest } from "./daemon/ui-bundles.js";
export type { StatusReport, ClientStatus } from "./cli/status.js";
