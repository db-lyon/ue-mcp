// @session per-session: Each mode is stored per project root as well as per user; the project's own wins.
import {
  DIALOG_MODES,
  FEEDBACK_MODES,
  PLAYTEST_MODES,
  SETTINGS,
  readEnv,
  resolveSetting,
  settingEnvName,
  type ResolvedSetting,
} from "../core/env.js";
import { ensureProjectEntry, projectEntry, readState, updateState, type UserState } from "./state-store.js";

/**
 * The per-user preferences that can also be scoped to one project: who
 * approves feedback, how a blocking dialog is handled, who starts playtests.
 * Each is one instance of a scoped mode: its values (declared in
 * core/env.ts SETTINGS), where it is stored in ~/.ue-mcp/state.json, and the
 * words `ue-mcp <name> mode` prints (cli/mode-cli.ts renders every one).
 *
 * None is read from ue-mcp.yml: whether a person is at the keyboard is a
 * property of the machine and the session, not project policy a collaborator
 * should inherit. With more than one editor, one can be a long unattended run
 * while the user sits in front of another, which is what the project scope is
 * for (#817).
 */

export type FeedbackMode = (typeof FEEDBACK_MODES)[number];

/**
 * interactive: the dialog is put to the user in an elicitation form and only
 * the button they pick is pressed. auto: the dialog is handed to the agent to
 * answer. defer: nothing is pressed or asked; the user answers it in the
 * editor. Only auto accepts editor(respond_to_dialog) from an agent; that rule
 * lives in DialogGuard.handsOverPressCalls.
 */
export type DialogMode = (typeof DIALOG_MODES)[number];

/** Who starts playtest runs (pie script_run and script_replay); playtest-mode.ts enforces manual. */
export type PlaytestMode = (typeof PLAYTEST_MODES)[number];

/** The words that clear a mode, in every mode's CLI. The first is the one help shows. */
export const CLEAR_WORDS = ["clear", "default", "unset"] as const;

export interface ScopedModeDef<M extends string> {
  /** The state.json key and the CLI command: `ue-mcp <name> mode`. */
  name: "feedback" | "dialog" | "playtest";
  setting: "feedbackMode" | "dialogMode" | "playtestMode";
  values: readonly M[];
  cli: {
    /** "Feedback mode": starts the confirmation lines. */
    title: string;
    /** One line under the command name in help. */
    summary: string;
    /** One help line per value. */
    describe: Record<M, string>;
    /** What applies when nothing is set, as `effective:` prints it. */
    unsetEffective: string;
    /** After an unset per-user preference. */
    unsetPreference: string;
    /** What applies once the per-user preference is cleared. */
    afterClear: string;
    /** Printed after setting a mode that hands a person's decision to something else. */
    caution?: Partial<Record<M, string>>;
  };
}

export interface ScopedMode<M extends string> {
  readonly def: ScopedModeDef<M>;
  /** The variable that outranks the stored modes, when the mode has one. */
  readonly envName: string | undefined;
  /** That variable's raw value, read now; undefined when unset or the mode has none. */
  envValue(env?: NodeJS.ProcessEnv): string | undefined;
  /** A raw value, trimmed and case-folded, or undefined when it names no mode. */
  parse(raw: unknown): M | undefined;
  /** What each scope holds, without the fallback, for a display. */
  scopes(projectRoot?: string | null): { project?: M; user?: M };
  /** The stored mode that applies: the project's own, else the user's. Ignores the env var. */
  stored(projectRoot?: string | null): M | undefined;
  /** Set (or with undefined, clear) the project's mode, or the user's when projectRoot is absent. */
  set(mode: M | undefined, projectRoot?: string | null): void;
  /** The mode that applies with the env var counted, and where it came from. */
  resolve(projectRoot?: string | null, env?: NodeJS.ProcessEnv): ResolvedSetting<M>;
}

function defineScopedMode<M extends string>(def: ScopedModeDef<M>): ScopedMode<M> {
  const parse = (raw: unknown): M | undefined => {
    const named = typeof raw === "string" ? raw.trim().toLowerCase() : undefined;
    return named !== undefined && (def.values as readonly string[]).includes(named) ? (named as M) : undefined;
  };
  const modeIn = (holder: unknown): unknown => (holder as { mode?: unknown } | undefined)?.mode;
  const scopesIn = (state: UserState, projectRoot?: string | null): { project?: M; user?: M } => ({
    project: projectRoot ? parse(modeIn(projectEntry(state, projectRoot)?.[def.name])) : undefined,
    user: parse(modeIn(state.preferences?.[def.name])),
  });
  const envKey = SETTINGS[def.setting].env;
  return {
    def,
    envName: settingEnvName(def.setting),
    envValue: (env) => (envKey ? readEnv(envKey, env) : undefined),
    parse,
    scopes: (projectRoot) => scopesIn(readState(), projectRoot),
    stored(projectRoot) {
      const s = scopesIn(readState(), projectRoot);
      return s.project ?? s.user;
    },
    set(mode, projectRoot) {
      // Stored in the canonical spelling, so the file never holds one only a lenient reader accepts.
      const value = mode === undefined ? undefined : parse(mode) ?? mode;
      updateState((state) => {
        const holder = projectRoot ? ensureProjectEntry(state, projectRoot) : (state.preferences ??= {});
        if (value === undefined) delete holder[def.name];
        else holder[def.name] = { mode: value };
      });
    },
    resolve(projectRoot, env) {
      const s = scopesIn(readState(), projectRoot);
      return resolveSetting(def.setting, s, env) as ResolvedSetting<M>;
    },
  };
}

export const FEEDBACK_MODE = defineScopedMode<FeedbackMode>({
  name: "feedback",
  setting: "feedbackMode",
  values: FEEDBACK_MODES,
  cli: {
    title: "Feedback mode",
    summary: "Who approves a feedback report before it is filed.",
    describe: {
      interactive: "each report is put to you in an MCP elicitation form before it is filed",
      "auto-approve": "reports are filed without asking",
      defer: "reports are queued on disk for `ue-mcp feedback review`; nothing is filed",
    },
    unsetEffective: "interactive",
    unsetPreference: "default interactive",
    afterClear: 'The default decides again: "interactive".',
  },
});

export const DIALOG_MODE = defineScopedMode<DialogMode>({
  name: "dialog",
  setting: "dialogMode",
  values: DIALOG_MODES,
  cli: {
    title: "Dialog mode",
    summary: "How a modal dialog blocking the Unreal Editor is handled.",
    describe: {
      interactive: "the dialog is put to you in an MCP elicitation form; only the button you pick is pressed",
      auto: "the dialog is handed back whole and the agent answers it; the server presses nothing",
      defer: "nothing is pressed and nothing is asked; you answer it in the editor yourself",
    },
    unsetEffective: "interactive when the connected MCP client advertises elicitation, otherwise defer",
    unsetPreference: "the default decides",
    afterClear: "The default decides again: interactive with elicitation, defer without. Never auto.",
    caution: {
      auto:
        "In auto mode a blocking dialog is handed to the agent to answer, including save prompts. " +
        "Nothing is pressed by the server, but the person at the keyboard is no longer the one deciding.",
    },
  },
});

export const PLAYTEST_MODE = defineScopedMode<PlaytestMode>({
  name: "playtest",
  setting: "playtestMode",
  values: PLAYTEST_MODES,
  cli: {
    title: "Playtest mode",
    summary: "Who starts playtest runs (pie script_run and script_replay).",
    describe: {
      manual: "a person starts playtests; an MCP agent's script_run and script_replay are refused",
      automatic: "an agent may start them",
    },
    unsetEffective: "not set (each client decides)",
    unsetPreference: "each client decides",
    afterClear: "Each client decides again.",
  },
});

export const SCOPED_MODES = [FEEDBACK_MODE, DIALOG_MODE, PLAYTEST_MODE] as const;
