/**
 * Every UE_MCP_* environment variable the server and its CLI read, documented
 * once. Values are read at call time, never cached at module load, so a test
 * or a caller that sets one between calls is seen by the next read.
 *
 * `readEnv` returns the raw value; each call site keeps its own rule for an
 * empty or malformed one. A function that takes an `env` argument for testing
 * passes it through as `source`.
 */

export const ENV_VARS = {
  // ── Bridge ─────────────────────────────────────────────────────────
  /** Bridge port for every session, over the per-project derived port. */
  port: "UE_MCP_PORT",
  /** Host every session's bridge is reached on. Unset means 127.0.0.1. */
  host: "UE_MCP_HOST",
  /** Floor under every bridge call's timeout, in milliseconds. */
  bridgeTimeoutMs: "UE_MCP_BRIDGE_TIMEOUT_MS",
  /** `1` refuses a call with a parameter the action does not forward, instead of warning. */
  strictParams: "UE_MCP_STRICT_PARAMS",

  // ── Config and context ─────────────────────────────────────────────
  /** Selects the `ue-mcp.<env>.yml` overlay merged over each project's config. */
  env: "UE_MCP_ENV",
  /** Path of the user-global config layer. Unset means ~/.ue-mcp/config.yml. */
  globalConfig: "UE_MCP_GLOBAL_CONFIG",
  /** full, lean or micro; wins over `context.strategy` in ue-mcp.yml. */
  contextStrategy: "UE_MCP_CONTEXT_STRATEGY",
  /** debug, info, warn or error. Unset means info. */
  logLevel: "UE_MCP_LOG_LEVEL",
  /** Path of the per-user state file (hooks, feedback and dialog modes). */
  userState: "UE_MCP_USER_STATE",

  // ── Editor and engine ──────────────────────────────────────────────
  /** Engine root to build and launch with, over the project's association. */
  testEngineRoot: "UE_MCP_TEST_ENGINE_ROOT",
  /** Engine roots the build and launch helpers refuse to touch, separated by `;`. */
  protectedEngineRoots: "UE_MCP_PROTECTED_ENGINE_ROOTS",
  /** Cap on UBT's parallel compile actions for `build`. */
  maxParallelActions: "UE_MCP_MAX_PARALLEL_ACTIONS",
  /** interactive, auto or defer; wins over the stored dialog mode. */
  dialogMode: "UE_MCP_DIALOG_MODE",
  /** on or off: whether a dialog is handed over as text before the approval form, whatever the client. */
  dialogRelay: "UE_MCP_DIALOG_RELAY",
  /** Set on a launched editor so the bridge applies a dialog policy at startup. */
  dialogPolicy: "UE_MCP_DIALOG_POLICY",
  /** Set on a launched editor so the bridge echoes the parameters it received. */
  paramEcho: "UE_MCP_PARAM_ECHO",

  // ── Flow HTTP surface ──────────────────────────────────────────────
  /** Bearer token for the flow HTTP server, over a freshly generated one. */
  httpToken: "UE_MCP_HTTP_TOKEN",

  // ── Feedback ───────────────────────────────────────────────────────
  /** interactive, defer or auto-approve; wins over the stored feedback mode. */
  feedbackMode: "UE_MCP_FEEDBACK_MODE",
  /** off (or 0, false, no) files every report against the core tracker. */
  feedbackRouting: "UE_MCP_FEEDBACK_ROUTING",
  /** Origin of the feedback service. Unset means https://feedback.ue-mcp.com. */
  feedback: "UE_MCP_FEEDBACK",
  /** Full URL feedback is signed at, over the ones derived from the service and registry origins. */
  feedbackEndpoint: "UE_MCP_FEEDBACK_ENDPOINT",
  /** Directory of deferred feedback. Unset means ~/.ue-mcp/pending-feedback. */
  pendingDir: "UE_MCP_PENDING_DIR",
  /** Fastest a person can answer an approval form, in milliseconds. 0 disables the check. */
  elicitMinHumanMs: "UE_MCP_ELICIT_MIN_HUMAN_MS",

  // ── Auth, registry and updates ─────────────────────────────────────
  /** Directory holding the GitHub and registry tokens. Unset means ~/.ue-mcp. */
  authDir: "UE_MCP_AUTH_DIR",
  /** GitHub OAuth app used for the device flow. */
  oauthClientId: "UE_MCP_OAUTH_CLIENT_ID",
  /** Origin of the plugin registry. Unset means https://plugins.ue-mcp.com. */
  registry: "UE_MCP_REGISTRY",
  /** Path of the cached registry catalog. */
  registryCache: "UE_MCP_REGISTRY_CACHE",
  /** Registry publish token for CI, over the one `ue-mcp login` cached. */
  publishToken: "UE_MCP_PUBLISH_TOKEN",
  /** minServerVersion written into a new plugin scaffold. Unset means this version. */
  pluginMinServer: "UE_MCP_PLUGIN_MIN_SERVER",
  /** `1` skips the npm registry check for a newer release. */
  disableUpdateCheck: "UE_MCP_DISABLE_UPDATE_CHECK",
  /** Path of the cached update-check result. */
  versionCache: "UE_MCP_VERSION_CACHE",
} as const;

export type EnvVar = keyof typeof ENV_VARS;

/** The raw value of one variable, read now. */
export function readEnv(name: EnvVar, source: NodeJS.ProcessEnv = process.env): string | undefined {
  return source[ENV_VARS[name]];
}
