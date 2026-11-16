/**
 * The engine version each Epic integration behaviour starts at, in one table.
 *
 * The integrations used to compare the project's engine against numbers typed
 * at each call site, so a feature that moved engines had to be found in three
 * files. A behaviour is named here once, with the first engine that has it, and
 * every branch asks `engineHas(feature, version)`.
 */

/** Major.minor as a comparable number pair, or null. */
export function parseEngineVersion(v: string | null | undefined): [number, number] | null {
  const m = v ? /^(\d+)\.(\d+)/.exec(v) : null;
  return m ? [Number(m[1]), Number(m[2])] : null;
}

/** Whether `v` is at least `major.minor`. Null when the version is unknown. */
export function engineAtLeast(v: string | null | undefined, major: number, minor: number): boolean | null {
  const p = parseEngineVersion(v);
  if (!p) return null;
  return p[0] > major || (p[0] === major && p[1] >= minor);
}

/** The first engine version that has each behaviour. */
export const EPIC_ENGINE_FEATURES = {
  /** Epic's Unreal MCP server, the ModelContextProtocol plugin. */
  unrealMcpServer: "5.8",
  /**
   * The server's settings class moved out of ModelContextProtocolEngine, and
   * the engine's Base ini turns bAutoStartServer on.
   */
  unrealMcpServerAutoStart: "5.9",
  /** The Toolset Registry and its BlockedNames / AllowedNames settings. */
  toolsetRegistry: "5.8",
  /** Toolset Registry settings saved in the project's DefaultGame.ini rather than per user. */
  toolsetSettingsInProject: "5.9",
  /** Unreal Assistant reading MCP servers from its settings. */
  unrealAssistantMcp: "5.9",
} as const;

export type EpicEngineFeature = keyof typeof EPIC_ENGINE_FEATURES;

/** Whether engine `v` has `feature`. Null when the version is unknown. */
export function engineHas(feature: EpicEngineFeature, v: string | null | undefined): boolean | null {
  const [major, minor] = parseEngineVersion(EPIC_ENGINE_FEATURES[feature])!;
  return engineAtLeast(v, major, minor);
}
