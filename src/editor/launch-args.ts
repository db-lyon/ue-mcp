/**
 * How an editor launch is shaped: which window mode it runs in, whether it may
 * open an IDE, and what extra arguments reach its command line.
 *
 * Pure: no I/O, no spawning, so the rules below are unit tested directly. The
 * launch itself lives in editor-control.ts, which hands the result of
 * buildEditorLaunchArgs to spawnOutsideTree.
 */
import type { UeMcpConfigRejection } from "../config/project.js";
import type { EditorProcess } from "./engine-observer.js";

/**
 * Points the editor's source code accessor at the null one, so nothing the
 * editor does (AddCodeToProject adds new files to the solution through the
 * accessor) can start an IDE. On Windows the null accessor's module does not
 * load, so the editor falls back to its default accessor, which opens nothing
 * either. The same string scripts/test-editor-args.mjs gives every test editor.
 */
export const NO_IDE_ARG = "-ini:EditorSettings:[/Script/SourceCodeAccess.SourceCodeAccessSettings]:PreferredAccessor=NullSourceCodeAccessor";

/**
 * Keeps the editor out of the background CPU throttle. An offscreen editor is
 * never the foreground window, and a throttled one ticks a few times a second,
 * so every call that waits on the game thread would wait for a tick. The same
 * string scripts/test-editor-args.mjs gives every test editor.
 */
export const NO_BACKGROUND_THROTTLE_ARG = "-ini:EditorSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False";

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
 *  NO_IDE_ARG, so the editor opens no IDE. */
export type EditorIdeMode = "allow" | "none";

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
 * A malformed `editor.window`, `editor.ide` or `editor.launchArgs` is dropped
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
    .filter((i) => /^(window|ide|launchArgs)(\.|$)/.test(i.path))
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
 * The editor's arguments: the project; when offscreen, -RenderOffScreen and
 * NO_BACKGROUND_THROTTLE_ARG; for `ide: none`, NO_IDE_ARG; the project's
 * configured `launchArgs`; then the call's own `extraArgs`.
 *
 * Each `launchArgs` item is one argument of a POSIX argv, and one piece of the
 * Windows command line exactly as written; the per-call string is split by
 * splitEditorArgs. A piece equal to an earlier one is dropped, so a project
 * whose `launchArgs` already holds a mode argument passes it once, at its
 * first position. Normal with the IDE allowed and no extras is exactly
 * [projectPath], which is what every launch did before this existed.
 * `keepQuotes` is for the Windows command line; see splitEditorArgs.
 */
export function buildEditorLaunchArgs(
  projectPath: string,
  opts: {
    window: EditorWindowMode;
    ide: EditorIdeMode;
    configLaunchArgs?: string[];
    extraArgs?: string;
    keepQuotes?: boolean;
  },
): string[] {
  const args = [projectPath];
  if (opts.window === "offscreen") args.push("-RenderOffScreen", NO_BACKGROUND_THROTTLE_ARG);
  if (opts.ide === "none") args.push(NO_IDE_ARG);
  args.push(...(opts.configLaunchArgs ?? []));
  if (opts.extraArgs) args.push(...splitEditorArgs(opts.extraArgs, { keepQuotes: opts.keepQuotes }));
  return [...new Set(args)];
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

/** Whether any of these pieces is NO_IDE_ARG, compared case-insensitively. */
export function hasNoIdeArg(args: string[]): boolean {
  const wanted = NO_IDE_ARG.toLowerCase();
  return args.some((arg) => arg.toLowerCase() === wanted);
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

/**
 * The window mode of a project's editor, read from its command line, or null
 * when no editor process is listed.
 *
 * The process read is the one whose pid is `snapshotPid` (the pid the plugin's
 * status snapshot names) when it is listed, else the first.
 */
export function windowModeOf(processes: EditorProcess[], snapshotPid?: number): EditorWindowMode | null {
  const editor = processes.find((p) => p.pid === snapshotPid) ?? processes[0];
  if (!editor) return null;
  return isOffscreenCommandLine(editor.commandLine) ? "offscreen" : "normal";
}
