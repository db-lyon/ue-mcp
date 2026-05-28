import * as fs from "node:fs";
import * as path from "node:path";

/**
 * Install-time deployer for plugin-supplied native UE modules.
 *
 * A plugin declares `nativeModule:` in ue-mcp.plugin.yml with a `source:`
 * directory inside its npm tarball. On install we copy that directory to
 * `<projectDir>/Plugins/<uePluginName>/` and record every copied file so
 * uninstall can clean up without nuking user edits.
 *
 * The tracking file lives at `<projectDir>/.ue-mcp/native-modules.json`:
 *
 *   {
 *     "<npm-package-name>": {
 *       "uePluginName": "PIE_Studio",
 *       "pluginVersion": "0.0.2",
 *       "installedAt": "2026-05-26T00:00:00.000Z",
 *       "files": ["Plugins/PIE_Studio/...", ...]   // relative to projectDir
 *     }
 *   }
 */

export interface NativeModuleRecord {
  uePluginName: string;
  pluginVersion: string;
  installedAt: string;
  files: string[];
}

export interface NativeModulesState {
  [npmPackageName: string]: NativeModuleRecord;
}

const STATE_DIR = ".ue-mcp";
const STATE_FILE = "native-modules.json";

function stateFilePath(projectDir: string): string {
  return path.join(projectDir, STATE_DIR, STATE_FILE);
}

export function readNativeModulesState(projectDir: string): NativeModulesState {
  const file = stateFilePath(projectDir);
  if (!fs.existsSync(file)) return {};
  try {
    const raw = JSON.parse(fs.readFileSync(file, "utf-8"));
    if (raw && typeof raw === "object") return raw as NativeModulesState;
    return {};
  } catch {
    return {};
  }
}

export function writeNativeModulesState(projectDir: string, state: NativeModulesState): void {
  const dir = path.join(projectDir, STATE_DIR);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(stateFilePath(projectDir), JSON.stringify(state, null, 2) + "\n");
}

export interface DeployNativeResult {
  destDir: string;
  filesCopied: number;
  fileList: string[];
}

/**
 * Recursive directory copy that tracks every written file path. Returns
 * paths relative to projectDir so the state file is portable across
 * machines and the entries match the user's checkout layout.
 */
export function deployNativeModule(
  pkgDir: string,
  sourceRel: string,
  uePluginName: string,
  projectDir: string,
): DeployNativeResult {
  const sourceAbs = path.join(pkgDir, sourceRel);
  if (!fs.existsSync(sourceAbs)) {
    throw new Error(
      `nativeModule.source '${sourceRel}' not found in plugin package at ${sourceAbs}`,
    );
  }
  const destAbs = path.join(projectDir, "Plugins", uePluginName);
  fs.mkdirSync(destAbs, { recursive: true });

  const copied: string[] = [];
  copyRecursive(sourceAbs, destAbs, projectDir, copied);
  return { destDir: destAbs, filesCopied: copied.length, fileList: copied };
}

function copyRecursive(src: string, dest: string, projectDir: string, copied: string[]): void {
  const stat = fs.statSync(src);
  if (stat.isDirectory()) {
    fs.mkdirSync(dest, { recursive: true });
    for (const entry of fs.readdirSync(src)) {
      copyRecursive(path.join(src, entry), path.join(dest, entry), projectDir, copied);
    }
    return;
  }
  fs.copyFileSync(src, dest);
  copied.push(path.relative(projectDir, dest).split(path.sep).join("/"));
}

/**
 * Delete the files recorded for `npmName` and prune the state entry.
 * Returns the count of files actually removed (missing files are tolerated
 * silently — they may have been moved/deleted by the user).
 *
 * Caller is responsible for refusing the operation when the editor still
 * has the plugin DLL loaded (Windows refuses to delete locked files).
 */
export function undeployNativeModule(projectDir: string, npmName: string): number {
  const state = readNativeModulesState(projectDir);
  const record = state[npmName];
  if (!record) return 0;

  let removed = 0;
  for (const rel of record.files) {
    const abs = path.join(projectDir, rel);
    try {
      if (fs.existsSync(abs)) {
        fs.unlinkSync(abs);
        removed++;
      }
    } catch {
      // Locked or otherwise unwritable - leave for the user. They'll see
      // the dangling file count in the install-tracking output.
    }
  }

  // Best-effort: remove now-empty directories upward from the deepest path.
  const dirs = new Set<string>();
  for (const rel of record.files) {
    let dir = path.dirname(path.join(projectDir, rel));
    while (dir.startsWith(projectDir) && dir !== projectDir) {
      dirs.add(dir);
      dir = path.dirname(dir);
    }
  }
  // Sort by depth descending so children are pruned before parents.
  const sorted = [...dirs].sort((a, b) => b.length - a.length);
  for (const dir of sorted) {
    try {
      if (fs.existsSync(dir) && fs.readdirSync(dir).length === 0) {
        fs.rmdirSync(dir);
      }
    } catch {
      // ignore - directory wasn't empty or wasn't ours to remove
    }
  }

  delete state[npmName];
  writeNativeModulesState(projectDir, state);
  return removed;
}
