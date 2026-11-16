// @session per-session: Turns --editor into the project a one-shot command acts on.
/**
 * `--editor <name-or-path>` for the one-shot CLI subcommands (#817, plan 6.2).
 *
 * `deploy`, `build`, `init`, `plugin`, `context`, `doctor`, `resolve`,
 * `update`, `feedback` and `uninstall-hooks` each run in their own process
 * with no session registry. The name is resolved from the MCP client configs
 * by integrations/mcp-clients/configured-editors.ts; this file reads the flag.
 *
 * Commands pick their project through selectProject, which puts --editor,
 * --project and a positional in one order for all of them.
 */
import * as path from "node:path";
import { isUProjectPath, projectDirOf } from "../config/uproject-path.js";
import { EDITOR_FLAG, EditorFlagError, resolveEditorFlag } from "../integrations/mcp-clients/configured-editors.js";
import { fail } from "../terminal/ansi.js";
import { firstPositional } from "./args.js";

export interface EditorFlagParse {
  /** The value passed to --editor, or undefined when the flag was absent. */
  editor?: string;
  /** argv with the flag and its value removed, so existing parsing is untouched. */
  rest: string[];
}

/**
 * Pull `--editor <value>` (or `--editor=<value>`) out of an argument list.
 *
 * Every other argument is returned untouched and in order, including other
 * flags: this must not become a general-purpose argument parser, because
 * several of these subcommands forward the remainder verbatim.
 */
export function parseEditorFlag(argv: string[]): EditorFlagParse {
  const rest: string[] = [];
  let editor: string | undefined;
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === EDITOR_FLAG) {
      const value = argv[i + 1];
      if (value !== undefined && !value.startsWith("-")) {
        editor = value;
        i++;
      }
      continue;
    }
    if (arg.startsWith(`${EDITOR_FLAG}=`)) {
      editor = arg.slice(EDITOR_FLAG.length + 1);
      continue;
    }
    rest.push(arg);
  }
  return { editor, rest };
}

/**
 * Read `--editor` out of argv and turn it into a project path.
 *
 * Returns the remaining argv either way, so a subcommand keeps parsing exactly
 * what it parsed before when the flag is absent.
 */
export function takeEditorTarget(
  argv: string[],
  cwd: string = process.cwd(),
): { projectPath?: string; rest: string[] } {
  const { editor, rest } = parseEditorFlag(argv);
  if (editor === undefined) return { rest };
  return { projectPath: resolveEditorFlag(editor, cwd), rest };
}

/** The project a command acts on, as every command selects it. */
export interface ProjectSelection {
  /** What selected it: a .uproject path, a directory, or undefined when nothing did. */
  projectPath?: string;
  /** The project root, for per-project state; null when nothing selected one. */
  projectRoot: string | null;
  /** The project's name for messages: the .uproject's base name, or the directory's. */
  label: string | null;
  /** argv with `--editor <value>` removed (and `--project <value>` when it was read). */
  rest: string[];
}

/**
 * Select the project a one-shot command acts on, with one precedence for every
 * command: `--editor`, then `--project` when the command takes it, then the
 * first positional when the command takes one (skipping the values of
 * `valueFlags`). Nothing selected leaves projectPath undefined, and the
 * command applies its own fallback (cwd, a walk up, or no project).
 * Throws EditorFlagError on an --editor value that names nothing.
 */
export function selectProject(
  argv: string[],
  opts: { projectFlag?: boolean; positional?: boolean; valueFlags?: ReadonlySet<string>; cwd?: string } = {},
): ProjectSelection {
  const { projectPath: fromEditor, rest: afterEditor } = takeEditorTarget(argv, opts.cwd);
  let rest = afterEditor;
  let projectPath = fromEditor;
  if (opts.projectFlag) {
    const i = rest.findIndex((a) => a === "--project" || a.startsWith("--project="));
    if (i >= 0) {
      const inline = rest[i].startsWith("--project=");
      const value = inline ? rest[i].slice("--project=".length) : rest[i + 1];
      if (!value || value.startsWith("-")) {
        throw new EditorFlagError("--project needs a .uproject path (or the directory holding one), for example --project ./MyGame.uproject.");
      }
      rest = [...rest.slice(0, i), ...rest.slice(i + (inline ? 1 : 2))];
      projectPath ??= value;
    }
  }
  if (opts.positional && projectPath === undefined) projectPath = firstPositional(rest, opts.valueFlags);
  return {
    projectPath,
    projectRoot: projectPath ? projectDirOf(projectPath) : null,
    label: projectPath ? projectLabel(projectPath) : null,
    rest,
  };
}

/** The name a project goes by in messages. */
export function projectLabel(projectPath: string): string {
  return isUProjectPath(projectPath)
    ? path.basename(projectPath, path.extname(projectPath))
    : path.basename(projectPath.replace(/[\\/]+$/, ""));
}

/**
 * selectProject for a command whose only answer to a bad --editor is to stop:
 * prints the reason, which names the registered editors, and exits 1.
 */
export function selectProjectOrExit(argv: string[], opts?: Parameters<typeof selectProject>[1]): ProjectSelection {
  try {
    return selectProject(argv, opts);
  } catch (e) {
    fail(e instanceof EditorFlagError ? e.message : String(e));
    process.exit(1);
  }
}
