// @session independent: Names and reads the process environment; one answer for the whole process.
/**
 * Every environment variable the server, its CLI, the editor plugin and the
 * repo's build scripts read, documented once. Values are read at call time,
 * never cached at module load, so a test or a caller that sets one between
 * calls is seen by the next read. tests/unit/env.test.ts fails on a read of an
 * unregistered name in src/, the plugin or scripts/build-utils.js.
 *
 * `readEnv` returns the raw value. A setting that can also come from a
 * project's ue-mcp.yml or from ~/.ue-mcp/state.json is declared in SETTINGS
 * below and read through `resolveSetting`, which owns the precedence and the
 * parsing so no call site restates them. A function that takes an `env`
 * argument for testing passes it through as `source`.
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
  /** Directory of the bridge's per-port session token files, read by the plugin too. Unset means ~/.ue-mcp/bridge. */
  bridgeTokenDir: "UE_MCP_BRIDGE_TOKEN_DIR",

  // ── Config and context ─────────────────────────────────────────────
  /** Selects the `ue-mcp.<env>.yml` overlay merged over each project's config. */
  env: "UE_MCP_ENV",
  /** Path of the user-global config layer. Unset means ~/.ue-mcp/config.yml. */
  globalConfig: "UE_MCP_GLOBAL_CONFIG",
  /** full, lean or micro; wins over `context.strategy` in ue-mcp.yml. */
  contextStrategy: "UE_MCP_CONTEXT_STRATEGY",
  /** debug, info, warn or error. Unset means info. */
  logLevel: "UE_MCP_LOG_LEVEL",
  /** Path of the per-user state file (hooks, the feedback, dialog and playtest modes, extension versions, the project list). */
  userState: "UE_MCP_USER_STATE",

  // ── Editor and engine ──────────────────────────────────────────────
  /** Engine root to build and launch with, over the project's association. */
  testEngineRoot: "UE_MCP_TEST_ENGINE_ROOT",
  /** Editor binary every session launches, over `editor.path`. */
  editorPath: "UE_EDITOR_PATH",
  /** Build tool every session compiles with, over `editor.buildToolPath`. */
  buildToolPath: "UE_BUILD_TOOL_PATH",
  /** Folder of Unreal's per-version user config, where its recent-project lists are. Unset means %LOCALAPPDATA%\UnrealEngine on Windows. */
  recentProjectsDir: "UE_MCP_RECENT_PROJECTS_DIR",
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
  /** Read by the plugin only: `auto` checks a file out of source control before a handler writes it. */
  checkoutMode: "UE_MCP_CHECKOUT_MODE",
  /** off turns off the scan for native Win32 dialogs the editor process shows. Unset means on, on Windows. */
  nativeDialogs: "UE_MCP_NATIVE_DIALOGS",
  /** JSON file of native dialog windows that stands in for the window scan, on any platform, for tests. */
  nativeDialogFixture: "UE_MCP_NATIVE_DIALOG_FIXTURE",

  // ── Daemon ─────────────────────────────────────────────────────────
  /** daemon or in-process; wins over `server.mode` in ue-mcp.yml. Unset means daemon. */
  serverMode: "UE_MCP_SERVER_MODE",
  /** Directory of daemon discovery files. Unset means ~/.ue-mcp/daemons. */
  daemonDir: "UE_MCP_DAEMON_DIR",
  /** enforce or warn: whether an extension needing a newer ue-mcp is refused or loaded with a warning. Unset means enforce; warn is for development daemons. */
  extensionFloor: "UE_MCP_EXTENSION_FLOOR",
  /** How long a daemon with no editor, client or stream attached waits before exiting, in milliseconds. */
  daemonIdleMs: "UE_MCP_DAEMON_IDLE_MS",
  /** The daemon session id of the agent that started this client (a subagent's primary), sent as x-ue-mcp-parent so the subagent is attributed to it. */
  parentSession: "UE_MCP_PARENT_SESSION",
  /** Directory daemon extensions are loaded from. Unset means ~/.ue-mcp/extensions. */
  extensionsDir: "UE_MCP_EXTENSIONS_DIR",
  /** JSON file of the names `ue-mcp install <name>` resolves, replacing the built-in list. */
  installIndex: "UE_MCP_INSTALL_INDEX",

  // ── Flow HTTP surface ──────────────────────────────────────────────
  /** Bearer token for the flow HTTP server, over a freshly generated one. */
  httpToken: "UE_MCP_HTTP_TOKEN",
  /** Model a flow agent step uses when it names none and no extension provides one. */
  flowModel: "UE_MCP_FLOW_MODEL",
  /** API key a flow agent step falls back to when no extension registered a model provider. */
  anthropicApiKey: "ANTHROPIC_API_KEY",
  /** Base URL for that fallback provider. */
  anthropicBaseUrl: "ANTHROPIC_BASE_URL",

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
  /** Origin of GitHub's OAuth device flow. Unset means https://github.com. */
  githubOAuth: "UE_MCP_GITHUB_OAUTH",
  /** Origin of the GitHub REST API. Unset means https://api.github.com. */
  githubApi: "UE_MCP_GITHUB_API",
  /** Origin of the plugin registry. Unset means https://plugins.ue-mcp.com. */
  registry: "UE_MCP_REGISTRY",
  /** Origin of the account sign-in endpoints `ue-mcp login` calls. Unset means https://accounts.ue-mcp.com. */
  authUrl: "UE_MCP_AUTH_URL",
  /** Path of the cached registry catalog. */
  registryCache: "UE_MCP_REGISTRY_CACHE",
  /** Registry publish token for CI, over the one `ue-mcp login` cached. */
  publishToken: "UE_MCP_PUBLISH_TOKEN",
  /** The older name of UE_MCP_PUBLISH_TOKEN, still read after it. */
  registryPublishToken: "REGISTRY_PUBLISH_TOKEN",
  /** minServerVersion written into a new plugin scaffold. Unset means this version. */
  pluginMinServer: "UE_MCP_PLUGIN_MIN_SERVER",
  /** Prebuilt bridge binaries: a plugin directory, a .zip, a directory of archives, or a URL base. */
  binaries: "UE_MCP_BINARIES",
  /** Origin of the npm registry the release lookups read. Unset means https://registry.npmjs.org. */
  npmRegistry: "UE_MCP_NPM_REGISTRY",
  /** `1` skips the npm registry check for a newer release. */
  disableUpdateCheck: "UE_MCP_DISABLE_UPDATE_CHECK",
  /** `0` turns off the Playtest Hub's own headless runner, so every run plays on the open editor. */
  hubHeadless: "UE_MCP_HUB_HEADLESS",
  /** Path of the cached update-check result. */
  versionCache: "UE_MCP_VERSION_CACHE",

  // ── Repo build scripts (scripts/build-utils.js), not the shipped package ──
  /** Engine root the repo's build scripts use after UE_MCP_TEST_ENGINE_ROOT. */
  ueRoot: "UE_ROOT",
  /** `true` lets a test build write into the engine tree (it passes -NoEngineChanges otherwise). */
  allowTestEngineChanges: "UE_MCP_ALLOW_TEST_ENGINE_CHANGES",
} as const;

export type EnvVar = keyof typeof ENV_VARS;

/** The raw value of one variable, read now. */
export function readEnv(name: EnvVar, source: NodeJS.ProcessEnv = process.env): string | undefined {
  return source[ENV_VARS[name]];
}

// ── Settings with more than one source ───────────────────────────────

/** The values each scoped mode takes. config/modes.ts builds the modes from these. */
export const FEEDBACK_MODES = ["interactive", "auto-approve", "defer"] as const;
export const DIALOG_MODES = ["interactive", "auto", "defer"] as const;
export const PLAYTEST_MODES = ["manual", "automatic"] as const;

/**
 * Where a setting's value came from, highest precedence first:
 *
 *   explicit  an argument the caller passed (a constructor arg, a CLI flag)
 *   env       the environment variable: one value for the whole process
 *   config    the project's ue-mcp.yml cascade (see configLayerFiles)
 *   project   ~/.ue-mcp/state.json, this project's entry
 *   user      ~/.ue-mcp/state.json, the per-user preference
 *   default   nothing was set
 *
 * No setting reads both config and state.json, so their relative order never
 * decides anything; it is fixed here so it cannot be decided per call site.
 */
export type SettingSource = "explicit" | "env" | "config" | "project" | "user" | "default";

export interface Setting<T> {
  env?: EnvVar;
  /** Dotted key in ue-mcp.yml. */
  config?: string;
  /** Key in ~/.ue-mcp/state.json, per project and per user. */
  state?: string;
  default?: T;
  /** A raw value from any layer, or undefined when it is empty or not a value of this setting. */
  parse(raw: unknown): T | undefined;
}

function positiveInt(raw: unknown): number | undefined {
  const n = typeof raw === "number" ? raw : typeof raw === "string" ? Number.parseInt(raw, 10) : Number.NaN;
  return Number.isFinite(n) && n > 0 ? Math.trunc(n) : undefined;
}

function nonEmpty(raw: unknown): string | undefined {
  return typeof raw === "string" && raw.trim() !== "" ? raw.trim() : undefined;
}

/** Trimmed and case-folded, in every layer: " AUTO " names auto in the env var and in state.json alike. */
function oneOf<V extends string>(values: readonly V[]): (raw: unknown) => V | undefined {
  return (raw) => {
    const named = typeof raw === "string" ? raw.trim().toLowerCase() : undefined;
    return named !== undefined && (values as readonly string[]).includes(named) ? (named as V) : undefined;
  };
}

export const SETTINGS = {
  /** No default here: a project with no pin derives its port from its root (deriveProjectPort). */
  bridgePort: { env: "port", config: "bridge.port", parse: positiveInt } as Setting<number>,
  bridgeHost: { env: "host", config: "bridge.host", default: "127.0.0.1", parse: nonEmpty } as Setting<string>,
  editorPath: { env: "editorPath", config: "editor.path", parse: nonEmpty } as Setting<string>,
  buildToolPath: { env: "buildToolPath", config: "editor.buildToolPath", parse: nonEmpty } as Setting<string>,
  feedbackMode: {
    env: "feedbackMode",
    state: "feedback.mode",
    default: "interactive",
    parse: oneOf(FEEDBACK_MODES),
  } as Setting<(typeof FEEDBACK_MODES)[number]>,
  /** No default here: it depends on whether the client can be elicited (resolveDialogMode). */
  dialogMode: { env: "dialogMode", state: "dialog.mode", parse: oneOf(DIALOG_MODES) } as Setting<(typeof DIALOG_MODES)[number]>,
  /** No env var and no default: unset leaves the choice to each client. */
  playtestMode: { state: "playtest.mode", parse: oneOf(PLAYTEST_MODES) } as Setting<(typeof PLAYTEST_MODES)[number]>,
};

export type SettingName = keyof typeof SETTINGS;
type SettingValue<K extends SettingName> = (typeof SETTINGS)[K] extends Setting<infer T> ? T : never;

export interface ResolvedSetting<T> {
  /** Undefined only when nothing is set and the setting declares no default. */
  value: T | undefined;
  source: SettingSource;
  /** The env var's raw value when it was set but named no value of this setting, so it was skipped. */
  ignoredEnv?: string;
}

/**
 * One setting's value and where it came from, with the one precedence above.
 * The caller hands over the layers it has read (ue-mcp.yml, state.json); this
 * reads the environment itself. A layer whose value does not parse is skipped.
 */
export function resolveSetting<K extends SettingName>(
  name: K,
  layers: { explicit?: unknown; config?: unknown; project?: unknown; user?: unknown } = {},
  source: NodeJS.ProcessEnv = process.env,
): ResolvedSetting<SettingValue<K>> {
  const setting = SETTINGS[name] as Setting<SettingValue<K>>;
  const explicit = setting.parse(layers.explicit);
  if (explicit !== undefined) return { value: explicit, source: "explicit" };
  const rawEnv = setting.env ? readEnv(setting.env, source) : undefined;
  const fromEnv = setting.parse(rawEnv);
  if (fromEnv !== undefined) return { value: fromEnv, source: "env" };
  const ignoredEnv = rawEnv !== undefined && rawEnv.trim() !== "" ? rawEnv.trim() : undefined;
  const withIgnored = <R extends ResolvedSetting<SettingValue<K>>>(r: R): R => (ignoredEnv !== undefined ? { ...r, ignoredEnv } : r);
  for (const layer of ["config", "project", "user"] as const) {
    const value = setting.parse(layers[layer]);
    if (value !== undefined) return withIgnored({ value, source: layer });
  }
  return withIgnored({ value: setting.default, source: "default" });
}

/** The variable name a setting reads, for messages. */
export function settingEnvName(name: SettingName): string | undefined {
  const env = SETTINGS[name].env;
  return env ? ENV_VARS[env] : undefined;
}
