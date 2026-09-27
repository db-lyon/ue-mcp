import path from "node:path";
import { fileURLToPath } from "node:url";

/** The repository root. Tests build paths from it rather than counting "..". */
export const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/** A path under the repository root. */
export function repoPath(...segments: string[]): string {
  return path.join(REPO_ROOT, ...segments);
}
