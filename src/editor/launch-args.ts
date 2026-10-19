/**
 * How an editor launch is shaped: which window mode it runs in, and what extra
 * arguments reach its command line.
 *
 * Pure: no I/O, no spawning, so the rules below are unit tested directly. The
 * launch itself lives in editor-control.ts, which hands the result of
 * buildEditorLaunchArgs to spawnOutsideTree.
 */
import type { UeMcpConfigRejection } from "../config/project.js";
import type { EditorProcess } from "./engine-observer.js";

/** `normal` shows the editor as it always has; `offscreen` passes
 *  -RenderOffScreen, which puts no editor window of any kind on screen. */
export type EditorWindowMode = "normal" | "offscreen";

/**
 * Which mode a launch uses: the per-call parameter, then the project's
 * `editor.window` in ue-mcp.yml, then today's behaviour.
 */
export function resolveWindowMode(
  perCall: EditorWindowMode | undefined,
  config: EditorWindowMode | undefined,
): EditorWindowMode {
  return perCall ?? config ?? "normal";
}

/** `allow` lets the editor open an IDE as it always has; `none` passes
 *  -MCPNoIde, which tells the bridge to stop the editor opening one and to
 *  hide the Live Coding console after each compile. */
export type EditorIdeMode = "allow" | "none";

/** The bridge switch an `ide: none` launch passes. */
const NO_IDE_SWITCH = "-MCPNoIde";

/**
 * Whether a launch may open an IDE: the per-call parameter, then the
 * project's `editor.ide` in ue-mcp.yml, then the window mode's default. An
 * offscreen editor has nobody at the screen to use an IDE, so it defaults to
 * `none`; a normal one keeps today's behaviour.
 */
export function resolveIdeMode(
  perCall: EditorIdeMode | undefined,
  config: EditorIdeMode | undefined,
  windowMode: EditorWindowMode,
): EditorIdeMode {
  return perCall ?? config ?? (windowMode === "offscreen" ? "none" : "allow");
}

/** The pieces of a command line, and whether a double quote was left open. */
function tokenize(text: string, keepQuotes: boolean): { args: string[]; unmatched: boolean } {
  const args: string[] = [];
  let current = "";
  let started = false;
  let quoted = false;
  for (const char of text) {
    if (char === "\"") {
      quoted = !quoted;
      started = true;
      if (keepQuotes) current += char;
      continue;
    }
    if (!quoted && /\s/.test(char)) {
      if (started) {
        args.push(current);
        current = "";
        started = false;
      }
      continue;
    }
    current += char;
    started = true;
  }
  if (started) args.push(current);
  return { args, unmatched: quoted };
}

/**
 * Why a launch must not go ahead on the project's config, or null.
 *
 * A malformed `editor.window`, `editor.ide` or `editor.extraArgs` is dropped
 * from the config like any other bad key, and a launch that carried on would
 * quietly start a different editor from the one asked for: on screen instead
 * of offscreen, free to open an IDE, or without its switches. So those three
 * refuse the launch instead, naming what to fix. Other rejections of the
 * `editor` key do not stop a launch.
 */
export function launchConfigRefusal(rejections: UeMcpConfigRejection[]): string | null {
  const problems = rejections
    .filter((r) => r.key === "editor")
    .flatMap((r) => r.issues)
    .filter((i) => /^(window|ide|extraArgs)(\.|$)/.test(i.path))
    .map((i) => `editor.${i.path}: ${i.message}`);
  if (problems.length === 0) return null;
  return (
    `ue-mcp.yml has an editor launch setting that is not valid, so no editor was started: ${problems.join("; ")}. ` +
    "Fix or remove it in ue-mcp.yml (or the local, env or global layer that sets it)."
  );
}

/**
 * Split an `extraArgs` string into the arguments it names.
 *
 * Whitespace separates arguments outside double quotes; a quoted run belongs
 * to the argument it sits in, and may continue an unquoted run
 * (`-ini:X:[S]:K="v w"` is one argument). Unreal parses switches by word, so
 * an argument that held a space has to stay one argument.
 *
 * `keepQuotes` leaves the quote characters in each piece, for the Windows
 * command line, where Unreal reads the raw text and skips anything quoted:
 * `-ExecCmds="stat fps"` has to arrive exactly so. Without it the quotes are
 * removed, for a POSIX argv.
 */
export function splitEditorArgs(text: string, opts: { keepQuotes?: boolean } = {}): string[] {
  const { args, unmatched } = tokenize(text, opts.keepQuotes === true);
  if (unmatched) throw new Error(`Unmatched double quote in editor arguments: ${text}`);
  return args;
}

/**
 * The editor's arguments: the project, the offscreen switch when asked for,
 * the no-IDE switch when asked for, the project's configured extras, then the
 * call's own.
 *
 * A configured string is split the same way a per-call one is; a configured
 * list is taken item by item, unsplit. Each list item is one argument of a
 * POSIX argv, and one piece of the Windows command line exactly as written.
 * Normal with the IDE allowed and no extras is exactly [projectPath], which is
 * what every launch did before this existed. `keepQuotes` is for the Windows
 * command line; see splitEditorArgs.
 */
export function buildEditorLaunchArgs(
  projectPath: string,
  opts: {
    window: EditorWindowMode;
    ide: EditorIdeMode;
    configExtraArgs?: string | string[];
    extraArgs?: string;
    keepQuotes?: boolean;
  },
): string[] {
  const split = { keepQuotes: opts.keepQuotes };
  const args = [projectPath];
  if (opts.window === "offscreen") args.push("-RenderOffScreen");
  if (opts.ide === "none") args.push(NO_IDE_SWITCH);
  const configured = opts.configExtraArgs;
  if (typeof configured === "string") {
    try {
      args.push(...splitEditorArgs(configured, split));
    } catch (error) {
      throw new Error(`ue-mcp.yml editor.extraArgs: ${error instanceof Error ? error.message : String(error)}`);
    }
  } else if (Array.isArray(configured)) {
    args.push(...configured);
  }
  if (opts.extraArgs) args.push(...splitEditorArgs(opts.extraArgs, split));
  return args;
}

/**
 * The Windows command line for buildEditorLaunchArgs' result (keepQuotes on):
 * the project path in double quotes, every other piece exactly as given.
 *
 * Spawned with `windowsVerbatimArguments`, so nothing quotes it again. Node's
 * own quoting would wrap a piece holding a space whole, `"-ExecCmds=stat fps"`,
 * and Unreal skips quoted text when it looks for a switch.
 */
export function renderWindowsCommandLine(args: string[]): string {
  const [projectPath, ...rest] = args;
  return [`"${projectPath}"`, ...rest].join(" ");
}

/**
 * Whether any of these pieces is the -RenderOffScreen switch.
 *
 * Unreal takes a `-` or `/` prefix and matches the name case-insensitively and
 * exactly, so -RenderOffScreenX and -RenderOffScreen=1 do not count. A piece
 * that still carries quotes does not either: Unreal skips quoted text when it
 * looks for a switch.
 */
export function hasOffscreenSwitch(args: string[]): boolean {
  return args.some((arg) => /^[-/]renderoffscreen$/i.test(arg));
}

/**
 * Whether any of these pieces is the -MCPNoIde switch, matched the way the
 * bridge reads it: a `-` or `/` prefix, the name case-insensitively and
 * exactly, and nothing quoted. See hasOffscreenSwitch.
 */
export function hasNoIdeSwitch(args: string[]): boolean {
  return args.some((arg) => /^[-/]mcpnoide$/i.test(arg));
}

/**
 * Whether a command line carries -RenderOffScreen as a switch of its own.
 *
 * Split the way the command line is grouped, double quotes included, so a
 * quoted path that merely contains the text does not count. An unclosed quote
 * runs to the end of the line.
 */
export function isOffscreenCommandLine(commandLine: string | null | undefined): boolean {
  if (!commandLine) return false;
  return hasOffscreenSwitch(tokenize(commandLine, true).args);
}

/** Where a reported window mode came from. */
export interface WindowModeReport {
  mode: EditorWindowMode;
  source: "bridge" | "commandLine";
}

/**
 * The window mode of a project's editor, or null when there is nothing to read
 * it from.
 *
 * The bridge's own snapshot is the strongest evidence: the flag is a fact
 * about the running process, not a guess from its text. It counts only while
 * that process runs: served over the bridge just now, or read from
 * status.json with a pid the process table still holds. The file outlives its
 * editor, and a dead editor's mode says nothing about the next one. Otherwise
 * the command line the process table holds answers the same question (the
 * snapshot's process when it is listed, else the first), and it works while
 * the bridge is down or the plugin is too old to publish the field.
 */
export function windowModeOf(state: {
  snapshot: unknown;
  processes: EditorProcess[];
  snapshotSource: "bridge" | "status.json" | "none";
}): WindowModeReport | null {
  const snapshot = state.snapshot && typeof state.snapshot === "object"
    ? (state.snapshot as { pid?: unknown; renderOffScreen?: unknown })
    : {};
  const described = typeof snapshot.pid === "number"
    ? state.processes.find((p) => p.pid === snapshot.pid)
    : undefined;
  const live = state.snapshotSource === "bridge" || described !== undefined;
  if (live && typeof snapshot.renderOffScreen === "boolean") {
    return { mode: snapshot.renderOffScreen ? "offscreen" : "normal", source: "bridge" };
  }
  const editor = described ?? state.processes[0];
  if (!editor) return null;
  return { mode: isOffscreenCommandLine(editor.commandLine) ? "offscreen" : "normal", source: "commandLine" };
}
