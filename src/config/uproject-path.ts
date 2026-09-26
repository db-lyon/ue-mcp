import * as fs from "node:fs";
import * as path from "node:path";
import { McpError, ErrorCode } from "../core/errors.js";

/**
 * True when a path names a `.uproject` file, whatever case the extension is
 * written in.
 *
 * The extension is the only thing telling a project file from the directory
 * holding one, and every other project-keyed part of the server folds case
 * (the session key, the derived bridge port, the lockfile path). Reading it
 * case-sensitively here made `C:\PROJ\GAME.UPROJECT` resolve as a directory,
 * which fails as "project directory not found" on a path that exists.
 */
export function isUProjectPath(candidate: string): boolean {
  return path.extname(candidate).toLowerCase() === ".uproject";
}

/**
 * Resolve a user-supplied path (a .uproject, or a directory holding one) to an
 * absolute .uproject path. Pure: it touches no state, so a caller that has to
 * move several things at once can validate the target first and leave
 * everything where it was when the path is bad.
 */
export function resolveUProjectPath(inputPath: string): string {
  if (isUProjectPath(inputPath)) {
    const resolved = path.resolve(inputPath);
    if (!fs.existsSync(resolved)) {
      throw new McpError(ErrorCode.NOT_FOUND, `No .uproject file at ${resolved}`);
    }
    return resolved;
  }

  let entries: string[];
  try {
    entries = fs.readdirSync(inputPath);
  } catch {
    throw new McpError(ErrorCode.NOT_FOUND, `Project directory not found: ${inputPath}`);
  }
  const files = entries.filter(isUProjectPath);
  if (files.length === 0) {
    throw new McpError(ErrorCode.NOT_FOUND, `No .uproject file found in ${inputPath}`);
  }
  return path.resolve(inputPath, files[0]);
}

/** The directory a project path names: a .uproject's parent, or the path itself. */
export function projectDirOf(projectPath: string): string {
  const resolved = path.resolve(projectPath);
  return isUProjectPath(resolved) ? path.dirname(resolved) : resolved;
}

/**
 * The .uproject that `start` names: `start` itself when it is an existing
 * .uproject, else the first one in the directory `start`. `walkUp` also
 * searches that many parent directories, for a command run from inside a
 * project tree. Null when there is none.
 */
export function findUProject(start: string, options: { walkUp?: number } = {}): string | null {
  const resolved = path.resolve(start);
  if (isUProjectPath(resolved)) return fs.existsSync(resolved) ? resolved : null;
  let dir = resolved;
  for (let level = 0; level <= (options.walkUp ?? 0); level++) {
    try {
      const found = fs.readdirSync(dir).find(isUProjectPath);
      if (found) return path.join(dir, found);
    } catch {
      // Not a readable directory; try the parent.
    }
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return null;
}
